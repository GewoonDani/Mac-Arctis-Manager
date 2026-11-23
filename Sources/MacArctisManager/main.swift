import AppKit
import ServiceManagement
import SwiftUI

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    let model = AppModel()

    func applicationWillTerminate(_ notification: Notification) {
        model.shutDown()
    }
}

@MainActor
struct MacArctisManager: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate

    var body: some Scene {
        MenuBarExtra("Mac Arctis Manager", systemImage: "headphones") {
            ControlPanel(model: appDelegate.model)
        }
        .menuBarExtraStyle(.window)
    }
}

struct ControlPanel: View {
    @ObservedObject var model: AppModel
    @State private var showsDiagnostics = false

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            VStack(alignment: .leading, spacing: 4) {
                Text("Mac Arctis Manager").font(.headline)
                Label(model.receiver ? "Arctis 7+ receiver connected" : "Arctis 7+ receiver disconnected",
                      systemImage: model.receiver ? "checkmark.circle.fill" : "circle.dashed")
                    .foregroundStyle(model.receiver ? Color.green : Color.secondary)
                Text(model.status).foregroundStyle(.secondary)
                Text(model.wheelStatus).font(.caption).foregroundStyle(.secondary)
            }

            Divider()

            VStack(spacing: 6) {
                Slider(value: Binding(get: { model.mix.balance }, set: { model.setBalance($0) }), in: -1...1) {
                    Text("ChatMix balance")
                } minimumValueLabel: {
                    Text("Game")
                } maximumValueLabel: {
                    Text("Chat")
                }
                .accessibilityValue(balanceLabel)
                HStack {
                    Text("Game \(model.mix.game, format: .percent.precision(.fractionLength(0)))")
                    Spacer()
                    Button("Center") { model.setBalance(0) }
                    Spacer()
                    Text("Chat \(model.mix.chat, format: .percent.precision(.fractionLength(0)))")
                }
                .font(.caption)
                .monospacedDigit()
            }

            Picker("Output", selection: $model.outputUID) {
                Text("Automatically select Arctis 7+").tag("")
                if !model.outputUID.isEmpty && !model.physicalOutputs.contains(where: { $0.uid == model.outputUID }) {
                    Text("Selected output (disconnected)").tag(model.outputUID)
                }
                ForEach(model.physicalOutputs) { output in Text(output.name).tag(output.uid) }
            }

            if model.hidPermissionDenied {
                Button("Open Input Monitoring Settings") {
                    openSettings("x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent")
                }
            }

            if model.needsAudioPermission {
                Button("Allow Virtual Audio Input") { model.requestAudioPermission() }
                Button("Open Microphone Settings") {
                    openSettings("x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone")
                }
            }

            Divider()

            Toggle("Open at login", isOn: Binding(
                get: { model.loginStatus == .enabled || model.loginStatus == .requiresApproval },
                set: { model.setLaunchAtLogin($0) }
            ))
            if model.loginStatus == .requiresApproval {
                Button("Open Login Items Settings") { SMAppService.openSystemSettingsLoginItems() }
            }
            if let error = model.loginError { Text(error).font(.caption).foregroundStyle(.secondary) }

            DisclosureGroup("Diagnostics", isExpanded: $showsDiagnostics) {
                VStack(alignment: .leading, spacing: 8) {
                    Text(model.diagnostics).font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                    HStack {
                        Button("Retry") { model.retry() }
                        Button("Copy") {
                            NSPasteboard.general.clearContents()
                            NSPasteboard.general.setString(model.diagnostics, forType: .string)
                        }
                        Button("Open Console") {
                            if let url = NSWorkspace.shared.urlForApplication(withBundleIdentifier: "com.apple.Console") {
                                NSWorkspace.shared.openApplication(at: url, configuration: .init())
                            }
                        }
                    }
                }
                .padding(.top, 6)
            }

            HStack {
                Spacer()
                Button("Quit") { NSApplication.shared.terminate(nil) }
                    .keyboardShortcut("q")
            }
        }
        .padding(16)
        .frame(width: 360)
    }

    private var balanceLabel: String {
        let balance = model.mix.balance
        if abs(balance) < 0.005 { return "Center" }
        return "\(Int((abs(balance) * 100).rounded())) percent toward \(balance < 0 ? "Game" : "Chat")"
    }

    private func openSettings(_ value: String) {
        if let url = URL(string: value) { NSWorkspace.shared.open(url) }
    }
}

if CommandLine.arguments == [CommandLine.arguments[0], "--list-audio"] {
    for device in PlaybackDevice.all() {
        print("\(device.id) \(device.name) | \(device.uid) | \(device.rate) Hz")
    }
} else {
    MacArctisManager.main()
}
