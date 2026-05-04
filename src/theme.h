#ifndef WH_THEME_H
#define WH_THEME_H

#include "skin.h"

typedef struct {
    const char* name;
    SDL_Color bg, panel, accent, text;
} Theme;

int theme_count(void);
const Theme* theme_at(int i);

// Overwrites skin's color fields and re-applies accent to the display elements.
void theme_apply(Skin* skin, int idx);

#endif
