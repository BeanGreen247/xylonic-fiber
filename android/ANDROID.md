# Android build (Xylonic Fiber)

The Android app is the same C++ code as the desktop app, loaded by SDL2's activity as `libmain.so`, plus three small Java classes
(`XylonicActivity`, `MediaService`/`MediaBridge` for background playback and the media session, `SecretStore` for Keystore credentials).

## Build the APK

Requirements: JDK 17 or 21 (with `javac`), Android SDK with platform 35, build-tools 35, NDK 27.2.12479018 and CMake 3.22.1
(Gradle installs missing NDK/CMake itself when the SDK licences are accepted), and network access for the first configure
(SDL2 and mbedTLS are fetched).

```
./android/build-apk.sh                 # arm64-v8a + armeabi-v7a debug APK
./android/build-apk.sh --emulator      # x86_64 debug APK for the Android emulator
```

The script runs `android/setup.sh` once (downloads SDL2 2.32.10 and copies its Java glue), writes `android/local.properties` from
`$ANDROID_HOME`, and calls `./gradlew assembleDebug`. Output: `android/app/build/outputs/apk/debug/app-debug.apk`.
Install with `adb install -r <apk>`.

## Tested

Built with Gradle 8.9 / AGP 8.7.3 / NDK r27c and run on an Android 14 x86_64 emulator against a local Subsonic server
(`adb reverse tcp:4533 tcp:4533`): launch, server discovery, login (credentials stored in the Keystore, auto-login on the next
start), library, performance-cache build, playback, keyboard handling in the login form, background playback with the app
backgrounded, foreground-service notification (3 actions) and media-session pause / play / next from outside the app.
Not tested on a physical phone, so the numbers that matter there (touch feel, DPI scale, battery) are still yours to check.

## Notes

- Background playback: `src/app/mpris.cpp` (Android branch) pushes state to `MediaBridge` over JNI; SDL's `BLOCK_ON_PAUSE` hint is off so the
  loop keeps running while the activity is paused. Drawing is skipped while backgrounded.
- Notification permission (Android 13+) is requested by `XylonicActivity`.
- Cache location (Settings > Storage) lists internal and external (SD) storage through SDL.
- Touch: window coordinates are used as-is for the mouse (`M_`), the UI scale comes from the display DPI.
- Foreground service also covers downloads (notification "Downloading N songs", dataSync type), holds a CPU wake lock and Wi-Fi lock while playing or downloading,
  takes audio focus (pauses for calls, resumes after), and pauses when headphones are unplugged.
- Back button: closes the keyboard, dialogs and panels, then walks up (album/artist page, Home); at the top level it moves the app to the background while music plays, otherwise it closes.
- Car Mode is immersive (system bars hidden) and keeps the screen on; Settings > Advanced > Background activity opens the battery-optimisation exemption.
- A multicast lock is held while the activity exists so Remote-mode discovery receives UDP broadcasts.
- App icon: `python3 tools/make-icon.py` renders it (assets/icons/app-icon-*.png, adaptive and legacy launcher icons, themed monochrome note).
- Release builds: put `storeFile`, `storePassword`, `keyAlias`, `keyPassword` in `android/keystore.properties` (git-ignored), then `./gradlew assembleRelease`.

## Found on the Oilsky G88 (2026-11-05) and fixed

- Audio: SDL's OpenSL ES backend fails to open ("notificationFrames=-2 not permitted for non-fast track"); AAudio at the device's native rate works (`SDL_HINT_AUDIODRIVER=aaudio`, `MediaBridge.outputRate`).
- Artwork: servers answer with WebP (Navidrome's artist placeholder always); decoded through `BitmapFactory` when the bundled JPEG/PNG decoder rejects the bytes.
- UI scale follows the display-size override (`DisplayMetrics.density`, 320 dpi on the G88), not the physical DPI (260).
- Transcoded playback (`maxBitRate`) now always asks for `format=mp3`; without it the server may answer Opus/AAC, which the native decoders cannot play.
- TLS: concurrent handshakes corrupted Mbed TLS 3.6's global PSA state ("This is a bug in the library"); `third_party/mbedtls_user_config.h` enables `MBEDTLS_THREADING_C`.
- Downloads: a 5000-song queue froze the UI (snapshot re-parsed every row per frame, a full store rewrite per removed row, `dl_all()` copies per pick). Now cached snapshots, batch enqueue/erase, throttled compaction; `tests/downloads_test.cpp` has a 4000-song stress case.
- Dialogs and sheets fit 320 px wide screens; the download quality list scrolls; the bottom nav is inert under a dialog; a dialog no longer receives the tap that opened it; long press opens the song context menu (the original relies on Chromium's `contextmenu`).
- Offline mode persists across restarts, lists cached albums and songs, and going online signs in again with the stored password.
- Car Mode forces landscape and restores the previous orientation; a tight layout for 320 px tall screens.
