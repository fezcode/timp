#include "ui.h"
#include "font.h"
#include "fft.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void ui_init(UI* ui, SDL_Renderer* ren, Skin* skin) {
    memset(ui, 0, sizeof(*ui));
    ui->ren = ren;
    ui->skin = skin;
    ui->pressed_btn = -1;
    ui->hover_btn = -1;
    ui->viz_mode = VIZ_WAVE;
    ui->spectrum_bar_count = 32;
    ui->pl_hover = -1;
    ui->pl_drag_idx = -1;
    ui->pl_drag_target = -1;
    ui->pl_press_idx = -1;
    ui->eq_drag_band = -1;
    ui->playlist_visible = true;
    snprintf(ui->display_title, sizeof(ui->display_title), "%s", "TIMP - DROP A FILE");
    fb_init(&ui->fb);
    settings_init(&ui->settings);
}

// ----- EQ panel layout -----
// y=0..14: drag region (shared with main UI)
// y=18..30: header (title + ON/OFF + FLAT + BACK buttons)
// y=34..148: 10 vertical sliders (114px high)
// y=152..160: freq labels
// y=164..174: dB readouts

#define EQ_SLIDER_TOP    34
#define EQ_SLIDER_BOTTOM 148
#define EQ_SLIDER_W       8
#define EQ_TRACK_W        4

static SDL_Rect eq_slider_track_rect(const Skin* sk, int band) {
    int margin = 14;
    int total_w = sk->window_w - 2 * margin;
    int spacing = total_w / EQ_BANDS;
    int cx = margin + spacing * band + spacing / 2;
    SDL_Rect r = { cx - EQ_TRACK_W / 2, EQ_SLIDER_TOP, EQ_TRACK_W, EQ_SLIDER_BOTTOM - EQ_SLIDER_TOP };
    return r;
}

static SDL_Rect eq_slider_hit_rect(const Skin* sk, int band) {
    SDL_Rect t = eq_slider_track_rect(sk, band);
    SDL_Rect r = { t.x - 4, t.y - 6, t.w + 8, t.h + 12 };
    return r;
}

// Map gain dB (-12..+12) to a y position inside the track (top=+12, bottom=-12).
static int eq_gain_to_y(SDL_Rect track, float gain_db) {
    float t = (gain_db + 12.f) / 24.f;  // 0..1
    if (t < 0) t = 0; if (t > 1) t = 1;
    return track.y + (int)((1.f - t) * track.h);
}
static float eq_y_to_gain(SDL_Rect track, int y) {
    float t = 1.f - (float)(y - track.y) / (float)track.h;
    if (t < 0) t = 0; if (t > 1) t = 1;
    return t * 24.f - 12.f;
}

static SDL_Rect eq_btn_onoff(const Skin* sk) { return (SDL_Rect){ sk->window_w - 132, 18, 40, 12 }; }
static SDL_Rect eq_btn_flat(const Skin* sk)  { return (SDL_Rect){ sk->window_w - 88, 18, 36, 12 }; }
static SDL_Rect eq_btn_back(const Skin* sk)  { return (SDL_Rect){ sk->window_w - 48, 18, 40, 12 }; }


#define PL_HEADER_H 12
#define PL_ROW_H 11

static const char* pl_basename(const char* path) {
    const char* s = strrchr(path, '/');
    const char* s2 = strrchr(path, '\\');
    if (s2 > s) s = s2;
    return s ? s + 1 : path;
}

static int pl_visible_rows(const Skin* sk) {
    if (!sk->playlist_rect.defined) return 0;
    int avail = sk->playlist_rect.rect.h - PL_HEADER_H;
    return avail / PL_ROW_H;
}

static int pl_clamp_scroll(int scroll, int total, int rows) {
    int max_s = total - rows;
    if (max_s < 0) max_s = 0;
    if (scroll < 0) scroll = 0;
    if (scroll > max_s) scroll = max_s;
    return scroll;
}

void ui_destroy(UI* ui) {
    fb_destroy(&ui->fb);
}

void ui_set_title(UI* ui, const char* title) {
    snprintf(ui->display_title, sizeof(ui->display_title), "%s", title);
}

int ui_picks_count(UI* ui) { return fb_result_count(&ui->fb); }
const char* ui_pick_path(UI* ui, int i) { return fb_result_path(&ui->fb, i); }
void ui_clear_picks(UI* ui) { fb_clear_result(&ui->fb); }

static void load_and_play(Audio* audio, const char* path) {
    if (!path) return;
    if (audio_load(audio, path)) audio_play(audio);
}

void ui_media(UI* ui, Audio* audio, Playlist* pl, MediaAction a) {
    (void)ui;
    switch (a) {
        case MEDIA_PLAY_PAUSE:
            if (audio_is_playing(audio)) audio_pause(audio);
            else                          audio_play(audio);
            break;
        case MEDIA_STOP:
            audio_stop(audio);
            break;
        case MEDIA_PREV:
            if (audio_position_seconds(audio) > 3.0) {
                audio_seek_seconds(audio, 0);
            } else if (pl && playlist_has_prev(pl)) {
                load_and_play(audio, playlist_prev(pl));
            } else {
                audio_seek_seconds(audio, 0);
            }
            break;
        case MEDIA_NEXT:
            if (pl && playlist_has_next(pl)) {
                load_and_play(audio, playlist_next(pl));
            } else {
                audio_stop(audio);
            }
            break;
    }
}

static UiAction fire_button(UI* ui, ButtonId id, Audio* audio, Playlist* pl) {
    UiAction act = {0};
    switch (id) {
        case BTN_PREV:
            if (audio_position_seconds(audio) > 3.0) {
                audio_seek_seconds(audio, 0);
            } else if (pl && playlist_has_prev(pl)) {
                load_and_play(audio, playlist_prev(pl));
            } else {
                audio_seek_seconds(audio, 0);
            }
            break;
        case BTN_PLAY:
            // Merged play/pause: toggles based on current state.
            if (audio_is_playing(audio)) audio_pause(audio);
            else                          audio_play(audio);
            break;
        case BTN_PAUSE: audio_pause(audio); break;  // kept for skin compat; not bound by default
        case BTN_STOP:  audio_stop(audio); break;
        case BTN_NEXT:
            if (pl && playlist_has_next(pl)) {
                load_and_play(audio, playlist_next(pl));
            } else {
                audio_stop(audio);
            }
            break;
        case BTN_OPEN:
            fb_show(&ui->fb);
            break;
        case BTN_EQ:
            ui->eq_open = true;
            break;
        case BTN_SHUFFLE:
            if (pl) playlist_set_shuffle(pl, !playlist_shuffle(pl));
            break;
        case BTN_LOOP:
            if (pl) playlist_set_loop(pl, !playlist_loop(pl));
            break;
        case BTN_SETTINGS: {
            // Mirror current state into the settings struct so toggles render correctly
            ui->settings.playlist_visible = ui->playlist_visible;
            settings_show(&ui->settings);
            break;
        }
        case BTN_MIN:   act.minimize_requested = true; break;
        case BTN_CLOSE: act.quit_requested = true; break;
        default: break;
    }
    return act;
}

