#include "theme.h"

static const Theme THEMES[] = {
    { "Classic Green",  {14,18,24,255},  {28,36,46,255},  {80,255,130,255},  {200,220,230,255} },
    { "Winamp Amber",   {18,12,8,255},   {38,28,18,255},  {255,180,40,255},  {220,200,180,255} },
    { "Ice",            {10,16,24,255},  {26,40,56,255},  {120,200,255,255}, {200,220,240,255} },
    { "Crimson",        {18,8,12,255},   {44,18,26,255},  {255,90,120,255},  {220,200,210,255} },
    { "Monochrome",     {16,16,18,255},  {36,36,40,255},  {220,220,220,255}, {200,200,200,255} },
    { "Sunset",         {28,12,24,255},  {52,26,40,255},  {255,140,90,255},  {230,210,200,255} },
    { "Mint",           {12,22,18,255},  {28,46,40,255},  {120,255,200,255}, {200,235,220,255} },
    { "High Contrast",  {0,0,0,255},     {32,32,32,255},  {255,255,255,255}, {220,220,220,255} },
};

int theme_count(void) { return (int)(sizeof(THEMES) / sizeof(THEMES[0])); }

const Theme* theme_at(int i) {
    if (i < 0) i = 0;
    if (i >= theme_count()) i = theme_count() - 1;
    return &THEMES[i];
}

void theme_apply(Skin* skin, int idx) {
    const Theme* t = theme_at(idx);
    skin->theme_bg     = t->bg;
    skin->theme_panel  = t->panel;
    skin->theme_accent = t->accent;
    skin->theme_text   = t->text;

    // Display elements that mirror the accent color
    skin->title.color      = t->accent;
    skin->time_disp.color  = t->accent;
    skin->viz.color        = t->accent;
    skin->pos_slider.color = t->accent;
    skin->vol_slider.color = t->accent;
    if (skin->playlist_rect.defined) skin->playlist_rect.color = t->accent;
}
