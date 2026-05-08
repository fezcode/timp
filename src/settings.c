#include "settings.h"
#include "theme.h"
#include "font.h"

#include <stdio.h>
#include <string.h>

// Layout pulled from the skin's [settings] block, with built-in defaults so
// older skin.ini files keep working unchanged.
static int set_header_h(const Skin* sk) { return sk->set.header_h > 0 ? sk->set.header_h : 16; }
static int set_tab_h(const Skin* sk)    { return sk->set.tab_h    > 0 ? sk->set.tab_h    : 18; }
static int set_footer_h(const Skin* sk) { return sk->set.footer_h > 0 ? sk->set.footer_h : 22; }
static int set_row_h(const Skin* sk)    { return sk->set.row_h    > 0 ? sk->set.row_h    : 16; }

static const char* TAB_NAMES[SET_TAB_COUNT] = { "THEMES", "SKINS", "OPTIONS", "ABOUT" };

static const char* ABOUT_LINES[] = {
    "TIMP",
    "",
    "VERSION " TIMP_VERSION,
    "BUILT  " TIMP_BUILD_DATE,
    "",
    "A NATIVE CROSS-PLATFORM PLAYER WRITTEN IN C.",
    "",
    "POWERED BY:",
    "  SDL2          - WINDOW, INPUT, RENDER",
    "  MINIAUDIO     - AUDIO DECODE + OUTPUT",
    "  STB_IMAGE     - PNG/JPG/BMP LOADER",
    "",
    "AUTHOR: SAMIL BULBUL",
    "",
    "ALL CODE WRITTEN FOR THIS PROJECT - PUBLIC DOMAIN",
};

void settings_init(Settings* s) {
    memset(s, 0, sizeof(*s));
    s->hover_theme = -1;
    s->hover_skin = -1;
    s->current_skin = -1;
}

// Refresh the SKINS list and figure out which entry matches the currently-
// loaded skin so the row can be highlighted.
static void rescan_skins(Settings* s) {
    s->skin_count = skin_scan(s->skins, SET_MAX_SKINS);
    s->current_skin = -1;
    if (s->selected_skin_path[0]) {
        for (int i = 0; i < s->skin_count; i++) {
            if (strcmp(s->skins[i].path, s->selected_skin_path) == 0) {
                s->current_skin = i;
                break;
            }
        }
    }
}

void settings_show(Settings* s) {
    s->open = true;
    s->theme_changed = false;
    s->aot_changed = false;
    s->plv_changed = false;
    s->skin_changed = false;
    rescan_skins(s);
}

void settings_close(Settings* s) { s->open = false; }

static SDL_Rect tab_rect(const Skin* sk, int t) {
    int total = SET_TAB_COUNT;
    int w = (sk->window_w - 16) / total;
    SDL_Rect r = { 8 + t * w, set_header_h(sk), w - 2, set_tab_h(sk) };
    return r;
}

static SDL_Rect content_rect(const Skin* sk) {
    int hh = set_header_h(sk), th = set_tab_h(sk), ff = set_footer_h(sk);
    SDL_Rect r = { 8, hh + th + 4, sk->window_w - 16,
                   sk->window_h - hh - th - 4 - ff };
    return r;
}

static SDL_Rect close_btn_rect(const Skin* sk) {
    if (sk->set.close_btn.w > 0) return sk->set.close_btn;
    SDL_Rect r = { sk->window_w - 64, sk->window_h - set_footer_h(sk) + 4, 56, 14 };
    return r;
}

