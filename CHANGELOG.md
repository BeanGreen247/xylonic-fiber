# Changelog

Versioning follows the other native projects (Lestrix, hootedit, fleetwm): 0.MINOR.PATCH while pre-1.0, a minor step for a batch of features, a patch step for fixes only,
the version lives in `kVersion` (src/app/app.h), `MACOSX_BUNDLE_SHORT_VERSION_STRING` (CMakeLists.txt) and `versionName`/`versionCode` (android/app/build.gradle), a release is one
`chore(release): 0.x.y` commit and an annotated tag `v0.x.y`.

## [Unreleased]

## 0.2.0 (2026-11-08)

### Added
- Riverbero (the former Pulse Deck): 24-band spectrum with peak caps, kick glow, reflection and cover wash. Its control rail hides, slides in on a swipe from the right edge (or a tap on the handle) and stays for 10 s; the battery percentage moves into the track pill while it is hidden. Car Mode controls live in a vertical rail so landscape keeps its height; system bars are kept hidden and re-hidden at once if they pop up.
- Winamp 2.x mini player drawn from the active theme (LCD time with elapsed/remaining, spectrum in three modes, scrolling title, volume and seek sliders, windowshade, always-on-top and double-size on desktop). Tablets, laptops and desktops only: it is hidden on phones.
- Car Mode rebuilt from measurements of the Electron CSS: minimal layout with the seek row on top, Reset EQ, repeat-one badge, loading spinner.
- Now Playing (compact): swipe down anywhere to slide the sheet away (spring back when short), swipe left/right to change track, tap the artwork to play or pause.
- Float audio pipeline for WAV, AIFF, Vorbis, Opus, AAC, ALAC and Ogg FLAC; storage volumes and folder picker; shared accounts across addresses; sign out and add server; selectable icon sets.

### Changed
- Clear all data, Clear all caches and Rebuild cache run in the background behind a "Working" dialog: batched journal erase, one-pass artwork eviction, parallel unlinks.
- The resampler uses a 128-phase polyphase table for rate conversion up or 1:1 (decode thread 23% -> under 4% of a core on the G88).
- Idle wake-ups: on Android and iOS the event loop sleeps on a semaphore fed by an event watch instead of SDL's 1 ms `SDL_Delay` poll (idle CPU 16% -> about 5%); the battery is read every 10 s instead of every frame.
- Riverbero frame rate tiers (30/25/15 fps) and a lower tier on a battery at 20% or less; FFT without `std::complex` multiplies and with cached band edges.
- Build: `-O3`, `-fno-plt` and, where accepted, GCC block-reorder and vector-cost tuning plus `-Wl,-O1 --as-needed -z pack-relative-relocs` on Linux (flags carried over from Lestrix and hootedit; numbers in docs/baseline.md).

### Fixed
- Resampler fast path could read one table row past the end when the fractional position rounded up to 1.0.
- Riverbero: the reveal-swipe strip no longer blocks EQ shaping away from the handle; toggling keep-screen-on no longer re-runs the whole Car Mode setup (and stacked a gesture-exclusion listener); clearing caches cancels a running preload first; the saved account is cleared before the slow part of Clear all data.
- The add-to-playlist dialog was never drawn on the compact Now Playing screen yet blocked all input.
- Downloads honour the chosen quality for transcoded files, filenames are sanitised, remote clients are capped, an out-of-bounds read in the licenses view.

### Earlier work (first scaffold)
- SQLite metadata cache, resumable offline downloads, Downloads view, offline mode, OS keyring credentials.
- App shell: login with certificate pinning, home, library, album, search, queue, settings, player bar, shortcuts, queue restore (docs/TESTING.md).
- Streaming playback from http/https URLs into the cache, cache-first lookup, certificate pinning for self-signed servers.
- Audio pipeline (mp3/flac decode thread, lock-free ring, SDL callback; device open only while playing).
- HTTP/1.1 client with TLS (mbedTLS) and URL handling.
- Subsonic client: auth, library browse, search, starred, scrobble, stream and cover URLs.
- CMake scaffold: C++17 SDL2 window with blocking event loop and ctest smoke test.
- docs/scope.md: feature scope grouped must/should/later from Xylonic 26.10.28.
- docs/baseline.md: measured idle/playing RAM, CPU, startup and size of the old app, plus the SDL2 floor.
- docs/stack-options.md: language/library options around the fixed SDL2 UI, pending owner pick.
- docs/session_summary.md: git-tracked memory bank (decisions, state, next step).
