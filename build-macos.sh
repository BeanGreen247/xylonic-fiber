#!/usr/bin/env bash
# Build the macOS app bundle (run on a Mac with Xcode command line tools and Homebrew: brew install cmake pkg-config sdl2 sdl2_image).
set -euo pipefail
ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${XF_BUILD_DIR:-"$ROOT_DIR/build-macos"}
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15
cmake --build "$BUILD_DIR" -j "$(sysctl -n hw.ncpu)"
[[ ${1:-} == --no-test ]] || ctest --test-dir "$BUILD_DIR" --output-on-failure
# app icon: build AppIcon.icns from the rendered PNGs (python3 tools/make-icon.py regenerates them)
ICONSET="$BUILD_DIR/AppIcon.iconset"; rm -rf "$ICONSET"; mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
  cp "$ROOT_DIR/assets/icons/app-icon-$s.png" "$ICONSET/icon_${s}x${s}.png"
  [[ $s -ge 16 ]] && d=$((s * 2)) && [[ -f "$ROOT_DIR/assets/icons/app-icon-$d.png" ]] && cp "$ROOT_DIR/assets/icons/app-icon-$d.png" "$ICONSET/icon_${s}x${s}@2x.png" || true
done
iconutil -c icns "$ICONSET" -o "$BUILD_DIR/xylonic-fiber.app/Contents/Resources/AppIcon.icns" || echo "iconutil failed: the bundle will have no icon"
echo "App bundle: $BUILD_DIR/xylonic-fiber.app"
