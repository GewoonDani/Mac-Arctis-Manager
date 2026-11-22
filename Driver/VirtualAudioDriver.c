#include <CoreAudio/AudioServerPlugIn.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <mach/mach_time.h>
#include <string.h>

enum {
    Game = 2, GameIn, GameOut, Chat, ChatIn, ChatOut,
    Period = 512, MonitorDelay = 512, Capacity = 16384
};
static const Float64 rate = 48000;
static const AudioObjectID devices[] = {Game, Chat};

typedef struct {
    _Atomic uint64_t samples[Capacity][2];
    _Atomic uint32_t clients;
} Endpoint;

_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2, "The monitor requires lock-free 64-bit atomics");
static Endpoint endpoints[2];
static uint64_t anchor;
static double ticksPerFrame;
static _Atomic ULONG refs;
static AudioServerPlugInDriverInterface interface;
static AudioServerPlugInDriverInterface *vtable = &interface;

static int indexFor(AudioObjectID id) {
    if (id >= Game && id <= GameOut) return 0;
    if (id >= Chat && id <= ChatOut) return 1;
    return -1;
}

static CFStringRef uid(int index) {
    return index == 0 ? CFSTR("org.macarctis.manager.game") : CFSTR("org.macarctis.manager.chat");
}

static ULONG retain(void *driver) { (void)driver; return atomic_fetch_add(&refs, 1) + 1; }

static ULONG release(void *driver) {
    (void)driver;
    ULONG count = atomic_load(&refs);
    while (count && !atomic_compare_exchange_weak(&refs, &count, count - 1)) {}
    return count ? count - 1 : 0;
}

static HRESULT query(void *driver, REFIID uuid, void **out) {
    if (!out) return E_POINTER;
    *out = NULL;
    CFUUIDRef requested = CFUUIDCreateFromUUIDBytes(NULL, uuid);
    if (!requested) return E_NOINTERFACE;
    Boolean matches = CFEqual(requested, IUnknownUUID) || CFEqual(requested, kAudioServerPlugInDriverInterfaceUUID);
    CFRelease(requested);
    if (!matches) return E_NOINTERFACE;
    *out = driver;
    retain(driver);
    return S_OK;
}

static OSStatus initialize(AudioServerPlugInDriverRef d, AudioServerPlugInHostRef host) {
    (void)d; (void)host;
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    ticksPerFrame = (1e9 * timebase.denom) / (timebase.numer * rate);
    anchor = mach_absolute_time();
    return 0;
}

static OSStatus create(AudioServerPlugInDriverRef d, CFDictionaryRef desc,
                       const AudioServerPlugInClientInfo *c, AudioObjectID *id) {
    (void)d; (void)desc; (void)c; (void)id;
    return kAudioHardwareUnsupportedOperationError;
}
static OSStatus destroy(AudioServerPlugInDriverRef d, AudioObjectID id) {
    (void)d; (void)id;
    return kAudioHardwareUnsupportedOperationError;
}
static OSStatus client(AudioServerPlugInDriverRef d, AudioObjectID id, const AudioServerPlugInClientInfo *c) {
    (void)d; (void)c;
    return id == Game || id == Chat ? 0 : kAudioHardwareBadObjectError;
}
static OSStatus configuration(AudioServerPlugInDriverRef d, AudioObjectID id, UInt64 action, void *info) {
    (void)d; (void)id; (void)action; (void)info;
    return kAudioHardwareUnsupportedOperationError;
}

typedef struct {
    UInt32 size;
    union {
        UInt32 integer;
        Float64 real;
        CFStringRef string;
        AudioObjectID objects[4];
        AudioValueRange range;
        AudioChannelLayout layout;
        AudioStreamBasicDescription format;
        AudioStreamRangedDescription ranged;
    } data;
} Property;

static const AudioStreamBasicDescription streamFormat = {
    48000, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked,
    8, 1, 8, 2, 32, 0
};

#define VALUE(field, value) do { p->data.field = (value); p->size = sizeof(p->data.field); return true; } while (0)

