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

    int last_pl_index = playlist_index(&pl);
    bool drop_active = false;

    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = 0; break;
                case SDL_DROPBEGIN:
                    playlist_clear(&pl);
                    drop_active = true;
                    break;
                case SDL_DROPFILE: {
                    char* path = e.drop.file;
                    if (!drop_active) playlist_clear(&pl);
                    playlist_add(&pl, path);
                    SDL_free(path);
                    if (!drop_active) load_current(audio, &pl, true);
                    break;
                }
                case SDL_DROPCOMPLETE:
                    drop_active = false;
                    if (playlist_count(&pl) > 0) load_current(audio, &pl, true);
                    break;
                default: {
                    UiAction act = ui_handle_event(&ui, &e, audio, &pl);
                    if (act.quit_requested) running = 0;
                    if (act.minimize_requested) SDL_MinimizeWindow(win);
                    break;
                }
            }
        }

        // File browser may have produced a selection.
        char picked[1024];
        if (ui_take_picked_file(&ui, picked, sizeof(picked))) {
            playlist_clear(&pl);
            playlist_add(&pl, picked);
            load_current(audio, &pl, true);
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
