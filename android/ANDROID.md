# Android build (owner runs this; the agent never starts device builds)

The Android app is the same C++ code as the desktop app, loaded by SDL2's `SDLActivity` as `libmain.so`.

1. One time: `android/setup.sh` downloads the SDL2 sources (2.32.x) into `android/app/jni/SDL2` and copies SDL's Java
   glue (`org/libsdl/app/*.java`) into `android/app/src/main/java`.
2. Open `android/` in Android Studio (or `gradle assembleDebug` with the Android SDK/NDK installed). minSdk 24 is
   required (`getifaddrs`, TLS roots). Target the G88 class of devices with `abiFilters "arm64-v8a", "armeabi-v7a"`.
3. Assets: `android/app/build.gradle` packages `../../assets` into the APK; the app reads fonts and icons through
   `SDL_RWFromFile` (APK assets), so no extra code is needed.

Platform gaps to fill next (tracked in docs/port-map.md): media notification + foreground service for
background playback and downloads, battery-optimisation exemption dialog, rotation lock for Car Mode, SD-card cache
location. Touch scrolling and DPI scaling are already in `src/app/app.cpp`.
