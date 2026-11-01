# Claude Handoff: Portable, Memory-Constrained Cache Design

Updated 2026-10-31.

## Current direction

Rewrite the original Xylonic cache and performance systems in native C++ without SQLite. The behavior should remain compatible with the original Electron application, while reducing CPU, RAM, GPU, startup, and network work on slower devices such as the Oilsky G88.

The latest original Xylonic version containing the complete custom cache systems was **26.10.28**:

- `26.09.10`: persistent metadata cache using memory + IndexedDB with stale-while-revalidate.
- `26.09.29`: permanent-cache storage/location work.
- `26.10.20`: performance-cache failure handling and retry fixes.
- `26.10.28`: latest original release containing all of the above.

## Storage principles

The native system must be disk-first and memory-aware:

- No SQLite.
- Persistent metadata, journal, indexes, audio cache, and artwork cache use custom files.
- Keep only hot state and compact indexes resident.
- Load cold metadata pages on demand and evict them with bounded LRU policies.
- Use shared string tables so artist, album, and song names are stored once.
- Use numeric IDs, packed records, sorted vectors, bitsets, and dictionary IDs instead of pointer-heavy containers.
- Keep audio and artwork bytes out of the metadata store.
- Keep all writes serialized through one persistence path.

Suggested layout:

```text
cache/
  manifest
  metadata.snapshot
  metadata.journal
  metadata.strings
  metadata.index
  audio/<content-hash>
  artwork/<content-hash>
  cache.manifest
```

## Metadata fields and indexes

The packed song record needs to support the original UI and playback behavior:

```text
song_id, artist_id, album_id
title, artist_name, album_name, album_artist_name
track_number, disc_number, duration, size, year
format, codec, bitrate, sample_rate, channels, bit_depth
liked, version, source URL/path
```

Required indexes:

```text
song_id       -> SongRecord
artist_id     -> ArtistRecord
album_id      -> AlbumRecord
normalized artist name -> sorted artist IDs
normalized song name   -> sorted song IDs
liked songs             -> compact ID set
playlist ID             -> compact ordered song ID set
artist ID               -> album ID set
album ID                -> song ID vector
```

Likes, playlist membership, format information, technical audio fields, and names must all be served by this cache/index layer rather than repeatedly fetched or recomputed by UI code.

## Persistence and corruption safety

Use an append-only journal plus validated snapshots. A journal record should contain:

```text
magic, format_version, transaction_id, generation,
payload_length, payload, checksum, commit_marker
```

Rules:

1. Validate records before applying them.
2. Write and flush a complete journal transaction before publishing it.
3. Publish new in-memory generations only after the journal write succeeds.
4. Write snapshots to temporary files, validate them, then atomically rename them.
5. On startup, validate the snapshot and replay only complete journal transactions.
6. Discard an incomplete journal tail safely.
7. Reconcile audio/artwork files against the cache manifest.
8. A damaged media file must never invalidate metadata.

## Threading model

Use bounded workers, not one thread per request:

```text
UI thread
  Reads published immutable generations and submits mutations.

Metadata workers
  Parse network responses, normalize records, build update batches.

Journal writer
  Serializes persistent mutations and publishes generations.

Snapshot worker
  Compacts journals into validated snapshots in the background.

Cache workers
  Download and verify audio/artwork through temporary files and atomic rename.

Audio thread
  Reads prepared streams only; never performs filesystem, network, journal,
  compression, or blocking-lock operations.
```

Use immutable published generations for reads. A writer creates generation N+1 and atomically publishes it, so UI queries never observe half-written indexes and do not need a global read lock.

## Platform profiles

The core must be portable C++ and independent of SDL2/platform APIs. SDL2 is the desktop, Android, and iOS UI path. A Rockbox UI/platform adapter can be added later without changing the storage core.

```text
core/
  metadata/
  indexes/
  journal/
  cache/
  playback/
  scheduler/

platform/
  filesystem/
  threads/
  clocks/
  networking/
  audio/

ui/
  sdl2/
  rockbox/       later, if needed
```

Profiles should change memory budgets and worker counts:

- Desktop: larger hot cache and prefetch window; full library, playlists, artwork, and Car Mode.
- Android/iOS: adaptive cache limits and reduced prefetch; retain the full feature set.
- Low-memory mobile: aggressive LRU eviction and smaller artwork/metadata windows.
- Rockbox/minimal: disk-first operation, tiny resident index, one worker or cooperative work, playback/queue/basic search first.

Do not constrain the desktop/mobile implementation around Rockbox yet. Keep the core portable now and add the minimized Rockbox UI later if SDL2 is unavailable or unsuitable there.

## Implementation order for Claude

