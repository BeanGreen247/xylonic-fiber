#!/usr/bin/env bash
# Build the macOS app bundle (run on a Mac with Xcode command line tools and Homebrew: brew install cmake pkg-config sdl2 sdl2_image).
set -euo pipefail
ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${XF_BUILD_DIR:-"$ROOT_DIR/build-macos"}
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15
cmake --build "$BUILD_DIR" -j "$(sysctl -n hw.ncpu)"
[[ ${1:-} == --no-test ]] || ctest --test-dir "$BUILD_DIR" --output-on-failure
echo "App bundle: $BUILD_DIR/xylonic-fiber.app"
