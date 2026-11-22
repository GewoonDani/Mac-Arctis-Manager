#pragma once
#include <CoreAudio/CoreAudio.h>
#include <stdint.h>

#define ARCTIS_GAME_UID "org.macarctis.manager.game"
#define ARCTIS_CHAT_UID "org.macarctis.manager.chat"

typedef struct ArctisMixer ArctisMixer;
typedef struct {
    uint64_t capturedFrames[2];
    uint64_t underruns[2];
    uint64_t overflows[2];
    uint64_t captureErrors[2];
    uint64_t renderedFrames;
    float inputPeak[2];
} ArctisMixerStats;

// All lifecycle calls are serialized by the owner. Gains and telemetry are atomic.
ArctisMixer *arctisMixerStart(AudioDeviceID game, AudioDeviceID chat, AudioDeviceID output,
                              OSStatus *status);
void arctisMixerStop(ArctisMixer *mixer);
void arctisMixerSetGains(ArctisMixer *mixer, float game, float chat);
ArctisMixerStats arctisMixerGetStats(const ArctisMixer *mixer);
