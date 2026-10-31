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
