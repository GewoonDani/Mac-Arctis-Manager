import ArctisCore
import Foundation

let arguments = Array(CommandLine.arguments.dropFirst())
guard let command = arguments.first, ["list", "watch"].contains(command),
      arguments.dropFirst().allSatisfy({ $0 == "--all-usb" }),
      arguments.count <= 2, command == "list" || arguments.count == 1 else {
    print("Usage: arctis-hid list [--all-usb] | arctis-hid watch")
    exit(arguments.isEmpty ? 0 : 64)
}

let manager = command == "watch"
    ? HIDDeviceManager(product: HIDDeviceManager.arctis7Plus, usagePages: [0xff00, 0xffc0])
    : HIDDeviceManager(vendor: arguments.contains("--all-usb") ? nil : HIDDeviceManager.steelSeries)
let clock = ProcessInfo.processInfo.systemUptime
func line(_ message: String) {
    print(String(format: "%8.3f ", ProcessInfo.processInfo.systemUptime - clock) + message)
    fflush(stdout)
}

if command == "list" {
    let devices = manager.interfaces
    devices.forEach { line($0.description) }
    if devices.isEmpty { line("No matching USB HID interfaces found.") }
} else {
    manager.onMatch = { interface, added in line("\(added ? "added" : "removed") \(interface.description)") }
    manager.onReport = { report in
        let bytes = report.bytes.map { String(format: "%02x", $0) }.joined(separator: " ")
        let id = String(format: "0x%02x", report.reportID)
        line("input registry=\(report.interface.id) interface=\(report.interface.number.map(String.init) ?? "?") report=\(id) len=\(report.bytes.count) \(bytes)")
    }
    manager.onError = { line(String(format: "HID callback error 0x%08x", UInt32(bitPattern: $0))) }
    let status = manager.start()
    guard status == 0 else {
        line(String(format: "HID open failed 0x%08x", UInt32(bitPattern: status)))
        exit(1)
    }
    line("Watching Arctis 7+ vendor USB HID input reports. Press Control-C to stop.")
    RunLoop.main.run()
}