static bool point_in(SDL_Rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static bool point_in_rect(SDL_Rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void eq_handle_event(UI* ui, const SDL_Event* e, Audio* audio, UiAction* act) {
    Skin* sk = ui->skin;
    Eq* eq = audio_get_eq(audio);

    if (e->type == SDL_KEYDOWN) {
        if (e->key.keysym.sym == SDLK_ESCAPE || e->key.keysym.sym == SDLK_e) {
            ui->eq_open = false;
            ui->eq_drag_band = -1;
        }
        return;
    }
    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        int mx = e->button.x, my = e->button.y;
        int btn = skin_button_at(sk, mx, my);
        if (btn == BTN_MIN) { act->minimize_requested = true; return; }
        if (btn == BTN_CLOSE) { act->quit_requested = true; return; }
        if (point_in_rect(eq_btn_onoff(sk), mx, my)) {
            eq_set_enabled(eq, !eq_is_enabled(eq));
            return;
        }
        if (point_in_rect(eq_btn_flat(sk), mx, my)) {
            eq_flat(eq);
            return;
        }
        if (point_in_rect(eq_btn_back(sk), mx, my)) {
            ui->eq_open = false;
            return;
        }
        for (int b = 0; b < EQ_BANDS; b++) {
            if (point_in_rect(eq_slider_hit_rect(sk, b), mx, my)) {
                ui->eq_drag_band = b;
                SDL_Rect tr = eq_slider_track_rect(sk, b);
                eq_set_gain(eq, b, eq_y_to_gain(tr, my));
                return;
            }
        }
    } else if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        ui->eq_drag_band = -1;
    } else if (e->type == SDL_MOUSEMOTION) {
        if (ui->eq_drag_band >= 0) {
            SDL_Rect tr = eq_slider_track_rect(sk, ui->eq_drag_band);
            eq_set_gain(eq, ui->eq_drag_band, eq_y_to_gain(tr, e->motion.y));
        }
    }
}

