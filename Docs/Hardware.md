# Hardware investigation

- [Linux Arctis Manager's Arctis 7+ profile](https://github.com/elegos/Linux-Arctis-Manager/blob/develop/src/linux_arctis_manager/devices/arctis_7_plus.yaml) identifies USB VID 0x1038 and PIDs 0x220e, 0x2212, 0x2216 and 0x2236. The published profile uses interface 3 for status. Its unnumbered status payload starts with 0xb0 and puts absolute Game/Chat percentages at offsets 4 and 5. The same value should represent the same gain on every report. The profile requests status and also initializes unrelated settings. The macOS implementation sends no reports.
- [awth13's actual Python implementation](https://github.com/awth13/Linux-Arctis-7-Plus-ChatMix/blob/main/Arctis_7_Plus_ChatMix.py) uses 0x1038:0x220e and selects interface 5, its first interrupt endpoint, through PyUSB. It reads up to 64 bytes and applies payload bytes 1 and 2 as absolute percentages. That implementation does not check the prefix. It creates two PipeWire null sinks, links their stereo monitors to the real sink and changes the virtual sink volumes through pactl.
- [rust-arctis-chatmix's protocol transcription](https://github.com/rdamron/rust-arctis-chatmix/blob/main/src/devices.rs) corroborates the 0xb0 status layout and lists a 0x45 event candidate with Game/Chat at offsets 1 and 2. The capture below confirms the event format on the tested receiver. Passive reports may depend on firmware, power state or prior software initialization.

## Capture on the target Mac

From this repository:

```sh
swift build --product arctis-hid
.build/debug/arctis-hid list
.build/debug/arctis-hid watch | tee /tmp/arctis-hid-wheel.log
```

With the receiver connected and headset powered on, hold each position for about three seconds: center, full Game, center, full Chat, center. Sweep slowly in both directions, then unplug and reconnect the USB receiver and repeat once. Stop with Control-C. Share the timed steps, list output and capture, including an empty capture if no reports arrive. The log describes SteelSeries USB HID interfaces and prints input report IDs, actual lengths and hex payloads. The diagnostic opens the interfaces without seizing them or sending reports. Quit GG or other headset utilities during this test.

If open fails, include the displayed IOKit status. A denial from Input Monitoring can be resolved by granting the terminal application access in Privacy & Security, relaunching it and repeating the capture. Grant access only if the OS requires it.

## Observed on the development Mac

The USB receiver enumerated as `1038:220e`. IOHID reported interfaces 5 (usage ff00:0001, maximum input length 64), 4 (consumer usage 000c:0001, maximum 4), and 3 (usage ffc0:0001, maximum 64). Report monitoring is limited to the two vendor-defined collections. The managed development session received `0xe00002e2` (not permitted) when opening those HID collections. A user Terminal run captured passive wheel reports.

The sweep produced input report ID `0x00` with 64-byte payloads on interface 5. Byte 0 was `0x45`, followed by absolute Game and Chat percentages at zero-based offsets 1 and 2. The observed payloads started with `45 64 00` at full Game (4.120 s), `45 64 64` at center (8.920 s), and `45 00 64` at full Chat (12.840 s). The user confirmed the dial worked in the running app after applying the calibration. The supplied capture ends before a reconnect, which still needs verification.

Offline replay through `WheelCalibration` decoded all 29 captured reports and confirmed the two endpoints and center.

## Applying a verified calibration

The app bundle includes the [1038:220e calibration](Calibrations/Arctis-7-Plus-1038-220e.json) from the sweep above. To override it, save a profile verified with a passive capture to `~/Library/Application Support/Mac Arctis Manager/WheelCalibration.json` and choose **Diagnostics → Retry**.

A hardware capture must establish a distinct input report prefix, ID, length, interface, byte offsets and absolute readings for center and both ends. The JSON object has integer fields `interface`, `reportID`, `length`, an integer-array `prefix` containing stable leading bytes, and two objects `game` and `chat`. Each channel object has an integer byte `offset`, a `muted` raw value measured with that channel suppressed, and a `centered` raw value measured in the center detent. Use decimal numbers in JSON. The app ignores reports with a different signature.
