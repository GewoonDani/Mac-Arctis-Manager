#include "../../Sources/AudioEngine/AudioEngine.c"
#include <assert.h>
#include <stdio.h>

static void closeEnough(float value, float expected) { assert(fabsf(value - expected) < 0.001f); }

int main(void) {
    ArctisMixer *mixer = calloc(1, sizeof(*mixer));
    assert(mixer);
    mixer->outputRate = 44100;
    mixer->outputChannels = 2;
    mixer->limiter = 1;
    mixer->gain[0] = mixer->gain[1] = 1;
    arctisMixerSetGains(mixer, 1, 1);

    Stereo game[2048], chat[2048];
    for (unsigned i = 0; i < 2048; i++) {
        game[i] = (Stereo){0.8f, 0};
        chat[i] = (Stereo){0.8f, 0.4f};
    }
    push(&mixer->monitor[0], game, 2048);
    push(&mixer->monitor[1], chat, 2048);
    float buffer[512];
    mix(mixer, buffer, 256);
    closeEnough(buffer[0], 1); // both sources drive the limiter
    closeEnough(buffer[1], 0.25f); // shared gain preserves stereo image
    const uint64_t consumed = atomic_load(&mixer->monitor[0].read);
    assert(consumed > 270 && consumed < 285); // 48 kHz input at a 44.1 kHz sink
    assert(arctisMixerGetStats(mixer).renderedFrames == 256);

    arctisMixerSetGains(mixer, 1, 0);
    for (unsigned i = 0; i < 32; i++) {
        push(&mixer->monitor[0], game, 278);
        push(&mixer->monitor[1], chat, 278);
        mix(mixer, buffer, 256);
    }
    closeEnough(buffer[510], 0.8f);
    closeEnough(buffer[511], 0);

    for (unsigned i = 0; i < 16; i++) mix(mixer, buffer, 256);
    closeEnough(buffer[0], 0);
    assert(arctisMixerGetStats(mixer).underruns[0] > 0);
    push(&mixer->monitor[0], game, Capacity + 1);
    assert(arctisMixerGetStats(mixer).overflows[0] == 1);
    arctisMixerStop(mixer);

    Monitor *input = calloc(1, sizeof(*input));
    assert(input);
    atomic_store(&input->read, Capacity - 1);
    atomic_store(&input->write, Capacity - 1);
    Stereo values[2] = {{0, 1}, {1, -1}};
    push(input, values, 2);
    Reader reader = {.input = input, .read = Capacity - 1, .step = 0.5, .active = true};
    closeEnough(next(&reader).left, 0);
    Stereo halfway = next(&reader);
    closeEnough(halfway.left, 0.5f);
    closeEnough(halfway.right, 0);
    free(input);
    puts("Dual-stream mix, ramp, limiter, resampling, ring wrap and fault handling passed.");
    return 0;
}