UiAction ui_handle_event(UI* ui, const SDL_Event* e, Audio* audio, Playlist* pl) {
    UiAction act = {0};

    if (ui->fb.open) {
        fb_handle_event(&ui->fb, e, ui->skin);
        return act;
    }
    if (ui->settings.open) {
        // Allow min/close on the title bar even while settings is open
        if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
            int btn = skin_button_at(ui->skin, e->button.x, e->button.y);
            if (btn == BTN_MIN)   { act.minimize_requested = true; return act; }
            if (btn == BTN_CLOSE) { act.quit_requested = true;     return act; }
        }
        settings_handle_event(&ui->settings, e, ui->skin);

        // Pull any changes out of settings.
        if (ui->settings.theme_changed) {
            theme_apply(ui->skin, ui->settings.current_theme);
            ui->settings.theme_changed = false;
            act.settings_changed = true;
        }
        if (ui->settings.aot_changed) {
            act.aot_changed = true;
            ui->settings.aot_changed = false;
            act.settings_changed = true;
        }
        if (ui->settings.plv_changed) {
            ui->playlist_visible = ui->settings.playlist_visible;
            act.playlist_vis_changed = true;
            ui->settings.plv_changed = false;
            act.settings_changed = true;
        }
        return act;
    }
    if (ui->eq_open) {
        eq_handle_event(ui, e, audio, &act);
        return act;
    }

    Skin* sk = ui->skin;

    // Playlist panel hover/scroll/click — handled before main button hit-testing.
    if (sk->playlist_rect.defined) {
        SDL_Rect plr = sk->playlist_rect.rect;
        SDL_Rect rows = { plr.x, plr.y + PL_HEADER_H, plr.w, plr.h - PL_HEADER_H };
        int rows_visible = pl_visible_rows(sk);
        const int X_W = 12;  // width of the [x] hit zone at the right of each row

        if (e->type == SDL_MOUSEMOTION) {
            int mx = e->motion.x, my = e->motion.y;
            int row_under = -1;
            if (mx >= rows.x && mx < rows.x + rows.w && my >= rows.y && my < rows.y + rows.h) {
                int row = (my - rows.y) / PL_ROW_H;
                int idx = ui->pl_scroll + row;
                if (idx >= 0 && idx < playlist_count(pl)) row_under = idx;
            }
            ui->pl_hover = (ui->pl_drag_idx >= 0) ? -1 : row_under;

            // Promote a press into a drag once the mouse moves more than a few pixels.
            if (ui->pl_drag_idx < 0 && ui->pl_press_idx >= 0) {
                int dx = mx - ui->pl_press_x;
                int dy = my - ui->pl_press_y;
                if (dx*dx + dy*dy > 16) {
                    ui->pl_drag_idx = ui->pl_press_idx;
                    ui->pl_drag_target = ui->pl_press_idx;
                }
            }
            if (ui->pl_drag_idx >= 0) {
                // Map to drop slot: clamp so we can drop at the very end.
                int target;
                if (my < rows.y) target = ui->pl_scroll;
                else if (my >= rows.y + rows_visible * PL_ROW_H) target = playlist_count(pl) - 1;
                else {
                    int row = (my - rows.y) / PL_ROW_H;
                    target = ui->pl_scroll + row;
                }
                if (target < 0) target = 0;
                if (target >= playlist_count(pl)) target = playlist_count(pl) - 1;
                ui->pl_drag_target = target;
            }
        } else if (e->type == SDL_MOUSEWHEEL) {
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            if (mx >= plr.x && mx < plr.x + plr.w && my >= plr.y && my < plr.y + plr.h) {
                ui->pl_scroll = pl_clamp_scroll(ui->pl_scroll - e->wheel.y * 3,
                                                playlist_count(pl), rows_visible);
                return act;
            }
        } else if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
            int mx = e->button.x, my = e->button.y;
            if (mx >= rows.x && mx < rows.x + rows.w && my >= rows.y && my < rows.y + rows.h) {
                int row = (my - rows.y) / PL_ROW_H;
                int idx = ui->pl_scroll + row;
                if (idx < 0 || idx >= playlist_count(pl)) return act;

                // Click on the [x] zone removes the row.
                int x_left = rows.x + rows.w - X_W - 4;
                if (mx >= x_left) {
                    bool removed_current = playlist_remove(pl, idx);
                    if (removed_current) {
                        const char* path = playlist_current(pl);
                        if (path) load_and_play(audio, path);
                        else audio_stop(audio);
                    }
                    if (ui->pl_hover >= playlist_count(pl)) ui->pl_hover = -1;
                    return act;
                }

                if (e->button.clicks >= 2) {
                    playlist_set_index(pl, idx);
                    const char* path = playlist_current(pl);
                    if (path && audio_load(audio, path)) audio_play(audio);
                    return act;
                }

                // Single press → arm a potential drag (committed once mouse moves).
                ui->pl_press_idx = idx;
                ui->pl_press_x = mx;
                ui->pl_press_y = my;
                return act;
            }
        } else if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
            if (ui->pl_drag_idx >= 0) {
                int from = ui->pl_drag_idx;
                int to   = ui->pl_drag_target;
                if (to >= 0 && from != to) playlist_move(pl, from, to);
            }
            ui->pl_drag_idx = -1;
            ui->pl_drag_target = -1;
            ui->pl_press_idx = -1;
        } else if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_RIGHT) {
            int mx = e->button.x, my = e->button.y;
            if (mx >= rows.x && mx < rows.x + rows.w && my >= rows.y && my < rows.y + rows.h) {
                int row = (my - rows.y) / PL_ROW_H;
                int idx = ui->pl_scroll + row;
                if (idx >= 0 && idx < playlist_count(pl)) {
                    bool removed_current = playlist_remove(pl, idx);
                    if (removed_current) {
                        const char* path = playlist_current(pl);
                        if (path) load_and_play(audio, path);
                        else audio_stop(audio);
                    }
                }
                return act;
            }
        }
    }

    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        int mx = e->button.x, my = e->button.y;
        int btn = skin_button_at(sk, mx, my);
        if (btn >= 0) {
            ui->pressed_btn = btn;
            return act;
        }
        if (sk->pos_slider.defined && point_in(sk->pos_slider.rect, mx, my)) {
            ui->dragging_pos = true;
            double len = audio_length_seconds(audio);
            if (len > 0) {
                double t = (double)(mx - sk->pos_slider.rect.x) / sk->pos_slider.rect.w;
                audio_seek_seconds(audio, t * len);
            }
            return act;
        }
        if (sk->vol_slider.defined && point_in(sk->vol_slider.rect, mx, my)) {
            ui->dragging_vol = true;
            float v = (float)(mx - sk->vol_slider.rect.x) / (float)sk->vol_slider.rect.w;
            audio_set_volume(audio, v);
            return act;
        }
        // Click on viz cycles modes
        if (sk->viz.defined && point_in(sk->viz.rect, mx, my)) {
            ui->viz_mode = (ui->viz_mode == VIZ_WAVE) ? VIZ_SPECTRUM : VIZ_WAVE;
            return act;
        }
    } else if (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT) {
        if (ui->pressed_btn >= 0) {
            int mx = e->button.x, my = e->button.y;
            int btn = skin_button_at(sk, mx, my);
            if (btn == ui->pressed_btn) act = fire_button(ui, (ButtonId)btn, audio, pl);
            ui->pressed_btn = -1;
        }
        ui->dragging_pos = false;
        ui->dragging_vol = false;
    } else if (e->type == SDL_MOUSEMOTION) {
        int mx = e->motion.x;
        ui->hover_btn = skin_button_at(sk, e->motion.x, e->motion.y);
        if (ui->dragging_pos && sk->pos_slider.defined) {
            double len = audio_length_seconds(audio);
            if (len > 0) {
                double t = (double)(mx - sk->pos_slider.rect.x) / sk->pos_slider.rect.w;
                if (t < 0) t = 0;
                if (t > 1) t = 1;
                audio_seek_seconds(audio, t * len);
            }
        }
        if (ui->dragging_vol && sk->vol_slider.defined) {
            float v = (float)(mx - sk->vol_slider.rect.x) / (float)sk->vol_slider.rect.w;
            if (v < 0) v = 0;
            if (v > 1) v = 1;
            audio_set_volume(audio, v);
        }
    } else if (e->type == SDL_KEYDOWN) {
        switch (e->key.keysym.sym) {
            case SDLK_SPACE:
                if (audio_is_playing(audio)) audio_pause(audio); else audio_play(audio);
                break;
            case SDLK_AUDIOPLAY:        ui_media(ui, audio, pl, MEDIA_PLAY_PAUSE); break;
            case SDLK_AUDIOSTOP:        ui_media(ui, audio, pl, MEDIA_STOP);       break;
            case SDLK_AUDIOPREV:        ui_media(ui, audio, pl, MEDIA_PREV);       break;
            case SDLK_AUDIONEXT:        ui_media(ui, audio, pl, MEDIA_NEXT);       break;
            case SDLK_VOLUMEUP:   audio_set_volume(audio, audio_get_volume(audio) + 0.05f); break;
            case SDLK_VOLUMEDOWN: audio_set_volume(audio, audio_get_volume(audio) - 0.05f); break;
            case SDLK_AUDIOMUTE:  audio_set_volume(audio, audio_get_volume(audio) > 0.f ? 0.f : 0.7f); break;
            case SDLK_s: audio_stop(audio); break;
            case SDLK_LEFT:  audio_seek_seconds(audio, audio_position_seconds(audio) - 5.0); break;
            case SDLK_RIGHT: audio_seek_seconds(audio, audio_position_seconds(audio) + 5.0); break;
            case SDLK_UP:   audio_set_volume(audio, audio_get_volume(audio) + 0.05f); break;
            case SDLK_DOWN: audio_set_volume(audio, audio_get_volume(audio) - 0.05f); break;
            case SDLK_v:    ui->viz_mode = (ui->viz_mode == VIZ_WAVE) ? VIZ_SPECTRUM : VIZ_WAVE; break;
            case SDLK_o:    fb_show(&ui->fb); break;
            case SDLK_l:    if (pl) playlist_set_loop(pl, !playlist_loop(pl)); break;
            case SDLK_h:    if (pl) playlist_set_shuffle(pl, !playlist_shuffle(pl)); break;
            case SDLK_e:    ui->eq_open = true; break;
            case SDLK_p:
                ui->playlist_visible = !ui->playlist_visible;
                ui->settings.playlist_visible = ui->playlist_visible;
                act.playlist_vis_changed = true;
                act.settings_changed = true;
                break;
            case SDLK_t:
                ui->settings.always_on_top = !ui->settings.always_on_top;
                act.aot_changed = true;
                act.settings_changed = true;
                break;
            case SDLK_F2:
                ui->settings.playlist_visible = ui->playlist_visible;
                settings_show(&ui->settings);
                break;
            case SDLK_DELETE:
            case SDLK_BACKSPACE: {
                if (ui->pl_hover >= 0 && ui->pl_hover < playlist_count(pl)) {
                    bool removed_current = playlist_remove(pl, ui->pl_hover);
                    if (removed_current) {
                        const char* path = playlist_current(pl);
                        if (path) load_and_play(audio, path);
                        else audio_stop(audio);
                    }
                }
                break;
            }
            default: break;
        }
    }
    return act;
}

