#ifndef TIMP_SKIN_H
#define TIMP_SKIN_H

#include <stdbool.h>
#include <SDL2/SDL.h>

typedef enum {
    BTN_PREV = 0,
    BTN_PLAY,
    BTN_PAUSE,
    BTN_STOP,
    BTN_NEXT,
    BTN_OPEN,
    BTN_EQ,
    BTN_SHUFFLE,
    BTN_LOOP,
    BTN_SETTINGS,
    BTN_MIN,
    BTN_CLOSE,
    BTN_COUNT
} ButtonId;

typedef struct {
    SDL_Rect hit;
    SDL_Rect normal;
    SDL_Rect pressed;
    bool defined;
} SkinButton;

typedef struct {
    SDL_Rect rect;
    SDL_Color color;
    bool defined;
} SkinElement;

typedef struct {
    char name[64];
    int window_w, window_h;

    SDL_Texture* bg_tex;
    int bg_w, bg_h;
    char dir[260];

    SkinButton buttons[BTN_COUNT];

    SkinElement title;
    SkinElement time_disp;
    SkinElement viz;
    SkinElement pos_slider;
    SkinElement vol_slider;
    SkinElement drag_region;
    SkinElement playlist_rect;

    SDL_Color theme_bg;
    SDL_Color theme_panel;
    SDL_Color theme_accent;
    SDL_Color theme_text;
} Skin;

bool skin_load(Skin* skin, SDL_Renderer* ren, const char* ini_path);
void skin_default(Skin* skin);
void skin_destroy(Skin* skin);

int skin_button_at(const Skin* skin, int x, int y);

#endif
