# Hisashi menubar (hoswl) — Design

**Date:** 2026-08-30
**Status:** Implemented and verified (v0.11.0)
**Reference:** pidi's `src/menubar.{h,c}` (same contract), tivi's port

## Problem

Timp is borderless and has no menu bar. Hisashi renders a macOS-style global
menu bar for the foreground app over the **hoswl** named-pipe protocol; when it
is running, Timp should get File / Playback / Audio / View / Help menus in the
bar — the same actions the keys and buttons run, with live checkmarks.

## Behavior

- `src/menubar.c` (contract `src/menubar.h`) rebuilds the tree as hoswl's line
  DSL from a `MenubarState` snapshot `rl_main.c` fills once per frame, and
  republishes only when its fingerprint changes. Clicks come back through
  `hoswl_poll()` and run `menu_click()` on the main thread, next to the media-
  key and single-instance polls.
- `src/hoswl.h` is a verbatim copy of `Hisashi/sdk/hoswl/hoswl.h`.
- Menus: **File** (Open Files… `O`, Save Playlist — enabled when the queue is
  dirty and opens the drawer's name prompt / overwrite confirm, Saved
  Playlists…, Clear Playlist, Open Data Folder, Quit), **Playback**
  (Play/Pause `Space`, Stop, Previous, Next, Back/Forward 5 s `←`/`→`,
  Shuffle `S`, Repeat › Off/One/All), **Audio** (Volume Up/Down, Mute,
  Equalizer on/off, Flatten Equalizer, Equalizer Panel `E`), **View**
  (Visualizer › Album Art / Spectrum Bars / Waveform, Playlist Drawer `Q`,
  Lyrics `Y`, Settings `G`, Always on Top `T`, Drawer Side › Right/Left,
  Previous Button › Smart/Direct), **Help** (About Timp… → Settings).
- `g_aot` and the data-dir path were `main` locals; they are file-scope now so
  the click handler can reach them. Quit sets a new `g_quit` flag that leaves
  the main loop the same way the × button does.
- **Toggle** — Settings row "Hisashi menubar" (row 7; rows are 31 px so they
  clear the DATA FOLDER block); persisted as `hisashi_menubar` in config.ini;
  default **on**.

## Tests

`tools/menubar_test.c` (hand-rolled `ok:`/`FAIL:` harness like
`tools/icon_test.c`) checks disabled rows, checkmarks, radio groups and that
the DSL compiles through `hoswl_compile_menu_text`. Build line in the header.
