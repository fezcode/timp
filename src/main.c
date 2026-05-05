#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "skin.h"
#include "ui.h"
#include "playlist.h"
#include "config.h"
#include "theme.h"

static const char* basename_only(const char* path) {
    const char* s = strrchr(path, '/');
    const char* s2 = strrchr(path, '\\');
    if (s2 > s) s = s2;
    return s ? s + 1 : path;
}

static void update_titles(SDL_Window* win, UI* ui, const Playlist* pl) {
    const char* path = playlist_current(pl);
    if (!path) {
        SDL_SetWindowTitle(win, "WHamp");
        ui_set_title(ui, "WHAMP - DROP A FILE");
        return;
    }
    const char* name = basename_only(path);
    char wt[320];
    snprintf(wt, sizeof(wt), "WHamp - %s", name);
    SDL_SetWindowTitle(win, wt);

    char shown[256];
    int total = playlist_count(pl);
    int idx = playlist_index(pl) + 1;
    if (total > 1)
        snprintf(shown, sizeof(shown), "[%d/%d] %s", idx, total, name);
    else
        snprintf(shown, sizeof(shown), "%s", name);
    ui_set_title(ui, shown);
}

static void load_current(Audio* audio, const Playlist* pl, bool start_playing) {
    const char* path = playlist_current(pl);
    if (!path) return;
    if (audio_load(audio, path) && start_playing) audio_play(audio);
}

