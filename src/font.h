#ifndef TIMP_FONT_H
#define TIMP_FONT_H

#include <SDL2/SDL.h>

// Logical glyph size in unscaled pixels — used everywhere we lay out text.
// Stays at 5x7 (the built-in bitmap font's metrics) so existing UI math works.
// When a skin overrides the font with a sprite sheet, the sheet's draw_w/h
// is used by font_draw and reflected by font_text_width / FONT_W / FONT_H.
#define FONT_W 5
#define FONT_H 7

struct Skin;

// Bind / unbind the active skin so font_draw can pick up the optional sprite-
// sheet font. NULL falls back to the built-in 5x7 bitmap.
void font_set_skin(const struct Skin* skin);

void font_draw(SDL_Renderer* ren, int x, int y, int scale,
               SDL_Color color, const char* text);
int font_text_width(int scale, const char* text);
int font_glyph_w(int scale);
int font_glyph_h(int scale);

#endif
