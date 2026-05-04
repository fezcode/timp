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
    skin->buttons[BTN_PREV].hit  = (SDL_Rect){ xs + 0*(w+gap), y, w, h };
    skin->buttons[BTN_PLAY].hit  = (SDL_Rect){ xs + 1*(w+gap), y, w, h };
    skin->buttons[BTN_PAUSE].hit = (SDL_Rect){ xs + 2*(w+gap), y, w, h };
    skin->buttons[BTN_STOP].hit  = (SDL_Rect){ xs + 3*(w+gap), y, w, h };
    skin->buttons[BTN_NEXT].hit  = (SDL_Rect){ xs + 4*(w+gap), y, w, h };
    skin->buttons[BTN_OPEN].hit  = (SDL_Rect){ xs + 5*(w+gap), y, w, h };
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
}

static SDL_Texture* load_texture(SDL_Renderer* ren, const char* path, int* out_w, int* out_h) {
    int w, h, ch;
    unsigned char* pixels = stbi_load(path, &w, &h, &ch, 4);
    if (!pixels) return NULL;
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

bool skin_load(Skin* skin, SDL_Renderer* ren, const char* ini_path) {
    skin_default(skin);

    Ini ini;
    if (!ini_load(&ini, ini_path)) return false;

    parse_dir(ini_path, skin->dir, sizeof(skin->dir));

    const char* name = ini_get(&ini, "meta", "name");
    if (name) snprintf(skin->name, sizeof(skin->name), "%s", name);

    skin->window_w = ini_get_int(&ini, "window", "width", skin->window_w);
    skin->window_h = ini_get_int(&ini, "window", "height", skin->window_h);

    const char* bg = ini_get(&ini, "window", "background");
    if (bg && *bg) {
        char path[520];
        snprintf(path, sizeof(path), "%s/%s", skin->dir, bg);
        skin->bg_tex = load_texture(ren, path, &skin->bg_w, &skin->bg_h);
    }

    bool any_button_in_file = false;
    for (int i = 0; i < BTN_COUNT; i++) {
        if (ini_get(&ini, btn_section[i], "hit")) { any_button_in_file = true; break; }
    }
    if (any_button_in_file) {
        for (int i = 0; i < BTN_COUNT; i++) {
            SkinButton* b = &skin->buttons[i];
            memset(b, 0, sizeof(*b));
            int x, y, w, h;
            if (ini_get_rect(&ini, btn_section[i], "hit", &x, &y, &w, &h)) {
                b->hit = (SDL_Rect){ x, y, w, h };
                b->defined = true;
                if (ini_get_rect(&ini, btn_section[i], "normal", &x, &y, &w, &h))
                    b->normal = (SDL_Rect){ x, y, w, h };
                if (ini_get_rect(&ini, btn_section[i], "pressed", &x, &y, &w, &h))
                    b->pressed = (SDL_Rect){ x, y, w, h };
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

    ini_free(&ini);
    return true;
}

void skin_destroy(Skin* skin) {
    if (skin->bg_tex) SDL_DestroyTexture(skin->bg_tex);
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
