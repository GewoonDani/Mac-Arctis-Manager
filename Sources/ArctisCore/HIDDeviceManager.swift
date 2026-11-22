import Foundation
import IOKit.hid

public struct HIDInterface: Equatable {
    public let id: UInt64
    public let vendor: Int
    public let product: Int
    public let name: String
    public let number: Int?
    public let usagePage: Int
    public let usage: Int
    public let inputBytes: Int

    public var description: String {
        let ids = String(format: "%04x:%04x", vendor, product)
        let usages = String(format: "%04x:%04x", usagePage, usage)
        return "\(ids) \(name) registry=\(id) interface=\(number.map(String.init) ?? "?") usage=\(usages) maxInput=\(inputBytes)"
    }

    fileprivate init(_ device: IOHIDDevice) {
        func property(_ key: String) -> Any? { IOHIDDeviceGetProperty(device, key as CFString) }
        let service = IOHIDDeviceGetService(device)
        var registryID: UInt64 = 0
        IORegistryEntryGetRegistryEntryID(service, &registryID)
        id = registryID
        vendor = property(kIOHIDVendorIDKey) as? Int ?? 0
        product = property(kIOHIDProductIDKey) as? Int ?? 0
        name = property(kIOHIDProductKey) as? String ?? "Unknown"
        number = (property("InterfaceNumber") as? Int)
            ?? (IORegistryEntrySearchCFProperty(service, kIOServicePlane, "bInterfaceNumber" as CFString,
                kCFAllocatorDefault, IOOptionBits(kIORegistryIterateRecursively | kIORegistryIterateParents)) as? Int)
        usagePage = property(kIOHIDPrimaryUsagePageKey) as? Int ?? 0
        usage = property(kIOHIDPrimaryUsageKey) as? Int ?? 0
        inputBytes = property(kIOHIDMaxInputReportSizeKey) as? Int ?? 0
    }
}

public struct HIDReport {
    public let interface: HIDInterface
    public let reportID: UInt32
    public let bytes: [UInt8]
}

public final class HIDDeviceManager {
    public static let steelSeries = 0x1038
    public static let arctis7Plus = 0x220e
    private let manager = IOHIDManagerCreate(kCFAllocatorDefault, 0)
    private var scheduled = false
    public var onMatch: ((HIDInterface, Bool) -> Void)?
    public var onReport: ((HIDReport) -> Void)?
    public var onError: ((IOReturn) -> Void)?

    public init(vendor: Int? = steelSeries, product: Int? = nil, usagePages: [Int] = []) {
        var match: [String: Any] = [kIOHIDTransportKey: "USB"]
        match[kIOHIDVendorIDKey] = vendor
        match[kIOHIDProductIDKey] = product
        if usagePages.isEmpty {
            IOHIDManagerSetDeviceMatching(manager, match as CFDictionary)
        } else {
            IOHIDManagerSetDeviceMatchingMultiple(manager, usagePages.map { page in
                var collection = match
                collection[kIOHIDPrimaryUsagePageKey] = page
                return collection
            } as CFArray)
        }
    }

    public var interfaces: [HIDInterface] {
        let devices = IOHIDManagerCopyDevices(manager) as? Set<IOHIDDevice> ?? []
        return devices.map(HIDInterface.init).sorted { $0.id < $1.id }
    }

    // Start, callbacks, and teardown all run on the main run loop.
    public func start() -> IOReturn {
        precondition(Thread.isMainThread)
        if scheduled { return kIOReturnSuccess }
        let context = Unmanaged.passUnretained(self).toOpaque()
        IOHIDManagerRegisterDeviceMatchingCallback(manager, { context, result, _, device in
            guard let context else { return }
            let owner = Unmanaged<HIDDeviceManager>.fromOpaque(context).takeUnretainedValue()
            guard result == kIOReturnSuccess else { owner.onError?(result); return }
            owner.onMatch?(HIDInterface(device), true)
        }, context)
        IOHIDManagerRegisterDeviceRemovalCallback(manager, { context, _, _, device in
            guard let context else { return }
            Unmanaged<HIDDeviceManager>.fromOpaque(context).takeUnretainedValue().onMatch?(HIDInterface(device), false)
        }, context)
        IOHIDManagerRegisterInputReportCallback(manager, { context, result, sender, type, id, bytes, length in
            guard let context else { return }
            let owner = Unmanaged<HIDDeviceManager>.fromOpaque(context).takeUnretainedValue()
            guard result == kIOReturnSuccess else { owner.onError?(result); return }
            guard let sender, type == kIOHIDReportTypeInput, length > 0 else { return }
            let device = Unmanaged<IOHIDDevice>.fromOpaque(sender).takeUnretainedValue()
            owner.onReport?(HIDReport(interface: HIDInterface(device), reportID: id,
                                      bytes: Array(UnsafeBufferPointer(start: bytes, count: length))))
        }, context)
        IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetMain(), RunLoop.Mode.common.rawValue as CFString)
        scheduled = true
        let status = IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone))
        if status != kIOReturnSuccess { stop() }
        return status
    }

    public func stop() {
        precondition(Thread.isMainThread)
        guard scheduled else { return }
        IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetMain(), RunLoop.Mode.common.rawValue as CFString)
        IOHIDManagerClose(manager, IOOptionBits(kIOHIDOptionsTypeNone))
        scheduled = false
    }

    deinit { if scheduled { stop() } }
}
