#ifndef TIMP_FONT_H
#define TIMP_FONT_H

#include <SDL2/SDL.h>

#define FONT_W 5
#define FONT_H 7

void font_draw(SDL_Renderer* ren, int x, int y, int scale,
               SDL_Color color, const char* text);
int font_text_width(int scale, const char* text);

#endif
