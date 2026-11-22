# Distribution

A local development build is ad-hoc signed. For distribution, use Developer ID Application and Developer ID Installer certificates from the same team, with their private keys available to codesign and pkgbuild. The app uses the hardened runtime when signed with a Developer ID and has the audio-input entitlement for its two CoreAudio monitor streams.

```sh
APP_SIGN_IDENTITY='Developer ID Application: Example (TEAMID)' \
INSTALLER_SIGN_IDENTITY='Developer ID Installer: Example (TEAMID)' \
Scripts/build.sh release

cd dist/release
ditto -c -k --keepParent 'Mac Arctis Manager.app' Mac-Arctis-Manager.zip
xcrun notarytool submit Mac-Arctis-Manager.zip --keychain-profile arctis-notary --wait
xcrun notarytool submit ArctisAudio.pkg --keychain-profile arctis-notary --wait
xcrun stapler staple 'Mac Arctis Manager.app'
xcrun stapler staple ArctisAudio.pkg
ditto -c -k --keepParent 'Mac Arctis Manager.app' Mac-Arctis-Manager.zip
spctl --assess --type execute -v 'Mac Arctis Manager.app'
spctl --assess --type install -v ArctisAudio.pkg
```

Create the `arctis-notary` keychain profile using `xcrun notarytool store-credentials`. Publish the app archive and driver package together with a corresponding source tag. Keep the two components explicit so users can install the system-wide driver knowingly. Verify fresh install, upgrade, uninstallation, audio routing and login startup on a clean Apple Silicon Mac with SIP enabled before shipping. The repository workflow builds and tests locally signed artifacts. Developer ID signing and notarization require external credentials and a live verification run.

[Apple notarization documentation](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
