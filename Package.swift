// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "MacArctisManager",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "arctis-hid", targets: ["ArctisHID"]),
        .executable(name: "MacArctisManager", targets: ["MacArctisManager"]),
    ],
    targets: [
        .target(name: "ArctisCore"),
        .target(name: "AudioEngine", linkerSettings: [.linkedFramework("CoreAudio"), .linkedFramework("AudioUnit")]),
        .executableTarget(name: "ArctisHID", dependencies: ["ArctisCore"]),
        .executableTarget(name: "MacArctisManager", dependencies: ["ArctisCore", "AudioEngine"]),
        .testTarget(name: "ArctisCoreTests", dependencies: ["ArctisCore"]),
    ],
    swiftLanguageModes: [.v5]
)
