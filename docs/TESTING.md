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
