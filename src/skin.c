#include "skin.h"
#include "ini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/stb_image.h"

static const char* btn_section[BTN_COUNT] = {
    "button.prev", "button.play", "button.pause",
    "button.stop", "button.next", "button.open",
    "button.eq", "button.shuffle", "button.loop",
    "button.settings", "button.min", "button.close"
};

static SDL_Color make_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    SDL_Color c = { r, g, b, a };
    return c;
}

void skin_default(Skin* skin) {
    memset(skin, 0, sizeof(*skin));
    snprintf(skin->name, sizeof(skin->name), "%s", "builtin");
    skin->window_w = 300;
    skin->window_h = 188;
    skin->compact_h = 88;
    skin->modal_w   = 520;
    skin->modal_h   = 380;

    // Title bar / drag region (settings + min + close on the right)
    skin->drag_region.rect = (SDL_Rect){ 0, 0, 252, 14 };
    skin->drag_region.defined = true;

    skin->buttons[BTN_SETTINGS].hit = (SDL_Rect){ 252, 2, 14, 10 };
    skin->buttons[BTN_MIN].hit      = (SDL_Rect){ 268, 2, 14, 10 };
    skin->buttons[BTN_CLOSE].hit    = (SDL_Rect){ 284, 2, 14, 10 };

    // Display elements
    skin->title.rect      = (SDL_Rect){ 6, 16, 288, 9 };
    skin->title.color     = make_color(80, 255, 130, 255);
    skin->title.defined   = true;

    skin->time_disp.rect  = (SDL_Rect){ 6, 28, 52, 11 };
    skin->time_disp.color = make_color(80, 255, 130, 255);
    skin->time_disp.defined = true;

    skin->viz.rect        = (SDL_Rect){ 60, 28, 234, 11 };
    skin->viz.color       = make_color(80, 255, 130, 255);
    skin->viz.defined     = true;

    skin->pos_slider.rect = (SDL_Rect){ 6, 42, 288, 6 };
    skin->pos_slider.color = make_color(80, 255, 130, 255);
    skin->pos_slider.defined = true;

    skin->vol_slider.rect = (SDL_Rect){ 6, 52, 160, 6 };
    skin->vol_slider.color = make_color(80, 255, 130, 255);
    skin->vol_slider.defined = true;

    // Transport row
    int y = 62, h = 22, w = 28, gap = 2;
    int xs = 6;
    // Play and pause are merged into a single state-driven BTN_PLAY button.
    skin->buttons[BTN_PREV].hit  = (SDL_Rect){ xs + 0*(w+gap), y, w, h };
    skin->buttons[BTN_PLAY].hit  = (SDL_Rect){ xs + 1*(w+gap), y, w, h };
    skin->buttons[BTN_STOP].hit  = (SDL_Rect){ xs + 2*(w+gap), y, w, h };
    skin->buttons[BTN_NEXT].hit  = (SDL_Rect){ xs + 3*(w+gap), y, w, h };
    skin->buttons[BTN_OPEN].hit  = (SDL_Rect){ xs + 4*(w+gap), y, w, h };
    skin->buttons[BTN_PAUSE].defined = false;  // legacy slot, kept in enum for skin compat
    // Right-side cluster: EQ, SHUFFLE, LOOP — three same-width buttons against the right edge
    skin->buttons[BTN_EQ].hit      = (SDL_Rect){ skin->window_w - 6 - 3*(w+gap) + gap, y, w, h };
    skin->buttons[BTN_SHUFFLE].hit = (SDL_Rect){ skin->window_w - 6 - 2*(w+gap) + gap, y, w, h };
    skin->buttons[BTN_LOOP].hit    = (SDL_Rect){ skin->window_w - 6 - (w+gap) + gap, y, w, h };
    for (int i = 0; i < BTN_COUNT; i++) {
        if (skin->buttons[i].hit.w > 0) skin->buttons[i].defined = true;
    }

    // Playlist panel
    skin->playlist_rect.rect    = (SDL_Rect){ 6, 90, 288, 92 };
    skin->playlist_rect.color   = make_color(80, 255, 130, 255);
    skin->playlist_rect.defined = true;

    skin->theme_bg     = make_color(14, 18, 24, 255);
    skin->theme_panel  = make_color(28, 36, 46, 255);
    skin->theme_accent = make_color(80, 255, 130, 255);
    skin->theme_text   = make_color(200, 220, 230, 255);

    // Font: built-in bitmap (sprite-sheet override is opt-in via skin.ini).
    skin->font.tex = NULL;
    skin->font.cell_w = 5;
    skin->font.cell_h = 7;
    skin->font.draw_w = 5;
    skin->font.draw_h = 7;
    skin->font.cols = 16;
    skin->font.rows = 6;
    skin->font.first_char = 32;
    skin->font.gap_x = 1;

    // File browser layout (matches what the renderer used to hardcode).
    skin->fb.header_h  = 22;
    skin->fb.footer_h  = 22;
    skin->fb.row_h     = 14;
    skin->fb.open_btn   = (SDL_Rect){ 0, 0, 0, 0 };  // 0 → derived from modal_w
    skin->fb.cancel_btn = (SDL_Rect){ 0, 0, 0, 0 };

    // Settings layout.
    skin->set.header_h = 16;
    skin->set.tab_h    = 18;
    skin->set.footer_h = 22;
    skin->set.row_h    = 16;
    skin->set.close_btn = (SDL_Rect){ 0, 0, 0, 0 };

    // EQ panel layout.
    skin->eq.slider_top    = 34;
    skin->eq.slider_bottom = 148;
    skin->eq.slider_w      = 8;
    skin->eq.track_w       = 4;
    skin->eq.title_y       = 20;
    skin->eq.label_y       = 154;   // slider_bottom + 6
    skin->eq.readout_y     = 164;   // slider_bottom + 16
    skin->eq.onoff_btn = (SDL_Rect){ 0, 0, 0, 0 };
    skin->eq.flat_btn  = (SDL_Rect){ 0, 0, 0, 0 };
    skin->eq.back_btn  = (SDL_Rect){ 0, 0, 0, 0 };

    // Playlist panel layout.
    skin->pl.header_h = 12;
    skin->pl.row_h    = 11;
}