static bool property(AudioObjectID id, const AudioObjectPropertyAddress *a, CFStringRef qualifier, Property *p) {
    int index = indexFor(id);
    bool plugin = id == kAudioObjectPlugInObject;
    if (!plugin && index < 0) return false;

    AudioObjectID device = index < 0 ? 0 : devices[index];
    bool isDevice = id == device;
    bool isInput = id == device + 1;
    bool global = a->mScope == kAudioObjectPropertyScopeGlobal;
    bool input = a->mScope == kAudioObjectPropertyScopeInput;
    bool output = a->mScope == kAudioObjectPropertyScopeOutput;
    if ((!global && (!isDevice || (!input && !output))) || a->mElement != kAudioObjectPropertyElementMain)
        return false;

    switch (a->mSelector) {
        case kAudioObjectPropertyBaseClass:
            VALUE(integer, kAudioObjectClassID);
        case kAudioObjectPropertyClass:
            VALUE(integer, plugin ? kAudioPlugInClassID : isDevice ? kAudioDeviceClassID : kAudioStreamClassID);
        case kAudioObjectPropertyOwner:
            VALUE(integer, plugin ? kAudioObjectUnknown : isDevice ? kAudioObjectPlugInObject : device);
        case kAudioObjectPropertyName:
            VALUE(string, plugin ? CFSTR("Mac Arctis Manager") : isDevice
                ? (index == 0 ? CFSTR("Arctis Game") : CFSTR("Arctis Chat"))
                : (isInput ? CFSTR("Loopback Monitor") : CFSTR("Playback")));
        case kAudioObjectPropertyManufacturer:
            VALUE(string, CFSTR("Mac Arctis Manager"));
        case kAudioObjectPropertyOwnedObjects:
            if (plugin) {
                memcpy(p->data.objects, devices, sizeof(devices));
                p->size = sizeof(devices);
            } else if (isDevice) {
                if (input || global) p->data.objects[p->size++] = device + 1;
                if (output || global) p->data.objects[p->size++] = device + 2;
                p->size *= sizeof(AudioObjectID);
            }
            return true;
    }

    if (plugin) {
        switch (a->mSelector) {
            case kAudioPlugInPropertyBundleID: VALUE(string, CFSTR("org.macarctis.manager.driver"));
            case kAudioPlugInPropertyDeviceList:
                memcpy(p->data.objects, devices, sizeof(devices));
                p->size = sizeof(devices);
                return true;
            case kAudioPlugInPropertyTranslateUIDToDevice:
                if (qualifier && CFEqual(qualifier, uid(0))) VALUE(integer, Game);
                if (qualifier && CFEqual(qualifier, uid(1))) VALUE(integer, Chat);
                VALUE(integer, kAudioObjectUnknown);
            case kAudioPlugInPropertyBoxList:
            case kAudioPlugInPropertyClockDeviceList: return true;
            default: return false;
        }
    }

    if (isDevice) {
        switch (a->mSelector) {
            case kAudioDevicePropertyDeviceUID: VALUE(string, uid(index));
            case kAudioDevicePropertyModelUID: VALUE(string, CFSTR("org.macarctis.manager.loopback"));
            case kAudioDevicePropertyTransportType: VALUE(integer, kAudioDeviceTransportTypeVirtual);
            case kAudioDevicePropertyRelatedDevices:
                VALUE(integer, id);
            case kAudioDevicePropertyDeviceIsAlive: VALUE(integer, 1);
            case kAudioDevicePropertyDeviceIsRunning:
                VALUE(integer, atomic_load_explicit(&endpoints[index].clients, memory_order_relaxed) != 0);
            case kAudioDevicePropertyIsHidden:
            case kAudioDevicePropertyClockDomain:
            case kAudioDevicePropertySafetyOffset: VALUE(integer, 0);
            case kAudioDevicePropertyLatency: VALUE(integer, input ? MonitorDelay : 0);
            case kAudioDevicePropertyDeviceCanBeDefaultDevice:
            case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice: VALUE(integer, output);
            case kAudioDevicePropertyZeroTimeStampPeriod: VALUE(integer, Period);
            case kAudioDevicePropertyNominalSampleRate: VALUE(real, rate);
            case kAudioDevicePropertyAvailableNominalSampleRates:
                VALUE(range, ((AudioValueRange){rate, rate}));
            case kAudioObjectPropertyControlList: return true;
            case kAudioDevicePropertyStreams:
                if (global || input) p->data.objects[p->size++] = device + 1;
                if (global || output) p->data.objects[p->size++] = device + 2;
                p->size *= sizeof(AudioObjectID);
                return true;
            case kAudioDevicePropertyPreferredChannelLayout:
                if (global) return false;
                p->data.layout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
                p->size = offsetof(AudioChannelLayout, mChannelDescriptions);
                return true;
            case kAudioDevicePropertyPreferredChannelsForStereo:
                if (global) return false;
                p->data.objects[0] = 1;
                p->data.objects[1] = 2;
                p->size = 2 * sizeof(UInt32);
                return true;
            default: return false;
        }
    }

    switch (a->mSelector) {
        case kAudioStreamPropertyIsActive: VALUE(integer, 1);
        case kAudioStreamPropertyDirection: VALUE(integer, isInput);
        case kAudioStreamPropertyTerminalType:
            VALUE(integer, isInput ? kAudioStreamTerminalTypeLine : kAudioStreamTerminalTypeSpeaker);
        case kAudioStreamPropertyStartingChannel: VALUE(integer, 1);
        case kAudioStreamPropertyLatency: VALUE(integer, 0);
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat: VALUE(format, streamFormat);
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
            VALUE(ranged, ((AudioStreamRangedDescription){streamFormat, {rate, rate}}));
        default: return false;
    }
}
#undef VALUE

