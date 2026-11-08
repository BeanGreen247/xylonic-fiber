# UI audit against the original Electron source

Standing rule (owner, 2026-11-03): always check the original (`/home/bean/xylonic/src`, CSS in `dist/assets/index-*.css`, runnable with `docs/ui-ref/shot.js`/`cdp.js`) for UI and behaviour that was implemented there but never screenshotted or documented, and port what it does instead of guessing.

Method of this audit: every `.tsx` component was listed and its user-visible strings (text nodes, titles, placeholders, aria labels, notifications) were checked for presence in the native sources; the components behind the gaps were then read and, where shown, captured from the running original at 1024x768 and 390x844.

## Found and ported (2026-11-03, fifth pass)

| Original | What the original does (not documented before) | Native |
|---|---|---|
| DownloadQualityPicker.tsx | 5 options (Original (Raw), 320, 256, 128, 64; no 192) with signal bars and descriptions, header, hint, Cancel + confirm row, bottom sheet on phones; default quality is 320 | `draw_download_picker`, default 320, quality stored per download |
| ArtistList.tsx / ShuffleAllButton.tsx | Shuffle All: first press "Confirm?" (3 s) while a song plays, then "Loading..."; offline it shuffles downloaded songs; error "Could not load songs for shuffle" for 4 s. Download button: hidden when everything is downloaded, "Download Missing" when some are, "Queuing..." while working; only songs not yet downloaded are queued, then the Download Manager opens | `shuffle_all_pressed`, `download_btn_state`, `queue_downloads` |
| SongList.tsx / AlbumList.tsx | Album download button opens the picker with "Download N Songs"; artist page "Download Artist" opens it too | `open_download_picker_songs` (the native Download Artist button had no click handler at all) |
| App.tsx | Missing-songs banner (blue strip, 5 s after connect, only when some but not all songs are downloaded, "Download Missing" with picker, dismiss); "No Internet Connection" prompt when the server becomes unreachable; "Mobile Data Detected" prompt when auto-offline switches on mobile data; offline mode leaves Home (Discover needs the server) | `draw_missing_banner`, `draw_prompt`, `check_connectivity`, `toggle_offline` |
| Header.tsx | Offline toggle shows a plane and "Offline" when active; Gaming/Eco mode badges; Mini disabled until a song is loaded; the Ctrl+M "toast" is unreachable in the original and was not ported | `draw_header` |
| LayoutModeContext / compact CSS | Compact (<=767 px): 56 px header with logo, title and offline toggle only; bottom nav Home / Library / Search / Queue (submenu Queue, History, Playlists) / Settings, Home disabled offline; 60 px mini player (progress line, cover, title, previous, 52 px play, next, tap opens Now Playing); Discover 180x133 cards; Library icon-only toggle, one stat, 48 px actions, 3-column grids; artist and album pages; search page with in-page input; queue panel as a full-width sheet | `shell.cpp`, `library.cpp`, `pages.cpp`, `search.cpp`, `panel.cpp` |
| HamburgerMenu.tsx, ReauthDialog.tsx | Both components exist in the source but are not mounted anywhere. The native hamburger menu (an invention) was removed from the header. The "Sign in again" dialog (reacts to HTTP 401 / Subsonic 40, 41, 44) stays as a native extra; the original does nothing on a rejected login | decision recorded here |

## Still missing (from the string audit, to port next, in this order)

1. Download Manager window (DownloadManagerWindow.tsx, 522 lines): Cache Statistics, Total Cache size across users, Cached Albums list with "Remove from cache", Cache Integrity / Verify Cache, Clear Completed, "Delete all cached songs?", "Now downloading", Cache Location.
2. Library offline and error states: "Error Loading Artists/Albums/Songs/Liked Songs" with "Switch to Online Mode", "No Albums/Songs Found", "Discover requires an online connection", cached badges ("Has cached songs", "Album cached", "Cover art cached"), SongList "No Cached Songs", "This album isn't downloaded for offline playback", "Cached - click to remove".
3. Settings sections not present natively: Danger Zone, Storage Location (app install, permanent cache), Advanced (Open Log Folder, Performance Overlay), Switch Server (saved connections, Enter Password), Library, About (View source on GitHub, Thank you for supporting Xylonic!), "Start Car Mode?" confirmation, Downloads section with Concurrent Downloads wording. Last.fm settings component exists but is not shown in the original Settings.
4. Right panel strings and actions: Queue (Clear queue, Drag to reorder, Remove from queue, New Playlist), History (Clear history, Add to queue), Playlists (Add all to queue, Delete this playlist?, Save current queue as playlist, Sync playlists from server, From queue, "No songs yet", Remove from playlist), panel filter "Filter...".
5. Now Playing technical info (FORMAT, BITRATE, SAMPLE RATE, BIT DEPTH, FILE SIZE: the data is already in the index) and the compact overlay; MiniPlayer "Return to main window (Ctrl+M)".
6. Compact search results layout (best card as a row, 130/140 px cards), compact Settings, Downloads and Now Playing pages.
7. Search bar placeholder "Search artists, albums, songs", clear button; CustomThemeEditor and ThemeSelector wording (Preset Themes, Custom Themes, Reset to default); RemoteDevicePicker messages; FirewallSetupDialog Windows commands; keyboard shortcut list vs useKeyboardShortcuts.ts; splash screen.
8. Between 768 and 900 px the original wraps Remote and Mini onto a second header row; natively they are hidden there.

The heuristic list that produced item 2 to 7 is reproducible with the script in `tools/ui-string-audit.py`.

## Pass: Downloads page + library error states
- Downloads page rewritten to the original DownloadManagerWindow inline layout (src/app/downloads_page.cpp): progress card, Pause/Resume, Retry Failed, Clear Completed, Clear Queue, active cards, Show queue list, Cache Statistics tiles, Manage Cache (location + disk space, Cached Albums with Delete confirm, Verify Cache, Clear All Cache confirm). Content area pixel-diff vs original at 1024x768: 1.35%.
- Library artists: "Error Loading Artists" / "No Cached Artists" / "No Artists Found" states, Retry and "Switch to Online Mode" button (offline error screenshot checked).
- Done in the follow-up: Albums/Songs/Liked empty and offline states (draw_library_state), album page "No Cached Songs"/"No Songs Found", Discover offline message, search "No results found for" state, panel "Filter…" placeholder/History empty strings/filter-no-match state.
- Header 768-899 px: measured on the original at 850 px: no second row, the search shrinks to 160 px and Remote/Mini stay on the single row; native now matches (the second-row claim was wrong).
- Compact: Downloads (wrapping controls, 24 px title), Settings (12 px padding, full width, 24 px title; descriptions still truncate instead of wrapping), search results (stacked best result, 130/140 px cards, @media 680 rules).
- Downloads Manage Cache pixel-diffed against the original at 1024x768 (4%, path text differs); "Change Location" is drawn but only shows a toast (no folder picker).
- Screenshot flake fixed: the frame is captured inside Gfx::end_frame before the present (--dm queue,manage and --scroll N open Downloads sections).
- Still open: Downloads active-download card not visually compared (local downloads finish too fast to capture); Settings compact description wrapping; Storage Location row; offline error states for Albums/Songs/Liked; panel tooltips.
