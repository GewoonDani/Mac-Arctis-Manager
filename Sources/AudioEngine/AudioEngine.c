#include "AudioEngine.h"
#include <AudioUnit/AudioUnit.h>
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum { Capacity = 16384, ScratchFrames = 8192, TargetFill = 768 };
static const double monitorRate = 48000;
_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2 && __atomic_always_lock_free(sizeof(float), 0),
               "The mixer requires lock-free queue indices and gains");

typedef struct { float left, right; } Stereo;

typedef struct {
    Stereo queue[Capacity];
    Stereo scratch[ScratchFrames];
    _Atomic uint64_t read, write;
    _Atomic uint64_t underruns, overflows, errors;
    _Atomic float peak;
    AudioUnit unit;
    double fraction;
    bool primed;
} Monitor;

struct ArctisMixer {
    Monitor monitor[2];
    AudioUnit output;
    _Atomic float target[2];
    _Atomic uint64_t rendered;
    float gain[2];
    float limiter;
    double outputRate;
    unsigned outputChannels;
};

static void push(Monitor *input, const Stereo *samples, unsigned count) {
    uint64_t write = atomic_load_explicit(&input->write, memory_order_relaxed);
    uint64_t read = atomic_load_explicit(&input->read, memory_order_acquire);
    if (count > Capacity - (write - read)) {
        atomic_fetch_add_explicit(&input->overflows, 1, memory_order_relaxed);
        return;
    }
    for (unsigned i = 0; i < count; i++) input->queue[(write + i) % Capacity] = samples[i];
    atomic_store_explicit(&input->write, write + count, memory_order_release);
}

static OSStatus capture(void *context, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time,
                        UInt32 bus, UInt32 frames, AudioBufferList *unused) {
    (void)bus; (void)unused;
    Monitor *input = context;
    if (frames > ScratchFrames) {
        atomic_fetch_add_explicit(&input->errors, 1, memory_order_relaxed);
        return 0;
    }
    AudioBufferList buffers = {1, {{2, frames * sizeof(Stereo), input->scratch}}};
    OSStatus status = AudioUnitRender(input->unit, flags, time, 1, frames, &buffers);
    if (status) {
        atomic_fetch_add_explicit(&input->errors, 1, memory_order_relaxed);
        return 0;
    }
    const Stereo *pcm = buffers.mBuffers[0].mData;
    float peak = 0;
    if (pcm && !(*flags & kAudioUnitRenderAction_OutputIsSilence)) {
        for (UInt32 i = 0; i < frames; i++) {
            peak = fmaxf(peak, fabsf(pcm[i].left));
            peak = fmaxf(peak, fabsf(pcm[i].right));
        }
    } else {
        memset(input->scratch, 0, frames * sizeof(Stereo));
        pcm = input->scratch;
    }
    atomic_store_explicit(&input->peak, isfinite(peak) ? fminf(peak, 1) : 0, memory_order_relaxed);
    push(input, pcm, frames);
    return 0;
}

typedef struct {
    Monitor *input;
    uint64_t read, write;
    double step, fraction;
    bool active;
} Reader;

static Reader beginRead(Monitor *input, double nominalStep, unsigned frames) {
    Reader r = {
        .input = input,
        .read = atomic_load_explicit(&input->read, memory_order_relaxed),
        .write = atomic_load_explicit(&input->write, memory_order_acquire),
        .fraction = input->fraction
    };
    uint64_t needed = (uint64_t)ceil(nominalStep * frames * 1.005) + 2;
    if (needed > Capacity / 2) return r;
    uint64_t desired = needed + TargetFill;
    uint64_t available = r.write - r.read;
    if (available > desired + TargetFill * 3) {
        r.read = r.write - desired;
        r.fraction = 0;
        available = desired;
        atomic_fetch_add_explicit(&input->overflows, 1, memory_order_relaxed);
    }
    if (!input->primed && available < desired) return r;
    if (available < needed) {
        input->primed = false;
        atomic_fetch_add_explicit(&input->underruns, 1, memory_order_relaxed);
        return r;
    }
    input->primed = r.active = true;
    // The USB clock and the HAL host clock drift even at the same nominal sample rate.
    double error = ((double)available - (double)desired) / (TargetFill * 100.0);
    r.step = nominalStep * (1 + fmax(-0.005, fmin(error, 0.005)));
    return r;
}

static Stereo next(Reader *r) {
    if (!r->active) return (Stereo){0, 0};
    Stereo a = r->input->queue[r->read % Capacity];
    Stereo b = r->input->queue[(r->read + 1) % Capacity];
    float fraction = (float)r->fraction;
    r->fraction += r->step;
    unsigned advance = (unsigned)r->fraction;
    r->read += advance;
    r->fraction -= advance;
    a.left += (b.left - a.left) * fraction;
    a.right += (b.right - a.right) * fraction;
    return a;
}

