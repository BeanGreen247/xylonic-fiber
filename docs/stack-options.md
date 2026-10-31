# Stack (decided 2026-10-31)

Fixed by owner: UI is SDL2 on all platforms. Open: implementation language, UI layer on top of SDL2, and the supporting libraries. Numbers: `docs/baseline.md`.

## Language candidates

| | C11 | C++17 (no exceptions/RTTI) | Rust (`sdl2` crate) |
|---|---|---|---|
| SDL2 floor PSS / init | 33MB / 226ms (measured) | same SDL floor, language-independent; not separately measured | not measurable here: no rustc/cargo installed |
| Android | SDL2 NDK project, native code direct | same | NDK + cargo-ndk glue on top of SDL2 Java shell |
| iOS / macOS / Windows | direct | direct | works, extra toolchain per target |
| Audio/TLS/JSON/SQLite libs (dr_libs, libFLAC/vorbis/opus, mbedTLS or libcurl, yyjson, SQLite) | all native C | all native | via bindings or ports |
| Build tooling on this box | gcc, clang, CMake-ready | same | needs user-level rustup install |
| Cost | manual containers/strings, more code for queue/cache logic | containers and RAII, larger binaries | memory safety by construction, slower compile |

Recommendation: C++17 restricted to no exceptions/RTTI, built with CMake and clang/gcc. Reason: every library on the list is C/C++ native, SDL2's Android/iOS projects are native-first, and the 33MB floor is identical across languages so the choice is about development speed and safety of the cache/audio code, not footprint. Rust is the alternative if you prefer compile-time memory safety; I would then install rustup into `~/.cargo` and measure the same floor before committing.

## Decisions needed from you

1. Language: C++17 (recommended), C11, or Rust.
2. UI layer on SDL2 (SDL2 has no widgets or text layout): custom retained/immediate UI from scratch, or an existing immediate-mode library (Dear ImGui, Nuklear). Text: FreeType vs stb_truetype; CJK/RTL shaping needed or not.
3. Audio decode: own pipeline over SDL audio callback with dr_mp3/dr_flac/stb_vorbis/opus (single owned pipeline, matches the requirement) vs SDL_mixer (rejected by the single-pipeline rule unless you say otherwise).
4. Network/TLS: libcurl (bigger, battle-tested, system certs) vs mbedTLS + minimal HTTP client (smaller).
5. Names: project, binary and module names are yours to choose; none used so far.

## Decisions (owner)

- Language: C++17, no exceptions, no RTTI. No Rust.
- UI: custom on SDL2 (no widget library).
- Audio: own decode pipeline over the SDL audio callback; open the device only while playing.
- Network/TLS: own minimal HTTP/1.1 client behind a thin TLS interface (owner delegated). Initial backend mbedTLS (same code on Linux/Android/Windows/macOS/iOS, small), OS root certificates loaded per platform. Native backends (SChannel, Network.framework) only if measured worthwhile. Not yet fetched or built.
- Naming: project name is the repo name, xylonic-fiber. Source dirs use plain descriptive names (audio, net, ui, cache), not brand names.