static bool point_in(SDL_Rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// --- Options tab toggle rows ---

#define OPT_COUNT 2
static const char* OPT_LABELS[OPT_COUNT] = { "ALWAYS ON TOP", "SHOW PLAYLIST" };

static SDL_Rect option_row_rect(const Skin* sk, int i) {
    SDL_Rect c = content_rect(sk);
    int rh = set_row_h(sk);
    SDL_Rect r = { c.x, c.y + i * rh, c.w, rh };
    return r;
}
static SDL_Rect option_check_rect(const Skin* sk, int i) {
    SDL_Rect r = option_row_rect(sk, i);
    SDL_Rect c = { r.x + 8, r.y + (r.h - 10) / 2, 10, 10 };
    return c;
}

// --- Themes tab list ---

static SDL_Rect theme_row_rect(const Skin* sk, int i, int scroll) {
    SDL_Rect c = content_rect(sk);
    int rh = set_row_h(sk);
    SDL_Rect r = { c.x, c.y + (i - scroll) * rh, c.w, rh };
    return r;
}

void settings_handle_event(Settings* s, const SDL_Event* e, const Skin* skin) {
    if (!s->open) return;

    if (e->type == SDL_KEYDOWN) {
        if (e->key.keysym.sym == SDLK_ESCAPE) { settings_close(s); return; }
    }

    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        int mx = e->button.x, my = e->button.y;

        if (point_in(close_btn_rect(skin), mx, my)) { settings_close(s); return; }
        for (int t = 0; t < SET_TAB_COUNT; t++) {
            if (point_in(tab_rect(skin, t), mx, my)) { s->tab = (SetTab)t; return; }
        }

        if (s->tab == SET_TAB_OPTIONS) {
            for (int i = 0; i < OPT_COUNT; i++) {
                if (point_in(option_row_rect(skin, i), mx, my)) {
                    if (i == 0) { s->always_on_top = !s->always_on_top; s->aot_changed = true; }
                    else if (i == 1) { s->playlist_visible = !s->playlist_visible; s->plv_changed = true; }
                    return;
                }
            }
        } else if (s->tab == SET_TAB_THEMES) {
            SDL_Rect c = content_rect(skin);
            int rows_visible = c.h / set_row_h(skin);
            for (int i = s->theme_scroll; i < theme_count() && i < s->theme_scroll + rows_visible; i++) {
                if (point_in(theme_row_rect(skin, i, s->theme_scroll), mx, my)) {
                    s->current_theme = i;
                    s->theme_changed = true;
                    return;
                }
            }
        } else if (s->tab == SET_TAB_SKINS) {
            SDL_Rect c = content_rect(skin);
            int rh = set_row_h(skin);
            int rows_visible = c.h / rh;
            for (int i = s->skin_scroll; i < s->skin_count && i < s->skin_scroll + rows_visible; i++) {
                SDL_Rect rr = { c.x, c.y + (i - s->skin_scroll) * rh, c.w, rh };
                if (point_in(rr, mx, my)) {
                    s->current_skin = i;
                    snprintf(s->selected_skin_path, sizeof(s->selected_skin_path),
                             "%s", s->skins[i].path);
                    s->skin_changed = true;
                    return;
                }
            }
        }
    } else if (e->type == SDL_MOUSEMOTION) {
        int mx = e->motion.x, my = e->motion.y;
        s->hover_theme = -1;
        s->hover_skin = -1;
        SDL_Rect c = content_rect(skin);
        if (mx >= c.x && mx < c.x + c.w && my >= c.y && my < c.y + c.h) {
            int row = (my - c.y) / set_row_h(skin);
            if (s->tab == SET_TAB_THEMES) {
                int idx = s->theme_scroll + row;
                if (idx >= 0 && idx < theme_count()) s->hover_theme = idx;
            } else if (s->tab == SET_TAB_SKINS) {
                int idx = s->skin_scroll + row;
                if (idx >= 0 && idx < s->skin_count) s->hover_skin = idx;
            }
        }
    } else if (e->type == SDL_MOUSEWHEEL) {
        SDL_Rect c = content_rect(skin);
        int rows_visible = c.h / set_row_h(skin);
        if (s->tab == SET_TAB_THEMES) {
            int max_scroll = theme_count() - rows_visible;
            if (max_scroll < 0) max_scroll = 0;
            s->theme_scroll -= e->wheel.y * 2;
            if (s->theme_scroll < 0) s->theme_scroll = 0;
            if (s->theme_scroll > max_scroll) s->theme_scroll = max_scroll;
        } else if (s->tab == SET_TAB_SKINS) {
            int max_scroll = s->skin_count - rows_visible;
            if (max_scroll < 0) max_scroll = 0;
            s->skin_scroll -= e->wheel.y * 2;
            if (s->skin_scroll < 0) s->skin_scroll = 0;
            if (s->skin_scroll > max_scroll) s->skin_scroll = max_scroll;
        }
    }
}