1. Add packed metadata types, shared string table, and platform cache-profile limits.
2. Implement `PackedMetadataStore` with compact indexes and generation-published read APIs.
3. Implement the checksummed append-only journal and atomic snapshot recovery.
4. Connect Subsonic responses to normalized metadata update batches.
5. Move likes, playlists, artist/album/song names, and technical audio metadata to the store.
6. Replace ad-hoc metadata reads in the UI with indexed query APIs.
7. Connect the permanent audio/artwork cache through a separate verified manifest.
8. Add stale-while-revalidate, prefetch, bounded LRU eviction, and cancellation.
9. Add corruption-recovery, concurrent-reader/writer, memory-budget, and performance tests.
10. Measure against the existing 33 MB baseline before adding platform-specific optimizations.

Do not reintroduce SQLite. Keep the original Electron behavior as the compatibility reference, but optimize the implementation around packed native data, disk-backed persistence, immutable generations, and bounded background work.

## Claude startup checklist

When resuming this project, read this file first, then read `docs/session_summary.md`, `docs/port-map.md`, `docs/TESTING.md`, `docs/baseline.md`, and `android/ANDROID.md`. Confirm the worktree state and run the existing build/tests before changing code:

```sh
git status --short
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

After that, continue the exact-port parity work documented in the Claude memory bank. The cache redesign is a planned foundation slice and must not silently replace the current working JSON store until its recovery and compatibility tests pass. Update this file and the Claude memory-bank entry after each bounded slice.

## Two-day preparation plan

The aim is to prepare the difficult boundaries before Claude starts wiring the complete system into the UI.

### Today: define and prove the foundations

Do not replace the current app store or UI yet. Prepare isolated, testable components first.

1. Add a `docs/cache-design.md` specification with the record layouts, file formats, generation rules, cache states, and memory budgets from this handoff.
2. Add packed metadata model types with fixed-width IDs and offsets into a shared string table. Avoid owning `std::string` values in every song record.
3. Define the query interface needed by the UI:
   - song/album/artist lookup by ID
   - artist, album, and song name search
   - liked-song lookup
   - playlist membership and ordered playlist contents
   - technical metadata lookup
4. Define a `CacheProfile` for desktop, mobile, low-memory, and minimal/Rockbox modes.
5. Define journal operations and versions without implementing recovery yet: insert/update song, update like, replace playlist, remove record, cache-file state change.
6. Add unit tests for string interning, packed/unpacked records, normalized names, ID lookups, playlist membership, and memory-budget calculations.
7. Add a small benchmark fixture based on the existing 33 MB baseline. Measure lookup latency, index memory, and load time before optimizing.

Today’s work should compile independently and must not alter playback behavior.

### Tomorrow: implement the persistence skeleton

1. Implement the checksummed journal writer with length validation and complete-transaction markers.
2. Implement snapshot writing to a temporary file followed by validation and atomic rename.
3. Implement startup recovery for valid snapshots, complete journal records, truncated journal tails, and invalid checksums.
4. Implement the in-memory generation object and atomic publication of a new generation after a successful journal transaction.
5. Implement the compact primary indexes and a bounded cold-record/page cache.
6. Add tests with forced truncation, checksum errors, interrupted snapshots, duplicate transactions, concurrent readers, and concurrent writer/readers.
7. Run the benchmark fixture with desktop and low-memory profiles and record results in `docs/baseline.md`.

### What should wait

Do not yet rewrite every UI screen, add Rockbox UI code, add aggressive compression, or connect network synchronization directly to the new store until the file format and recovery tests pass. Do not put compression, disk access, or cache locks on the audio callback.

### Claude’s first integration pass after preparation

Once the persistence skeleton is stable, Claude should connect Subsonic normalization to update batches, then move likes, playlists, names, and technical fields behind the query API. The existing JSON store can remain as a migration/bootstrap source temporarily, but the new cache must become the authoritative implementation before removing the old path.

## Progress log

### 2026-10-31 — foundation slice completed

Added the isolated `cachecore` library and `cache_test` without changing the existing JSON store, UI, or playback path:

- `src/cache/metadata.h/.cpp`: packed song records, shared string table, normalized name-search index, liked state, playlist membership/order, technical audio fields, and desktop/mobile/low-memory/minimal profiles.
- `docs/cache-design.md`: initial format and memory-profile specification.
- `tests/cache_test.cpp`: interning, packed lookups, normalized search, likes, playlists, and profile-limit tests.
- `CMakeLists.txt`: cachecore target and cache test registration.

Verification: full native build succeeded and all 17 CTest tests passed in 48.69 seconds; the follow-up cache test also passes after replacing the linear string-ID reverse lookup with the string table’s hash map. The new cache layer is not authoritative yet. Search is currently prefix-only and should remain that way until query semantics are compared with the original app. Next implement the checksummed journal and atomic snapshot recovery described above.

### 2026-10-31 — persistence skeleton completed

Added `src/cache/journal.h/.cpp` and `tests/journal_test.cpp`. The isolated persistence layer now has:

- versioned binary records with magic, transaction ID, generation, length, and CRC32;
- data records followed by explicit commit markers;
- recovery that applies only committed transactions and ignores an incomplete tail;
- checksummed snapshots written through a temporary file and rename;
- tests for multiple transactions, truncated journal data, valid snapshots, and corrupted snapshots.

The cache and journal tests pass, and the full project has no pending build work. This is still a persistence skeleton: production integration needs platform-specific durable flush/atomic-replace behavior, immutable generation publication, and a bounded cold-page cache. Do not connect it to UI/playback yet.

### 2026-10-31 — Linux build helper completed

Added executable `build-linux.sh`. From the project root, the owner can now run:

```sh
./build-linux.sh
```

This creates a Release build in ignored `build-linux/`, builds all Linux targets, runs all tests, and prints the executable path. Use `./build-linux.sh --run` to launch it, passing application flags after `--run`; use `--no-test` for a quick rebuild. The helper was verified locally: all 18 tests passed and `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build-linux.sh --no-test --run --selftest` exited successfully.

### 2026-10-31 — icon parity slice completed

Generic queue/download controls in `Ui::icon_button` previously used hand-drawn triangles and lines, which did not match the Electron Font Awesome icons. They now route through the same Font Awesome subset and application renderer used by the exact shell, covering play, pause, next, previous, shuffle, repeat, heart, volume, queue, home, library, settings, search, close, back, download, and plus. The main executable rebuilt successfully and the dummy SDL self-test exited successfully.

This is a focused parity fix, not completion of the full UI port. Continue comparing every screen against `docs/ui-ref/*.png`; remaining high-priority parity gaps are speed/quality popups, expanded navigation/hamburger, settings inert rows, and exact screenshot verification.

### 2026-10-31 — playback/settings parity slice completed

- The player-bar quality control now opens an anchored six-option selector (`Original`, `320`, `256`, `192`, `128`, `64 kbps`) with checkmark, hover state, outside-click dismissal, and persisted selection. Speed already had the corresponding selector.
- Settings rows for Firewall Setup, Tech Stack, and Licenses now open functional themed dialogs instead of doing nothing.

Verification: Linux executable rebuilt; dummy SDL self-test passed; smoke and UI tests passed. Remaining UI work is expanded navigation/hamburger behavior and screenshot-by-screen parity review. Do not mark the full UI port complete yet.

The original Electron hamburger is a separate anchored dropdown; the 1024px reference uses the collapsed left rail. Confirm the target viewport/reference before adding a desktop expanded rail.

### 2026-10-31 — Now Playing and Car Mode visual pass

- Now Playing’s cover-colour backdrop now uses three slowly drifting radial blobs, matching the Electron animation direction while requesting frames at the app’s throttled 2.5 Hz cadence instead of forcing 60 FPS.
- Car Mode now requests a 900px artwork variant for its large cover display instead of upscaling the normal 450px library variant.

Verification: Linux target rebuilt and dummy SDL self-test passed. Remaining work is mobile Now Playing/layout verification, artwork fallback/orientation behavior, hamburger dropdown parity, and screenshot comparison.

### 2026-10-31 — narrow-window hamburger pass

For windows below 900 logical pixels, the header now hides the desktop Remote/Mini pills and exposes an Electron-style anchored hamburger button. Its dropdown includes Remote Devices, Mini Player, Downloads, Settings, Keyboard Help, Offline Mode, and Sign out. While open, underlying content input is suppressed and outside-click/escape closes the menu.

Verification: Linux target rebuilt; dummy SDL self-test, smoke, and UI tests passed. This is the responsive behavior foundation; exact mobile screenshots and spacing still need comparison against the original at 680px/phone sizes.

### 2026-10-31 — sidebar and player hit-testing pass

- The left rail now supports a 200px expanded state and the existing 64px collapsed state. The collapsed state remains the default so the 1024px reference layout is preserved; the top strip expands it, and the expanded rail has the Electron-style Collapse control and labels.
- The content view now receives suppressed pointer coordinates whenever the pointer is in the bottom player bar or a seek/volume drag is active. This prevents clicks/releases from passing through to album cards, rows, or settings behind the player controls.

Verification: Linux target rebuilt; dummy SDL self-test, smoke, and UI tests passed. Next verify expanded/collapsed screenshots and exercise player clicks/drags over populated pages against a real server.
