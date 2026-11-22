#!/bin/bash
set -euo pipefail
driver=/Library/Audio/Plug-Ins/HAL/ArctisAudio.driver
if [[ ! -e "$driver" && ! -L "$driver" ]]; then
    echo "ArctisAudio.driver is already absent."
    exit 0
fi
if [[ -L "$driver" || ! -d "$driver" ]] ||
   [[ "$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$driver/Contents/Info.plist")" != org.macarctis.manager.driver ]]; then
    echo "Unexpected item at $driver. Inspect it before removal." >&2
    exit 1
fi
read -r -p "Remove $driver? [y/N] " answer
[[ "$answer" == y || "$answer" == Y ]] || exit 0
sudo /bin/rm -rf "$driver"
if /usr/sbin/pkgutil --pkg-info org.macarctis.manager.driver.pkg >/dev/null 2>&1; then
    sudo /usr/sbin/pkgutil --forget org.macarctis.manager.driver.pkg
fi
echo "Restart macOS when convenient to unload the driver."