static void fill(SDL_Renderer* r, SDL_Rect rect, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rect);
}
static void stroke(SDL_Renderer* r, SDL_Rect rect, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(r, &rect);
}
static SDL_Color dim_c(SDL_Color c, float k) {
    SDL_Color o = { (Uint8)(c.r * k), (Uint8)(c.g * k), (Uint8)(c.b * k), c.a };
    return o;
}

static void render_themes_tab(Settings* s, SDL_Renderer* ren, const Skin* skin) {
    SDL_Rect c = content_rect(skin);
    fill(ren, c, (SDL_Color){ 6, 10, 14, 255 });
    stroke(ren, c, dim_c(skin->theme_accent, 0.4f));

    int rows_visible = c.h / set_row_h(skin);
    if (s->theme_scroll < 0) s->theme_scroll = 0;
    int max_scroll = theme_count() - rows_visible;
    if (max_scroll < 0) max_scroll = 0;
    if (s->theme_scroll > max_scroll) s->theme_scroll = max_scroll;

    for (int i = s->theme_scroll; i < theme_count() && i - s->theme_scroll < rows_visible; i++) {
        SDL_Rect r = theme_row_rect(skin, i, s->theme_scroll);
        const Theme* t = theme_at(i);
        bool is_current = (i == s->current_theme);
        bool is_hover   = (i == s->hover_theme);

        if (is_current) fill(ren, r, dim_c(skin->theme_accent, 0.18f));
        else if (is_hover) fill(ren, r, dim_c(skin->theme_panel, 0.7f));

        // swatches
        int sx = r.x + 8;
        int sy = r.y + (r.h - 8) / 2;
        SDL_Color swatches[4] = { t->bg, t->panel, t->accent, t->text };
        for (int j = 0; j < 4; j++) {
            SDL_Rect sw = { sx + j * 10, sy, 8, 8 };
            fill(ren, sw, swatches[j]);
            stroke(ren, sw, (SDL_Color){0,0,0,255});
        }

        SDL_Color name_c = is_current ? skin->theme_accent : skin->theme_text;
        font_draw(ren, sx + 50, r.y + (r.h - FONT_H) / 2, 1, name_c, t->name);
    }
}

static void render_skins_tab(Settings* s, SDL_Renderer* ren, const Skin* skin) {
    SDL_Rect c = content_rect(skin);
    fill(ren, c, (SDL_Color){ 6, 10, 14, 255 });
    stroke(ren, c, dim_c(skin->theme_accent, 0.4f));

    if (s->skin_count == 0) {
        font_draw(ren, c.x + 8, c.y + 8, 1, skin->theme_text,
                  "NO SKINS FOUND IN ./SKINS/");
        return;
    }

    int rh = set_row_h(skin);
    int rows_visible = c.h / rh;
    if (s->skin_scroll < 0) s->skin_scroll = 0;
    int max_scroll = s->skin_count - rows_visible;
    if (max_scroll < 0) max_scroll = 0;
    if (s->skin_scroll > max_scroll) s->skin_scroll = max_scroll;

    for (int i = s->skin_scroll; i < s->skin_count && i - s->skin_scroll < rows_visible; i++) {
        SDL_Rect r = { c.x, c.y + (i - s->skin_scroll) * rh, c.w, rh };
        bool is_current = (i == s->current_skin);
        bool is_hover   = (i == s->hover_skin);
        if (is_current)    fill(ren, r, dim_c(skin->theme_accent, 0.18f));
        else if (is_hover) fill(ren, r, dim_c(skin->theme_panel, 0.7f));

        // tag prefix marks the active skin
        const char* tag = is_current ? " * " : "   ";
        SDL_Color name_c = is_current ? skin->theme_accent : skin->theme_text;
        font_draw(ren, r.x + 8,  r.y + (r.h - FONT_H) / 2, 1, name_c, tag);
        font_draw(ren, r.x + 32, r.y + (r.h - FONT_H) / 2, 1, name_c, s->skins[i].name);
    }
}