// ---------- Drawing helpers ----------

static void fill_rect(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren, &r);
}
static void draw_rect(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(ren, &r);
}
static SDL_Color dim(SDL_Color c, float k) {
    SDL_Color o = { (Uint8)(c.r * k), (Uint8)(c.g * k), (Uint8)(c.b * k), c.a };
    return o;
}
static SDL_Color lighten(SDL_Color c, float k) {
    int r = (int)(c.r + (255 - c.r) * k);
    int g = (int)(c.g + (255 - c.g) * k);
    int b = (int)(c.b + (255 - c.b) * k);
    SDL_Color o = { (Uint8)(r > 255 ? 255 : r), (Uint8)(g > 255 ? 255 : g), (Uint8)(b > 255 ? 255 : b), c.a };
    return o;
}
// Beveled rect: vertical-gradient face + bright top/left edge + dark bottom/right edge.
// `recess` inverts the bevel (pressed state).
static void draw_bevel(SDL_Renderer* ren, SDL_Rect r, SDL_Color face, SDL_Color edge, bool recess) {
    SDL_Color top    = recess ? dim(face, 0.55f)     : lighten(face, 0.30f);
    SDL_Color bot    = recess ? lighten(face, 0.20f) : dim(face, 0.65f);
    SDL_Color hi     = recess ? dim(edge, 0.40f)     : lighten(edge, 0.45f);
    SDL_Color lo     = recess ? lighten(edge, 0.30f) : dim(edge, 0.45f);

    // Vertical gradient face — three bands keeps the cost trivial at 28x22.
    int h1 = r.h / 3;
    int h2 = r.h / 3;
    SDL_Rect b1 = { r.x + 1, r.y + 1,           r.w - 2, h1 };
    SDL_Rect b2 = { r.x + 1, r.y + 1 + h1,      r.w - 2, h2 };
    SDL_Rect b3 = { r.x + 1, r.y + 1 + h1 + h2, r.w - 2, r.h - 2 - h1 - h2 };
    fill_rect(ren, b1, top);
    fill_rect(ren, b2, face);
    fill_rect(ren, b3, bot);

    // Outer border + inner highlight/shadow on two sides.
    draw_rect(ren, r, dim(edge, 0.6f));
    SDL_SetRenderDrawColor(ren, hi.r, hi.g, hi.b, 255);
    SDL_RenderDrawLine(ren, r.x + 1, r.y + 1, r.x + r.w - 2, r.y + 1);              // top
    SDL_RenderDrawLine(ren, r.x + 1, r.y + 1, r.x + 1, r.y + r.h - 2);              // left
    SDL_SetRenderDrawColor(ren, lo.r, lo.g, lo.b, 255);
    SDL_RenderDrawLine(ren, r.x + 1, r.y + r.h - 2, r.x + r.w - 2, r.y + r.h - 2);  // bottom
    SDL_RenderDrawLine(ren, r.x + r.w - 2, r.y + 1, r.x + r.w - 2, r.y + r.h - 2);  // right
}

// Draw a filled triangle by row-scanning between two edges.
static void fill_triangle(SDL_Renderer* ren, int x1, int y1, int x2, int y2, int x3, int y3) {
    // sort by y
    if (y2 < y1) { int t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y3 < y1) { int t = y1; y1 = y3; y3 = t; t = x1; x1 = x3; x3 = t; }
    if (y3 < y2) { int t = y2; y2 = y3; y3 = t; t = x2; x2 = x3; x3 = t; }
    for (int y = y1; y <= y3; y++) {
        float t13 = (y3 == y1) ? 0.f : (float)(y - y1) / (float)(y3 - y1);
        int xa = (int)(x1 + (x3 - x1) * t13);
        int xb;
        if (y < y2) {
            float t12 = (y2 == y1) ? 0.f : (float)(y - y1) / (float)(y2 - y1);
            xb = (int)(x1 + (x2 - x1) * t12);
        } else {
            float t23 = (y3 == y2) ? 0.f : (float)(y - y2) / (float)(y3 - y2);
            xb = (int)(x2 + (x3 - x2) * t23);
        }
        if (xa > xb) { int t = xa; xa = xb; xb = t; }
        SDL_RenderDrawLine(ren, xa, y, xb, y);
    }
}

// ---------- Icon glyphs (shapes inside button rect) ----------

static void icon_play(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    int pad = 6;
    int x1 = r.x + pad, y1 = r.y + pad;
    int x2 = x1, y2 = r.y + r.h - pad;
    int x3 = r.x + r.w - pad, y3 = r.y + r.h / 2;
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    fill_triangle(ren, x1, y1, x2, y2, x3, y3);
}

static void icon_pause(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    int pad = 8;
    int bw = (r.w - 2 * pad - 4) / 2;
    SDL_Rect a = { r.x + pad, r.y + pad - 2, bw, r.h - 2 * (pad - 2) };
    SDL_Rect b = { r.x + pad + bw + 4, r.y + pad - 2, bw, r.h - 2 * (pad - 2) };
    fill_rect(ren, a, c);
    fill_rect(ren, b, c);
}

static void icon_stop(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    int pad = 8;
    SDL_Rect a = { r.x + pad, r.y + pad - 1, r.w - 2 * pad, r.h - 2 * (pad - 1) };
    fill_rect(ren, a, c);
}

static void icon_prev(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    int pad = 6;
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_Rect bar = { r.x + pad, r.y + pad, 3, r.h - 2 * pad };
    fill_rect(ren, bar, c);
    int x1 = r.x + pad + 5, y1 = r.y + r.h / 2;
    int x2 = r.x + r.w - pad, y2 = r.y + pad;
    int x3 = x2, y3 = r.y + r.h - pad;
    fill_triangle(ren, x1, y1, x2, y2, x3, y3);
}

static void icon_next(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    int pad = 6;
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_Rect bar = { r.x + r.w - pad - 3, r.y + pad, 3, r.h - 2 * pad };
    fill_rect(ren, bar, c);
    int x1 = r.x + pad, y1 = r.y + pad;
    int x2 = x1, y2 = r.y + r.h - pad;
    int x3 = r.x + r.w - pad - 5, y3 = r.y + r.h / 2;
    fill_triangle(ren, x1, y1, x2, y2, x3, y3);
}

static void icon_open(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    // simple folder: short tab on top + body rect outline
    int pad = 6;
    SDL_Rect body = { r.x + pad, r.y + pad + 2, r.w - 2 * pad, r.h - 2 * pad - 2 };
    SDL_Rect tab  = { body.x + 1, body.y - 3, body.w / 2, 4 };
    fill_rect(ren, tab, c);
    fill_rect(ren, body, dim(c, 0.55f));
    draw_rect(ren, body, c);
}

