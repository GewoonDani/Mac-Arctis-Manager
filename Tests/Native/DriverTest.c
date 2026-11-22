#include <CoreAudio/AudioServerPlugIn.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef void *(*Factory)(CFAllocatorRef, CFUUIDRef);

static UInt32 get(AudioServerPlugInDriverRef driver, AudioObjectID id, UInt32 scope,
                  UInt32 selector, void *buffer, UInt32 capacity) {
    AudioObjectPropertyAddress property = {selector, scope, kAudioObjectPropertyElementMain};
    UInt32 bytes = 0;
    assert((*driver)->HasProperty(driver, id, 0, &property));
    assert((*driver)->GetPropertyDataSize(driver, id, 0, &property, 0, NULL, &bytes) == 0);
    assert(bytes <= capacity);
    UInt32 returned = 0;
    assert((*driver)->GetPropertyData(driver, id, 0, &property, 0, NULL, capacity, &returned, buffer) == 0);
    assert(bytes == returned);
    return returned;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)argv[1],
                                                           (CFIndex)strlen(argv[1]), true);
    assert(url);
    CFPlugInRef plugin = CFPlugInCreate(NULL, url);
    assert(plugin);
    CFRelease(url);
    CFArrayRef factories = CFPlugInFindFactoriesForPlugInTypeInPlugIn(kAudioServerPlugInTypeUUID, plugin);
    assert(factories && CFArrayGetCount(factories) == 1);
    AudioServerPlugInDriverRef driver = CFPlugInInstanceCreate(NULL, CFArrayGetValueAtIndex(factories, 0),
                                                              kAudioServerPlugInTypeUUID);
    assert(driver);
    CFRelease(factories);
    struct AudioServerPlugInHostInterface host = {0};
    assert((*driver)->Initialize(driver, &host) == 0);
    const UInt32 global = kAudioObjectPropertyScopeGlobal;
    AudioObjectID devices[2];
    assert(get(driver, kAudioObjectPlugInObject, global, kAudioPlugInPropertyDeviceList, devices, sizeof(devices))
        == sizeof(devices));
    assert(devices[0] != devices[1]);

    AudioObjectID in[2], out[2];
    const CFStringRef uids[] = {CFSTR("org.macarctis.manager.game"), CFSTR("org.macarctis.manager.chat")};
    for (unsigned i = 0; i < 2; i++) {
        CFStringRef uid;
        get(driver, devices[i], global, kAudioDevicePropertyDeviceUID, &uid, sizeof(uid));
        assert(CFEqual(uid, uids[i]));
        CFRelease(uid);
        get(driver, devices[i], kAudioObjectPropertyScopeInput, kAudioDevicePropertyStreams, &in[i], sizeof(in[i]));
        get(driver, devices[i], kAudioObjectPropertyScopeOutput, kAudioDevicePropertyStreams, &out[i], sizeof(out[i]));
        assert(in[i] != out[i]);
        AudioStreamBasicDescription format;
        get(driver, out[i], global, kAudioStreamPropertyVirtualFormat, &format, sizeof(format));
        assert(format.mSampleRate == 48000 && format.mChannelsPerFrame == 2 && format.mBytesPerFrame == 8);
        assert((*driver)->StartIO(driver, devices[i], 7) == 0);
        Float64 sample;
        UInt64 hostTime, seed;
        assert((*driver)->GetZeroTimeStamp(driver, devices[i], 7, &sample, &hostTime, &seed) == 0);
        assert(sample >= 0 && hostTime > 0 && seed > 0);
    }

    // Cross a ring boundary, carry different stereo signals, then verify gaps return silence.
    AudioServerPlugInIOCycleInfo cycle = {0};
    cycle.mOutputTime.mSampleTime = 16382;
    cycle.mInputTime.mSampleTime = cycle.mOutputTime.mSampleTime + 512;
    float game[8] = {0.1f, -0.2f, 0.3f, 0.4f, -0.5f, 0.4f, 0.1f, 0.2f};
    float chat[8] = {-0.7f, 0.6f, -0.2f, -0.1f, 0.8f, 0.3f, 0.1f, -0.4f};
    float *signals[] = {game, chat};
    for (unsigned i = 0; i < 2; i++) {
        assert((*driver)->DoIOOperation(driver, devices[i], out[i], 7, kAudioServerPlugInIOOperationWriteMix,
                                        4, &cycle, signals[i], NULL) == 0);
    }
    for (unsigned i = 0; i < 2; i++) {
        float actual[8] = {0};
        assert((*driver)->DoIOOperation(driver, devices[i], in[i], 7, kAudioServerPlugInIOOperationReadInput,
                                        4, &cycle, actual, NULL) == 0);
        assert(memcmp(actual, signals[i], sizeof(actual)) == 0);
    }
    cycle.mInputTime.mSampleTime += 16384;
    for (unsigned i = 0; i < 2; i++) {
        float actual[8] = {1, 1, 1, 1, 1, 1, 1, 1};
        const float silence[8] = {0};
        assert((*driver)->DoIOOperation(driver, devices[i], in[i], 7, kAudioServerPlugInIOOperationReadInput,
                                        4, &cycle, actual, NULL) == 0);
        assert(memcmp(actual, silence, sizeof(actual)) == 0);
        assert((*driver)->StopIO(driver, devices[i], 7) == 0);
    }
    (*driver)->Release(driver);
    CFRelease(plugin);
    puts("HAL factory, properties, clock, dual stereo PCM isolation and underrun silence passed.");
    return 0;
}
