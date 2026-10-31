# Port map: old Xylonic (Electron + React) to xylonic-fiber (C++17 + SDL2)

Goal (owner, 2026-10-31): exact port of behaviour and UI. Method per module: read the old source, carry constants and algorithms over unchanged, add a ported test, then compare the SDL render against a reference screenshot of the old app (docs/ui-ref/*.png, captured from `/home/bean/xylonic` at 1024x768 with a local fake server) and fix differences. Layout numbers come from the old DOM (getBoundingClientRect + computed styles), not from guessing.

Status: done / partial / todo. "exact" means constants and algorithm ported and checked.

| Old module | New module | Status |
|---|---|---|
| utils/colorContrast.ts, types/theme.ts, ThemeContext | src/ui/themes.* | done, exact, ported tests (system mode follows dark until OS detection lands) |
| Header.tsx, AppNav.tsx (collapsed) | src/app/shell.cpp | partial: pixel-checked at 1024x768; expanded nav/hamburger todo |
| PlaybackControls, ProgressBar, VolumeControl, StreamingQualitySelector (bar) | shell.cpp draw_player_bar | partial: layout exact; speed and quality popups todo |
| DiscoverView.tsx | shell.cpp draw_discover | done: sections, 20 albums, skeletons, arrows, Play All; 5 min cache TTL todo |
| subsonicApi.ts | src/subsonic | partial: core calls done; playlists, getSong stats, starred list todo |
| PlayerContext, playerQueue, useQueueActions, playerPersistence | src/app/queue.* | partial: shuffle/repeat/wrap exact; history, play-next UI, neighbours todo |
| audioPlayer, usePlaybackEngine | src/audio | done: decode, resample, EQ shelves, ring, async seek, speed 0.5-2x, gapless preload 15 s before end (rate-converted), spectrum tap; ReplayGain not in the old app |
| offlineCacheService, downloadManagerService, downloadReconciler | src/app/store, downloads | partial: resumable downloads, verify, offline browse; quality picker, refcount dedup, orphan reconcile todo |
| imageCacheService, AlbumArt, coverArtDownloader, internetArtworkService | src/app/images.* | partial: disk + LRU; mode-aware sizing, iTunes fallback, placeholder detection todo |
| persistentCache, metadataCache, searchCacheService, searchWorker | store.* | partial: SWR kv cache; full search index todo |
| secureCredentialService, credentialsService | src/app/secrets.* | done (Linux Secret Service); Android Keystore, Windows, macOS todo |
| LoginForm (layout, test-then-login, offline toggle, connection history picker), ipc/serverDiscovery + serverDiscoveryProbe | src/app/login.cpp, src/net/discovery.* | done (discovery tested; LAN, Tailscale CLI, hostnames); ReauthDialog todo |
| ArtistList, AllAlbumsGrid, AllSongsGrid, LikedSongsView, LibraryViewToggle, ShuffleAll, Pagination | src/app/library.cpp | done (layout from reference); Shuffle All pages 500 songs serially, quality picker for Download All todo |
| AlbumList (artist page), SongList (album hero + rows) | src/app/pages.cpp | done (layout from reference) |
| SearchResults, SearchContext (server search3, 300 ms debounce, best result rule) | src/app/search.cpp | done; local search index (searchCacheService) todo |
| RightPanel Queue + History tabs, recentlyPlayedService (50, per account in SQLite) | src/app/panel.cpp | done (layout from reference, drag reorder, search, Q/H/P/Esc); Playlists tab, AddToPlaylistDialog and SongContextMenu done |
| DesktopNowPlaying | src/app/nowplaying.cpp | partial: static backdrop; mobile overlay and animated blobs todo; MiniPlayer desktop done |
| CarMode (header, art, transport, seek, flip, minimal layout, keep-awake, battery) | src/app/carmode.cpp | done for layout; Pulse Deck (24-band FFT, caps, lag guard), per-song bass/treble EQ by dragging; hi-res art cache and orientation lock todo |
| SettingsView (all 13 sections), SleepTimerPicker, ThemeSelector + CustomThemeEditor, clear-all-data | src/app/settings.cpp | partial: settings/theme/sleep/performance modes and Render Timer HUD done; Car Mode, Remote, Tech Stack, Licenses and performance-cache stats rows still inert/placeholder |
| lastfmService, LastfmSettings, useScrobbler | todo (plain scrobble call done) | |
| remote (ipc/remote.js, RemoteModeContext, RemoteDevicePicker) | src/net/remote.*, src/app/remote_ui.cpp | done: UDP presence + multicast, HTTP /cmd server, pairing rules (account match, busy), Be Controlled actions, Control Others picker, player bar routes play/next/prev/seek/volume/shuffle/repeat; wire-compatible with the old app (tested on loopback, TSAN clean); FirewallSetupDialog todo, real two-device test is yours |
| mpris.js | src/app/mpris.* (sd-bus, own thread) | done: status, metadata + art, transport, seek, volume, shuffle, loop; verified with busctl on the real session bus |
| CachePreloadDialog, perfModeService (Gaming/Balanced/Eco), RenderTimerHUD | partial: performance modes, Render Timer HUD and native preload dialog done; search-index persistence and exact IndexedDB-equivalent cache still todo | |
| Android native (capacitorBridge, MusicService, download service) | todo: SDL2 NDK project, owner builds | |

Reference capture tooling lives in the session scratchpad only; to recreate: run the old app with `ELECTRON_IS_DEV=0 electron . --user-data-dir=X --remote-debugging-port=9333`, log in to the fake Subsonic server, drive it over CDP (`Emulation.setDeviceMetricsOverride` 1024x768, `Page.captureScreenshot`, DOM walk with getBoundingClientRect/getComputedStyle).