// Loads a PNG/BMP/JPEG into an RGBA streaming texture. For glyph sheets we
// also normalize: pixel alpha = max(R,G,B) so plain black-on-white sheets
// (no alpha channel) still render correctly when tinted.
static SDL_Texture* load_texture(SDL_Renderer* ren, const char* path,
                                 int* out_w, int* out_h, bool as_mask) {
    int w, h, ch;
    unsigned char* pixels = stbi_load(path, &w, &h, &ch, 4);
    if (!pixels) return NULL;
    if (as_mask) {
        for (int i = 0; i < w * h; i++) {
            unsigned char r = pixels[i*4 + 0];
            unsigned char g = pixels[i*4 + 1];
            unsigned char b = pixels[i*4 + 2];
            unsigned char a = pixels[i*4 + 3];
            unsigned char lum = r > g ? r : g;
            if (b > lum) lum = b;
            // If the source had an alpha channel, prefer it when it's nonzero;
            // otherwise treat luminance as the mask.
            unsigned char mask = (a > 0 && a < 255) ? a : lum;
            pixels[i*4 + 0] = 255;
            pixels[i*4 + 1] = 255;
            pixels[i*4 + 2] = 255;
            pixels[i*4 + 3] = mask;
        }
    }
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) { stbi_image_free(pixels); return NULL; }
    SDL_UpdateTexture(tex, NULL, pixels, w * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    stbi_image_free(pixels);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return tex;
}

static void parse_dir(const char* ini_path, char* out_dir, size_t out_size) {
    snprintf(out_dir, out_size, "%s", ini_path);
    char* sep = strrchr(out_dir, '/');
    char* sep2 = strrchr(out_dir, '\\');
    if (sep2 > sep) sep = sep2;
    if (sep) *sep = 0;
    else snprintf(out_dir, out_size, ".");
}

static SDL_Texture* load_relative(SDL_Renderer* ren, const Skin* skin,
                                  const char* relpath, int* out_w, int* out_h,
                                  bool as_mask) {
    char path[520];
    snprintf(path, sizeof(path), "%s/%s", skin->dir, relpath);
    return load_texture(ren, path, out_w, out_h, as_mask);
}

static void load_button(const Ini* ini, SkinButton* b, const char* sec) {
    int x, y, w, h;
    if (ini_get_rect(ini, sec, "hit", &x, &y, &w, &h)) {
        b->hit = (SDL_Rect){ x, y, w, h };
        b->defined = true;
    }
    if (ini_get_rect(ini, sec, "normal", &x, &y, &w, &h)) {
        b->normal = (SDL_Rect){ x, y, w, h };
    }
    if (ini_get_rect(ini, sec, "pressed", &x, &y, &w, &h)) {
        b->pressed = (SDL_Rect){ x, y, w, h };
        b->has_pressed = true;
    }
    if (ini_get_rect(ini, sec, "hover", &x, &y, &w, &h)) {
        b->hover = (SDL_Rect){ x, y, w, h };
        b->has_hover = true;
    }
}

