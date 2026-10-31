# Session summary (git-tracked memory bank)

Updated after every step. Newest first.

## 2026-10-31

Decisions (owner): UI is SDL2 on all platforms. Platforms: Linux desktop, then Android, then Windows/macOS/iOS. Local machine only for measuring; Nobara box off unless asked. No naming without asking. Commits: single-line conventional, no attribution, main only for verified slices. Never run Android/iOS builds, ADB, Gradle, cap sync.

Done: scope.md (must/should/later), baseline.md (old app idle PSS ~300MB, playing ~335MB, 12% CPU playing, 812ms startup, SDL2 floor 33MB PSS), stack-options.md.

Stack decided (C++17, custom SDL2 UI, own audio pipeline, own HTTP + mbedTLS); scaffolded CMake + SDL2 window + ctest smoke (builds, passes). Next: audio pipeline spike (decode mp3 to SDL callback), measure vs floor.

Was open: owner pick on language, UI layer, audio decode, TLS (see stack-options.md). No scaffolding until answered.

Finding: opening an SDL audio device costs ~1.85% CPU on this box even when idle; open only while playing.