static void icon_shuffle(SDL_Renderer* ren, SDL_Rect r, SDL_Color c, bool active) {
    SDL_Color col = active ? c : dim(c, 0.5f);
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    int pad = 6;
    int x0 = r.x + pad, x1 = r.x + r.w - pad;
    int yA = r.y + pad + 2, yB = r.y + r.h - pad - 2;
    // two crossing lines
    SDL_RenderDrawLine(ren, x0, yA, x1, yB);
    SDL_RenderDrawLine(ren, x0, yA + 1, x1, yB + 1);
    SDL_RenderDrawLine(ren, x0, yB, x1, yA);
    SDL_RenderDrawLine(ren, x0, yB - 1, x1, yA - 1);
    // arrowheads at right
    int ah = 3;
    SDL_RenderDrawLine(ren, x1, yB, x1 - ah, yB - ah);
    SDL_RenderDrawLine(ren, x1, yA, x1 - ah, yA + ah);
}

static void icon_loop(SDL_Renderer* ren, SDL_Rect r, SDL_Color c, bool active) {
    SDL_Color col = active ? c : dim(c, 0.5f);
    int pad = 5;
    SDL_Rect outer = { r.x + pad, r.y + pad + 1, r.w - 2 * pad, r.h - 2 * pad - 2 };
    draw_rect(ren, outer, col);
    SDL_Rect outer2 = { outer.x + 1, outer.y, outer.w - 2, outer.h };
    draw_rect(ren, outer2, col);
    // arrowhead
    int ah = 3;
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    int ax = outer.x + outer.w - 1;
    int ay = outer.y;
    SDL_RenderDrawLine(ren, ax, ay, ax - ah, ay - ah);
    SDL_RenderDrawLine(ren, ax, ay, ax - ah, ay + ah);
}

static void icon_min(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    SDL_Rect bar = { r.x + 3, r.y + r.h - 4, r.w - 6, 2 };
    fill_rect(ren, bar, c);
}

static void icon_eq(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    int pad = 5;
    int slots = 4;
    int slot_w = (r.w - 2 * pad) / slots;
    int top = r.y + pad;
    int bot = r.y + r.h - pad;
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    static const float heights[4] = { 0.4f, 0.7f, 0.5f, 0.85f };
    for (int i = 0; i < slots; i++) {
        int x = r.x + pad + i * slot_w + 1;
        int h = (int)((bot - top) * heights[i]);
        SDL_Rect bar = { x, bot - h, slot_w - 2, h };
        SDL_RenderFillRect(ren, &bar);
    }
}

static void icon_settings(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    // Three horizontal lines (hamburger)
    int pad_x = 3;
    int spacing = (r.h - 2) / 4;
    if (spacing < 2) spacing = 2;
    int y = r.y + (r.h - spacing * 3) / 2;
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    for (int i = 0; i < 3; i++) {
        SDL_Rect bar = { r.x + pad_x, y + i * spacing, r.w - 2 * pad_x, 1 };
        SDL_RenderFillRect(ren, &bar);
    }
}

static void icon_close(SDL_Renderer* ren, SDL_Rect r, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    int pad = 3;
    int x0 = r.x + pad, y0 = r.y + pad;
    int x1 = r.x + r.w - pad - 1, y1 = r.y + r.h - pad - 1;
    SDL_RenderDrawLine(ren, x0, y0, x1, y1);
    SDL_RenderDrawLine(ren, x0 + 1, y0, x1, y1 - 1);
    SDL_RenderDrawLine(ren, x0, y0 + 1, x1 - 1, y1);
    SDL_RenderDrawLine(ren, x1, y0, x0, y1);
    SDL_RenderDrawLine(ren, x1 - 1, y0, x0, y1 - 1);
    SDL_RenderDrawLine(ren, x1, y0 + 1, x0 + 1, y1);
}

// ---------- Compound rendering ----------

static void render_background(UI* ui) {
    Skin* sk = ui->skin;
    if (sk->bg_tex) {
        SDL_Rect dst = { 0, 0, sk->window_w, sk->window_h };
        SDL_RenderCopy(ui->ren, sk->bg_tex, NULL, &dst);
        return;
    }
    for (int y = 0; y < sk->window_h; y++) {
        float t = (float)y / (float)(sk->window_h - 1);
        SDL_Color c = {
            (Uint8)((1 - t) * sk->theme_bg.r + t * sk->theme_panel.r),
            (Uint8)((1 - t) * sk->theme_bg.g + t * sk->theme_panel.g),
            (Uint8)((1 - t) * sk->theme_bg.b + t * sk->theme_panel.b),
            255
        };
        SDL_SetRenderDrawColor(ui->ren, c.r, c.g, c.b, c.a);
        SDL_RenderDrawLine(ui->ren, 0, y, sk->window_w - 1, y);
    }
    // title-bar accent strip
    if (sk->drag_region.defined) {
        SDL_Rect tb = sk->drag_region.rect;
        SDL_Color tbc = dim(sk->theme_panel, 0.7f);
        fill_rect(ui->ren, tb, tbc);
        SDL_SetRenderDrawColor(ui->ren, sk->theme_accent.r, sk->theme_accent.g, sk->theme_accent.b, 80);
        SDL_RenderDrawLine(ui->ren, tb.x, tb.y + tb.h - 1, tb.x + tb.w, tb.y + tb.h - 1);
        font_draw(ui->ren, tb.x + 4, tb.y + (tb.h - FONT_H) / 2, 1,
                  sk->theme_accent, "TIMP");
    }
    SDL_Rect b = { 0, 0, sk->window_w, sk->window_h };
    draw_rect(ui->ren, b, (SDL_Color){0,0,0,255});
}

