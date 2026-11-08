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
| persistentCache, metadataCache, searchCacheService, searchWorker | store.*, future search cache | partial: atomic JSON/file cache; full search index todo |
| secureCredentialService, credentialsService | src/app/secrets.* | done (Linux Secret Service); Android Keystore, Windows, macOS todo |
| LoginForm (layout, test-then-login, offline toggle, connection history picker), ipc/serverDiscovery + serverDiscoveryProbe | src/app/login.cpp, src/net/discovery.* | done (discovery tested; LAN, Tailscale CLI, hostnames); ReauthDialog todo |
| ArtistList, AllAlbumsGrid, AllSongsGrid, LikedSongsView, LibraryViewToggle, ShuffleAll, Pagination | src/app/library.cpp | done (layout from reference); Shuffle All pages 500 songs serially, quality picker for Download All todo |
| AlbumList (artist page), SongList (album hero + rows) | src/app/pages.cpp | done (layout from reference) |
| SearchResults, SearchContext (server search3, 300 ms debounce, best result rule) | src/app/search.cpp | done; local search index (searchCacheService) todo |
| RightPanel Queue + History tabs, recentlyPlayedService (50, per account in local JSON) | src/app/panel.cpp | done (layout from reference, drag reorder, search, Q/H/P/Esc); Playlists tab, AddToPlaylistDialog and SongContextMenu done |
| DesktopNowPlaying | src/app/nowplaying.cpp | done (static backdrop; the old one animates blobs); NowPlayingOverlay (mobile layout), MiniPlayer (ipc/playerWindow) todo |
| CarMode (header, art, transport, seek, flip, minimal layout, keep-awake, battery) | src/app/carmode.cpp | done for layout; Pulse Deck (24-band FFT, caps, lag guard), per-song bass/treble EQ by dragging; hi-res art cache and orientation lock todo |
| SettingsView (all 13 sections), SleepTimerPicker, ThemeSelector + CustomThemeEditor, clear-all-data | src/app/settings.cpp | done for layout; Car Mode, Remote, Tech Stack, Licenses, performance-cache stats rows inert or placeholder; HamburgerMenu, KeyboardHelp todo |
| lastfmService, LastfmSettings, useScrobbler | todo (plain scrobble call done) | |
| remote (ipc/remote.js, RemoteModeContext, RemoteDevicePicker) | src/net/remote.*, src/app/remote_ui.cpp | done: UDP presence + multicast, HTTP /cmd server, pairing rules (account match, busy), Be Controlled actions, Control Others picker, player bar routes play/next/prev/seek/volume/shuffle/repeat; wire-compatible with the old app (tested on loopback, TSAN clean); FirewallSetupDialog todo, real two-device test is yours |
| mpris.js | src/app/mpris.* (sd-bus, own thread) | done: status, metadata + art, transport, seek, volume, shuffle, loop; verified with busctl on the real session bus |
| CachePreloadDialog, perfModeService (Gaming/Balanced/Eco), RenderTimerHUD | todo | |
| Android native (capacitorBridge, MusicService, download service) | todo: SDL2 NDK project, owner builds | |

Reference capture tooling lives in the session scratchpad only; to recreate: run the old app with `ELECTRON_IS_DEV=0 electron . --user-data-dir=X --remote-debugging-port=9333`, log in to the fake Subsonic server, drive it over CDP (`Emulation.setDeviceMetricsOverride` 1024x768, `Page.captureScreenshot`, DOM walk with getBoundingClientRect/getComputedStyle).

For native screenshots, use the executable's `--shot output.bmp --shot-after 1500 --size 1024x768` flags after starting the same fake server and login state. Compare the result pixel-for-pixel with:

```sh
tools/compare-screenshot.sh docs/ui-ref/home.png native-home.bmp native-home-diff.png
```

The command fails on any differing pixel and writes a visual diff when the third argument is supplied. Exact parity comparisons must use the same server fixture, account data, viewport, font assets, and settled wait time.

## Checklist for part 9 (audio features, MPRIS, mini player, remote)

- Speed: click the 1x chip, pick 0.5x to 2x (pitch follows speed). Gapless: let a track end with another queued, no gap; the next track is prepared 15 s before the end.
- Car Mode (Settings > Car Mode > Launch): flip, minimal layout, keep screen on, Pulse Deck (bars icon); drag on the spectrum to shape bass (left third) and treble (right third) for that song, Reset EQ.
- MPRIS: media keys and the desktop media widget control playback (`busctl --user introspect org.mpris.MediaPlayer2.xylonic-fiber /org/mpris/MediaPlayer2`).
- Mini player: header Mini button or Ctrl+M opens the 350x100 window and hides the main one; expand button or closing it brings the main window back.
- Remote: two devices on one account and one LAN (old app or this one): header Remote button lists them; connect, then play/pause, next, seek and volume act on the other device. Settings > Remote switches Be Controlled / Control Others. Needs UDP 7766 and TCP 7767 open.
- Playlists (P): create, rename, expand, play, add to queue, remove songs; they mirror to the server and sync from it. Right-click a song row for the context menu.

## Checklist for the cache foundation (generations, media manifest, store speed)

