#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "skin.h"
#include "ui.h"
#include "playlist.h"

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
    if (ui->fb.open) return SDL_HITTEST_NORMAL;
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
    SDL_SetWindowSize(win, skin.window_w, skin.window_h);

    UI ui;
    ui_init(&ui, ren, &skin);

    SDL_SetWindowHitTest(win, hittest_cb, &ui);

    Audio* audio = audio_create();
    if (!audio) {
        fprintf(stderr, "audio init failed\n");
        return 1;
    }

    load_current(audio, &pl, true);
    update_titles(win, &ui, &pl);

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    // Window-size juggling for the file browser: FB needs more room than the
    // compact main player. Save the player's dimensions at startup, swap to a
    // larger size while the browser is open, restore on close.
    const int FB_WIN_W = 520, FB_WIN_H = 380;
    int orig_skin_w = skin.window_w;
    int orig_skin_h = skin.window_h;
    bool fb_was_open = false;

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

        if (ui.fb.open != fb_was_open) {
            if (ui.fb.open) {
                SDL_SetWindowSize(win, FB_WIN_W, FB_WIN_H);
                SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                skin.window_w = FB_WIN_W;
                skin.window_h = FB_WIN_H;
            } else {
                SDL_SetWindowSize(win, orig_skin_w, orig_skin_h);
                SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                skin.window_w = orig_skin_w;
                skin.window_h = orig_skin_h;
            }
            fb_was_open = ui.fb.open;
        }

        ui_render(&ui, audio, &pl);
        SDL_Delay(16);
    }

    audio_destroy(audio);
    ui_destroy(&ui);
    skin_destroy(&skin);
    playlist_free(&pl);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
