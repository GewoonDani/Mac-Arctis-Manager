#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
export CLANG_MODULE_CACHE_PATH="$PWD/.build/clang-cache"
mkdir -p "$CLANG_MODULE_CACHE_PATH" .build/native-tests
swift test --disable-sandbox -Xswiftc -warnings-as-errors

destination=.build/native-tests
driver="$destination/ArctisAudio.driver"
mkdir -p "$driver/Contents/MacOS"
cp Packaging/Driver-Info.plist "$driver/Contents/Info.plist"
flags=(-arch arm64 -mmacosx-version-min=14.0 -std=c17 -O2 -Wall -Wextra -Werror)
xcrun clang "${flags[@]}" -fvisibility=hidden -bundle Driver/VirtualAudioDriver.c \
    -framework CoreAudio -framework CoreFoundation -o "$driver/Contents/MacOS/ArctisAudio"
codesign --force --sign - "$driver"
xcrun clang "${flags[@]}" Tests/Native/DriverTest.c -framework CoreAudio -framework CoreFoundation \
    -o "$destination/driver-test"
xcrun clang "${flags[@]}" -I Sources/AudioEngine/include Tests/Native/MixerTest.c \
    -framework CoreAudio -framework AudioUnit -o "$destination/mixer-test"
"$destination/driver-test" "$PWD/$driver"
"$destination/mixer-test"
