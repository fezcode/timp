#ifndef TIMP_UI_H
#define TIMP_UI_H

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
    int hover_btn;
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
    int pl_drag_idx;       // index being dragged, -1 if none
    int pl_drag_target;    // current target slot during drag
    int pl_press_idx;      // row pressed on mouse-down (becomes drag if mouse moves)
    int pl_press_x, pl_press_y;

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
    bool settings_changed;     // host should persist config
    bool skin_changed;         // host should hot-swap to ui->settings.selected_skin_path
} UiAction;

void ui_init(UI* ui, SDL_Renderer* ren, Skin* skin);
void ui_destroy(UI* ui);

void ui_set_title(UI* ui, const char* title);

UiAction ui_handle_event(UI* ui, const SDL_Event* e, Audio* audio, Playlist* pl);
void ui_render(UI* ui, Audio* audio, Playlist* pl);

int  ui_picks_count(UI* ui);
const char* ui_pick_path(UI* ui, int i);
void ui_clear_picks(UI* ui);

typedef enum {
    MEDIA_PLAY_PAUSE = 0,
    MEDIA_STOP       = 1,
    MEDIA_PREV       = 2,
    MEDIA_NEXT       = 3,
} MediaAction;

// Single dispatch point for transport actions - used by SDL keys, the on-screen
// buttons, and (on Windows) the system-wide media hotkeys.
void ui_media(UI* ui, Audio* audio, Playlist* pl, MediaAction a);

#endif
