# Changelog

## [Unreleased]

### Added
- Audio pipeline (mp3/flac decode thread, lock-free ring, SDL callback; device open only while playing).
- HTTP/1.1 client with TLS (mbedTLS) and URL handling.
- Subsonic client: auth, library browse, search, starred, scrobble, stream and cover URLs.
- CMake scaffold: C++17 SDL2 window with blocking event loop and ctest smoke test.
- docs/scope.md: feature scope grouped must/should/later from Xylonic 26.10.28.
- docs/baseline.md: measured idle/playing RAM, CPU, startup and size of the old app, plus the SDL2 floor.
- docs/stack-options.md: language/library options around the fixed SDL2 UI, pending owner pick.
- docs/session_summary.md: git-tracked memory bank (decisions, state, next step).
