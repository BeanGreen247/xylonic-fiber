#!/usr/bin/env bash
# Builds the debug APK. Usage: android/build-apk.sh [--emulator] [--install|-i] [--run|-r]
#   --install  sideload onto the connected device with adb (set ANDROID_SERIAL to pick one)
#   --run      install, then launch the app
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
: "${ANDROID_HOME:=${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
[[ -d "$ANDROID_HOME" ]] || { echo "Set ANDROID_HOME to your Android SDK" >&2; exit 1; }
[[ -d android/app/jni/SDL2 ]] || sh android/setup.sh
echo "sdk.dir=$ANDROID_HOME" > android/local.properties
ABIS=(); INSTALL=0; RUN=0
for a in "$@"; do
  case "$a" in
    --emulator) ABIS=(-Pxf.abis=x86_64) ;;
    --install|-i) INSTALL=1 ;;
    --run|-r) INSTALL=1; RUN=1 ;;
    *) echo "Unknown option: $a" >&2; exit 2 ;;
  esac
done
if [[ -z ${XF_JAVA_HOME:-} ]]; then
  for j in /usr/lib/jvm/java-21-openjdk-amd64 /usr/lib/jvm/java-17-openjdk-amd64; do
    [[ -d $j ]] && XF_JAVA_HOME=$j && break
  done
fi
[[ -n ${XF_JAVA_HOME:-} ]] && export JAVA_HOME=$XF_JAVA_HOME PATH="$XF_JAVA_HOME/bin:$PATH"
cd android
./gradlew assembleDebug -x test -x lint -x lintDebug --parallel --build-cache "${ABIS[@]}"
echo "APK: $(pwd)/app/build/outputs/apk/debug/app-debug.apk"
if ((INSTALL)); then
  ADB="$ANDROID_HOME/platform-tools/adb"; command -v adb >/dev/null && ADB=adb
  "$ADB" get-state >/dev/null 2>&1 || { echo "No adb device connected" >&2; exit 1; }
  "$ADB" install -r app/build/outputs/apk/debug/app-debug.apk
  ((RUN)) && "$ADB" shell monkey -p org.xylonic.fiber -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
fi
