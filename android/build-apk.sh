#!/usr/bin/env bash
# Builds the debug APK. Usage: android/build-apk.sh [--emulator]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
: "${ANDROID_HOME:=${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
[[ -d "$ANDROID_HOME" ]] || { echo "Set ANDROID_HOME to your Android SDK" >&2; exit 1; }
[[ -d android/app/jni/SDL2 ]] || sh android/setup.sh
echo "sdk.dir=$ANDROID_HOME" > android/local.properties
ABIS=()
[[ ${1:-} == --emulator ]] && ABIS=(-Pxf.abis=x86_64)
cd android
./gradlew assembleDebug "${ABIS[@]}"
echo "APK: $(pwd)/app/build/outputs/apk/debug/app-debug.apk"
