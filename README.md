# Mac Arctis Manager

A native macOS menu bar app for routing separate Game and Chat audio streams to a SteelSeries Arctis 7+ USB receiver. Requires Apple Silicon, macOS 14 or later, and Xcode with Swift 6 to build from source.

The app includes a physical ChatMix wheel calibration verified on an Arctis 7+ USB receiver with ID `1038:220e`. Manual ChatMix is also available. The app works without SteelSeries GG or Sonar.

**Development status.** The wheel has been verified live on one Mac. Reconnect behavior and installation of a notarized release on a clean Mac still need verification. Offline tests cover the two virtual audio streams and the mixer.

## Build and test

```sh
Scripts/test.sh
Scripts/build.sh
```

The build places the app, driver, installer and diagnostic executables in `dist/release/`. It signs locally with an ad-hoc identity by default and makes no system changes.

## Install and route audio

1. Open `ArctisAudio.pkg`, complete the installer and restart macOS when convenient. Administrator credentials are required to install the HAL bundle. When building from source, the package is in `dist/release/`.
2. Open Audio MIDI Setup and check both **Arctis Game** and **Arctis Chat**. The app executable also supports a read-only listing:

   ```sh
   "dist/release/Mac Arctis Manager.app/Contents/MacOS/MacArctisManager" --list-audio
   ```

3. Copy **Mac Arctis Manager.app** to Applications and open it. Use the headphones icon in the menu bar. Automatic output selects the unique Arctis 7+ USB playback device. Use the output picker if the receiver has a different audio name.
4. Use **Allow Virtual Audio Input** when prompted. macOS calls this **Microphone** access because the driver provides loopback monitor input streams. The app opens those two inputs only. Set Discord's input to your physical headset microphone.
5. Choose **Arctis Chat** for Discord's output. Choose **Arctis Game** for your game or set it as the system output for apps that follow the system default. The app keeps routing while the menu is closed and leaves default audio devices under your control. Use **Open at login** after moving the app to Applications if you want it to launch automatically.
6. Turn the physical ChatMix wheel and check for **Wheel active**. If the app shows **Open Input Monitoring Settings**, grant the app access there and relaunch it.

## Verify simultaneous playback

With the app reporting **Routing to Arctis 7+**, start at low headset volume and run:

```sh
dist/release/arctis-tone
```

It plays 440 Hz to Arctis Game and 660 Hz to Arctis Chat simultaneously for 20 seconds. Both should be audible at center. Slide fully toward Game and then Chat to hear each separately. In Diagnostics, both input peaks should become nonzero and the rendered frame counter should advance. Repeat with a game/music app and Discord. Unplug the receiver while playing, wait for the route to report it unavailable, then reconnect and verify playback resumes. If an application switched its own assigned output, reselect its virtual destination. Capture the status and diagnostics if playback fails.

## Capture the wheel

If your Arctis 7+ wheel does not respond with the included calibration, follow [Hardware investigation](Docs/Hardware.md) to capture and label a passive HID sweep.

## Uninstall

1. Select a physical device as macOS's sound output and revert app-specific output selections as desired. Disable **Open at login** and quit Mac Arctis Manager.
2. Run `Scripts/uninstall-driver.sh`, confirm the removal and restart macOS when convenient.
3. Move **Mac Arctis Manager.app** to Trash. Saved settings and the optional calibration remain in your user account so they can be reused if you reinstall.

The app and plug-in use public APIs with SIP enabled. See [Architecture](Docs/Architecture.md), [Distribution](Docs/Distribution.md), [License](LICENSE) and [Attribution](THIRD_PARTY.md).
