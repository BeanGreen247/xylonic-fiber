# Testing against a real server

Build (once) and run. After a successful login the password goes to the OS keyring (Secret Service) and the next launch signs in by itself; Log out forgets it. Needs an unlocked keyring (GNOME Keyring / KWallet); otherwise you type it each launch.

    cmake -S . -B build -G Ninja && cmake --build build
    build/xylonic-fiber

Or skip the login screen: `build/xylonic-fiber --server https://host:port --user NAME --pass PASS`.
Self-signed https: the login screen offers "Trust this certificate" with the fingerprint; it is pinned in `~/.local/share/xylonic-fiber/xylonic-fiber/config.json`.
Isolated profile: `XDG_DATA_HOME=/tmp/xf build/xylonic-fiber`.

## Checklist for part 1 (shell, library, playback)

- Login over http and https; wrong password shows the server error.
- Home: covers appear, Refresh re-rolls Random mix; click an album, Play and Shuffle.
- Library: large artist list scrolls smoothly; artist then album then song click plays.
- Search (Ctrl+K, then 2+ letters).
- Player bar: play/pause, prev (restarts after 3s), next, seek by dragging, volume, shuffle, repeat.
- Keys: Space, Shift+Left/Right, Left/Right seek 5s, Shift+Up/Down volume, S, R, M, Q.
- Queue view: jump, remove, clear. Quit and relaunch: queue is restored, nothing auto-plays.
- flac and non-mp3 formats (ogg/opus/m4a are transcoded to mp3 by the server).
- Resource use: `ps -o pid,rss,pcpu -p $(pgrep xylonic-fiber)` idle and playing; report numbers.
- Known gaps: no offline downloads UI, no themes, no MPRIS, no Android build yet.

## Checklist for part 2 (metadata cache, offline downloads, offline mode)

- Album page: Download (all tracks), "Download rest", dots on rows (filled = downloaded, ring = pending, red = failed).
- Downloads view: progress, pause/resume, remove, Verify (drops rows whose file is gone), Retry failed, Clear all. Quit mid-download and relaunch: queued items continue.
- Playback order: a downloaded track plays from disk (works with the server stopped); otherwise it streams into `stream/` and is reused next time.
- Offline mode: login screen button "Offline mode (downloaded music)" needs a saved account; Home/Library/Album show only downloaded music, covers come from the disk cache.
- Library, artists, albums, home load instantly from the SQLite cache and refresh in the background.
- Data lives in `~/.local/share/xylonic-fiber/xylonic-fiber/` (`data_<account>.db`, `downloads/`, `stream/`, `covers/`).

## Checklist for part 3 (exact UI port, in progress)

Run the old app and the new one side by side at 1024x768 and compare: header (logo, Xylonic, github, heart, search, Remote, Mini, globe), icon nav, Discover (4 sections, card hover, scroll arrows, Play All, refresh), Library (Artists/Albums/Songs/Liked toggle, stats pills, Shuffle All, Download All, 4-column grids, song rows, pagination), player bar (progress, transport, speed, quality, volume). Known gaps are listed in docs/port-map.md. Reference screenshots: docs/ui-ref/*.png.

## Checklist for parts 4-8 (login, search, panel, now playing, settings)

- Login screen: the server list fills in by itself (LAN, Tailscale peers, common hostnames); pick one, enter user and password, Test Connection, then Login (Login stays disabled until the test passes). Previous Connections picker keeps the last 10 and can forget one; "Enter Offline Mode" needs a saved account.
- Search (header field or Ctrl+K): results appear after 300 ms; Best Result rule artist > album > song, Songs (top 5 + Play all), Artists, Albums, More Songs.
- Right panel: Q opens the queue, H history (Recently Played, 50), P playlists (not yet); Esc closes; drag the grip to reorder; search icon filters.
- Click the current song in the player bar: Now Playing (Up Next list, seek, transport). Esc or X closes.
- Settings: appearance mode, accent theme dialog (12 themes, 4 custom slots), sleep timer, streaming and download quality, library default view, performance modes, clear caches, about. Car Mode, Remote, Tech Stack and Licenses rows are placeholders.