- Unit tests: `ctest --test-dir build-linux -R "cache|journal|media|downloads"` (generation publish, recovery, corruption, concurrent readers, page cache bounds, media dedup/eviction/reconcile).
- Benchmarks: `build-linux/cache_bench 100000` and `build-linux/store_bench 300`; compare with the table in `docs/cache-design.md`.
- Real server: open a large library, browse albums, then search; search results should appear without a pause and the first search after start builds the index once. Quit and relaunch: albums and downloads must still be there (the journal is replayed if the app was killed).
- Player bar right side: Queue, History, Playlists toggle buttons (same as Q/H/P keys) left of nothing else; check at 1024x768.

## Checklist for the wired cache (likes, playlists, artwork, downloads)

- Heart a song in the player bar, Now Playing and Car Mode; quit, relaunch offline (stop the server): Library > Liked still lists it.
- Add a song to a playlist from the row menu; the dialog shows a check on playlists that already hold it, also after a restart.
- Search finds songs from albums you opened earlier without contacting the server; first search after a fresh start may take a moment (index rebuild), later starts load the persisted index.
- Covers: browse, restart offline, covers still show; Settings > clear image cache empties it. Disk use stays under 64 MiB of artwork.
- Downloads: download two albums that share a track file (or the same song twice); one file under `downloads/<account>/media/audio`; removing one keeps the other playable; removing both deletes the file.
- Kill the app with `kill -9` during a crawl and relaunch: albums and likes survive (journal replay).

## Checklist for Shuffle/Download All, quality picker, Reauth, Cache Statistics, Car Mode

- Library > Songs > Shuffle All on a large library: playback starts after the first page; skipping forward later reaches songs from later pages; the queue panel count grows.
- Library > Download All: a quality list appears first; downloads start immediately and queue grows page by page. Settings > Downloads quality changes affect the next download without restarting.
- Change the account password on the server while the app is running (or revoke the token), then browse: "Sign in again" appears; a wrong password shows the server message, the right one closes it and downloads continue. `--modal reauth|cachestats|dlquality` opens the dialogs for screenshots.
- Settings > Offline & Cache > Cache Statistics matches what you expect (artwork stays under 64 MB).
- Car Mode with no cover or offline: a thumbnail or the music glyph shows instead of an empty square. On Android the screen stays landscape while Car Mode is open.

## Checklist for the third pass (text size, mobile layout, covers, auto-offline)

- 1024x768: text sizes and positions look like the Electron app (compare `docs/ui-ref/home.png`); no Queue/History/Playlists buttons on the bar below 1100 px, they appear from 1100 px.
- Drag the window narrower than 700 px (minimum 320x480): bottom nav, compact header, mini player; tap the mini player title to open Now Playing; Library tabs and pills wrap.
- Artists whose server image is the grey star show an iTunes cover (or an empty tile with the fallback off). Settings > Cover art fallback toggles it; it sends artist/album names to Apple.
- Eco mode: covers load smaller; Cache Statistics shows the lower artwork budget.
- Metered connection (Linux): mark the connection metered in NetworkManager, enable Settings > Auto-offline on mobile data with songs downloaded; the app goes offline within 30 s and returns when the flag is cleared.
- Screenshot flags: `--size WxH --nav home|library|settings --panel queue --modal reauth --shot out.bmp --shot-after 6000`.

## Checklist for the performance cache prestep

- Fresh profile, first login: "Building Performance Cache" appears by itself with four gauges, finishes with "Library Ready!" and a 15 s countdown (Restart Now skips it). Second launch: no dialog. `--no-precache` suppresses it for scripted runs.
- Settings > Performance Cache shows images/bytes, the search index counts and size, and the cache/network counters; the refresh icon re-reads them. Settings > Rebuild Cache asks first, then rebuilds. Clear All Caches asks first and also deletes downloaded files.
- Change the server library (add or remove an artist or songs), relaunch, wait 3 s: "Library Change Detected!" with the differences; Later dismisses it, Rebuild Cache Now runs "Updating Library Cache".
- Stop the server mid-build or use a flaky link: the dialog shows the failure text with Try Again and Skip for Now; the done flag stays unset after a failure.
- Artists without a photo show the user-circle icon (not Navidrome's star); items without any cover id ask iTunes (online only).
- `--modal newcontent|outdated|rebuild`, `--scroll N` (settings) and `--shot` open the prompt and dialogs for screenshots.

## Checklist for the fifth pass (picker, banner, prompts, compact layout)

- Library > Download All opens "Download Quality" (Original (Raw), 320 default, 256, 128, 64) with Cancel and "Download All"/"Download Missing"; after confirming, only songs that are not downloaded are queued and the Downloads page opens. On an album page the confirm reads "Download N Songs"; on an artist page "Download Artist".
- Shuffle All: with a song playing the first press shows "Confirm?" for 3 s; the second press shows "Loading..." and starts a shuffled queue. Offline it shuffles downloaded songs.
- Banner: with some (not all) songs downloaded, 5 s after connecting a blue "N songs not downloaded" strip appears; Download Missing opens the picker, the cross dismisses it. `--modal banner` shows it for screenshots.
- Stop the server while the app runs: within 30 s "No Internet Connection" appears (Continue Online Mode / Enable Offline Mode). `--modal offlineprompt|cellprompt` show the prompts.
- Header: offline mode shows the plane and "Offline"; Gaming/Eco show a coloured badge; Mini is dimmed until a song is loaded.
- Narrow window (<=767 px, minimum 320x480): bottom nav with the Queue submenu, mini player, 3-column library and artist pages, album hero page, search page with its own input. `--size 390x844 --nav home|library|search|artist:ID|album:ID` for screenshots.