static void render_button(UI* ui, ButtonId id, Audio* audio, Playlist* pl) {
    Skin* sk = ui->skin;
    SkinButton* btn = &sk->buttons[id];
    if (!btn->defined) return;

    bool pressed = (ui->pressed_btn == (int)id);
    bool hovered = (ui->hover_btn == (int)id) && !pressed;
    SDL_Rect hit = btn->hit;

    if (sk->bg_tex && (btn->normal.w > 0 || btn->pressed.w > 0)) {
        SDL_Rect src = pressed && btn->pressed.w ? btn->pressed : btn->normal;
        if (src.w > 0) {
            SDL_RenderCopy(ui->ren, sk->bg_tex, &src, &hit);
            return;
        }
    }

    SDL_Color face = sk->theme_panel;
    SDL_Color edge = sk->theme_accent;
    if (hovered) face = lighten(face, 0.18f);

    bool tiny_titlebar = (id == BTN_MIN || id == BTN_CLOSE || id == BTN_SETTINGS);
    if (!tiny_titlebar) {
        draw_bevel(ui->ren, hit, face, edge, pressed);
    } else if (hovered) {
        // subtle hover hint for tiny title-bar icons
        fill_rect(ui->ren, hit, dim(sk->theme_accent, 0.18f));
    }

    SDL_Color icon_color = sk->theme_accent;
    if (pressed) icon_color = dim(icon_color, 0.6f);
    else if (hovered) icon_color = lighten(icon_color, 0.2f);

    switch (id) {
        case BTN_PREV:    icon_prev(ui->ren, hit, icon_color); break;
        case BTN_PLAY:
            // Single button: pause icon while playing, play icon otherwise.
            if (audio_is_playing(audio)) icon_pause(ui->ren, hit, icon_color);
            else                         icon_play(ui->ren, hit, icon_color);
            break;
        case BTN_PAUSE:   icon_pause(ui->ren, hit, icon_color); break;
        case BTN_STOP:    icon_stop(ui->ren, hit, icon_color); break;
        case BTN_NEXT:    icon_next(ui->ren, hit, icon_color); break;
        case BTN_OPEN:    icon_open(ui->ren, hit, icon_color); break;
        case BTN_EQ:      icon_eq(ui->ren, hit, icon_color); break;
        case BTN_SHUFFLE: icon_shuffle(ui->ren, hit, icon_color, pl ? playlist_shuffle(pl) : false); break;
        case BTN_LOOP:    icon_loop(ui->ren, hit, icon_color, pl ? playlist_loop(pl) : false); break;
        case BTN_SETTINGS: icon_settings(ui->ren, hit, sk->theme_text); break;
        case BTN_MIN:     icon_min(ui->ren, hit, sk->theme_text); break;
        case BTN_CLOSE:   icon_close(ui->ren, hit, sk->theme_text); break;
        default: break;
    }
}

static void render_slider(UI* ui, SkinElement* el, float t) {
    if (!el->defined) return;
    SDL_Rect r = el->rect;
    fill_rect(ui->ren, r, (SDL_Color){ 8, 12, 16, 255 });
    draw_rect(ui->ren, r, dim(el->color, 0.4f));

    if (t < 0) t = 0;
    if (t > 1) t = 1;
    SDL_Rect fill = { r.x + 1, r.y + 1, (int)((r.w - 2) * t), r.h - 2 };
    fill_rect(ui->ren, fill, el->color);

    int tx = r.x + (int)((r.w - 4) * t);
    SDL_Rect thumb = { tx, r.y - 1, 4, r.h + 2 };
    fill_rect(ui->ren, thumb, ui->skin->theme_text);
}

static void render_viz_wave(UI* ui, Audio* audio) {
    SkinElement* v = &ui->skin->viz;
    SDL_Rect r = v->rect;
    int n = r.w;
    if (n > VIZ_SAMPLES) n = VIZ_SAMPLES;
    float buf[VIZ_SAMPLES];
    int got = audio_snapshot_waveform(audio, buf, n);
    if (got <= 1) return;

    SDL_SetRenderDrawColor(ui->ren, v->color.r, v->color.g, v->color.b, v->color.a);
    int mid = r.y + r.h / 2;
    int amp = r.h / 2 - 2;
    int prev_y = mid;
    for (int i = 0; i < got; i++) {
        float s = buf[i];
        if (s > 1.f) s = 1.f;
        if (s < -1.f) s = -1.f;
        int yy = mid - (int)(s * amp);
        int xx = r.x + i;
        if (i > 0) SDL_RenderDrawLine(ui->ren, r.x + i - 1, prev_y, xx, yy);
        prev_y = yy;
    }
}

static void render_viz_spectrum(UI* ui, Audio* audio) {
    SkinElement* v = &ui->skin->viz;
    SDL_Rect r = v->rect;

    int n_bars = ui->spectrum_bar_count;
    if (n_bars > 64) n_bars = 64;
    int bar_w = r.w / n_bars;
    if (bar_w < 2) {
        n_bars = r.w / 2;
        bar_w = 2;
    }

    // FFT input
    int fft_n = 512;
    float samples[512];
    int got = audio_snapshot_waveform(audio, samples, fft_n);
    if (got < fft_n) {
        for (int i = got; i < fft_n; i++) samples[i] = 0.0f;
    }

    float mags[256];
    fft_magnitudes(samples, fft_n, mags);

    float bands[64];
    fft_log_bands(mags, fft_n / 2, bands, n_bars);

    // smooth bars (attack fast, decay slow)
    for (int i = 0; i < n_bars; i++) {
        float target = bands[i];
        float current = ui->spectrum_bars[i];
        if (target > current) ui->spectrum_bars[i] = current + (target - current) * 0.6f;
        else                  ui->spectrum_bars[i] = current + (target - current) * 0.12f;
    }

    int pad = 1;
    for (int i = 0; i < n_bars; i++) {
        float h = ui->spectrum_bars[i];
        if (h < 0) h = 0;
        if (h > 1) h = 1;
        int bh = (int)((r.h - 2) * h);
        SDL_Rect bar = { r.x + i * bar_w + pad, r.y + r.h - 1 - bh,
                         bar_w - pad, bh };
        // gradient: dim base → bright top
        for (int y = 0; y < bar.h; y++) {
            float t = (float)y / (float)(bar.h > 1 ? bar.h - 1 : 1);
            SDL_Color c = {
                (Uint8)(v->color.r * (0.4f + 0.6f * t)),
                (Uint8)(v->color.g * (0.4f + 0.6f * t)),
                (Uint8)(v->color.b * (0.4f + 0.6f * t)),
                255
            };
            SDL_SetRenderDrawColor(ui->ren, c.r, c.g, c.b, c.a);
            SDL_RenderDrawLine(ui->ren, bar.x, bar.y + bar.h - 1 - y,
                               bar.x + bar.w - 1, bar.y + bar.h - 1 - y);
        }
    }
}

static void render_viz(UI* ui, Audio* audio) {
    SkinElement* v = &ui->skin->viz;
    if (!v->defined) return;
    SDL_Rect r = v->rect;

    fill_rect(ui->ren, r, (SDL_Color){ 6, 10, 14, 255 });
    draw_rect(ui->ren, r, dim(v->color, 0.33f));

    if (ui->viz_mode == VIZ_WAVE) render_viz_wave(ui, audio);
    else                          render_viz_spectrum(ui, audio);
}

static void render_time(UI* ui, Audio* audio) {
    SkinElement* td = &ui->skin->time_disp;
    if (!td->defined) return;
    double pos = audio_position_seconds(audio);
    double len = audio_length_seconds(audio);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d/%02d:%02d",
             (int)pos / 60, (int)pos % 60,
             (int)len / 60, (int)len % 60);
    int scale = 1;
    int tw = font_text_width(scale, buf);
    while (tw > td->rect.w && scale > 0) { scale--; tw = font_text_width(scale, buf); }
    if (scale < 1) scale = 1;
    int tx = td->rect.x;
    int ty = td->rect.y + (td->rect.h - FONT_H * scale) / 2;
    font_draw(ui->ren, tx, ty, scale, td->color, buf);
}

