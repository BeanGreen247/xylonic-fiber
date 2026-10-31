# Session summary (git-tracked memory bank)

Updated after every step. Newest first.

## 2026-10-31 (http/https + streaming)

Owner: http and https must both work; playback order = permanent cache first, else web URL. Done: Player::load_url(url, cache_path, kind, opt, duration_hint): plays cache file if present, else streams into cache_path.part while decoding (blocking reads, seek beyond downloaded part waits), renames to cache_path on completion, removes the partial on failure/cancel. Self-signed servers: ConnectOptions.pin_sha256 + fetch_cert_fingerprint() for a trust-this-certificate prompt. Verified http and https (pinned self-signed) end to end against the fake server, ASAN/UBSAN/TSAN clean, 6 ctest green. Open: Range requests (seek currently waits for the sequential download), Windows Winsock shim, OS cert store on Windows/macOS/iOS (Linux and Android paths done).

## 2026-10-31 (later)

Done and committed: audio pipeline (dr_mp3/dr_flac decode thread -> SPSC ring -> SDL callback, device opened only while playing; ASAN/UBSAN/TSAN clean; decode 1200x realtime on a synthetic mp3), net (URL, TCP, mbedTLS 3.6.7 via FetchContent, HTTP/1.1 GET with chunked/redirect, TLS verified against local openssl s_server and example.com), subsonic client (yyjson, token auth, artists/artist/album/albumList2/search3/starred/star/scrobble, stream/cover URLs), all with ctest (6 tests green). Live-checked against the fake server.

Next, in order: SQLite metadata + file cache (offline), custom UI (stb_truetype text, virtualised lists, damage-only redraw), queue/shuffle/repeat persistence, settings/themes, credentials in OS keychain, MPRIS2, then Android via SDL2 NDK project (owner builds devices). Target device for slow-hardware tuning: oilsky G88 music player (debloat script exists); measure on desktop with CPU/RAM limits until owner provides device numbers.

## 2026-10-31

Decisions (owner): UI is SDL2 on all platforms. Platforms: Linux desktop, then Android, then Windows/macOS/iOS. Local machine only for measuring; Nobara box off unless asked. No naming without asking. Commits: single-line conventional, no attribution, main only for verified slices. Never run Android/iOS builds, ADB, Gradle, cap sync.

Done: scope.md (must/should/later), baseline.md (old app idle PSS ~300MB, playing ~335MB, 12% CPU playing, 812ms startup, SDL2 floor 33MB PSS), stack-options.md.

Stack decided (C++17, custom SDL2 UI, own audio pipeline, own HTTP + mbedTLS); scaffolded CMake + SDL2 window + ctest smoke (builds, passes). Next: audio pipeline spike (decode mp3 to SDL callback), measure vs floor.

Was open: owner pick on language, UI layer, audio decode, TLS (see stack-options.md). No scaffolding until answered.

Finding: opening an SDL audio device costs ~1.85% CPU on this box even when idle; open only while playing.