static void render_options_tab(Settings* s, SDL_Renderer* ren, const Skin* skin) {
    SDL_Rect c = content_rect(skin);
    fill(ren, c, (SDL_Color){ 6, 10, 14, 255 });
    stroke(ren, c, dim_c(skin->theme_accent, 0.4f));

    bool vals[OPT_COUNT] = { s->always_on_top, s->playlist_visible };
    for (int i = 0; i < OPT_COUNT; i++) {
        SDL_Rect row = option_row_rect(skin, i);
        SDL_Rect chk = option_check_rect(skin, i);
        SDL_Color edge = dim_c(skin->theme_accent, 0.6f);
        fill(ren, chk, vals[i] ? skin->theme_accent : (SDL_Color){10,14,18,255});
        stroke(ren, chk, edge);
        if (vals[i]) {
            // little X check mark (clear inside)
            SDL_SetRenderDrawColor(ren, skin->theme_bg.r, skin->theme_bg.g, skin->theme_bg.b, 255);
            SDL_RenderDrawLine(ren, chk.x + 2, chk.y + 4, chk.x + 4, chk.y + 6);
            SDL_RenderDrawLine(ren, chk.x + 4, chk.y + 6, chk.x + 8, chk.y + 2);
        }
        font_draw(ren, chk.x + 18, row.y + (row.h - FONT_H) / 2, 1, skin->theme_text, OPT_LABELS[i]);
    }
}

static void render_about_tab(Settings* s, SDL_Renderer* ren, const Skin* skin) {
    (void)s;
    SDL_Rect c = content_rect(skin);
    fill(ren, c, (SDL_Color){ 6, 10, 14, 255 });
    stroke(ren, c, dim_c(skin->theme_accent, 0.4f));

    int n_lines = (int)(sizeof(ABOUT_LINES) / sizeof(ABOUT_LINES[0]));
    for (int i = 0; i < n_lines; i++) {
        const char* line = ABOUT_LINES[i];
        SDL_Color color = (i == 0) ? skin->theme_accent : skin->theme_text;
        int scale = (i == 0) ? 2 : 1;
        font_draw(ren, c.x + 12, c.y + 8 + i * 11, scale, color, line);
    }
}

void settings_render(Settings* s, SDL_Renderer* ren, const Skin* skin) {
    if (!s->open) return;

    SDL_Rect full = { 0, 0, skin->window_w, skin->window_h };
    fill(ren, full, skin->theme_bg);

    // Header
    SDL_Rect hdr = { 0, 0, skin->window_w, set_header_h(skin) };
    fill(ren, hdr, dim_c(skin->theme_panel, 0.8f));
    font_draw(ren, 8, 4, 1, skin->theme_accent, "SETTINGS");

    // Tabs
    for (int t = 0; t < SET_TAB_COUNT; t++) {
        SDL_Rect r = tab_rect(skin, t);
        bool active = (t == s->tab);
        if (active) {
            fill(ren, r, dim_c(skin->theme_accent, 0.25f));
            stroke(ren, r, skin->theme_accent);
        } else {
            fill(ren, r, dim_c(skin->theme_panel, 0.7f));
            stroke(ren, r, dim_c(skin->theme_accent, 0.3f));
        }
        int tw = font_text_width(1, TAB_NAMES[t]);
        font_draw(ren, r.x + (r.w - tw) / 2, r.y + (r.h - FONT_H) / 2, 1,
                  active ? skin->theme_accent : skin->theme_text, TAB_NAMES[t]);
    }

    if (s->tab == SET_TAB_THEMES)       render_themes_tab(s, ren, skin);
    else if (s->tab == SET_TAB_SKINS)   render_skins_tab(s, ren, skin);
    else if (s->tab == SET_TAB_OPTIONS) render_options_tab(s, ren, skin);
    else if (s->tab == SET_TAB_ABOUT)   render_about_tab(s, ren, skin);

    // Footer + close button
    int ff = set_footer_h(skin);
    SDL_Rect footer = { 0, skin->window_h - ff, skin->window_w, ff };
    fill(ren, footer, dim_c(skin->theme_panel, 0.8f));

    SDL_Rect cb = close_btn_rect(skin);
    fill(ren, cb, skin->theme_bg);
    stroke(ren, cb, skin->theme_accent);
    int tw = font_text_width(1, "CLOSE");
    font_draw(ren, cb.x + (cb.w - tw) / 2, cb.y + (cb.h - FONT_H) / 2, 1,
              skin->theme_text, "CLOSE");
}
