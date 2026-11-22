import CoreAudio
import Foundation

struct PlaybackDevice: Equatable, Identifiable {
    let id: AudioDeviceID
    let uid: String
    let name: String
    let rate: Double
    let transport: UInt32

    var physical: Bool {
        switch transport {
        case kAudioDeviceTransportTypeBuiltIn, kAudioDeviceTransportTypeUSB, kAudioDeviceTransportTypeBluetooth,
             kAudioDeviceTransportTypeBluetoothLE, kAudioDeviceTransportTypeHDMI, kAudioDeviceTransportTypeDisplayPort,
             kAudioDeviceTransportTypeThunderbolt, kAudioDeviceTransportTypeFireWire, kAudioDeviceTransportTypePCI:
            return true
        default:
            return false
        }
    }

    var arctis: Bool {
        let normalized = name.lowercased().filter { $0.isLetter || $0.isNumber || $0 == "+" }
        return transport == kAudioDeviceTransportTypeUSB &&
            (normalized.contains("arctis7+") || normalized.contains("arctis7plus"))
    }

    static func all() -> [Self] {
        var address = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal, mElement: kAudioObjectPropertyElementMain)
        let system = AudioObjectID(kAudioObjectSystemObject)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(system, &address, 0, nil, &size) == noErr else { return [] }
        var devices = [AudioDeviceID](repeating: 0, count: Int(size) / MemoryLayout<AudioDeviceID>.stride)
        guard !devices.isEmpty,
              AudioObjectGetPropertyData(system, &address, 0, nil, &size, &devices) == noErr else { return [] }
        return devices.compactMap { id in
            guard id != 0,
                  let uid = string(kAudioDevicePropertyDeviceUID, on: id),
                  let name = string(kAudioObjectPropertyName, on: id),
                  let rate = scalar(kAudioDevicePropertyNominalSampleRate, on: id, initial: 0.0),
                  let transport = scalar(kAudioDevicePropertyTransportType, on: id, initial: UInt32(0)),
                  scalar(kAudioDevicePropertyDeviceIsAlive, on: id, initial: UInt32(0)) == 1,
                  let bytes = propertySize(kAudioDevicePropertyStreams, on: id, scope: kAudioDevicePropertyScopeOutput),
                  bytes > 0 else { return nil }
            return Self(id: id, uid: uid, name: name, rate: rate, transport: transport)
        }.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }

    private static func propertySize(_ selector: UInt32, on id: AudioDeviceID, scope: UInt32) -> UInt32? {
        var address = AudioObjectPropertyAddress(mSelector: selector, mScope: scope,
            mElement: kAudioObjectPropertyElementMain)
        var size: UInt32 = 0
        return AudioObjectGetPropertyDataSize(id, &address, 0, nil, &size) == noErr ? size : nil
    }

    private static func scalar<T>(_ selector: UInt32, on id: AudioDeviceID, initial: T) -> T? {
        var address = AudioObjectPropertyAddress(mSelector: selector, mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain)
        var size = UInt32(MemoryLayout<T>.size)
        var value = initial
        return withUnsafeMutableBytes(of: &value) { bytes in
            AudioObjectGetPropertyData(id, &address, 0, nil, &size, bytes.baseAddress!) == noErr
        } ? value : nil
    }

    private static func string(_ selector: UInt32, on id: AudioDeviceID) -> String? {
        guard let unmanaged = scalar(selector, on: id, initial: Optional<Unmanaged<CFString>>.none) else { return nil }
        return unmanaged?.takeRetainedValue() as String?
    }
}
