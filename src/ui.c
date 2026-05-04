#include "ui.h"
#include "font.h"
#include "fft.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void ui_init(UI* ui, SDL_Renderer* ren, Skin* skin) {
    memset(ui, 0, sizeof(*ui));
    ui->ren = ren;
    ui->skin = skin;
    ui->pressed_btn = -1;
    ui->viz_mode = VIZ_WAVE;
    ui->spectrum_bar_count = 32;
    ui->pl_hover = -1;
    snprintf(ui->display_title, sizeof(ui->display_title), "%s", "WHAMP - DROP A FILE");
    fb_init(&ui->fb);
}

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

bool ui_take_picked_file(UI* ui, char* out, int out_size) {
    return fb_take_result(&ui->fb, out, out_size);
}

static void load_and_play(Audio* audio, const char* path) {
    if (!path) return;
    if (audio_load(audio, path)) audio_play(audio);
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
        case BTN_PLAY:  audio_play(audio); break;
        case BTN_PAUSE: audio_pause(audio); break;
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
        case BTN_SHUFFLE:
            if (pl) playlist_set_shuffle(pl, !playlist_shuffle(pl));
            break;
        case BTN_LOOP:
            if (pl) playlist_set_loop(pl, !playlist_loop(pl));
            break;
        case BTN_MIN:   act.minimize_requested = true; break;
        case BTN_CLOSE: act.quit_requested = true; break;
        default: break;
    }
    return act;
}