static void render_playlist(UI* ui, Playlist* pl) {
    Skin* sk = ui->skin;
    if (!sk->playlist_rect.defined) return;
    SDL_Rect r = sk->playlist_rect.rect;

    // Outer panel
    fill_rect(ui->ren, r, (SDL_Color){ 6, 10, 14, 255 });
    draw_rect(ui->ren, r, dim(sk->theme_accent, 0.33f));

    // Header
    SDL_Rect head = { r.x + 1, r.y + 1, r.w - 2, PL_HEADER_H - 1 };
    fill_rect(ui->ren, head, dim(sk->theme_panel, 0.85f));
    SDL_SetRenderDrawColor(ui->ren, sk->theme_accent.r, sk->theme_accent.g, sk->theme_accent.b, 80);
    SDL_RenderDrawLine(ui->ren, head.x, head.y + head.h, head.x + head.w, head.y + head.h);

    char hdr[64];
    int total = playlist_count(pl);
    snprintf(hdr, sizeof(hdr), "PLAYLIST  [%d]", total);
    font_draw(ui->ren, head.x + 4, head.y + (head.h - FONT_H) / 2, 1, sk->theme_accent, hdr);

    // Rows
    SDL_Rect rows_rect = { r.x + 1, r.y + PL_HEADER_H, r.w - 2, r.h - PL_HEADER_H - 1 };
    int rows_visible = pl_visible_rows(sk);
    ui->pl_scroll = pl_clamp_scroll(ui->pl_scroll, total, rows_visible);

    int cur = playlist_index(pl);
    const int X_W = 12;  // [x] zone on the right of each row
    for (int row = 0; row < rows_visible; row++) {
        int idx = ui->pl_scroll + row;
        if (idx >= total) break;
        SDL_Rect rrow = { rows_rect.x, rows_rect.y + row * PL_ROW_H, rows_rect.w, PL_ROW_H };

        bool is_current = (idx == cur);
        bool is_hover   = (idx == ui->pl_hover);
        bool is_dragged = (idx == ui->pl_drag_idx);
        if (is_current) {
            fill_rect(ui->ren, rrow, dim(sk->theme_accent, 0.18f));
        } else if (is_hover) {
            fill_rect(ui->ren, rrow, dim(sk->theme_panel, 0.7f));
        }
        if (is_dragged) {
            // dim the row that's being lifted
            fill_rect(ui->ren, rrow, (SDL_Color){0, 0, 0, 120});
        }

        char num[8];
        snprintf(num, sizeof(num), "%2d", idx + 1);
        SDL_Color num_color = is_current ? sk->theme_accent : dim(sk->theme_text, 0.6f);
        font_draw(ui->ren, rrow.x + 4, rrow.y + (rrow.h - FONT_H) / 2, 1, num_color, num);

        const char* path = pl->paths[idx];
        const char* name = pl_basename(path);
        char shown[256];
        snprintf(shown, sizeof(shown), "%s", name);
        int max_chars = (rrow.w - 28 - X_W - 8) / (FONT_W + 1);
        if ((int)strlen(shown) > max_chars && max_chars > 3) {
            shown[max_chars - 3] = '.';
            shown[max_chars - 2] = '.';
            shown[max_chars - 1] = '.';
            shown[max_chars] = 0;
        }
        SDL_Color name_color = is_current ? sk->theme_accent : sk->theme_text;
        font_draw(ui->ren, rrow.x + 22, rrow.y + (rrow.h - FONT_H) / 2, 1, name_color, shown);

        // [x] remove icon — visible on the hovered row.
        if (is_hover && ui->pl_drag_idx < 0) {
            int xc = rrow.x + rrow.w - X_W / 2 - 4;
            int yc = rrow.y + rrow.h / 2;
            SDL_Color xc_col = dim(sk->theme_text, 0.85f);
            SDL_SetRenderDrawColor(ui->ren, xc_col.r, xc_col.g, xc_col.b, 255);
            for (int d = -3; d <= 3; d++) {
                SDL_RenderDrawPoint(ui->ren, xc + d, yc + d);
                SDL_RenderDrawPoint(ui->ren, xc + d, yc - d);
            }
        }
    }

    // Insertion-line indicator while dragging.
    if (ui->pl_drag_idx >= 0 && ui->pl_drag_target >= 0) {
        int t = ui->pl_drag_target;
        if (t >= ui->pl_scroll && t < ui->pl_scroll + rows_visible) {
            int line_y = rows_rect.y + (t - ui->pl_scroll) * PL_ROW_H;
            if (ui->pl_drag_target > ui->pl_drag_idx) line_y += PL_ROW_H;  // drop below
            SDL_Rect line = { rows_rect.x + 2, line_y - 1, rows_rect.w - 4, 2 };
            fill_rect(ui->ren, line, sk->theme_accent);
        }
    }

    // Scrollbar
    if (total > rows_visible) {
        SDL_Rect sb = { rows_rect.x + rows_rect.w - 4, rows_rect.y + 1, 3, rows_rect.h - 2 };
        fill_rect(ui->ren, sb, dim(sk->theme_panel, 0.6f));
        float th_h = (float)rows_visible / (float)total * (float)sb.h;
        if (th_h < 8) th_h = 8;
        float th_y = (float)ui->pl_scroll / (float)(total - rows_visible) * (sb.h - th_h);
        SDL_Rect th = { sb.x, sb.y + (int)th_y, sb.w, (int)th_h };
        fill_rect(ui->ren, th, sk->theme_accent);
    }
}

static void render_title(UI* ui) {
    SkinElement* el = &ui->skin->title;
    if (!el->defined) return;
    int scale = 1;
    int max_chars = el->rect.w / ((FONT_W + 1) * scale);
    char shown[256];
    snprintf(shown, sizeof(shown), "%s", ui->display_title);
    if ((int)strlen(shown) > max_chars && max_chars > 3) {
        shown[max_chars - 3] = '.';
        shown[max_chars - 2] = '.';
        shown[max_chars - 1] = '.';
        shown[max_chars] = 0;
    }
    int ty = el->rect.y + (el->rect.h - FONT_H * scale) / 2;
    font_draw(ui->ren, el->rect.x, ty, scale, el->color, shown);
}

static void render_text_button(SDL_Renderer* ren, SDL_Rect r, const char* label,
                               SDL_Color face, SDL_Color edge, SDL_Color text) {
    fill_rect(ren, r, face);
    draw_rect(ren, r, edge);
    int tw = font_text_width(1, label);
    font_draw(ren, r.x + (r.w - tw) / 2, r.y + (r.h - FONT_H) / 2, 1, text, label);
}

