#ifndef WH_UI_H
#define WH_UI_H

#include <SDL2/SDL.h>
#include "skin.h"
#include "audio.h"
#include "playlist.h"
#include "filebrowser.h"

typedef enum { VIZ_WAVE = 0, VIZ_SPECTRUM = 1 } VizMode;

typedef struct {
    SDL_Renderer* ren;
    Skin* skin;

    int pressed_btn;
    bool dragging_pos;
    bool dragging_vol;

    char display_title[256];

    VizMode viz_mode;
    FileBrowser fb;

    // smoothed spectrum bars (decays toward target so motion is fluid)
    float spectrum_bars[64];
    int spectrum_bar_count;

    // playlist panel state
    int pl_scroll;
    int pl_hover;
} UI;

typedef struct {
    bool quit_requested;
    bool minimize_requested;
} UiAction;

void ui_init(UI* ui, SDL_Renderer* ren, Skin* skin);
void ui_destroy(UI* ui);

void ui_set_title(UI* ui, const char* title);

UiAction ui_handle_event(UI* ui, const SDL_Event* e, Audio* audio, Playlist* pl);
void ui_render(UI* ui, Audio* audio, Playlist* pl);

bool ui_take_picked_file(UI* ui, char* out, int out_size);

#endif