static bool point_in(SDL_Rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

UiAction ui_handle_event(UI* ui, const SDL_Event* e, Audio* audio, Playlist* pl) {
    UiAction act = {0};

    if (ui->fb.open) {
        fb_handle_event(&ui->fb, e, ui->skin);
        return act;
    }

    Skin* sk = ui->skin;

    // Playlist panel hover/scroll/click — handled before main button hit-testing.
    if (sk->playlist_rect.defined) {
        SDL_Rect plr = sk->playlist_rect.rect;
        SDL_Rect rows = { plr.x, plr.y + PL_HEADER_H, plr.w, plr.h - PL_HEADER_H };
        int rows_visible = pl_visible_rows(sk);

        if (e->type == SDL_MOUSEMOTION) {
            int mx = e->motion.x, my = e->motion.y;
            ui->pl_hover = -1;
            if (mx >= rows.x && mx < rows.x + rows.w && my >= rows.y && my < rows.y + rows.h) {
                int row = (my - rows.y) / PL_ROW_H;
                int idx = ui->pl_scroll + row;
                if (idx >= 0 && idx < playlist_count(pl)) ui->pl_hover = idx;
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
                if (idx >= 0 && idx < playlist_count(pl)) {
                    playlist_set_index(pl, idx);
                    const char* path = playlist_current(pl);
                    if (path && audio_load(audio, path)) audio_play(audio);
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
            case SDLK_s: audio_stop(audio); break;
            case SDLK_LEFT:  audio_seek_seconds(audio, audio_position_seconds(audio) - 5.0); break;
            case SDLK_RIGHT: audio_seek_seconds(audio, audio_position_seconds(audio) + 5.0); break;
            case SDLK_UP:   audio_set_volume(audio, audio_get_volume(audio) + 0.05f); break;
            case SDLK_DOWN: audio_set_volume(audio, audio_get_volume(audio) - 0.05f); break;
            case SDLK_v:    ui->viz_mode = (ui->viz_mode == VIZ_WAVE) ? VIZ_SPECTRUM : VIZ_WAVE; break;
            case SDLK_o:    fb_show(&ui->fb); break;
            case SDLK_l:    if (pl) playlist_set_loop(pl, !playlist_loop(pl)); break;
            case SDLK_h:    if (pl) playlist_set_shuffle(pl, !playlist_shuffle(pl)); break;
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
                  sk->theme_accent, "WHAMP");
    }
    SDL_Rect b = { 0, 0, sk->window_w, sk->window_h };
    draw_rect(ui->ren, b, (SDL_Color){0,0,0,255});
}

static void render_button(UI* ui, ButtonId id, Playlist* pl) {
    Skin* sk = ui->skin;
    SkinButton* btn = &sk->buttons[id];
    if (!btn->defined) return;

    bool pressed = (ui->pressed_btn == (int)id);
    SDL_Rect hit = btn->hit;

    if (sk->bg_tex && (btn->normal.w > 0 || btn->pressed.w > 0)) {
        SDL_Rect src = pressed && btn->pressed.w ? btn->pressed : btn->normal;
        if (src.w > 0) {
            SDL_RenderCopy(ui->ren, sk->bg_tex, &src, &hit);
            return;
        }
    }

    // procedural button: rounded-corner-ish (just rect for now) + icon overlay
    SDL_Color face = sk->theme_panel;
    SDL_Color edge = sk->theme_accent;
    if (pressed) face = dim(face, 0.55f);

    if (id != BTN_MIN && id != BTN_CLOSE) {
        fill_rect(ui->ren, hit, face);
        draw_rect(ui->ren, hit, edge);
    }

    SDL_Color icon_color = sk->theme_accent;
    if (pressed) icon_color = dim(icon_color, 0.7f);

    switch (id) {
        case BTN_PREV:    icon_prev(ui->ren, hit, icon_color); break;
        case BTN_PLAY:    icon_play(ui->ren, hit, icon_color); break;
        case BTN_PAUSE:   icon_pause(ui->ren, hit, icon_color); break;
        case BTN_STOP:    icon_stop(ui->ren, hit, icon_color); break;
        case BTN_NEXT:    icon_next(ui->ren, hit, icon_color); break;
        case BTN_OPEN:    icon_open(ui->ren, hit, icon_color); break;
        case BTN_SHUFFLE: icon_shuffle(ui->ren, hit, icon_color, pl ? playlist_shuffle(pl) : false); break;
        case BTN_LOOP:    icon_loop(ui->ren, hit, icon_color, pl ? playlist_loop(pl) : false); break;
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
    for (int row = 0; row < rows_visible; row++) {
        int idx = ui->pl_scroll + row;
        if (idx >= total) break;
        SDL_Rect rrow = { rows_rect.x, rows_rect.y + row * PL_ROW_H, rows_rect.w, PL_ROW_H };

        bool is_current = (idx == cur);
        bool is_hover   = (idx == ui->pl_hover);
        if (is_current) {
            fill_rect(ui->ren, rrow, dim(sk->theme_accent, 0.18f));
        } else if (is_hover) {
            fill_rect(ui->ren, rrow, dim(sk->theme_panel, 0.7f));
        }

        char num[8];
        snprintf(num, sizeof(num), "%2d", idx + 1);
        SDL_Color num_color = is_current ? sk->theme_accent : dim(sk->theme_text, 0.6f);
        font_draw(ui->ren, rrow.x + 4, rrow.y + (rrow.h - FONT_H) / 2, 1, num_color, num);

        const char* path = pl->paths[idx];
        const char* name = pl_basename(path);
        char shown[256];
        snprintf(shown, sizeof(shown), "%s", name);
        int max_chars = (rrow.w - 28 - 6) / (FONT_W + 1);
        if ((int)strlen(shown) > max_chars && max_chars > 3) {
            shown[max_chars - 3] = '.';
            shown[max_chars - 2] = '.';
            shown[max_chars - 1] = '.';
            shown[max_chars] = 0;
        }
        SDL_Color name_color = is_current ? sk->theme_accent : sk->theme_text;
        font_draw(ui->ren, rrow.x + 22, rrow.y + (rrow.h - FONT_H) / 2, 1, name_color, shown);
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

void ui_render(UI* ui, Audio* audio, Playlist* pl) {
    if (ui->fb.open) {
        fb_render(&ui->fb, ui->ren, ui->skin);
        SDL_RenderPresent(ui->ren);
        return;
    }

    SDL_SetRenderDrawColor(ui->ren, 0, 0, 0, 255);
    SDL_RenderClear(ui->ren);

    render_background(ui);

    for (int i = 0; i < BTN_COUNT; i++) render_button(ui, (ButtonId)i, pl);

    double len = audio_length_seconds(audio);
    double pos = audio_position_seconds(audio);
    float pos_t = (len > 0) ? (float)(pos / len) : 0.f;
    render_slider(ui, &ui->skin->pos_slider, pos_t);
    render_slider(ui, &ui->skin->vol_slider, audio_get_volume(audio));

    render_time(ui, audio);
    render_title(ui);
    render_viz(ui, audio);
    render_playlist(ui, pl);

    SDL_RenderPresent(ui->ren);
}
