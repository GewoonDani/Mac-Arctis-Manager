#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
configuration=${1:-release}
case "$configuration" in
    debug|release) ;;
    *) echo "Usage: $0 [debug|release]" >&2; exit 64 ;;
esac
export CLANG_MODULE_CACHE_PATH="$PWD/.build/clang-cache"
mkdir -p "$CLANG_MODULE_CACHE_PATH"
build_options=()
if [[ "$configuration" == release ]]; then build_options=(-debug-info-format none); fi
swift build --disable-sandbox -c "$configuration" --arch arm64 -Xswiftc -warnings-as-errors ${build_options[@]+"${build_options[@]}"}
bin=$(swift build --disable-sandbox -c "$configuration" --arch arm64 --show-bin-path)
destination="$PWD/dist/$configuration"
mkdir -p "$destination"
stage=$(mktemp -d "$destination/.build.XXXXXX")
trap 'rm -rf "$stage"' EXIT

app="$stage/Mac Arctis Manager.app"
driver="$stage/payload/ArctisAudio.driver"
mkdir -p "$app/Contents/"{MacOS,Resources} "$driver/Contents/MacOS"
install -m 755 "$bin/MacArctisManager" "$app/Contents/MacOS/MacArctisManager"
install -m 755 "$bin/arctis-hid" "$stage/arctis-hid"
install -m 644 Packaging/App-Info.plist "$app/Contents/Info.plist"
install -m 644 Packaging/Driver-Info.plist "$driver/Contents/Info.plist"
install -m 644 LICENSE THIRD_PARTY.md "$app/Contents/Resources/"
install -m 644 Docs/Calibrations/Arctis-7-Plus-1038-220e.json "$app/Contents/Resources/"
xcrun clang -arch arm64 -mmacosx-version-min=14.0 -std=c17 -O2 -Wall -Wextra -Werror \
    -fvisibility=hidden -bundle Driver/VirtualAudioDriver.c -framework CoreAudio -framework CoreFoundation \
    -o "$driver/Contents/MacOS/ArctisAudio"
xcrun clang -arch arm64 -mmacosx-version-min=14.0 -std=c17 -O2 -Wall -Wextra -Werror \
    -I Sources/AudioEngine/include Tools/AudioTone.c -framework CoreAudio -framework AudioUnit -framework CoreFoundation \
    -o "$stage/arctis-tone"
plutil -lint "$app/Contents/Info.plist" "$driver/Contents/Info.plist" Packaging/App.entitlements
identity=${APP_SIGN_IDENTITY:--}
sign_options=()
if [[ "$identity" != - ]]; then sign_options=(--options runtime --timestamp); fi
codesign --force --sign "$identity" ${sign_options[@]+"${sign_options[@]}"} --entitlements Packaging/App.entitlements "$app"
codesign --force --sign "$identity" ${sign_options[@]+"${sign_options[@]}"} "$driver"
codesign --verify --strict --verbose=2 "$app" "$driver"

installer_options=()
if [[ -n "${INSTALLER_SIGN_IDENTITY:-}" ]]; then
    installer_options=(--sign "$INSTALLER_SIGN_IDENTITY" --timestamp)
fi
COPYFILE_DISABLE=1 pkgbuild --root "$stage/payload" --component-plist Packaging/Driver-Package.plist \
    --install-location /Library/Audio/Plug-Ins/HAL \
    --identifier org.macarctis.manager.driver.pkg --version 0.1.0 --ownership recommended \
    ${installer_options[@]+"${installer_options[@]}"} "$stage/ArctisAudio.pkg"
pkgutil --payload-files "$stage/ArctisAudio.pkg"

mv "$driver" "$stage/ArctisAudio.driver"
for name in 'Mac Arctis Manager.app' ArctisAudio.driver ArctisAudio.pkg arctis-hid arctis-tone; do
    rm -rf "$destination/$name"
    mv "$stage/$name" "$destination/$name"
done
printf 'Build artifacts: %s\n' "$destination"
