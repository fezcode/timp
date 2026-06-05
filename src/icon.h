#ifndef TIMP_ICON_H
#define TIMP_ICON_H

#include <stdint.h>

// Render the Timp "T-play" app icon into a tightly-packed size*size buffer of
// RGBA8888 pixels (byte order R,G,B,A; straight/unpremultiplied alpha).
// This function is SDL-free so standalone tools can reuse it.
//   accent (ar,ag,ab) — color of the mark (the T + play triangle)
//   tile   (tr,tg,tb) — color of the rounded background tile
//   tile_bg           — nonzero: draw the rounded tile; zero: transparent background
void icon_render_rgba(uint8_t *rgba, int size,
                      uint8_t ar, uint8_t ag, uint8_t ab,
                      uint8_t tr, uint8_t tg, uint8_t tb,
                      int tile_bg);

#ifdef ICON_WITH_SDL
#include <SDL.h>
// Build an SDL RGBA surface containing the icon, themed to accent/tile.
// Returns NULL on allocation failure. Caller owns the surface and must
// SDL_FreeSurface it (after SDL_SetWindowIcon copies it).
SDL_Surface *icon_make_surface(int size, SDL_Color accent, SDL_Color tile);
#endif

#endif