static float safeSample(float value) { return isfinite(value) ? fmaxf(-1, fminf(1, value)) : 0; }

static void mix(ArctisMixer *mixer, float *out, unsigned frames) {
    double step = monitorRate / mixer->outputRate;
    Reader game = beginRead(&mixer->monitor[0], step, frames);
    Reader chat = beginRead(&mixer->monitor[1], step, frames);
    float targetGame = atomic_load_explicit(&mixer->target[0], memory_order_relaxed);
    float targetChat = atomic_load_explicit(&mixer->target[1], memory_order_relaxed);
    const float gainStep = (float)(1.0 / (0.015 * mixer->outputRate));
    const float limiterRelease = (float)(1.0 / (0.050 * mixer->outputRate));
    for (unsigned frame = 0; frame < frames; frame++) {
        Stereo g = next(&game);
        Stereo c = next(&chat);
        mixer->gain[0] += (targetGame - mixer->gain[0]) * gainStep;
        mixer->gain[1] += (targetChat - mixer->gain[1]) * gainStep;
        float left = safeSample(g.left) * mixer->gain[0] + safeSample(c.left) * mixer->gain[1];
        float right = safeSample(g.right) * mixer->gain[0] + safeSample(c.right) * mixer->gain[1];
        float peak = fmaxf(fabsf(left), fabsf(right));
        float ceiling = peak > 1 ? 1 / peak : 1;
        mixer->limiter = fminf(ceiling, mixer->limiter + limiterRelease);
        if (mixer->outputChannels == 1) {
            *out++ = 0.5f * (left + right) * mixer->limiter;
        } else {
            *out++ = left * mixer->limiter;
            *out++ = right * mixer->limiter;
        }
    }
    game.input->fraction = game.fraction;
    chat.input->fraction = chat.fraction;
    atomic_store_explicit(&game.input->read, game.read, memory_order_release);
    atomic_store_explicit(&chat.input->read, chat.read, memory_order_release);
    atomic_fetch_add_explicit(&mixer->rendered, frames, memory_order_relaxed);
}

static OSStatus render(void *context, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time,
                       UInt32 bus, UInt32 frames, AudioBufferList *buffers) {
    (void)flags; (void)time; (void)bus;
    ArctisMixer *mixer = context;
    if (buffers->mNumberBuffers != 1 || !buffers->mBuffers[0].mData ||
        buffers->mBuffers[0].mDataByteSize < frames * mixer->outputChannels * sizeof(float)) {
        for (unsigned i = 0; i < buffers->mNumberBuffers; i++) {
            AudioBuffer *buffer = &buffers->mBuffers[i];
            if (buffer->mData) memset(buffer->mData, 0, buffer->mDataByteSize);
        }
        return 0;
    }
    mix(mixer, buffers->mBuffers[0].mData, frames);
    return 0;
}

static OSStatus setup(AudioUnit *unit, AudioDeviceID device, bool input, double rate, unsigned channels,
                      AURenderCallback callback, void *context) {
    AudioComponentDescription descriptor = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_HALOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple
    };
    AudioComponent component = AudioComponentFindNext(NULL, &descriptor);
    if (!component) return kAudioHardwareUnsupportedOperationError;
    OSStatus status = AudioComponentInstanceNew(component, unit);
    if (status) return status;

    UInt32 in = input, out = !input;
    status = AudioUnitSetProperty(*unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &in, sizeof(in));
    if (status) return status;
    status = AudioUnitSetProperty(*unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &out, sizeof(out));
    if (status) return status;
    status = AudioUnitSetProperty(*unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                  &device, sizeof(device));
    if (status) return status;

    UInt32 stride = channels * sizeof(float);
    AudioStreamBasicDescription format = {
        rate, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked,
        stride, 1, stride, channels, 32, 0
    };
    status = AudioUnitSetProperty(*unit, kAudioUnitProperty_StreamFormat,
        input ? kAudioUnitScope_Output : kAudioUnitScope_Input, input ? 1 : 0, &format, sizeof(format));
    if (status) return status;

    AURenderCallbackStruct cb = {callback, context};
    status = AudioUnitSetProperty(*unit,
        input ? kAudioOutputUnitProperty_SetInputCallback : kAudioUnitProperty_SetRenderCallback,
        input ? kAudioUnitScope_Global : kAudioUnitScope_Input, 0, &cb, sizeof(cb));
    if (status) return status;
    return AudioUnitInitialize(*unit);
}