static SDL_HitTestResult SDLCALL hittest_cb(SDL_Window* win, const SDL_Point* area, void* data) {
    (void)win;
    UI* ui = (UI*)data;
    // Modal title bars: drag from the header band (above any tabs / list / buttons).
    if (ui->fb.open) {
        if (area->y < 22) return SDL_HITTEST_DRAGGABLE;
        return SDL_HITTEST_NORMAL;
    }
    if (ui->settings.open) {
        if (area->y < 16) return SDL_HITTEST_DRAGGABLE;
        return SDL_HITTEST_NORMAL;
    }
    if (skin_button_at(ui->skin, area->x, area->y) >= 0) return SDL_HITTEST_NORMAL;
    SDL_Rect r = ui->skin->drag_region.rect;
    if (ui->skin->drag_region.defined &&
        area->x >= r.x && area->x < r.x + r.w &&
        area->y >= r.y && area->y < r.y + r.h) {
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

int main(int argc, char** argv) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    Skin skin;
    const char* skin_arg = NULL;
    Playlist pl;
    playlist_init(&pl);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--skin") == 0 && i + 1 < argc) {
            skin_arg = argv[++i];
        } else {
            playlist_add(&pl, argv[i]);
        }
    }

    const char* default_skin = "skins/default/skin.ini";
    const char* skin_path = skin_arg ? skin_arg : default_skin;

    SDL_Window* win = SDL_CreateWindow("WHamp",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        380, 110,
        SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    if (!skin_load(&skin, ren, skin_path)) {
        fprintf(stderr, "no skin at %s, using built-in\n", skin_path);
        skin_default(&skin);
    } else {
        printf("loaded skin: %s\n", skin.name);
    }

    WhConfig cfg;
    config_load(&cfg);
    if (cfg.current_theme > 0 && cfg.current_theme < theme_count()) {
        theme_apply(&skin, cfg.current_theme);
    }
    SDL_SetWindowSize(win, skin.window_w, skin.window_h);

    UI ui;
    ui_init(&ui, ren, &skin);
    ui.settings.always_on_top    = cfg.always_on_top;
    ui.settings.playlist_visible = cfg.playlist_visible;
    ui.settings.current_theme    = cfg.current_theme;
    ui.playlist_visible          = cfg.playlist_visible;

    SDL_SetWindowHitTest(win, hittest_cb, &ui);

    Audio* audio = audio_create();
    if (!audio) {
        fprintf(stderr, "audio init failed\n");
        return 1;
    }

    load_current(audio, &pl, true);
    update_titles(win, &ui, &pl);

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    // Window-size juggling. Three states:
    //   modal (file browser or settings open) -> larger window
    //   playlist visible                       -> compact + playlist
    //   playlist hidden                        -> compact only
    const int MODAL_WIN_W = 520, MODAL_WIN_H = 380;
    const int PLAYER_NO_PL_H = 88;  // y where the playlist panel starts in default skin
    int orig_skin_w = skin.window_w;
    int orig_skin_h = skin.window_h;  // full height with playlist
    SDL_Rect orig_btn_min   = skin.buttons[BTN_MIN].hit;
    SDL_Rect orig_btn_close = skin.buttons[BTN_CLOSE].hit;
    bool was_modal = false;
    bool was_pl_visible = ui.playlist_visible;
    bool always_on_top = cfg.always_on_top;
    if (always_on_top) SDL_SetWindowAlwaysOnTop(win, SDL_TRUE);

    int last_pl_index = playlist_index(&pl);
    bool drop_active = false;
    bool drop_was_idle = false;       // was nothing loaded/playing when this drop started?
    int  drop_first_new_index = 0;    // first slot the dropped files landed in

    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = 0; break;
                case SDL_DROPBEGIN:
                    drop_active = true;
                    drop_was_idle = !audio_is_loaded(audio) || !audio_is_playing(audio);
                    drop_first_new_index = playlist_count(&pl);
                    break;
                case SDL_DROPFILE: {
                    char* path = e.drop.file;
                    if (!drop_active) {
                        // unbracketed single drop: start now if idle, otherwise just append
                        bool was_idle = !audio_is_loaded(audio) || !audio_is_playing(audio);
                        int first_new = playlist_count(&pl);
                        playlist_add(&pl, path);
                        if (was_idle) {
                            playlist_set_index(&pl, first_new);
                            load_current(audio, &pl, true);
                        }
                    } else {
                        playlist_add(&pl, path);
                    }
                    SDL_free(path);
                    break;
                }
                case SDL_DROPCOMPLETE:
                    if (drop_active && drop_was_idle &&
                        playlist_count(&pl) > drop_first_new_index) {
                        playlist_set_index(&pl, drop_first_new_index);
                        load_current(audio, &pl, true);
                    }
                    drop_active = false;
                    break;
                default: {
                    UiAction act = ui_handle_event(&ui, &e, audio, &pl);
                    if (act.quit_requested) running = 0;
                    if (act.minimize_requested) SDL_MinimizeWindow(win);
                    if (act.aot_changed) {
                        always_on_top = ui.settings.always_on_top;
                        SDL_SetWindowAlwaysOnTop(win, always_on_top ? SDL_TRUE : SDL_FALSE);
                    }
                    if (act.settings_changed) {
                        cfg.always_on_top    = ui.settings.always_on_top;
                        cfg.playlist_visible = ui.settings.playlist_visible;
                        cfg.current_theme    = ui.settings.current_theme;
                        config_save(&cfg);
                    }
                    break;
                }
            }
        }

        // File browser may have produced one or more selections — append to playlist;
        // start playing the first new one if nothing was playing.
        int n_picks = ui_picks_count(&ui);
        if (n_picks > 0) {
            bool was_idle = !audio_is_loaded(audio) || !audio_is_playing(audio);
            int first_new = playlist_count(&pl);
            for (int i = 0; i < n_picks; i++) {
                playlist_add(&pl, ui_pick_path(&ui, i));
            }
            ui_clear_picks(&ui);
            if (was_idle && playlist_count(&pl) > first_new) {
                playlist_set_index(&pl, first_new);
                load_current(audio, &pl, true);
            }
        }

        if (audio_finished(audio)) {
            if (playlist_has_next(&pl)) {
                playlist_next(&pl);
                load_current(audio, &pl, true);
            } else {
                audio_stop(audio);
            }
        }

        if (playlist_index(&pl) != last_pl_index) {
            last_pl_index = playlist_index(&pl);
            update_titles(win, &ui, &pl);
        }

        bool modal = ui.fb.open || ui.settings.open;
        // Compute the expected window size for the current state.
        int want_w, want_h;
        if (modal) {
            want_w = MODAL_WIN_W;
            want_h = MODAL_WIN_H;
        } else {
            want_w = orig_skin_w;
            want_h = ui.playlist_visible ? orig_skin_h : PLAYER_NO_PL_H;
        }

        if (modal != was_modal) {
            // State change: anchor title-bar buttons to the new layout, recenter,
            // and update the skin's logical size used by renderers.
            if (modal) {
                skin.buttons[BTN_CLOSE].hit = (SDL_Rect){ MODAL_WIN_W - 16, 2, 14, 10 };
                skin.buttons[BTN_MIN].hit   = (SDL_Rect){ MODAL_WIN_W - 32, 2, 14, 10 };
            } else {
                skin.buttons[BTN_MIN].hit   = orig_btn_min;
                skin.buttons[BTN_CLOSE].hit = orig_btn_close;
            }
            SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            was_modal = modal;
        }

        // Defensively re-assert the window size every frame. SDL_SetWindowSize on
        // borderless Windows occasionally drops a shrink request, leaving the player
        // UI floating in a too-large window — querying the actual size and re-issuing
        // the request when it disagrees keeps state coherent.
        int actual_w = 0, actual_h = 0;
        SDL_GetWindowSize(win, &actual_w, &actual_h);
        if (actual_w != want_w || actual_h != want_h) {
            SDL_SetWindowSize(win, want_w, want_h);
        }
        skin.window_w = want_w;
        skin.window_h = want_h;
        was_pl_visible = ui.playlist_visible;

        ui_render(&ui, audio, &pl);
        SDL_Delay(16);
    }

    cfg.always_on_top    = ui.settings.always_on_top;
    cfg.playlist_visible = ui.settings.playlist_visible;
    cfg.current_theme    = ui.settings.current_theme;
    config_save(&cfg);

    audio_destroy(audio);
    ui_destroy(&ui);
    skin_destroy(&skin);
    playlist_free(&pl);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
