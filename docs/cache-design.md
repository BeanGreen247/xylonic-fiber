# Native cache foundation

This is the first isolated foundation slice for replacing the original Electron performance and permanent cache systems. It deliberately does not replace the current JSON-backed `Store` yet.

## Goals

- Preserve original Xylonic 26.10.28 behavior for metadata, likes, playlists, playback, and permanent media cache.
- Keep hot reads fast without duplicate heap strings in every record.
- Keep cold metadata, audio, and artwork on disk and load it on demand.
- Share one portable core across desktop, Android/iOS, low-memory devices, and a future minimal Rockbox target.
- Never block SDL UI or the audio callback on network, disk, compression, or cache locks.

## First data model

`SongInput` is the normalized boundary from Subsonic/UI code. `PackedSong` stores shared-string IDs and fixed-width numeric fields. `StringTable` interns repeated names, IDs, formats, and codecs. `MetadataSnapshot` owns a generation-local record vector and query indexes.

The first query surface covers song lookup by server ID, prefix search over normalized title/artist/album names, liked state, ordered playlist contents and membership, and technical audio metadata.

The current implementation is single-writer and thread-neutral. Later generations can be constructed away from the published snapshot and atomically published after journal persistence succeeds.

## Starting memory profiles

| Profile | Hot songs | Cold pages | Artwork | Workers | Prefetch |
|---|---:|---:|---:|---:|---|
| Desktop | 1024 | 64 | 64 MiB | 4 | yes |
| Mobile | 384 | 24 | 24 MiB | 2 | yes |
| Low memory | 128 | 8 | 8 MiB | 1 | no |
| Minimal | 64 | 2 | 2 MiB | 1 | no |

These are starting limits, not device claims; measure them against `docs/baseline.md`.

## Persistence skeleton

`src/cache/journal.h/.cpp` now provides a versioned binary journal with transaction completion markers and CRC32 validation. Recovery accepts only complete committed transactions and safely ignores an incomplete tail. Snapshots are validated before replacement and corrupted snapshots are rejected. `tests/journal_test.cpp` covers these cases.

This is not yet the final platform durability layer. Before making it authoritative, add platform-specific durable flush/atomic-replace adapters, immutable generation publication, journal compaction, and bounded cold-page caching. The existing JSON store remains the migration/bootstrap path until those tests and integration work pass.

## Generations, media manifest and policies (2026-11-03)

- `generation.h/.cpp`: `MetadataStore` builds generation N+1 from a copy of N, journals it as one transaction, then publishes it with a pointer swap. Readers hold a `shared_ptr` to a generation and never block the writer. `compact()` writes a snapshot carrying the generation number, then drops the journal; recovery skips journal records at or below that generation, so a crash between the two steps is safe. A corrupt snapshot falls back to journal-only recovery.
- `batch.h`: Subsonic `Song`/`Album`/playlist responses to `UpdateBatch`.
- `lru.h`: `PageCache`, byte- and page-bounded LRU for cold pages; loads run outside the lock.
- `media.h/.cpp`: audio/artwork manifest (`cache.manifest`), content-addressed files, refcount dedup, quality picker (`key@quality`), pinned (offline) content, LRU eviction to a byte budget, orphan/missing-file reconcile. Damaged media drops only its own entries.
- `swr.h`: Fresh/Stale/Missing classification, `kDiscoverTtlSeconds` = 5 min.
- `MetadataSnapshot`: upserts are O(1); `finalize()` merges new search entries into the sorted prefix index (full rebuild only when a searchable name changed). Until finalized, `search_songs` scans, so readers never mutate. Also `liked_songs()`, `album_songs()`.

## Wired into the app (2026-11-03, second pass)

