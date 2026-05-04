#ifndef WH_UI_H
#define WH_UI_H

#include <SDL2/SDL.h>
#include "skin.h"
#include "audio.h"
#include "playlist.h"
#include "filebrowser.h"
#include "settings.h"

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

    // EQ modal state
    bool eq_open;
    int eq_drag_band;

    // Settings modal state
    Settings settings;

    // Playlist visibility (host resizes window when this flips)
    bool playlist_visible;
} UI;

typedef struct {
    bool quit_requested;
    bool minimize_requested;
    bool aot_changed;          // host should call SDL_SetWindowAlwaysOnTop
    bool playlist_vis_changed; // host should resize window
} UiAction;

void ui_init(UI* ui, SDL_Renderer* ren, Skin* skin);
void ui_destroy(UI* ui);

void ui_set_title(UI* ui, const char* title);

UiAction ui_handle_event(UI* ui, const SDL_Event* e, Audio* audio, Playlist* pl);
void ui_render(UI* ui, Audio* audio, Playlist* pl);

int  ui_picks_count(UI* ui);
const char* ui_pick_path(UI* ui, int i);
void ui_clear_picks(UI* ui);

#endif
