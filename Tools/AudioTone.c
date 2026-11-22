// Deliberately target only the two project endpoint UIDs.
#include "../Sources/AudioEngine/AudioEngine.c"
#include <stdio.h>
#include <unistd.h>

typedef struct { double phase, increment; } Tone;

static OSStatus tone(void *context, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time,
                     UInt32 bus, UInt32 frames, AudioBufferList *buffers) {
    (void)flags; (void)time; (void)bus;
    if (buffers->mNumberBuffers != 1 || !buffers->mBuffers[0].mData ||
        buffers->mBuffers[0].mDataByteSize < frames * sizeof(Stereo)) return kAudio_ParamError;
    Stereo *pcm = buffers->mBuffers[0].mData;
    Tone *oscillator = context;
    for (unsigned i = 0; i < frames; i++) {
        float value = 0.045f * (float)sin(oscillator->phase);
        pcm[i] = (Stereo){value, value};
        oscillator->phase += oscillator->increment;
        if (oscillator->phase >= 2.0 * M_PI) oscillator->phase -= 2.0 * M_PI;
    }
    return 0;
}

static AudioDeviceID deviceWithUID(CFStringRef uid) {
    AudioObjectPropertyAddress address = {
        kAudioHardwarePropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
    };
    AudioDeviceID id = 0;
    UInt32 size = sizeof(id);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(uid), &uid, &size, &id))
        return 0;
    return id;
}

int main(int argc, char **argv) {
    if (argc != 1) {
        fprintf(stderr, "Usage: %s\n", argv[0]);
        return 64;
    }
    AudioDeviceID devices[] = {deviceWithUID(CFSTR(ARCTIS_GAME_UID)), deviceWithUID(CFSTR(ARCTIS_CHAT_UID))};
    if (!devices[0] || !devices[1]) {
        fprintf(stderr, "Install the driver and restart macOS before running the tone test.\n");
        return 1;
    }
    Tone oscillators[] = {{0, 2 * M_PI * 440 / monitorRate}, {0, 2 * M_PI * 660 / monitorRate}};
    AudioUnit unit[2] = {0};
    OSStatus status = setup(&unit[0], devices[0], false, monitorRate, 2, tone, &oscillators[0]);
    if (!status) status = setup(&unit[1], devices[1], false, monitorRate, 2, tone, &oscillators[1]);
    if (!status) status = AudioOutputUnitStart(unit[0]);
    if (!status) status = AudioOutputUnitStart(unit[1]);
    if (!status) {
        puts("Playing 440 Hz to Arctis Game and 660 Hz to Arctis Chat for 20 seconds.");
        fflush(stdout);
        sleep(20);
    } else {
        fprintf(stderr, "Opening virtual output failed (%d).\n", status);
    }
    closeUnit(unit[1]);
    closeUnit(unit[0]);
    return status != 0;
}