static OSStatus outputFormat(AudioDeviceID output, double *rate, unsigned *channels) {
    AudioObjectPropertyAddress address = {
        kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
    };
    UInt32 bytes = sizeof(*rate);
    OSStatus status = AudioObjectGetPropertyData(output, &address, 0, NULL, &bytes, rate);
    if (status) return status;
    if (!isfinite(*rate) || *rate < 8000 || *rate > 384000) return kAudioDeviceUnsupportedFormatError;
    address.mSelector = kAudioDevicePropertyStreamConfiguration;
    address.mScope = kAudioObjectPropertyScopeOutput;
    status = AudioObjectGetPropertyDataSize(output, &address, 0, NULL, &bytes);
    if (status) return status;
    AudioBufferList *buffers = malloc(bytes);
    if (!buffers) return kAudioHardwareUnspecifiedError;
    status = AudioObjectGetPropertyData(output, &address, 0, NULL, &bytes, buffers);
    *channels = 0;
    if (!status) {
        for (unsigned i = 0; i < buffers->mNumberBuffers; i++) *channels += buffers->mBuffers[i].mNumberChannels;
        if (*channels == 0) status = kAudioDeviceUnsupportedFormatError;
        if (*channels > 2) *channels = 2;
    }
    free(buffers);
    return status;
}

ArctisMixer *arctisMixerStart(AudioDeviceID game, AudioDeviceID chat, AudioDeviceID output, OSStatus *status) {
    if (!game || !chat || !output || game == chat || game == output || chat == output) {
        *status = kAudioHardwareBadDeviceError;
        return NULL;
    }
    ArctisMixer *mixer = calloc(1, sizeof(*mixer));
    if (!mixer) {
        *status = kAudioHardwareUnspecifiedError;
        return NULL;
    }
    for (unsigned i = 0; i < 2; i++) {
        Monitor *input = &mixer->monitor[i];
        atomic_init(&input->read, 0);
        atomic_init(&input->write, 0);
        atomic_init(&input->underruns, 0);
        atomic_init(&input->overflows, 0);
        atomic_init(&input->errors, 0);
        atomic_init(&input->peak, 0);
        atomic_init(&mixer->target[i], 0);
    }
    atomic_init(&mixer->rendered, 0);
    mixer->limiter = 1;
    *status = outputFormat(output, &mixer->outputRate, &mixer->outputChannels);
    if (!*status) *status = setup(&mixer->monitor[0].unit, game, true, monitorRate, 2, capture, &mixer->monitor[0]);
    if (!*status) *status = setup(&mixer->monitor[1].unit, chat, true, monitorRate, 2, capture, &mixer->monitor[1]);
    if (!*status) *status = setup(&mixer->output, output, false, mixer->outputRate, mixer->outputChannels, render, mixer);
    if (!*status) *status = AudioOutputUnitStart(mixer->monitor[0].unit);
    if (!*status) *status = AudioOutputUnitStart(mixer->monitor[1].unit);
    if (!*status) *status = AudioOutputUnitStart(mixer->output);
    if (!*status) return mixer;

    arctisMixerStop(mixer);
    return NULL;
}

static void closeUnit(AudioUnit unit) {
    if (!unit) return;
    AudioOutputUnitStop(unit);
    AudioUnitUninitialize(unit);
    AudioComponentInstanceDispose(unit);
}

void arctisMixerStop(ArctisMixer *mixer) {
    if (!mixer) return;
    closeUnit(mixer->output);
    closeUnit(mixer->monitor[0].unit);
    closeUnit(mixer->monitor[1].unit);
    free(mixer);
}

void arctisMixerSetGains(ArctisMixer *mixer, float game, float chat) {
    if (!mixer) return;
    atomic_store_explicit(&mixer->target[0], fmaxf(0, fminf(1, game)), memory_order_relaxed);
    atomic_store_explicit(&mixer->target[1], fmaxf(0, fminf(1, chat)), memory_order_relaxed);
}

ArctisMixerStats arctisMixerGetStats(const ArctisMixer *mixer) {
    ArctisMixerStats stats = {0};
    if (!mixer) return stats;
    stats.renderedFrames = atomic_load_explicit(&mixer->rendered, memory_order_relaxed);
    for (unsigned i = 0; i < 2; i++) {
        stats.capturedFrames[i] = atomic_load_explicit(&mixer->monitor[i].write, memory_order_relaxed);
        stats.underruns[i] = atomic_load_explicit(&mixer->monitor[i].underruns, memory_order_relaxed);
        stats.overflows[i] = atomic_load_explicit(&mixer->monitor[i].overflows, memory_order_relaxed);
        stats.captureErrors[i] = atomic_load_explicit(&mixer->monitor[i].errors, memory_order_relaxed);
        stats.inputPeak[i] = atomic_load_explicit(&mixer->monitor[i].peak, memory_order_relaxed);
    }
    return stats;
}
