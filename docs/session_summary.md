# Session summary (git-tracked memory bank)

## 2026-10-31 (continued native port)

Added the persistent Render Timer HUD from the old app: Settings > Advanced > Render Timer HUD stores its state in the native config and displays measured frame time/FPS plus the active performance mode. Corrected the port map for features already implemented in C++ (playlists, add-to-playlist, song context menu, desktop mini player, and performance modes). Next exact-port slice is the cache preload dialog, followed by hamburger/mobile layouts and the remaining inert settings actions.

The cache preload dialog is now implemented from the original Electron flow: Settings > Offline & Cache > Preload Library Cache loads all artists, artist album metadata, album song metadata and home lists, then warms persistent cover artwork with progress/cancel/retry. It uses session metadata plus the persistent image cache, matching the original split. The remaining difference from the original is the search-index build/persistence, which is still unported.

Updated after every step. Newest first.

Owner correction: the original Electron project has no SQLite dependency; it uses localStorage, IndexedDB and platform files. The native build now uses an atomic JSON manifest for metadata/download state; the account manifest is `data_<account>.json`.

## 2026-10-31 (exact UI port, in progress)

Ported since: artist and album pages, search results, right panel (queue, history), desktop now playing, settings page with sleep timer and accent theme dialogs, anti-aliased rounded shapes (atlas masks), rounded cover images. Remaining screens: car mode (+Pulse Deck, EQ), mini player (second window), hamburger menu (narrow layouts), keyboard help dialog, playlists tab, add-to-playlist dialog, song context menu, cache preload dialog, remote picker + firewall dialog, login screen exact (saved connections, discovery), mobile layouts (bottom nav, overlay), splash.

Owner direction: port the old app exactly (behaviour and UI). Method and status table: docs/port-map.md. Reference screenshots and the CDP capture scripts: docs/ui-ref/. Done so far: themes (exact), shell (header, nav, player bar), Discover, Library (4 modes + pagination). Fonts: Roboto 400/500/700 + Font Awesome 7 subset TTF in assets/fonts; SVG icons via nanosvg in assets/icons. Next screens to capture and port: artist page, album page, search results, right panel (queue/history/playlists), now playing overlay, settings and hamburger, mini player, dialogs, car mode. Test checklist for the owner: docs/TESTING.md part 3 (UI parity: compare against the old app side by side).

## 2026-10-31 (app part 2: metadata cache, downloads, offline)

Done: JSON manifest store (metadata/download state), JSON models, resumable Downloads manager (2 threads, pause/resume/retry/verify/remove, TSAN clean), SWR loading for home/artists/artist/album, offline mode (downloaded albums only), cover prefetch for offline, Downloads view, keyring credentials (libsecret via dlopen). Test checklist: docs/TESTING.md part 2. Next: themes + settings, MPRIS2, mini player, sleep timer, Last.fm, playlists, history, Now Playing view, then remote control, car mode, Android (owner builds), perf pass for the G88.

## 2026-10-31 (app part 1: shell, library, playback)

Owner wants to test each main part against a real server himself: see docs/TESTING.md (updated per part). I never contact his real server. Done: Worker pool + UI mailbox, cover ImageCache (disk + LRU textures), Config, App shell with Login (pin prompt), Home, Library, Artist, Album, Search, Queue, Settings, player bar, keyboard shortcuts, scrobble, queue restore, cache-first playback, async seek. Measured: idle 35MB PSS / 0.03% CPU, playing 36MB / 0.47% (old: 298 / 0.5-3 and 333 / 12), selftest startup 147ms. Next: credentials in OS keychain (libsecret), JSON metadata cache + offline downloads UI, themes, MPRIS2, mini player, sleep timer, Last.fm, remote control, car mode, Android (SDL2 project; owner builds). Slow-device work for the G88: measure with cpulimit/cgroup caps; avoid per-frame allocations; keep damage-based redraw.

## 2026-10-31 (http/https + streaming)

Owner: http and https must both work; playback order = permanent cache first, else web URL. Done: Player::load_url(url, cache_path, kind, opt, duration_hint): plays cache file if present, else streams into cache_path.part while decoding (blocking reads, seek beyond downloaded part waits), renames to cache_path on completion, removes the partial on failure/cancel. Self-signed servers: ConnectOptions.pin_sha256 + fetch_cert_fingerprint() for a trust-this-certificate prompt. Verified http and https (pinned self-signed) end to end against the fake server, ASAN/UBSAN/TSAN clean, 6 ctest green. Open: Range requests (seek currently waits for the sequential download), Windows Winsock shim, OS cert store on Windows/macOS/iOS (Linux and Android paths done).

