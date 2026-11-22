import Foundation
import XCTest
@testable import ArctisCore

final class ChatMixTests: XCTestCase {
    // Synthetic fixture. It does not describe a captured Arctis 7+ report.
    private let calibration = """
    {"interface":5,"reportID":0,"length":4,"prefix":[165],
     "game":{"offset":1,"muted":20,"centered":92},
     "chat":{"offset":2,"muted":211,"centered":111}}
    """

    func testManualBalance() {
        XCTAssertEqual(ChatMix(balance: -2).game, 1)
        XCTAssertEqual(ChatMix(balance: -2).chat, 0)
        XCTAssertEqual(ChatMix(balance: 0).game, 1)
        XCTAssertEqual(ChatMix(balance: 0).chat, 1)
        XCTAssertEqual(ChatMix(balance: 0.25).game, 0.75)
        XCTAssertEqual(ChatMix(balance: 0.25).chat, 1)
        XCTAssertEqual(ChatMix(balance: .nan).balance, 0)
    }

    func testCalibrationUsesAbsoluteReadingsAndRejectsUnrelatedReports() throws {
        let profile = try WheelCalibration.load(Data(calibration.utf8))
        let center = profile.decode(interface: 5, reportID: 0, bytes: [165, 92, 111, 0])
        XCTAssertEqual(center, ChatMix(balance: 0))
        XCTAssertEqual(profile.decode(interface: 5, reportID: 0, bytes: [165, 56, 111, 0]), ChatMix(balance: 0.5))
        XCTAssertEqual(profile.decode(interface: 5, reportID: 0, bytes: [165, 92, 211, 0]), ChatMix(balance: -1))
        XCTAssertNil(profile.decode(interface: 3, reportID: 0, bytes: [165, 92, 111, 0]))
        XCTAssertNil(profile.decode(interface: 5, reportID: 1, bytes: [165, 92, 111, 0]))
        XCTAssertNil(profile.decode(interface: 5, reportID: 0, bytes: [162, 92, 111, 0]))
        XCTAssertNil(profile.decode(interface: 5, reportID: 0, bytes: [165, 92, 111]))
    }

    func testCalibrationRejectsUnsafeOffsetsAndUnusableRange() {
        for invalid in [
            calibration.replacingOccurrences(of: "\"offset\":1", with: "\"offset\":-1"),
            calibration.replacingOccurrences(of: "\"offset\":1", with: "\"offset\":2"),
            calibration.replacingOccurrences(of: "\"offset\":1", with: "\"offset\":4"),
            calibration.replacingOccurrences(of: "\"centered\":92", with: "\"centered\":20")
        ] {
            XCTAssertThrowsError(try WheelCalibration.load(Data(invalid.utf8)))
        }
    }

    func testManualOverrideLastsUntilWheelChangesOrReceiverReconnects() {
        var controller = ChatMixController(balance: 0)
        let game = ChatMix(balance: -1)
        XCTAssertTrue(controller.acceptWheel(game))
        controller.setManual(balance: 0.5)
        XCTAssertFalse(controller.acceptWheel(game))
        XCTAssertEqual(controller.mix.balance, 0.5)
        controller.receiverRemoved()
        XCTAssertTrue(controller.acceptWheel(game))
        XCTAssertEqual(controller.mix.balance, -1)
    }
}
