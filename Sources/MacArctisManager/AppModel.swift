import ArctisCore
import AudioEngine
import AVFoundation
import CoreAudio
import Foundation
import IOKit
import ServiceManagement
import os

@MainActor
final class AppModel: ObservableObject {
    private static let log = Logger(subsystem: "org.macarctis.manager", category: "routing")

    @Published private(set) var mix: ChatMix
    @Published private(set) var physicalOutputs: [PlaybackDevice] = []
    @Published private(set) var driverReady = false
    @Published private(set) var receiver = false
    @Published private(set) var status = "Checking audio devices…"
    @Published private(set) var wheelStatus = "Waiting for receiver"
    var hidPermissionDenied: Bool { hidError == kIOReturnNotPermitted }
    @Published private(set) var loginStatus = SMAppService.mainApp.status
    @Published private(set) var loginError: String?
    @Published private(set) var needsAudioPermission = false
    @Published private(set) var stats = arctisMixerGetStats(nil)
    @Published var outputUID: String {
        didSet {
            UserDefaults.standard.set(outputUID, forKey: "outputUID")
            refresh()
        }
    }

    private var controller: ChatMixController
    private var wheelProfile: WheelCalibration?
    private var profileError: String?
    private var hidError: IOReturn?
    private var interfaceIDs = Set<UInt64>()
    private let hid = HIDDeviceManager(product: HIDDeviceManager.arctis7Plus, usagePages: [0xff00, 0xffc0])
    private var mixer: OpaquePointer?
    private var route: [PlaybackDevice] = []
    private var lastAttempt: Date = .distantPast
    private var lastProgress: Date = .distantPast
    private var timer: Timer?

