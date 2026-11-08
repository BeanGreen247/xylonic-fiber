#!/usr/bin/env bash
# Generate the Xcode project for iOS (run on a Mac with Xcode). Open build-ios/xylonic-fiber.xcodeproj, set your signing team, run on a device.
# SDL2 is fetched and built as a static library by CMake; there is no SDL_image on iOS (artwork uses stb_image).
set -euo pipefail
ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${XF_BUILD_DIR:-"$ROOT_DIR/build-ios"}
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_OSX_ARCHITECTURES=arm64
echo "Open $BUILD_DIR/xylonic-fiber.xcodeproj in Xcode (signing: Signing & Capabilities > Team)."
echo "Simulator: add -DCMAKE_OSX_SYSROOT=iphonesimulator and the matching -DCMAKE_OSX_ARCHITECTURES."
