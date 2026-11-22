# Mac Arctis Manager

- Build from the repository root with `Scripts/build.sh` and run `Scripts/test.sh`. Both are local and require no privileged installation.
- Keep targeting Apple Silicon and macOS 14 unless the user requests a change. The Swift package owns app targets. The SDK C driver compiles and packages separately.
- Preserve stable HAL UIDs and the two isolated endpoints. Keep audio callbacks allocation-free and lock-free. Use the native harnesses to exercise I/O and DSP changes.
- Do not add USB commands or enable a guessed wheel profile. Record the receiver/interface, report framing, center and endpoints from passive hardware evidence.
- Ask the user before installing or removing the system HAL driver, restarting audio services, or changing system audio defaults. The SDK harness runs in a normal process without loading anything into coreaudiod.
- Record live validation and offline results separately. Keep third-party protocol attribution and licenses current.