    init() {
        let balance = UserDefaults.standard.double(forKey: "balance")
        controller = ChatMixController(balance: balance)
        mix = controller.mix
        outputUID = UserDefaults.standard.string(forKey: "outputUID") ?? ""
        reloadWheelProfile()
        hid.onMatch = { [weak self] interface, added in
            guard let self else { return }
            if added { self.interfaceIDs.insert(interface.id) } else { self.interfaceIDs.remove(interface.id) }
            self.updateWheelStatus()
        }
        hid.onError = { [weak self] status in
            Self.log.error("HID callback failed (\(status))")
            self?.hidError = status
            self?.updateWheelStatus()
        }
        hid.onReport = { [weak self] report in
            guard let self, let reading = self.wheelProfile?.decode(interface: report.interface.number,
                reportID: report.reportID, bytes: report.bytes),
                self.controller.acceptWheel(reading) else { return }
            self.wheelStatus = "Wheel active"
            self.applyMix()
        }
        startHID()
        refresh()
        timer = Timer(timeInterval: 1, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.refresh() }
        }
        if let timer { RunLoop.main.add(timer, forMode: .common) }
    }

    deinit {
        timer?.invalidate()
        arctisMixerStop(mixer)
    }

    func setBalance(_ value: Double) {
        controller.setManual(balance: value)
        UserDefaults.standard.set(controller.mix.balance, forKey: "balance")
        applyMix()
    }

    private func applyMix() {
        mix = controller.mix
        arctisMixerSetGains(mixer, mix.game, mix.chat)
    }

    func reloadWheelProfile() {
        let userProfile = URL.applicationSupportDirectory.appending(path: "Mac Arctis Manager/WheelCalibration.json")
        let url = FileManager.default.fileExists(atPath: userProfile.path)
            ? userProfile
            : Bundle.main.url(forResource: "Arctis-7-Plus-1038-220e", withExtension: "json")
        wheelProfile = nil
        profileError = nil
        do {
            if let url { wheelProfile = try WheelCalibration.load(Data(contentsOf: url)) }
        } catch {
            profileError = "Wheel calibration file is invalid"
        }
        controller.receiverRemoved()
        updateWheelStatus()
    }

    func startHID() {
        if hidError != nil { hid.stop() }
        let result = hid.start()
        hidError = result == 0 ? nil : result
        updateWheelStatus()
    }

    private func updateWheelStatus() {
        let present = !interfaceIDs.isEmpty || (hidError != nil && !hid.interfaces.isEmpty)
        if receiver && !present { controller.receiverRemoved() }
        receiver = present
        if let hidError {
            wheelStatus = String(format: "HID open failed (0x%08x)", UInt32(bitPattern: hidError))
        } else if !present {
            wheelStatus = "Waiting for receiver"
        } else if let profileError {
            wheelStatus = profileError
        } else {
            wheelStatus = wheelProfile == nil ? "Wheel capture needed" : "Waiting for wheel movement"
        }
    }

    func requestAudioPermission() {
        AVCaptureDevice.requestAccess(for: .audio) { [weak self] _ in
            DispatchQueue.main.async { self?.retry() }
        }
    }

    func retry() {
        lastAttempt = .distantPast
        reloadWheelProfile()
        startHID()
        refresh()
    }

    func setLaunchAtLogin(_ enabled: Bool) {
        do {
            if enabled { try SMAppService.mainApp.register() }
            else { try SMAppService.mainApp.unregister() }
            loginError = nil
        } catch { loginError = error.localizedDescription }
        loginStatus = SMAppService.mainApp.status
    }

    private func refresh() {
        loginStatus = SMAppService.mainApp.status
        if hidError != nil {
            let present = !hid.interfaces.isEmpty
            if present != receiver { startHID() }
        }
        let devices = PlaybackDevice.all()
        let physical = devices.filter(\.physical)
        if physical != physicalOutputs { physicalOutputs = physical }
        let game = devices.first { $0.uid == ARCTIS_GAME_UID }
        let chat = devices.first { $0.uid == ARCTIS_CHAT_UID }
        driverReady = game != nil && chat != nil
        needsAudioPermission = false

        guard let game, let chat else {
            disconnect("Virtual driver missing")
            return
        }
        let automatic = physical.filter(\.arctis)
        let output = outputUID.isEmpty
            ? (automatic.count == 1 ? automatic.first : nil)
            : physical.first { $0.uid == outputUID }
        guard let output else {
            disconnect(outputUID.isEmpty
                ? (automatic.count > 1 ? "Choose an Arctis output" : "Arctis audio output unavailable")
                : "Selected output disconnected")
            return
        }

        let authorization = AVCaptureDevice.authorizationStatus(for: .audio)
        guard authorization == .authorized else {
            needsAudioPermission = true
            disconnect(authorization == .notDetermined
                ? "Allow audio input for the virtual monitors"
                : "Audio input is unavailable. Check Microphone access.")
            return
        }

        let newRoute = [game, chat, output]
        if route != newRoute {
            disconnect("Opening \(output.name)…")
            route = newRoute
            lastAttempt = .distantPast
        }
        if let mixer {
            let updated = arctisMixerGetStats(mixer)
            if updated.renderedFrames > stats.renderedFrames &&
                updated.capturedFrames.0 > stats.capturedFrames.0 &&
                updated.capturedFrames.1 > stats.capturedFrames.1 {
                lastProgress = .now
            }
            stats = updated
            if Date.now.timeIntervalSince(lastProgress) < 5 { return }
            closeMixer()
            setStatus("Audio callbacks stopped. Reconnecting…")
        }

        guard Date.now.timeIntervalSince(lastAttempt) >= 3 else { return }
        lastAttempt = .now
        var error: OSStatus = noErr
        mixer = arctisMixerStart(game.id, chat.id, output.id, &error)
        if mixer != nil {
            lastProgress = .now
            applyMix()
            setStatus("Routing to \(output.name)")
        } else {
            setStatus("Couldn’t open audio route (\(error))")
        }
    }

    private func disconnect(_ message: String) {
        closeMixer()
        route = []
        setStatus(message)
    }

    private func closeMixer() {
        guard let mixer else { return }
        arctisMixerStop(mixer)
        self.mixer = nil
        stats = arctisMixerGetStats(nil)
    }

    private func setStatus(_ message: String) {
        guard message != status else { return }
        status = message
        Self.log.info("\(message, privacy: .public)")
    }

    var diagnostics: String {
        """
        Driver visible: \(driverReady ? "yes" : "no")
        USB receiver: \(receiver ? "detected" : "not detected")
        Wheel: \(wheelStatus)
        Route: \(status)
        Input peaks Game/Chat: \(stats.inputPeak.0.formatted(.number.precision(.fractionLength(3))))/\(stats.inputPeak.1.formatted(.number.precision(.fractionLength(3))))
        Captured frames Game/Chat: \(stats.capturedFrames.0)/\(stats.capturedFrames.1)
        Rendered frames: \(stats.renderedFrames)
        Underruns Game/Chat: \(stats.underruns.0)/\(stats.underruns.1)
        Overflows Game/Chat: \(stats.overflows.0)/\(stats.overflows.1)
        Capture errors Game/Chat: \(stats.captureErrors.0)/\(stats.captureErrors.1)
        """
    }
}
