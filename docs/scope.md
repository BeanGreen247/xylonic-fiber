# Scope

Source: old Xylonic 26.10.28 (`/home/bean/xylonic`: README, FEATURES.md, CHANGELOG.md, docs/). Feature scope only; architecture is not carried over. Nothing cut yet. Grouping is a proposal, owner edits.

## Must (v1, Linux desktop first)

- Subsonic/Navidrome API client: token auth (salted MD5), ping, getArtists/getArtist/getAlbum/getAlbumList2 (recent/frequent/random/newest), getStarred2, search3, star/unstar, getSong, getCoverArt, stream/download with maxBitRate, playlists CRUD, scrobble
- HTTPS enforcement for non-LAN servers; credentials in OS keychain (Secret Service on Linux), never plaintext
- Single owned audio pipeline: play/pause/next/prev/seek, volume/mute, speed 0.5x-2x, auto-advance, gapless (preload next before end), streaming quality (original/320/256/192/128/64)
- Queue: view, reorder, remove, clear, play next, add to queue; persisted with position across restarts (no auto-play on restore)
- Shuffle (Fisher-Yates, each track once per cycle), repeat off/all/one (persisted per user), wrap-around
- Library browse: artists, all albums, all songs, artist > album > song, liked songs; paginated or virtualised for 25k+ songs
- Search (real-time, off the UI thread) across artists/albums/songs
- Offline cache: download song/album, quality choice, download manager (pause/resume/retry, disk space), offline-first playback, cache verify, delete/clear, refcounted dedup, per-user isolation, offline mode with stored credentials
- Cover art: server art, size-aware cache, LRU memory + disk cache, offline art
- Persistent metadata cache with stale-while-revalidate (library paints from disk on launch)
- Multiple saved servers, switch server without logout, connection history
- Keyboard shortcuts (space, shift+arrows, arrows seek, S, R, M, Q, H, P, Ctrl+K, Ctrl+M, Ctrl+Shift+Del)
- MPRIS2 (Linux media keys / shell controls)
- Themes: presets + custom slots, persisted per user, contrast-safe
- Resource modes (low-power / balanced / max-performance equivalents)
- Min supported screen 1024x768

## Should

- Playlists: create/rename/delete/reorder, save queue as playlist, add from song row, context menu (play now/next/queue/playlist/download)
- Recently played history (last 50)
- Home/Discover: recently added, recently played, most played, random mix (+ play all)
- Now Playing view: art, audio stats (format/bitrate/sample rate/depth/size), quality and speed selectors, like
- Mini player (compact always-on-top)
- Sleep timer (15/30/45/60)
- Last.fm scrobbling with user API key, per-user
- Missing-songs banner (bulk download uncached), new-content detection on launch
- Download notifications with speed/ETA, taskbar progress
- iTunes artwork fallback
- Server auto-discovery on LAN/Tailscale
- Clear-all-data, cache wipe shortcut
- Help dialog

## Later

- Android: foreground-service downloads, battery exemption, orphan recovery, permanent cache on SD, bottom nav, swipe gestures, car mode (Pulse Deck spectrum, per-song bass/treble EQ, hi-res art, keep-awake, minimal layout)
- LAN remote control (UDP 7766 discovery, TCP 7767 commands, pair/unpair, state sync, firewall helper)
- Windows, macOS, iOS ports
- Planned in old app, never built: crossfade, equalizer presets, ReplayGain, drag library to playlist, M3U/JSON playlist import/export, "more by artist", ListenBrainz, lyrics, visualiser/waveform

## Old-app bugs to design out

Idle RAM ~430MB; WebView/Electron overhead; queue serialised in full on every skip; 40k DOM nodes for a 3.3k queue; cache build marked complete over empty cache; restored-song play spinner hang; cover cache reusing first-loaded size; placeholder artist art cached forever; Pulse Deck lag.
