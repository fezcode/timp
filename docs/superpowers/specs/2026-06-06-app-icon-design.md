# Timp App Icon — Design Spec

**Date:** 2026-06-06
**Status:** Approved (design), pending implementation plan

## Problem

Timp ships with no application icon. `SDL_CreateWindow("Timp", ...)` in `src/main.c`
has no matching `SDL_SetWindowIcon`, and there is no Windows resource icon, so the
title bar, taskbar, Alt-Tab switcher, and Explorer all show the generic default SDL/OS
icon. The app should have a custom mark that identifies it.

## Goal

A single custom icon — the **"T-play" mark** — applied in two places:

1. **Runtime** — window / taskbar / Alt-Tab via `SDL_SetWindowIcon`, themed to the
   active skin (cross-platform).
2. **Embedded** — a multi-resolution `.ico` compiled into `timp.exe` via a Windows
   `.rc` resource, so Explorer and a pinned taskbar entry show it too.

The artwork is **drawn procedurally in C**, matching the rest of the app (every glyph
in `ui.c` is code, not an asset). No hand-authored image file is the source of truth.

## The mark

A unified glyph that reads as both the letter **T** and a **play** button: a full-width
horizontal crossbar (top of the T) with a right-pointing play triangle hanging from its
center as the stem. Accent-green fill on a rounded dark tile.

```
   ░░░░░░░░░░░░░░
   ░████████████░     crossbar (top of the T)
   ░░░░░░██░░░░░░
   ░░░░░░███░░░░░
   ░░░░░░████░░░░     right-pointing play triangle
   ░░░░░░█████░░░       = the T's stem
   ░░░░░░██████░░
   ░░░░░░█████░░░
   ░░░░░░████░░░░
   ░░░░░░███░░░░░
   ░░░░░░██░░░░░░
   ░░░░░░░░░░░░░░
```

The triangle's flat back edge is centered under the crossbar so it reads as a stem that
swells into a play arrow. The mark is drawn at high resolution and downsampled (4×
supersample) so the diagonal edge and rounded tile corners are smooth.

### Style decisions (locked)

- **Tile:** rounded dark tile in the skin's `panel` color (default `28,36,46`), so the
  mark stays visible on any taskbar background. Outside the rounded corners is transparent.
- **Mark color:** the skin's `accent` (default `80,255,130`).
- **Embedded `.ico`:** uses the **default** skin's colors (green-on-slate), static —
  a file icon cannot follow runtime theme changes.

## Components

### `src/icon.h` / `src/icon.c` — artwork, single source of truth

- `void icon_render_rgba(uint8_t *rgba, int size, uint8_t ar, uint8_t ag, uint8_t ab,
  uint8_t tr, uint8_t tg, uint8_t tb, int tile_bg);`
  - SDL-free core. Renders the rounded tile + T-play mark into a tightly-packed
    `size * size * 4` RGBA8888 buffer (straight alpha). Uses 4× supersampling for
    anti-aliasing. `tile_bg` toggles the rounded tile vs. transparent background.
  - SDL-free so the standalone generator can reuse it without linking SDL.
- `SDL_Surface *icon_make_surface(int size, SDL_Color accent, SDL_Color tile);`
  - Thin SDL wrapper: allocates an RGBA surface, calls `icon_render_rgba`, returns it.
    Caller owns the surface and frees it after `SDL_SetWindowIcon`.

### `tools/makeicon.c` — committed `.ico` generator

- Standalone `main()` that includes/links the same `icon_render_rgba` and writes
  `assets/timp.ico` containing 16, 32, 48, and 256 px frames.
- `.ico` encoding (dependency-free, all frames as 32-bit BGRA DIB):
  - One `ICONDIR` header, then one `ICONDIRENTRY` per frame (with byte offset/size).
  - Each frame: a `BITMAPINFOHEADER` with `biHeight = 2 * size` (the ICO convention:
    height counts the XOR color rows plus the AND mask rows), `biBitCount = 32`,
    followed by `size * size` BGRA pixels bottom-up, then a zeroed AND-mask bitfield
    (alpha already lives in the BGRA data, so the 1-bpp mask is all zero, padded to a
    4-byte row stride).
  - Storing the 256 px frame as BGRA DIB too (~256 KB) keeps the generator free of any
    PNG encoder. Total `.ico` is well under 1 MB.
- Built/run manually (not part of the normal build); the resulting `assets/timp.ico`
  is committed so contributors don't need to regenerate it.

### `src/app.rc` — Windows resource

```
1 ICON "assets/timp.ico"
```

Resource ID `1` (numeric) so the OS uses it as the executable's default icon.

### `assets/timp.ico`

Committed multi-resolution icon produced by `tools/makeicon`.

## Wiring

### `src/main.c`

After `SDL_CreateWindow` (and after the skin is loaded, so theme colors are known):

```c
SDL_Surface *ico = icon_make_surface(64, skin.theme_accent, skin.theme_panel);
if (ico) { SDL_SetWindowIcon(win, ico); SDL_FreeSurface(ico); }
```

(`Skin` exposes `theme_accent` and `theme_panel` as `SDL_Color` — `skin.h:113-116`.)

**Bonus (in scope, low cost):** when the skin is hot-swapped via the Settings → SKINS
tab, rebuild and re-set the window icon so it follows the new theme. If this turns out
to require threading state awkwardly, it can drop without affecting the core goal.

### `build.ps1` (Windows)

- Add `icon` to the `$srcs` list.
- Before linking, compile the resource:
  `& windres src\app.rc -o build\app_rc.o` (skip gracefully if `windres` is absent —
  the runtime icon still works without it).
- Append `build\app_rc.o` to the link object list.

### `Makefile`

- Add `src/icon.c` to `SRCS`.
- Under `ifeq ($(OS),Windows_NT)`: add a rule to build `$(BUILD)/app_rc.o` from
  `src/app.rc` with `windres`, and append it to the objects used by the link rule.
- Non-Windows builds get the runtime icon only (no `.rc`), which is correct.

## Out of scope (YAGNI)

- No new `config.ini` keys.
- No per-skin `.ico` variants.
- No animated or multi-state icon.
- No icon for the in-app modal windows (they're borderless child surfaces).

## Testing / verification

- **Build:** `.\build.ps1` compiles `icon.c`, runs `windres`, links cleanly; `make`
  builds on non-Windows without the `.rc`.
- **Runtime:** launch `build\timp.exe`; the taskbar / Alt-Tab entry shows the green
  T-play mark. Switch skins in Settings → SKINS and confirm the icon recolors (if the
  hot-swap bonus is implemented).
- **Embedded:** in Explorer, `timp.exe` shows the T-play icon at small and large view
  sizes; pinning to the taskbar keeps it.
- **Visual sanity:** the mark reads as a "T" and a play button at 16 px and is smooth
  (no jaggies) at 48/256 px.

## Files touched

| File | Change |
| --- | --- |
| `src/icon.h` | new — API |
| `src/icon.c` | new — procedural renderer |
| `tools/makeicon.c` | new — `.ico` generator |
| `assets/timp.ico` | new — committed generated icon |
| `src/app.rc` | new — Windows resource |
| `src/main.c` | set window icon after window creation |
| `build.ps1` | compile `icon.c` + `windres` step + link `app_rc.o` |
| `Makefile` | add `icon.c`; Windows `windres` rule + link |
| `README.md` | (optional) note the icon under Features/Project layout |