static Boolean has(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a) {
    (void)d; (void)pid;
    Property p = {0};
    return a && property(id, a, NULL, &p);
}
static OSStatus settable(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid,
                        const AudioObjectPropertyAddress *a, Boolean *out) {
    if (!has(d, id, pid, a)) return kAudioHardwareUnknownPropertyError;
    *out = false;
    return 0;
}
static OSStatus size(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a,
                    UInt32 qsize, const void *q, UInt32 *out) {
    (void)d; (void)pid; (void)qsize; (void)q;
    Property p = {0};
    if (!property(id, a, NULL, &p)) return kAudioHardwareUnknownPropertyError;
    *out = p.size;
    return 0;
}
static OSStatus get(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a,
                   UInt32 qsize, const void *q, UInt32 capacity, UInt32 *outSize, void *out) {
    (void)d; (void)pid;
    Property p = {0};
    CFStringRef qualifier = NULL;
    if (a->mSelector == kAudioPlugInPropertyTranslateUIDToDevice) {
        if (qsize != sizeof(qualifier) || !q) return kAudioHardwareBadPropertySizeError;
        memcpy(&qualifier, q, sizeof(qualifier));
    }
    if (!property(id, a, qualifier, &p)) return kAudioHardwareUnknownPropertyError;
    if (capacity < p.size) return kAudioHardwareBadPropertySizeError;
    if (p.size) {
        if (a->mSelector == kAudioObjectPropertyName || a->mSelector == kAudioObjectPropertyManufacturer ||
            a->mSelector == kAudioPlugInPropertyBundleID || a->mSelector == kAudioDevicePropertyDeviceUID ||
            a->mSelector == kAudioDevicePropertyModelUID) CFRetain(p.data.string);
        memcpy(out, &p.data, p.size);
    }
    *outSize = p.size;
    return 0;
}
static OSStatus set(AudioServerPlugInDriverRef d, AudioObjectID id, pid_t pid, const AudioObjectPropertyAddress *a,
                   UInt32 qsize, const void *q, UInt32 count, const void *in) {
    (void)d; (void)id; (void)pid; (void)a; (void)qsize; (void)q; (void)count; (void)in;
    return kAudioHardwareUnsupportedOperationError;
}

