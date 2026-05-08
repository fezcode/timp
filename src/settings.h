#ifndef TIMP_SETTINGS_H
#define TIMP_SETTINGS_H

#include <stdbool.h>
#include <SDL2/SDL.h>
#include "skin.h"

#define TIMP_VERSION "0.2.0"
#define TIMP_BUILD_DATE __DATE__

typedef enum {
    SET_TAB_THEMES = 0,
    SET_TAB_SKINS,
    SET_TAB_OPTIONS,
    SET_TAB_ABOUT,
    SET_TAB_COUNT
} SetTab;

#define SET_MAX_SKINS 32

typedef struct {
    bool open;
    SetTab tab;

    int current_theme;
    int hover_theme;
    int theme_scroll;

    // SKINS tab: list rebuilt each time the modal opens.
    SkinEntry skins[SET_MAX_SKINS];
    int skin_count;
    int current_skin;        // index into skins[], -1 if path not found
    char selected_skin_path[300];  // host reads this when skin_changed is set
    int hover_skin;
    int skin_scroll;

    // Mirrored options (host writes on open, settings_handle_event flips them
    // and sets the matching *_changed flag).
    bool always_on_top;
    bool playlist_visible;

    bool theme_changed;
    bool aot_changed;
    bool plv_changed;
    bool skin_changed;
} Settings;

void settings_init(Settings* s);
void settings_show(Settings* s);
void settings_close(Settings* s);

void settings_handle_event(Settings* s, const SDL_Event* e, const Skin* skin);
void settings_render(Settings* s, SDL_Renderer* ren, const Skin* skin);

#endif
