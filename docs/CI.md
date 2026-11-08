# CI/CD

Workflows in `.github/workflows/` (modelled on the old Xylonic app's android/ios/desktop/release set, rebuilt for CMake and Gradle):

| Workflow | Runs on | Produces |
|---|---|---|
| `ci.yml` | every push to `main`, every pull request | calls `linux.yml` (build + tests) |
| `linux.yml` | `ubuntu-24.04` and `ubuntu-24.04-arm`; reusable, or start by hand | `xylonic-fiber-<version>-linux-<arch>.tar.gz` (stripped binary + `assets/`) |
| `android.yml` | start by hand (debug or release) or from `release.yml` | `xylonic-fiber-<version>-android-<debug\|release>.apk`, arm64-v8a + armeabi-v7a |
| `ios.yml` | `macos-15`; start by hand or from `release.yml` | `xylonic-fiber-<version>-ios-unsigned.ipa` (or `-signed.ipa`) |
| `release.yml` | push of a tag `v<version>` | checks the tag equals `kVersion` in `src/app/app.h`, builds all of the above, publishes a GitHub **prerelease** with the CHANGELOG section as notes |

Release flow: bump the version (`src/app/app.h`, `CMakeLists.txt`, `android/app/build.gradle`), write the CHANGELOG section, `chore(release): 0.x.y` commit, `git tag -a v0.x.y`, push the commit and the tag. Promote after testing: `gh release edit v0.x.y --prerelease=false --latest`.

## Testing the IPA on your iPhone

The build needs no Apple account, so the IPA is **unsigned**. Install it with one of:

- **Sideloadly** (Windows/macOS/Linux-via-wine) or **AltStore**: sign in with your own Apple ID; the app is signed on your machine and runs for 7 days (free account) or a year (paid), then re-sign.
- **TrollStore** on a supported iOS version: installs unsigned IPAs permanently.

To get a ready-signed IPA out of CI instead, add these repository secrets and re-run `ios.yml`: `IOS_CERT_P12_BASE64` (development or ad-hoc certificate, `base64 -i cert.p12`), `IOS_CERT_PASSWORD`, `IOS_PROVISION_BASE64` (a provisioning profile that contains your iPhone's UDID, `base64 -i profile.mobileprovision`). The bundle id is `org.xylonic.fiber`. The device must be listed in the profile.

### One command on Linux: `scripts/ios/ios-autoload.sh`

Carried over from the old Xylonic app (`scripts/ios-*.sh`, same iLoader + zsign + pymobiledevice3 flow), with two changes: it keeps its own signing folder
(`~/.xylonic-fiber-sign`, override `XF_SIGN_DIR`) and it picks the provisioning profile issued for `org.xylonic.fiber` out of the ones on the phone
(`XF_BUNDLE_MATCH`), so it can never reuse the old Xylonic app's app id and replace that app.

| Script | Does |
|---|---|
| `scripts/ios/download-ios-ipa.sh [dir]` | `gh`: downloads the `ios-ipa` artifact of the latest successful `ios.yml` (or `release.yml`) run |
| `scripts/ios/ios-extract-signing.py` | rebuilds `cert.p12`, the profile and the bundle id from the iLoader key in the keyring and the profile on the phone |
| `scripts/ios/ios-tunnel.sh [--stop]` | starts the `pymobiledevice3 remote tunneld` daemon (one sudo prompt) |
| `scripts/ios/ios-autoload.sh` | download, sign with `zsign`, install over USB with `pymobiledevice3 apps install` |

First time (the free-account app id does not exist yet): download the unsigned IPA (`bash scripts/ios/download-ios-ipa.sh`), install it once with
**iLoader**. That creates the app id `org.xylonic.fiber.<team>` and its profile on the phone. From then on `bash scripts/ios/ios-autoload.sh` is all
you run; repeat the iLoader step when the 7-day profile is about to expire (`0xe8008018` at the install step). Needs `gh`, `zsign`, `pymobiledevice3`,
`python3-gi` + `gir1.2-secret-1` (setup table: the old repo's `IOS_SETUP.md`).

## Android signing

Without secrets the release job produces an unsigned APK. For a signed one set `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD` (the build reads `android/keystore.properties`, see `android/app/build.gradle`).

## Notes

- Linux tests run with the dummy SDL audio/video drivers; the `audio` test is excluded because it hangs on a runner without sound hardware.
- The iOS and Android workflows could not be run from the development machine: the first push of a tag or a manual run is their first real test, so expect a round of fixes (Xcode scheme names and the CMake iOS target are the likeliest).
