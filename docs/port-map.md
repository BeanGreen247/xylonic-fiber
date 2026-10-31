# Port map: old Xylonic (Electron + React) to xylonic-fiber (C++17 + SDL2)

Goal (owner, 2026-10-31): exact port of behaviour and UI. Method per module: read the old source, carry constants and algorithms over unchanged, add a ported test, then compare the SDL render against a reference screenshot of the old app (docs/ui-ref/*.png, captured from `/home/bean/xylonic` at 1024x768 with a local fake server) and fix differences. Layout numbers come from the old DOM (getBoundingClientRect + computed styles), not from guessing.

Status: done / partial / todo. "exact" means constants and algorithm ported and checked.

| Old module | New module | Status |
|---|---|---|
| utils/colorContrast.ts, types/theme.ts, ThemeContext | src/ui/themes.* | done, exact, ported tests (system mode follows dark until OS detection lands) |
| Header.tsx, AppNav.tsx (collapsed) | src/app/shell.cpp | done, pixel-checked at 1024x768; expanded nav, Remote and Mini buttons inert |
| PlaybackControls, ProgressBar, VolumeControl, StreamingQualitySelector (bar) | shell.cpp draw_player_bar | partial: layout exact; speed and quality popups todo |
| DiscoverView.tsx | shell.cpp draw_discover | done: sections, 20 albums, skeletons, arrows, Play All; 5 min cache TTL todo |
| subsonicApi.ts | src/subsonic | partial: core calls done; playlists, getSong stats, starred list todo |
| PlayerContext, playerQueue, useQueueActions, playerPersistence | src/app/queue.* | partial: shuffle/repeat/wrap exact; history, play-next UI, neighbours todo |
| audioPlayer, usePlaybackEngine | src/audio | done: decode, ring, async seek; gapless preload (15 s before end), speed 0.5-2x, ReplayGain todo |
| offlineCacheService, downloadManagerService, downloadReconciler | src/app/store, downloads | partial: resumable downloads, verify, offline browse; quality picker, refcount dedup, orphan reconcile todo |
| imageCacheService, AlbumArt, coverArtDownloader, internetArtworkService | src/app/images.* | partial: disk + LRU; mode-aware sizing, iTunes fallback, placeholder detection todo |
| persistentCache, metadataCache, searchCacheService, searchWorker | store.* | partial: SWR kv cache; full search index todo |
| secureCredentialService, credentialsService | src/app/secrets.* | done (Linux Secret Service); Android Keystore, Windows, macOS todo |
| LoginForm, ConnectionTest, ReauthDialog, serverDiscoveryProbe, ipc/serverDiscovery | app.cpp login | partial: login + cert pin; saved connections list, discovery, reauth todo |
| ArtistList, AllAlbumsGrid, AllSongsGrid, LikedSongsView, LibraryViewToggle, ShuffleAll, Pagination | src/app/library.cpp | done (layout from reference); Shuffle All pages 500 songs serially, quality picker for Download All todo |
| AlbumList (artist page), SongList (album hero + rows) | src/app/pages.cpp | done (layout from reference) |
| SearchResults, SearchContext (server search3, 300 ms debounce, best result rule) | src/app/search.cpp | done; local search index (searchCacheService) todo |
| RightPanel Queue + History tabs, recentlyPlayedService (50, per account in SQLite) | src/app/panel.cpp | done (layout from reference, drag reorder, search, Q/H/P/Esc); Playlists tab, AddToPlaylistDialog, SongContextMenu todo |
| DesktopNowPlaying | src/app/nowplaying.cpp | done (static backdrop; the old one animates blobs); NowPlayingOverlay (mobile layout), MiniPlayer (ipc/playerWindow) todo |
| CarMode, useSongEq, Pulse Deck | todo | |
| SettingsView (all 13 sections), SleepTimerPicker, ThemeSelector + CustomThemeEditor, clear-all-data | src/app/settings.cpp | done for layout; Car Mode, Remote, Tech Stack, Licenses, performance-cache stats rows inert or placeholder; HamburgerMenu, KeyboardHelp todo |
| lastfmService, LastfmSettings, useScrobbler | todo (plain scrobble call done) | |
| remote (ipc/remote.js, remoteDiscoveryService, RemoteModeContext, FirewallSetupDialog) | todo | |
| mpris.js | todo | |
| CachePreloadDialog, perfModeService (Gaming/Balanced/Eco), RenderTimerHUD | todo | |
| Android native (capacitorBridge, MusicService, download service) | todo: SDL2 NDK project, owner builds | |

Reference capture tooling lives in the session scratchpad only; to recreate: run the old app with `ELECTRON_IS_DEV=0 electron . --user-data-dir=X --remote-debugging-port=9333`, log in to the fake Subsonic server, drive it over CDP (`Emulation.setDeviceMetricsOverride` 1024x768, `Page.captureScreenshot`, DOM walk with getBoundingClientRect/getComputedStyle).
