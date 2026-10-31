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
| audioPlayer, usePlaybackEngine | src/audio | done: decode, resample, EQ shelves, ring, async seek, speed 0.5-2x, gapless preload 15 s before end (rate-converted), spectrum tap; ReplayGain not in the old app |
| offlineCacheService, downloadManagerService, downloadReconciler | src/app/store, downloads | partial: resumable downloads, verify, offline browse; quality picker, refcount dedup, orphan reconcile todo |
| imageCacheService, AlbumArt, coverArtDownloader, internetArtworkService | src/app/images.* | partial: disk + LRU; mode-aware sizing, iTunes fallback, placeholder detection todo |
| persistentCache, metadataCache, searchCacheService, searchWorker | store.* | partial: SWR kv cache; full search index todo |
| secureCredentialService, credentialsService | src/app/secrets.* | done (Linux Secret Service); Android Keystore, Windows, macOS todo |
| LoginForm (layout, test-then-login, offline toggle, connection history picker), ipc/serverDiscovery + serverDiscoveryProbe | src/app/login.cpp, src/net/discovery.* | done (discovery tested; LAN, Tailscale CLI, hostnames); ReauthDialog todo |
| ArtistList, AllAlbumsGrid, AllSongsGrid, LikedSongsView, LibraryViewToggle, ShuffleAll, Pagination | src/app/library.cpp | done (layout from reference); Shuffle All pages 500 songs serially, quality picker for Download All todo |
| AlbumList (artist page), SongList (album hero + rows) | src/app/pages.cpp | done (layout from reference) |
| SearchResults, SearchContext (server search3, 300 ms debounce, best result rule) | src/app/search.cpp | done; local search index (searchCacheService) todo |
| RightPanel Queue + History tabs, recentlyPlayedService (50, per account in SQLite) | src/app/panel.cpp | done (layout from reference, drag reorder, search, Q/H/P/Esc); Playlists tab, AddToPlaylistDialog, SongContextMenu todo |
| DesktopNowPlaying | src/app/nowplaying.cpp | done (static backdrop; the old one animates blobs); NowPlayingOverlay (mobile layout), MiniPlayer (ipc/playerWindow) todo |
| CarMode (header, art, transport, seek, flip, minimal layout, keep-awake, battery) | src/app/carmode.cpp | done for layout; Pulse Deck (24-band FFT, caps, lag guard), per-song bass/treble EQ by dragging; hi-res art cache and orientation lock todo |
| SettingsView (all 13 sections), SleepTimerPicker, ThemeSelector + CustomThemeEditor, clear-all-data | src/app/settings.cpp | done for layout; Car Mode, Remote, Tech Stack, Licenses, performance-cache stats rows inert or placeholder; HamburgerMenu, KeyboardHelp todo |
| lastfmService, LastfmSettings, useScrobbler | todo (plain scrobble call done) | |
| remote (ipc/remote.js, RemoteModeContext, RemoteDevicePicker) | src/net/remote.*, src/app/remote_ui.cpp | done: UDP presence + multicast, HTTP /cmd server, pairing rules (account match, busy), Be Controlled actions, Control Others picker, player bar routes play/next/prev/seek/volume/shuffle/repeat; wire-compatible with the old app (tested on loopback, TSAN clean); FirewallSetupDialog todo, real two-device test is yours |
| mpris.js | src/app/mpris.* (sd-bus, own thread) | done: status, metadata + art, transport, seek, volume, shuffle, loop; verified with busctl on the real session bus |
| CachePreloadDialog, perfModeService (Gaming/Balanced/Eco), RenderTimerHUD | todo | |
| Android native (capacitorBridge, MusicService, download service) | todo: SDL2 NDK project, owner builds | |

Reference capture tooling lives in the session scratchpad only; to recreate: run the old app with `ELECTRON_IS_DEV=0 electron . --user-data-dir=X --remote-debugging-port=9333`, log in to the fake Subsonic server, drive it over CDP (`Emulation.setDeviceMetricsOverride` 1024x768, `Page.captureScreenshot`, DOM walk with getBoundingClientRect/getComputedStyle).

## Checklist for part 9 (audio features, MPRIS, mini player, remote)

- Speed: click the 1x chip, pick 0.5x to 2x (pitch follows speed). Gapless: let a track end with another queued, no gap; the next track is prepared 15 s before the end.
- Car Mode (Settings > Car Mode > Launch): flip, minimal layout, keep screen on, Pulse Deck (bars icon); drag on the spectrum to shape bass (left third) and treble (right third) for that song, Reset EQ.
- MPRIS: media keys and the desktop media widget control playback (`busctl --user introspect org.mpris.MediaPlayer2.xylonic-fiber /org/mpris/MediaPlayer2`).
- Mini player: header Mini button or Ctrl+M opens the 350x100 window and hides the main one; expand button or closing it brings the main window back.
- Remote: two devices on one account and one LAN (old app or this one): header Remote button lists them; connect, then play/pause, next, seek and volume act on the other device. Settings > Remote switches Be Controlled / Control Others. Needs UDP 7766 and TCP 7767 open.
- Playlists (P): create, rename, expand, play, add to queue, remove songs; they mirror to the server and sync from it. Right-click a song row for the context menu.
