#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${XF_BUILD_DIR:-"$ROOT_DIR/build-linux"}
BUILD_JOBS=${XF_BUILD_JOBS:-2}
RUN_APP=0
RUN_ARGS=()
RUN_TESTS=1

usage() {
    cat <<'EOF'
Build the native Linux Xylonic Fiber executable.

Usage:
  ./build-linux.sh                 Build release executable and run tests
  ./build-linux.sh --run [args]    Build, test, then launch the executable
  ./build-linux.sh --no-test       Build without running CTest
  ./build-linux.sh --help          Show this help

Examples:
  ./build-linux.sh --run
  ./build-linux.sh --run --server https://music.example --user me --pass secret
  ./build-linux.sh --run --selftest

Environment:
  XF_BUILD_DIR   Build directory (default: ./build-linux)
  XF_BUILD_JOBS  Parallel build jobs (default: 2)
EOF
}

while (($#)); do
    case "$1" in
        --run) RUN_APP=1; shift; RUN_ARGS=("$@"); break ;;
        --no-test) RUN_TESTS=0; shift ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

command -v cmake >/dev/null || { echo "cmake is required" >&2; exit 1; }

echo "Configuring Linux Release build: $BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DXF_BUILD_TESTS=ON

echo "Building Linux targets with $BUILD_JOBS job(s)"
cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"

if ((RUN_TESTS)); then
    echo "Running native test suite"
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

EXECUTABLE="$BUILD_DIR/xylonic-fiber"
if [[ ! -x "$EXECUTABLE" ]]; then
    echo "Build completed but executable was not found: $EXECUTABLE" >&2
    exit 1
fi

echo
echo "Executable ready: $EXECUTABLE"
if ((RUN_APP)); then
    echo "Launching xylonic-fiber"
    exec "$EXECUTABLE" "${RUN_ARGS[@]}"
fi