static void render_eq(UI* ui, Audio* audio) {
    Skin* sk = ui->skin;
    Eq* eq = audio_get_eq(audio);

    SDL_Rect full = { 0, 0, sk->window_w, sk->window_h };
    fill_rect(ui->ren, full, sk->theme_bg);

    // Title bar (drag region)
    if (sk->drag_region.defined) {
        SDL_Rect tb = sk->drag_region.rect;
        fill_rect(ui->ren, tb, dim(sk->theme_panel, 0.7f));
        SDL_SetRenderDrawColor(ui->ren, sk->theme_accent.r, sk->theme_accent.g, sk->theme_accent.b, 80);
        SDL_RenderDrawLine(ui->ren, tb.x, tb.y + tb.h - 1, tb.x + tb.w, tb.y + tb.h - 1);
        font_draw(ui->ren, tb.x + 4, tb.y + (tb.h - FONT_H) / 2, 1, sk->theme_accent, "TIMP / EQ");
    }
    // Reuse min/close
    if (sk->buttons[BTN_MIN].defined) {
        icon_min(ui->ren, sk->buttons[BTN_MIN].hit, sk->theme_text);
    }
    if (sk->buttons[BTN_CLOSE].defined) {
        icon_close(ui->ren, sk->buttons[BTN_CLOSE].hit, sk->theme_text);
    }

    // Header line: "EQUALIZER" + ON/OFF + FLAT + BACK
    font_draw(ui->ren, 8, 20, 1, sk->theme_accent, "EQUALIZER");

    SDL_Color on_face  = eq_is_enabled(eq) ? sk->theme_accent : dim(sk->theme_panel, 0.85f);
    SDL_Color on_text  = eq_is_enabled(eq) ? sk->theme_bg : sk->theme_text;
    SDL_Color edge_dim = dim(sk->theme_accent, 0.6f);
    render_text_button(ui->ren, eq_btn_onoff(sk), eq_is_enabled(eq) ? "ON" : "OFF",
                       on_face, edge_dim, on_text);
    render_text_button(ui->ren, eq_btn_flat(sk), "FLAT",
                       dim(sk->theme_panel, 0.85f), edge_dim, sk->theme_text);
    render_text_button(ui->ren, eq_btn_back(sk), "BACK",
                       dim(sk->theme_panel, 0.85f), edge_dim, sk->theme_text);

    // Sliders
    static const char* labels[EQ_BANDS] = { "60", "170", "310", "600", "1K", "3K", "6K", "12K", "14K", "16K" };
    SDL_Color track_color = dim(sk->theme_accent, 0.4f);
    SDL_Color cent_color  = dim(sk->theme_accent, 0.5f);

    for (int b = 0; b < EQ_BANDS; b++) {
        SDL_Rect tr = eq_slider_track_rect(sk, b);
        // track
        fill_rect(ui->ren, tr, (SDL_Color){ 8, 12, 16, 255 });
        draw_rect(ui->ren, tr, track_color);

        // center detent line
        int mid_y = tr.y + tr.h / 2;
        SDL_SetRenderDrawColor(ui->ren, cent_color.r, cent_color.g, cent_color.b, cent_color.a);
        SDL_RenderDrawLine(ui->ren, tr.x - 3, mid_y, tr.x + tr.w + 2, mid_y);

        // gain fill (from center toward thumb)
        float g = eq_get_gain(eq, b);
        int thumb_y = eq_gain_to_y(tr, g);
        SDL_Rect fillr;
        if (thumb_y < mid_y) {
            fillr = (SDL_Rect){ tr.x + 1, thumb_y, tr.w - 2, mid_y - thumb_y };
        } else {
            fillr = (SDL_Rect){ tr.x + 1, mid_y, tr.w - 2, thumb_y - mid_y };
        }
        SDL_Color fc = eq_is_enabled(eq) ? sk->theme_accent : dim(sk->theme_accent, 0.5f);
        fill_rect(ui->ren, fillr, fc);

        // thumb
        SDL_Rect thumb = { tr.x - 4, thumb_y - 2, tr.w + 8, 4 };
        fill_rect(ui->ren, thumb, sk->theme_text);

        // freq label below
        int label_w = font_text_width(1, labels[b]);
        font_draw(ui->ren, tr.x + (tr.w - label_w) / 2, EQ_SLIDER_BOTTOM + 6,
                  1, sk->theme_text, labels[b]);

        // dB readout — shown for whatever's currently being dragged, otherwise for all if non-zero
        char db[8];
        if (g >= 0.05f) snprintf(db, sizeof(db), "+%d", (int)(g + 0.5f));
        else if (g <= -0.05f) snprintf(db, sizeof(db), "%d", (int)(g - 0.5f));
        else db[0] = 0;
        if (db[0]) {
            int dw = font_text_width(1, db);
            SDL_Color dc = (b == ui->eq_drag_band) ? sk->theme_accent : dim(sk->theme_text, 0.7f);
            font_draw(ui->ren, tr.x + (tr.w - dw) / 2, EQ_SLIDER_BOTTOM + 16, 1, dc, db);
        }
    }
}

void ui_render(UI* ui, Audio* audio, Playlist* pl) {
    if (ui->fb.open) {
        fb_render(&ui->fb, ui->ren, ui->skin);
        SDL_RenderPresent(ui->ren);
        return;
    }
    if (ui->settings.open) {
        settings_render(&ui->settings, ui->ren, ui->skin);
        // Title-bar min/close stay clickable above the settings panel
        if (ui->skin->buttons[BTN_MIN].defined)
            icon_min(ui->ren, ui->skin->buttons[BTN_MIN].hit, ui->skin->theme_text);
        if (ui->skin->buttons[BTN_CLOSE].defined)
            icon_close(ui->ren, ui->skin->buttons[BTN_CLOSE].hit, ui->skin->theme_text);
        SDL_RenderPresent(ui->ren);
        return;
    }
    if (ui->eq_open) {
        render_eq(ui, audio);
        SDL_RenderPresent(ui->ren);
        return;
    }

    SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 255);
    SDL_RenderClear(ui->ren);

    render_background(ui);

    for (int i = 0; i < BTN_COUNT; i++) render_button(ui, (ButtonId)i, audio, pl);

    double len = audio_length_seconds(audio);
    double pos = audio_position_seconds(audio);
    float pos_t = (len > 0) ? (float)(pos / len) : 0.f;
    render_slider(ui, &ui->skin->pos_slider, pos_t);
    render_slider(ui, &ui->skin->vol_slider, audio_get_volume(audio));

    render_time(ui, audio);
    render_title(ui);
    render_viz(ui, audio);
    if (ui->playlist_visible) render_playlist(ui, pl);

    SDL_RenderPresent(ui->ren);
}