- `Store` now owns a `MetadataStore` (`<data>.json.meta/`): album/artist crawls, the starred list and playlists are indexed through journaled update batches; `local_search`, `song`, `liked`, `liked_songs`, `playlist_contains` read the published generation. The old `indexed_songs_` map of full `Song` copies is gone (songs are rebuilt from packed records, including cover art and technical fields). A shrinking album refresh or a download removal marks the index stale; it is rebuilt from the cached albums on the next query. First run on an old store builds it the same way.
- Likes: every heart (player bar, Now Playing, Car Mode) goes through `App::set_current_liked` (server star + index). The Liked view falls back to the indexed likes when the server is unreachable. Playlist copies never overwrite a known song (so likes are not reset).
- Playlists: `pl_save` mirrors membership into the index; the Add-to-playlist dialog asks the index.
- Artwork: `ImageCache` stores covers through `MediaCache` (verified atomic writes, 64 MiB LRU budget, manifest, orphan reconcile). Legacy `.img` files are adopted on first use.
- Downloads: finished files are streamed into `MediaCache` (`put_file`, pinned, key `song:<id>@<quality>`): refcount dedup, release on removal, manifest saved on completion. Rows downloaded before the manifest existed keep the old path and removal logic.
- Persisted search index: `compact()` writes `metadata.index` (sorted prefix entries) next to the snapshot; `open()` imports it only when the generation matches and nothing newer is in the journal, and rejects it on any inconsistency (then rebuilds).
- Durability: snapshots and the manifest are fsynced (file, then directory after rename). `MetadataStore::apply` fsyncs its journal by default (one batch = one response page). `Store` turns that off and group-commits: `sync()` with every 64th JSON write and `compact()` at exit.
- `MetadataStore::apply` updates the live generation in place when no reader holds it, otherwise builds N+1 from a copy, so a held generation never changes and a large crawl does not copy the library per album.

Not done: media quality picker is only used inside `MediaCache` (downloads keep one quality per song); no Rockbox/minimal adapter; no Android build run.

## Measured (2026-11-03, i5-8265U, Release, `build-linux/cache_bench`, `build-linux/store_bench`)

| Workload | Before | After |
|---|---|---|
| `MetadataSnapshot` load, 2000 songs | 3140 ms | 1.8 ms |
| `MetadataSnapshot` load, 100k songs | did not finish in 120 s (20k) | ~180 ms + 120 ms `finalize()` |
| `cache_crc32` 8 MiB | 99.5 ms | 3.7 ms |
| Store crawl, 300 albums x 12 songs (put + index) | 1768 ms | 37 ms |
| Store search x100 | 485 ms (index dirty) | 14 ms |
| Store reopen | 8.0 ms | 1.4 ms |
| 100k songs, RSS delta | n/a | ~63-75 MB (630-750 B/song) |

App playing 20 s against the local fake server (`--autoplay`): 0.25 s user + 0.20 s sys including startup; idle is the same within noise. No app-code hotspot remains in the sampling profile; the top symbol is dynamic-linker symbol lookup at startup. Recordings: `~/claude-memory-bank/perf-data/xylonic-fiber-2026-11-03/`.

### Second pass numbers (same machine, `build-linux/cache_bench 100000`, NVMe)

| Workload | Value |
|---|---|
| load 100k songs (StringTable open-addressing, was 194 ms) | 130 ms |
| 100k id lookups (was 24 ms) | 12 ms |
| apply 100k-song batch: journal + fsync + publish | 303 ms |
| compact: snapshot + persisted index | 143 ms |
| cold open with persisted index / index rebuilt | 172 ms / 289 ms |
| small batch, fsync each / group commit | 1.76 ms / 0.004 ms |

App memory, whole process tree is one process, `smaps_rollup`, 1024x768, Release, local fake server (1000 songs), 30 s window after 10 s settle, fresh profile directory unless noted. Method as in `docs/baseline.md`.

| Build | Idle PSS | Playing PSS | Idle RSS |
|---|---|---|---|
| HEAD before this work (ad8323b) | 48.4 MB | 42.1 MB (warm) | 100.8 MB |
| This work | 48.8 MB | 43.2 MB (warm) | 101.2 MB |
| This work, warm profile, idle | 37.8-38.5 MB | | 90 MB |
| SDL2 floor (baseline.md) | 32.8 MB | | 85 MB |
| Old Electron app (baseline.md) | 295-300 MB | 329-338 MB | 677-691 MB |

Differences between HEAD and this work are inside run-to-run spread (fresh vs warm profile alone moves it by about 10 MB), so the integration neither costs nor saves resident memory at 1000 songs. Largest PSS owners at idle: heap 15 MB, Mesa/LLVM 11 MB (software GL driver, part of the floor), gallium 3 MB, the binary 2 MB. `MALLOC_ARENA_MAX`/trim settings were tried and showed no repeatable gain.