bool skin_load(Skin* skin, SDL_Renderer* ren, const char* ini_path) {
    skin_default(skin);

    Ini ini;
    if (!ini_load(&ini, ini_path)) return false;

    parse_dir(ini_path, skin->dir, sizeof(skin->dir));

    const char* name = ini_get(&ini, "meta", "name");
    if (name) snprintf(skin->name, sizeof(skin->name), "%s", name);

    skin->window_w  = ini_get_int(&ini, "window", "width",  skin->window_w);
    skin->window_h  = ini_get_int(&ini, "window", "height", skin->window_h);
    skin->compact_h = ini_get_int(&ini, "window", "compact_height", skin->compact_h);
    skin->modal_w   = ini_get_int(&ini, "window", "modal_width",  skin->modal_w);
    skin->modal_h   = ini_get_int(&ini, "window", "modal_height", skin->modal_h);

    const char* bg = ini_get(&ini, "window", "background");
    if (bg && *bg) {
        skin->bg_tex = load_relative(ren, skin, bg, &skin->bg_w, &skin->bg_h, false);
    }

    // Shared transport-button sprite sheet (optional). Per-button rects refer
    // into this texture unless that button has its own [button.X] sheet=...
    const char* btn_sheet = ini_get(&ini, "buttons", "sheet");
    if (btn_sheet && *btn_sheet) {
        int bw, bh;
        skin->btn_sheet = load_relative(ren, skin, btn_sheet, &bw, &bh, false);
    }

    // Buttons: only override defaults when the file actually defines any.
    bool any_button_in_file = false;
    for (int i = 0; i < BTN_COUNT; i++) {
        if (ini_get(&ini, btn_section[i], "hit")) { any_button_in_file = true; break; }
    }
    if (any_button_in_file) {
        for (int i = 0; i < BTN_COUNT; i++) {
            SkinButton* b = &skin->buttons[i];
            SDL_Texture* prev_sheet = b->sheet;
            memset(b, 0, sizeof(*b));
            b->sheet = prev_sheet;
            load_button(&ini, b, btn_section[i]);
            const char* sheet = ini_get(&ini, btn_section[i], "sheet");
            if (sheet && *sheet) {
                int sw, sh;
                b->sheet = load_relative(ren, skin, sheet, &sw, &sh, false);
            }
        }
    }

    struct { const char* sec; SkinElement* el; } els[] = {
        { "display.title", &skin->title },
        { "display.time", &skin->time_disp },
        { "viz", &skin->viz },
        { "slider.position", &skin->pos_slider },
        { "slider.volume", &skin->vol_slider },
        { "drag", &skin->drag_region },
        { "playlist", &skin->playlist_rect },
    };
    for (size_t i = 0; i < sizeof(els)/sizeof(els[0]); i++) {
        int x, y, w, h;
        if (ini_get_rect(&ini, els[i].sec, "rect", &x, &y, &w, &h)) {
            els[i].el->rect = (SDL_Rect){ x, y, w, h };
            els[i].el->defined = true;
        }
        unsigned char r, g, b;
        if (ini_get_color(&ini, els[i].sec, "color", &r, &g, &b)) {
            els[i].el->color = make_color(r, g, b, 255);
        }
    }

    unsigned char r, g, b;
    if (ini_get_color(&ini, "theme", "background", &r, &g, &b))
        skin->theme_bg = make_color(r, g, b, 255);
    if (ini_get_color(&ini, "theme", "panel", &r, &g, &b))
        skin->theme_panel = make_color(r, g, b, 255);
    if (ini_get_color(&ini, "theme", "accent", &r, &g, &b))
        skin->theme_accent = make_color(r, g, b, 255);
    if (ini_get_color(&ini, "theme", "text", &r, &g, &b))
        skin->theme_text = make_color(r, g, b, 255);

    // Optional bitmap font sprite sheet.
    const char* font_sheet = ini_get(&ini, "font", "sheet");
    if (font_sheet && *font_sheet) {
        int fw, fh;
        skin->font.tex = load_relative(ren, skin, font_sheet, &fw, &fh, true);
        skin->font.cell_w     = ini_get_int(&ini, "font", "cell_w",     skin->font.cell_w);
        skin->font.cell_h     = ini_get_int(&ini, "font", "cell_h",     skin->font.cell_h);
        skin->font.cols       = ini_get_int(&ini, "font", "cols",       skin->font.cols);
        skin->font.rows       = ini_get_int(&ini, "font", "rows",       skin->font.rows);
        skin->font.first_char = ini_get_int(&ini, "font", "first_char", skin->font.first_char);
        skin->font.gap_x      = ini_get_int(&ini, "font", "gap_x",      skin->font.gap_x);
        skin->font.draw_w     = ini_get_int(&ini, "font", "draw_w",     skin->font.cell_w);
        skin->font.draw_h     = ini_get_int(&ini, "font", "draw_h",     skin->font.cell_h);
    }

    // File browser layout overrides (all optional).
    skin->fb.header_h = ini_get_int(&ini, "filebrowser", "header_h", skin->fb.header_h);
    skin->fb.footer_h = ini_get_int(&ini, "filebrowser", "footer_h", skin->fb.footer_h);
    skin->fb.row_h    = ini_get_int(&ini, "filebrowser", "row_h",    skin->fb.row_h);
    int x, y, w, h;
    if (ini_get_rect(&ini, "filebrowser", "open_btn", &x, &y, &w, &h))
        skin->fb.open_btn = (SDL_Rect){ x, y, w, h };
    if (ini_get_rect(&ini, "filebrowser", "cancel_btn", &x, &y, &w, &h))
        skin->fb.cancel_btn = (SDL_Rect){ x, y, w, h };

    // Settings modal layout overrides.
    skin->set.header_h = ini_get_int(&ini, "settings", "header_h", skin->set.header_h);
    skin->set.tab_h    = ini_get_int(&ini, "settings", "tab_h",    skin->set.tab_h);
    skin->set.footer_h = ini_get_int(&ini, "settings", "footer_h", skin->set.footer_h);
    skin->set.row_h    = ini_get_int(&ini, "settings", "row_h",    skin->set.row_h);
    if (ini_get_rect(&ini, "settings", "close_btn", &x, &y, &w, &h))
        skin->set.close_btn = (SDL_Rect){ x, y, w, h };

    // EQ layout overrides.
    skin->eq.slider_top    = ini_get_int(&ini, "eq", "slider_top",    skin->eq.slider_top);
    skin->eq.slider_bottom = ini_get_int(&ini, "eq", "slider_bottom", skin->eq.slider_bottom);
    skin->eq.slider_w      = ini_get_int(&ini, "eq", "slider_w",      skin->eq.slider_w);
    skin->eq.track_w       = ini_get_int(&ini, "eq", "track_w",       skin->eq.track_w);
    skin->eq.title_y       = ini_get_int(&ini, "eq", "title_y",       skin->eq.title_y);
    skin->eq.label_y       = ini_get_int(&ini, "eq", "label_y",       skin->eq.label_y);
    skin->eq.readout_y     = ini_get_int(&ini, "eq", "readout_y",     skin->eq.readout_y);
    if (ini_get_rect(&ini, "eq", "onoff_btn", &x, &y, &w, &h))
        skin->eq.onoff_btn = (SDL_Rect){ x, y, w, h };
    if (ini_get_rect(&ini, "eq", "flat_btn", &x, &y, &w, &h))
        skin->eq.flat_btn = (SDL_Rect){ x, y, w, h };
    if (ini_get_rect(&ini, "eq", "back_btn", &x, &y, &w, &h))
        skin->eq.back_btn = (SDL_Rect){ x, y, w, h };

    // Playlist layout overrides.
    skin->pl.header_h = ini_get_int(&ini, "playlist", "header_h", skin->pl.header_h);
    skin->pl.row_h    = ini_get_int(&ini, "playlist", "row_h",    skin->pl.row_h);

    ini_free(&ini);
    return true;
}

void skin_destroy(Skin* skin) {
    if (skin->bg_tex)     SDL_DestroyTexture(skin->bg_tex);
    if (skin->btn_sheet)  SDL_DestroyTexture(skin->btn_sheet);
    if (skin->font.tex)   SDL_DestroyTexture(skin->font.tex);
    for (int i = 0; i < BTN_COUNT; i++) {
        SDL_Texture* s = skin->buttons[i].sheet;
        if (!s) continue;
        if (s == skin->btn_sheet || s == skin->bg_tex) continue;
        // Avoid double-free if multiple buttons happen to share the same loaded sheet.
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (skin->buttons[j].sheet == s) { dup = true; break; }
        }
        if (!dup) SDL_DestroyTexture(s);
    }
    memset(skin, 0, sizeof(*skin));
}

int skin_button_at(const Skin* skin, int x, int y) {
    for (int i = 0; i < BTN_COUNT; i++) {
        const SkinButton* b = &skin->buttons[i];
        if (!b->defined) continue;
        if (x >= b->hit.x && x < b->hit.x + b->hit.w &&
            y >= b->hit.y && y < b->hit.y + b->hit.h) return i;
    }
    return -1;
}
