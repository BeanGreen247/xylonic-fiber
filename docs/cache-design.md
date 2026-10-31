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