static OSStatus start(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 clientID) {
    (void)d; (void)clientID;
    if (id != Game && id != Chat) return kAudioHardwareBadObjectError;
    atomic_fetch_add_explicit(&endpoints[indexFor(id)].clients, 1, memory_order_relaxed);
    return 0;
}
static OSStatus stop(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 clientID) {
    (void)d; (void)clientID;
    if (id != Game && id != Chat) return kAudioHardwareBadObjectError;
    _Atomic uint32_t *clients = &endpoints[indexFor(id)].clients;
    uint32_t count = atomic_load_explicit(clients, memory_order_relaxed);
    while (count && !atomic_compare_exchange_weak_explicit(clients, &count, count - 1,
                                                            memory_order_relaxed, memory_order_relaxed)) {}
    return 0;
}
static OSStatus timestamp(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 clientID,
                          Float64 *sample, UInt64 *host, UInt64 *seed) {
    (void)d; (void)clientID;
    if (id != Game && id != Chat) return kAudioHardwareBadObjectError;
    uint64_t frame = (uint64_t)((mach_absolute_time() - anchor) / (ticksPerFrame * Period)) * Period;
    *sample = (Float64)frame;
    *host = anchor + (uint64_t)(frame * ticksPerFrame);
    *seed = 1;
    return 0;
}
static OSStatus willDo(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 clientID, UInt32 op,
                       Boolean *doIO, Boolean *inPlace) {
    (void)d; (void)clientID;
    if (id != Game && id != Chat) return kAudioHardwareBadObjectError;
    *doIO = op == kAudioServerPlugInIOOperationWriteMix || op == kAudioServerPlugInIOOperationReadInput;
    *inPlace = true;
    return 0;
}
static OSStatus boundary(AudioServerPlugInDriverRef d, AudioObjectID id, UInt32 clientID, UInt32 op,
                         UInt32 frames, const AudioServerPlugInIOCycleInfo *cycle) {
    (void)d; (void)id; (void)clientID; (void)op; (void)frames; (void)cycle;
    return 0;
}

static OSStatus io(AudioServerPlugInDriverRef d, AudioObjectID id, AudioObjectID stream, UInt32 clientID,
                   UInt32 op, UInt32 frames, const AudioServerPlugInIOCycleInfo *cycle, void *main, void *secondary) {
    (void)d; (void)clientID; (void)secondary;
    if (id != Game && id != Chat) return kAudioHardwareBadObjectError;
    bool write = op == kAudioServerPlugInIOOperationWriteMix;
    bool read = op == kAudioServerPlugInIOOperationReadInput;
    if (!write && !read) return 0;
    if (!main || stream != id + (write ? 2 : 1)) return kAudioHardwareBadObjectError;
    int64_t startFrame = write ? (int64_t)cycle->mOutputTime.mSampleTime
                              : (int64_t)cycle->mInputTime.mSampleTime - MonitorDelay;
    Endpoint *endpoint = &endpoints[indexFor(id)];
    float *pcm = main;
    for (UInt32 frame = 0; frame < frames; frame++) {
        int64_t position = startFrame + frame;
        uint32_t slot = (uint64_t)position % Capacity;
        // Timestamp and IEEE bits travel in one atomic word, so overwritten frames read as silence.
        uint64_t tag = (uint64_t)(uint32_t)(position + 1) << 32;
        for (unsigned ch = 0; ch < 2; ch++, pcm++) {
            _Atomic uint64_t *sample = &endpoint->samples[slot][ch];
            uint32_t bits = 0;
            if (write) {
                memcpy(&bits, pcm, sizeof(bits));
                atomic_store_explicit(sample, tag | bits, memory_order_relaxed);
            } else {
                uint64_t packed = atomic_load_explicit(sample, memory_order_relaxed);
                if (position >= 0 && (packed & UINT64_C(0xffffffff00000000)) == tag) bits = (uint32_t)packed;
                memcpy(pcm, &bits, sizeof(bits));
            }
        }
    }
    return 0;
}

static AudioServerPlugInDriverInterface interface = {
    .QueryInterface = query, .AddRef = retain, .Release = release,
    .Initialize = initialize, .CreateDevice = create, .DestroyDevice = destroy,
    .AddDeviceClient = client, .RemoveDeviceClient = client,
    .PerformDeviceConfigurationChange = configuration, .AbortDeviceConfigurationChange = configuration,
    .HasProperty = has, .IsPropertySettable = settable, .GetPropertyDataSize = size,
    .GetPropertyData = get, .SetPropertyData = set,
    .StartIO = start, .StopIO = stop, .GetZeroTimeStamp = timestamp,
    .WillDoIOOperation = willDo, .BeginIOOperation = boundary, .DoIOOperation = io,
    .EndIOOperation = boundary
};

__attribute__((visibility("default")))
void *ArctisDriverFactory(CFAllocatorRef allocator, CFUUIDRef type) {
    (void)allocator;
    if (!type || !CFEqual(type, kAudioServerPlugInTypeUUID)) return NULL;
    retain(&vtable);
    return &vtable;
}
