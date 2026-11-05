# Port map: old Xylonic (Electron + React) to xylonic-fiber (C++17 + SDL2)

Goal (owner, 2026-10-31): exact port of behaviour and UI. Method per module: read the old source, carry constants and algorithms over unchanged, add a ported test, then compare the SDL render against a reference screenshot of the old app (docs/ui-ref/*.png, captured from `/home/bean/xylonic` at 1024x768 with a local fake server) and fix differences. Layout numbers come from the old DOM (getBoundingClientRect + computed styles), not from guessing.

Status: done / partial / todo. "exact" means constants and algorithm ported and checked.

| Old module | New module | Status |
|---|---|---|
| utils/colorContrast.ts, types/theme.ts, ThemeContext | src/ui/themes.* | done, exact, ported tests (system mode follows dark until OS detection lands) |
| Header.tsx, AppNav.tsx (collapsed) | src/app/shell.cpp | partial: 200px expandable / 64px collapsed rail, narrow-window hamburger, and player-bar input shielding; screenshot verification and full mobile spacing todo |
| PlaybackControls, ProgressBar, VolumeControl, StreamingQualitySelector (bar) | shell.cpp draw_player_bar | partial: layout exact; speed and quality popups todo |
| DiscoverView.tsx | shell.cpp draw_discover | done: sections, 20 albums, skeletons, arrows, Play All; 5 min cache TTL todo |
| subsonicApi.ts | src/subsonic | partial: core calls done; playlists, getSong stats, starred list todo |
| PlayerContext, playerQueue, useQueueActions, playerPersistence | src/app/queue.* | partial: shuffle/repeat/wrap exact; history, play-next UI, neighbours todo |
| audioPlayer, usePlaybackEngine | src/audio | done: decode, resample, EQ shelves, ring, async seek, speed 0.5-2x, gapless preload 15 s before end (rate-converted), spectrum tap; ReplayGain not in the old app |
| offlineCacheService, downloadManagerService, downloadReconciler | src/app/store, downloads | partial: resumable downloads, verify, offline browse; quality picker, refcount dedup, orphan reconcile todo |
| imageCacheService, AlbumArt, coverArtDownloader, internetArtworkService | src/app/images.* | partial: disk + LRU; mode-aware sizing, iTunes fallback, placeholder detection todo |
| persistentCache, metadataCache, searchCacheService, searchWorker | store.*, future search cache | partial: atomic JSON/file cache; full search index todo |
| secureCredentialService, credentialsService | src/app/secrets.* | done (Linux Secret Service); Android Keystore, Windows, macOS todo |
| LoginForm (layout, test-then-login, offline toggle, connection history picker), ipc/serverDiscovery + serverDiscoveryProbe | src/app/login.cpp, src/net/discovery.* | done (discovery tested; LAN, Tailscale CLI, hostnames); ReauthDialog todo |
| ArtistList, AllAlbumsGrid, AllSongsGrid, LikedSongsView, LibraryViewToggle, ShuffleAll, Pagination | src/app/library.cpp | done (layout from reference); Shuffle All pages 500 songs serially, quality picker for Download All todo |
| AlbumList (artist page), SongList (album hero + rows) | src/app/pages.cpp | done (layout from reference) |
| SearchResults, SearchContext (server search3, 300 ms debounce, best result rule) | src/app/search.cpp | done; local search index (searchCacheService) todo |
| RightPanel Queue + History tabs, recentlyPlayedService (50, per account in local JSON) | src/app/panel.cpp | done (layout from reference, drag reorder, search, Q/H/P/Esc); Playlists tab, AddToPlaylistDialog and SongContextMenu done |
| DesktopNowPlaying | src/app/nowplaying.cpp | partial: animated low-rate cover-colour blobs and MiniPlayer desktop done; mobile overlay and screenshot verification todo |
| CarMode (header, art, transport, seek, flip, minimal layout, keep-awake, battery) | src/app/carmode.cpp | partial: layout, Pulse Deck, EQ and 900px high-resolution art request done; artwork fallback and orientation lock todo |
| SettingsView (all 13 sections), SleepTimerPicker, ThemeSelector + CustomThemeEditor, clear-all-data | src/app/settings.cpp | partial: settings/theme/sleep/performance modes and Render Timer HUD done; Car Mode, Remote, Tech Stack, Licenses and performance-cache stats rows still inert/placeholder |
| lastfmService, LastfmSettings, useScrobbler | todo (plain scrobble call done) | |
| remote (ipc/remote.js, RemoteModeContext, RemoteDevicePicker) | src/net/remote.*, src/app/remote_ui.cpp | done: UDP presence + multicast, HTTP /cmd server, pairing rules (account match, busy), Be Controlled actions, Control Others picker, player bar routes play/next/prev/seek/volume/shuffle/repeat; wire-compatible with the old app (tested on loopback, TSAN clean); FirewallSetupDialog todo, real two-device test is yours |
| mpris.js | src/app/mpris.* (sd-bus, own thread) | done: status, metadata + art, transport, seek, volume, shuffle, loop; verified with busctl on the real session bus |
| CachePreloadDialog, perfModeService (Gaming/Balanced/Eco), RenderTimerHUD | partial: performance modes, Render Timer HUD and native preload dialog done; search-index persistence and exact IndexedDB-equivalent cache still todo | |
| Android native (capacitorBridge, MusicService, download service) | todo: SDL2 NDK project, owner builds | |

Reference capture tooling lives in the session scratchpad only; to recreate: run the old app with `ELECTRON_IS_DEV=0 electron . --user-data-dir=X --remote-debugging-port=9333`, log in to the fake Subsonic server, drive it over CDP (`Emulation.setDeviceMetricsOverride` 1024x768, `Page.captureScreenshot`, DOM walk with getBoundingClientRect/getComputedStyle).

## UI and parity gaps (noted 2026-11-03, status after the third pass)

Done earlier: Shuffle All / Download All paging, download quality picker and live setting, Cache Statistics, ReauthDialog, Car Mode artwork fallback and landscape hint, queue history/play-next (see session_summary.md).

Done in the third pass:
- Text size: every font was scaled to its line height instead of its em, so all text was about 15% smaller than the reference. Fonts now use CSS-px (em) scale; measured ink extents of "Discover", "Recently Added" and "Xylonic" match the reference captures to the pixel. Pixel difference against docs/ui-ref (fake-server fixture, >24/255): home 18.3% to 2.2%, library 29.9% to 2.0%, search 5.5% to 2.6%. Queue/playlist/settings references were captured in other app states (search text in the header, queue filled), so their remaining difference is fixture, not layout.
- Screenshot harness: the read-back came from an undefined back buffer after present (random stale frames); the scene is now drawn twice before the read.
- Mobile layout under 700 logical px: bottom nav (Home, Library, Downloads, Queue, Settings), compact header (search flexes, menu and offline buttons stay), mini player with progress line and tap-to-open Now Playing, Library header that wraps (segmented control full width, pills and actions on their own rows), centred grids. Window minimum is now 320x480. Now Playing already had a single-column layout below 900.
- Panel buttons (Queue, History, Playlists) on the player bar only from 1100 px wide so 1024x768 stays identical to the reference; below that they are in the menu and on Q/H/P.
- Images (corrected against the original source, see below): placeholder, iTunes and sizing rules now follow imageCacheService.ts / AlbumArt.tsx / internetArtworkService.ts.
- Neighbours: the two previous and two next queue tracks have their covers prefetched on each track change.
- Auto-offline on mobile data: real on Linux through NetworkManager's Metered property (checked every 30 s, only with downloads present, restores online when the connection is no longer metered); Android needs its own detection in `network_metered()`.

Still open:
- Exact mobile spacing per page (Settings, Album, Artist, Search pages were not reworked beyond the shell and Library header; Settings already adapts).
- Reference captures for panels/settings/now playing in their original app state, so the comparison can be run page by page.
- Last.fm settings (scrobbling is server side; the row is informational).

## Performance cache, images and Settings stats, ported from the original source (2026-11-03)

Reference files: `/home/bean/xylonic/src/components/common/CachePreloadDialog.tsx`, `App.tsx` (triggers), `services/imageCacheService.ts`, `components/common/AlbumArt.tsx`, `services/internetArtworkService.ts`, `services/networkStatsService.ts`, `components/common/settings/PerformanceCacheSection.tsx`, `SettingsView.tsx` (Rebuild/Clear), CSS in `dist/assets/index-*.css`. Native: `src/app/precache.cpp`, `src/app/images.cpp`, `src/app/settings.cpp`.

- Prestep ("Building Performance Cache"): runs by itself on the first launch of an account; afterwards only when the user asks (Settings > Rebuild Cache, which clears the caches and flags first) or accepts the prompt. Every launch, 3 s after connecting, the server's artist count and song count (getSongCount = sum of songCount over the first 500 albums) are compared with the last known values: a change opens "Library Change Detected!" (Later / Rebuild Cache Now); a cache older than one year opens "Cache Is Outdated". A failed build never sets the done flag. After completion the dialog counts 15 s and "restarts" (here: refreshes app state). Skip for Now sets the flags and closes. Geometry measured against the original (dialog 400 wide, 72 px rings, 446 px tall when complete); pixel diff of the complete state vs the original capture is 4.4% (text rasterisation).
- Build pipeline as in the original: artist images (300 px, batches of 12, 3 retries) while album metadata loads in the background; album covers (unique cover ids) while song metadata loads; search index (artists, albums and songs into the Store plus the persisted index); more than 2% of artists/albums still failing after the retry passes is reported as a failure; API-call and image counters are reset at the end.
- Images: cache keyed by cover id (300 px; `hi:<id>` at 2000 px for Car Mode with 4 textures / 40 files), the display texture is scaled from it. Navidrome's artist placeholder (exact check: id `ar-*`, 546 bytes, SHA-256 521259c6...) is never cached or drawn (user-circle / music-note icon instead); validated on the real server (tests/fixtures/navidrome_artist_placeholder.webp). Artist photos older than 7 days are refreshed once per session (this is the weekly rule; albums last a year). iTunes is asked ("artist album", up to 5 results, first one, 600x600) only for items with no cover id at all. Fetch concurrency by performance mode (gaming 1, balanced 4, eco 1) with 429 back-off 2 s doubling to 30 s; 3 retries 5 s apart; no on-demand fetches while the precache runs.
- Settings: "Performance Cache" section (overview bar, Image Cache, Search Index, Images No Network / Internet Used, Metadata) with the original labels and layout (451 px card, 71 px rows); "Rebuild Cache" and "Clear All Caches" ask for confirmation with the original wording. The earlier invented "Cache Statistics" dialog and "Cover art fallback" toggle were removed.
- Not reproduced: the background blur of the overlays; the original restarts the whole window.

## Fifth pass (2026-11-03): audit of undocumented original UI

See `docs/ui-audit.md` for the component-by-component audit (what the original did that no document or screenshot covered, what was ported, what is still missing). The hamburger menu that earlier passes added below 900 px does not exist in the original (HamburgerMenu.tsx is never mounted) and was removed; the compact layout (<=767 px) now follows the original CSS.

## Pass 2026-11-05 (parity sweep, Pulse Deck, Apple)

Done: movable permanent cache (Settings Storage on mobile, Change Location with folder picker on desktop, `src/app/storage*.cpp`, rows re-rooted by `heal_path`); app-store thank-you banner (`XF_APP_STORE`); hover tooltips (panel, player bar, header, mini player, remote picker); exact Choose Theme / Create Custom Theme / Firewall Setup dialogs (`src/app/modals.cpp`); remote picker wording and busy-tap; startup splash; Ctrl+Shift+Delete; mute restores 70%; Discover 5 min TTL (random never cached); compact Settings descriptions wrap; Pulse Deck redesign (smoothed bands, falling caps, kick glow, reflection, cover backdrop, EQ readout, tiered lag guard); table-driven spectrum FFT; Release LTO. Already present and now confirmed: search index persistence, download refcount dedup and reconcile, starred list and getSong.

Apple and Android: see docs/APPLE.md (Keychain, reachability, bundles, build scripts) and `SecretStore.java` / JNI metered detection for Android. Still open: Last.fm settings row (informational in the original too), media controls on Android/Apple, mobile page-by-page spacing diff against the original (Artist, Album, Search), Now Playing mobile overlay screenshot compare.

Compared against the running original at 390x844 (2026-11-05, Library, Artist, Album, Search, Now Playing overlay): the tinted Shuffle All / Download All / Download Artist buttons were drawn without their fill (`Gfx::stroke_round` ignores its last argument since the anti-aliasing rewrite, callers must `fill_round` first); fixed. Library toggle icon size matched. Layout of Artist, Album and Now Playing matches; Search differs only by fixture (the original had its local song index built).