## 2026-10-31 (later)

Done and committed: audio pipeline (dr_mp3/dr_flac decode thread -> SPSC ring -> SDL callback, device opened only while playing; ASAN/UBSAN/TSAN clean; decode 1200x realtime on a synthetic mp3), net (URL, TCP, mbedTLS 3.6.7 via FetchContent, HTTP/1.1 GET with chunked/redirect, TLS verified against local openssl s_server and example.com), subsonic client (yyjson, token auth, artists/artist/album/albumList2/search3/starred/star/scrobble, stream/cover URLs), all with ctest (6 tests green). Live-checked against the fake server.

Next, in order: JSON/file metadata + offline cache, custom UI (stb_truetype text, virtualised lists, damage-only redraw), queue/shuffle/repeat persistence, settings/themes, credentials in OS keychain, MPRIS2, then Android via SDL2 NDK project (owner builds devices). Target device for slow-hardware tuning: oilsky G88 music player (debloat script exists); measure on desktop with CPU/RAM limits until owner provides device numbers.

## 2026-10-31

Decisions (owner): UI is SDL2 on all platforms. Platforms: Linux desktop, then Android, then Windows/macOS/iOS. Local machine only for measuring; Nobara box off unless asked. No naming without asking. Commits: single-line conventional, no attribution, main only for verified slices. Never run Android/iOS builds, ADB, Gradle, cap sync.

Done: scope.md (must/should/later), baseline.md (old app idle PSS ~300MB, playing ~335MB, 12% CPU playing, 812ms startup, SDL2 floor 33MB PSS), stack-options.md.

Stack decided (C++17, custom SDL2 UI, own audio pipeline, own HTTP + mbedTLS); scaffolded CMake + SDL2 window + ctest smoke (builds, passes). Next: audio pipeline spike (decode mp3 to SDL callback), measure vs floor.

Was open: owner pick on language, UI layer, audio decode, TLS (see stack-options.md). No scaffolding until answered.

Finding: opening an SDL audio device costs ~1.85% CPU on this box even when idle; open only while playing.

2026-11-03: cache foundation continued. Profiled with perf (Hotspot recordings in claude-memory-bank): `MetadataSnapshot::upsert` re-sorted the whole search index per song (quadratic) and `Store::put("album:")` rebuilt the full index and rewrote the full JSON per call. Fixed (O(1) upsert, incremental merge, lazy index, JSON save every 64 journaled ops, slice-by-8 CRC, fread journal reads). Added generation publishing, PageCache, MediaCache manifest, SWR policy, Subsonic batch adapter, tests and benchmarks (numbers in cache-design.md). Still open: wire `MetadataStore`/`MediaCache` into the app, search index persistence, fsync adapter.

2026-11-03 (second pass): wired `MetadataStore` (likes, playlists, search, technical fields) and `MediaCache` (artwork, downloads) into the app, added the persisted search index and the fsync adapter (fsync'd snapshots/manifest, durable or group-committed journal), open-addressing `StringTable`, and measured the app against the 33 MB floor (numbers in cache-design.md). Tests: 20/20 plus store/media/generation cases.

2026-11-03 (third pass, owner authorised the main server for read-only checks with a scratch profile; credentials were not stored anywhere): fixed global font scale (em instead of line height), screenshot read-back, mobile shell and Library header, placeholder detection + iTunes fallback + mode-aware cover sizes, neighbour cover prefetch, NetworkManager auto-offline. Real library (79 artists, 503 albums, 5026 songs): Shuffle/Download All paging 2207 ms sequential vs 328 ms with 3 parallel workers (second run, warm server cache).

2026-11-03 (fourth pass): after the owner asked twice to read the original code instead of guessing, the image cache, the first-run/library-change cache prestep, the iTunes rule and the Settings cache stats were rewritten from `/home/bean/xylonic/src` (see docs/port-map.md). Earlier guesses removed: pixel-based placeholder test, iTunes for placeholders, Cache Statistics dialog, Cover art fallback toggle.
