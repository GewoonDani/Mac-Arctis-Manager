import Foundation

public struct ChatMix: Equatable {
    public let game: Float
    public let chat: Float
    public var balance: Double { Double(chat - game) }

    public init(balance: Double) {
        let position = balance.isFinite ? Float(max(-1, min(1, balance))) : 0
        self.init(game: 1 - max(0, position), chat: 1 + min(0, position))
    }

    fileprivate init(game: Float, chat: Float) {
        self.game = game
        self.chat = chat
    }
}

public struct WheelCalibration: Decodable {
    public struct Channel: Decodable {
        public let offset: Int
        public let muted: UInt8
        public let centered: UInt8

        fileprivate func gain(_ value: UInt8) -> Float {
            let position = Float(Int(value) - Int(muted)) / Float(Int(centered) - Int(muted))
            return max(0, min(1, position))
        }
    }

    public let interface: Int
    public let reportID: UInt32
    public let length: Int
    public let prefix: [UInt8]
    public let game: Channel
    public let chat: Channel

    public static func load(_ data: Data) throws -> Self {
        let profile = try JSONDecoder().decode(Self.self, from: data)
        guard [3, 5].contains(profile.interface), profile.reportID <= 255,
              (3...64).contains(profile.length), !profile.prefix.isEmpty,
              profile.prefix.count <= min(profile.game.offset, profile.chat.offset),
              profile.game.offset != profile.chat.offset,
              max(profile.game.offset, profile.chat.offset) < profile.length,
              profile.game.muted != profile.game.centered, profile.chat.muted != profile.chat.centered else {
            throw CocoaError(.fileReadCorruptFile)
        }
        return profile
    }

    public func decode(interface: Int?, reportID: UInt32, bytes: [UInt8]) -> ChatMix? {
        guard interface == self.interface, reportID == self.reportID,
              bytes.count == length, bytes.starts(with: prefix) else { return nil }
        return ChatMix(game: game.gain(bytes[game.offset]), chat: chat.gain(bytes[chat.offset]))
    }
}

public struct ChatMixController {
    public private(set) var mix: ChatMix
    private var previousWheel: ChatMix?

    public init(balance: Double) { mix = ChatMix(balance: balance) }

    public mutating func setManual(balance: Double) { mix = ChatMix(balance: balance) }

    @discardableResult
    public mutating func acceptWheel(_ reading: ChatMix) -> Bool {
        guard previousWheel != reading else { return false }
        previousWheel = reading
        mix = reading
        return true
    }

    public mutating func receiverRemoved() { previousWheel = nil }
}
