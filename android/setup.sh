#!/bin/sh
# Fetches SDL2 and its Android Java glue. Run once from the repo root: sh android/setup.sh
set -e
V=2.32.10
D=android/app/jni
mkdir -p "$D" android/app/src/main/java
if [ ! -d "$D/SDL2" ]; then
  curl -fL "https://github.com/libsdl-org/SDL/releases/download/release-$V/SDL2-$V.tar.gz" | tar xz -C "$D"
  mv "$D/SDL2-$V" "$D/SDL2"
fi
cp -r "$D/SDL2/android-project/app/src/main/java/org" android/app/src/main/java/
echo "SDL2 $V ready in $D/SDL2"
