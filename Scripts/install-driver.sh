#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
pkg="${1:-dist/release/ArctisAudio.pkg}"
[[ -f "$pkg" ]] || { echo "Driver package missing. Run Scripts/build.sh first." >&2; exit 1; }
echo "This installs ArctisAudio.driver under /Library/Audio/Plug-Ins/HAL."
read -r -p "Install the package at $pkg? [y/N] " answer
[[ "$answer" == y || "$answer" == Y ]] || exit 0
sudo /usr/sbin/installer -pkg "$pkg" -target /
echo "Restart macOS when convenient, then verify Arctis Game and Arctis Chat in Audio MIDI Setup."
