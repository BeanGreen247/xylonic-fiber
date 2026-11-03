# Baseline: old Xylonic 26.10.28 (Electron 27 + React 18)

Measured 2026-10-31 on this machine: Intel i5-8265U (4c/8t, 1.6GHz), 40GB RAM, Linux 6.12 (Debian 13), Wayland. Nobara box not used.

## Method

- App: `/home/bean/xylonic` at `dist/` (version 26.10.28), run with its own `node_modules/electron` (v27.3.11), `ELECTRON_IS_DEV=0`, scratch `--user-data-dir` (real profile untouched), CDP on :9333 for scripted login/play.
- Server: local fake Subsonic (20 artists x 5 albums x 10 songs, one 10-min 192kbps mp3, 600x600 cover). No real server or credentials used.
- State "idle": logged in, Discover view open, nothing playing, performance cache already built, 30s window.
- State "playing": album song playing via the stock queue, Discover/album view, 2 x 30s windows.
- RAM: sum over the whole process tree of `smaps_rollup`. RSS double counts shared pages; PSS divides them and is the fair number. CPU: summed utime+stime ticks over the window, 100% = one core.
- Startup: spawn to first document text (>50 chars) via CDP polling, warm profile, 7 runs, tree killed between runs.
- Tools: `scratchpad/sample.py`, `startup.js`, `fake-subsonic.js` (not committed; reproducible from this description).

## Results

| Metric | Idle | Playing |
|---|---|---|
| Processes / threads | 7-9 / 84-86 | 10 / 97 |
| RSS (MB) | 677-691 | 774-783 |
| PSS (MB) | 295-300 | 329-338 |
| CPU (% of one core) | 0.5-3.1 | 11.9-12.7 |
| Startup to first text | median 812ms (779-938ms, n=7) | |

Largest processes (RSS, playing): main 224MB, GPU 160MB, renderer 152MB, network utility 75MB, audio utility 59MB.

## Size on disk

| Item | Size |
|---|---|
| Electron runtime (`node_modules/electron/dist`) | 248MB |
| App bundle (`dist/`) | 2.4MB (index.js 602kB raw / 165kB gzip modern, plus 982kB legacy bundle) |
| Linux tar.gz / deb / AppImage (26.6.30 release) | 114MB / 92MB / 121MB |

## Not measured

Battery/power draw (desktop machine, no Android device access), Android numbers (hard stop on device tooling), idle RAM of the user's real profile (owner reports ~430MB; consistent with PSS above plus a larger library). Cold-disk startup (page cache warm). Per-feature CPU (Pulse Deck, car mode).

## SDL2 floor (new-app lower bound)

Minimal C program: SDL2 window 1024x768, accelerated vsync renderer, one clear+present, blocking `SDL_WaitEvent` loop. Same sampler. 16KB binary.

| State | PSS | RSS | CPU | Threads |
|---|---|---|---|---|
| Window, no audio device | 32.8MB | 85MB | 0.0% | 8 |
| Window + audio device open (idle or unpaused) | 32.9MB | 85MB | 1.85% | 8 |

Init to window+renderer+audio ready: 226ms. Finding: merely opening the audio device costs ~1.85% CPU on this box (PipeWire/Pulse shim); open it only while playing. The ~33MB PSS is the SDL + Mesa/Wayland floor and does not depend on the implementation language.

Ratio vs old app, playing PSS: 330MB vs 33MB floor, i.e. headroom of 10x before any app code; real app will land above the floor and must be measured per slice.

## New app vs old (measured 2026-10-31, same machine, same fake server)

First end-to-end slice (login, home grid with covers, library, album, queue, search, player bar, streaming). Release build, GL renderer on Wayland, 1024x768, 24+24 album covers on Home, one 10-min mp3 playing. Sampled by PID, 30s windows.

| Metric | Old app idle | New idle | Old playing | New playing |
|---|---|---|---|---|
| PSS (MB) | 295-300 | 35 | 329-338 | 36 |
| RSS (MB) | 677-691 | 87 | 774-783 | 88 |
| CPU (% of one core) | 0.5-3.1 | 0.03 | 11.9-12.7 | 0.47 |
| Processes / threads | 7-9 / 84-86 | 1 / 10 | 10 / 97 | 1 / 11 |
| Startup | 812ms to first text | 147ms to window, renderer and fonts ready (`--selftest`, hyperfine n=15, mean) | | |

Caveats: tiny fake library (20 artists), so memory will grow with a real library; the new app has no keychain, full file-backed search index, MPRIS or themes yet; startup definitions differ (old = first text via CDP, new = init complete). Re-measure on the owner's real server and on the G88 before claiming parity.

## Re-measure after the exact UI port, audio chain, MPRIS and remote service (2026-10-31)

Same machine and fake server, real GL window on Wayland, 1024x768. The earlier "playing 0.47%" row was taken before audio really reached the PulseAudio device; the numbers below are with real playback (decode + resample chain + PipeWire/Pulse client threads).

| Metric | Old idle | New idle | Old playing | New playing |
|---|---|---|---|---|
| PSS (MB) | 295-300 | 42 | 329-338 | 40 |
| RSS (MB) | 677-691 | 98 | 774-783 | 95 |
| CPU (% of one core) | 0.5-3.1 | 0.10 | 11.9-12.7 | 4.1-4.6 |
| Threads | 84-86 | 13 | 97 | 15 |
| Startup | 812ms to first text | 273ms to init complete (`--selftest`, hyperfine n=12) | | |
| Binary | Electron 248MB runtime | 4.1MB executable + 5.7MB assets (fonts) | | |

perf of the playing process: about half of the CPU is the mp3 decoder (dr_mp3), the rest is the audio server client threads (PulseMainloop ~25%) and our resample/ring path. Floor measured earlier: an open audio device alone costs ~1.9%. Startup grew from 147ms because init now also opens the D-Bus session (MPRIS), starts the remote service and loads Roboto plus Font Awesome; not yet profiled.

## After the cache work (2026-11-03)

Same machine and method, 1000-song fake library, fresh profile: idle PSS 48.8 MB (HEAD before: 48.4 MB), playing 43.2 MB (42.1 MB). Warm profile idle 37.8-38.5 MB against the 32.8 MB SDL2 floor. Details and the 100k-song cache benchmark are in `docs/cache-design.md`.
