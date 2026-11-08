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

## Native app measurements (2026-11-05, same machine, fake server, `docs/ui-ref/fake-subsonic.js`)

Method: `cpu.sh` samples utime+stime of the process between 6 s and 16 s after start, PSS from `smaps_rollup`; `perf record -F 999 --call-graph dwarf -p PID` for the profile (KDE Hotspot opens the `.data`).

| State | CPU (% of one core) | PSS |
|---|---|---|
| Home, idle | 0 | 60 MB |
| Playing (Home) | 0.3 | 60 MB |
| Car Mode, Pulse Deck off | 0.5 | 57 MB |
| Car Mode, Pulse Deck on (30 fps, 32 bands) | 3.3 | 49 MB |

Changes measured: `spectrum_bands` 81 -> 45 us per call (cached bit-reversal, Hann and twiddle tables, no per-call allocation; identical output checksum over 32 bands). Release LTO + `-ffunction-sections -Wl,--gc-sections`: binary 3,731,216 -> 2,911,808 bytes (-22%), Pulse Deck CPU unchanged (3.3 vs 3.4%), PSS -0.7 MB. Remaining Pulse Deck cost is spread across SDL render submission and the kernel (no single user function above 3% of samples); libdbus frames come from SDL's session-bus handling, not from MPRIS (change-detected).

## Re-measurement 2026-11-08 (laptop, same method; native build with Riverbero, Winamp mini, float pipeline)

Same i5-8265U, fake Subsonic, 2 x 30 s windows, scripts in the session scratchpad (`bench.sh`, run in a network namespace). Old app needed `--no-sandbox --ignore-certificate-errors` and ran under XWayland; the new app used the GL renderer on Wayland with real PipeWire audio.

| State | Old Electron PSS / CPU | New native PSS / CPU | Ratio (PSS, CPU) |
|---|---|---|---|
| Idle | 279-283 MB / 0.27-0.57% | 58.8 MB / 0.17-0.23% | 4.8x, ~2x |
| Playing | 311-313 MB / 8.0-10.0% | 57.0 MB / 1.27-1.30% | 5.5x, ~7x |
| Car Mode + Riverbero bars playing | n/a | 57.8 MB / 3.5-3.8% | |
| Winamp mini playing | n/a | 67.2 MB / 2.3-2.4% | |
| Startup | 646 ms to first text (634-696) | 240 ms to init complete (213-435) | different end points |

Disk: Electron runtime 248 MB + 2.4 MB bundle; native 4.34 MB executable + 5.2 MB assets. PSS of the native app is higher than the 2026-10-31 slice (35 MB) because the app now carries the float pipeline, codecs, storage manager and the image cache.

## Device (Oilsky G88, Android 12, arm64), CPU as share of one core, before and after 2026-11-08 work

Sampled from `/proc/<pid>/stat` over 20-30 s (`devsample.sh`, per thread with `thr.sh`), debug APK, Wi-Fi on, nothing else foregrounded.

| State | Before | After | What changed |
|---|---|---|---|
| Home, nothing playing | 15.9% | ~5-7% | SDL 2.32 has no native event wait on Android: `SDL_WaitEvent` is a `SDL_Delay(1)` loop, 1000 wakeups/s. Replaced by an event-watch semaphore (`App::wait_event`). The UI thread dropped out of the profile |
| Home, playing a 44.1 kHz FLAC (device rate 48 kHz) | 29.4% | 14.7% | Resampler: per-tap `double` weight lookups replaced by a 128-phase x 32-tap polyphase table with linear phase interpolation (decode thread 22.8% -> 3.6%). `dsp_test` still passes |
| Car Mode, Riverbero bars, playing | 55% (top: 45-60%) | 31.7% | Battery read once per 10 s instead of a JNI round trip per frame (~2 ms of an 8 ms frame), FFT without `std::complex` multiplies and with cached band edges, frame rate tiers (30/25/15 fps), no redraw when the rail is hidden and nothing moves |

Rejected: `-O3 -fno-math-errno -fno-trapping-math` for the native code (Riverbero 714 -> 731 ticks per 20 s, within noise; build 9 min). The per-frame cost is now mostly the GL driver (`SDL_RenderPresent` 2.8 ms of a 4.8 ms frame); not measured: GPU load (counters need root), battery drain.

## Final pass for 0.2.0 (2026-11-08)

Device (same method as above, 30 s windows): Home idle 4.9% (was 15.9%), Home playing a 44.1 kHz FLAC 15.0% (was 29.4%), Riverbero bars playing 30.9% (was 55%). PSS 84 / 91 / 86 MB.

Laptop, same binary run outside the isolated network namespace: idle 0.17% / 63 MB PSS, playing 1.73% / 64 MB, Riverbero 2.7-2.8% / 60-64 MB (was 3.5-3.8% before the real-input half-size FFT), Winamp mini 2.8% / 70 MB. The 0.2.0 playing figure is higher than the 1.3% / 57 MB from the namespace run above because the namespace run had no LAN, so no discovery traffic and 3 fewer threads; the binary is not the difference (below).

A/B of the extra compiler flags on the laptop (`-DXF_TUNE_FLAGS=OFF` vs ON, same tree, two rounds, 25 s windows): playing 1.80 / 1.76 vs 1.76 / 1.72 %, Riverbero 2.72 / 2.72 vs 2.76 / 2.72 %: no difference, binary 4.35 vs 4.43 MB. Kept ON (a user request, same flags as Lestrix and hootedit); `-z pack-relative-relocs` is not used because it needs glibc 2.36 at run time.
