#ifndef TIMP_SKIN_H
#define TIMP_SKIN_H

#include <stdbool.h>
#include <SDL2/SDL.h>

typedef enum {
    BTN_PREV = 0,
    BTN_PLAY,
    BTN_PAUSE,
    BTN_STOP,
    BTN_NEXT,
    BTN_OPEN,
    BTN_EQ,
    BTN_SHUFFLE,
    BTN_LOOP,
    BTN_SETTINGS,
    BTN_MIN,
    BTN_CLOSE,
    BTN_COUNT
} ButtonId;

typedef struct {
    SDL_Rect hit;
    SDL_Rect normal;       // sprite source rect inside this button's sheet
    SDL_Rect pressed;
    SDL_Rect hover;        // optional, used when set
    SDL_Texture* sheet;    // per-button sprite sheet (NULL = use shared/procedural)
    bool defined;
    bool has_pressed;
    bool has_hover;
} SkinButton;

typedef struct {
    SDL_Rect rect;
    SDL_Color color;
    bool defined;
} SkinElement;

// Optional bitmap font sheet. When tex is NULL, the built-in 5x7 font is used.
// Sheet layout: glyphs in a cols x rows grid, indexed left-to-right, top-to-bottom
// starting at ASCII `first_char`. Each cell is `cell_w x cell_h`. Pixels with any
// luminance count as "ink" and are tinted to the requested text color.
typedef struct {
    SDL_Texture* tex;
    int cell_w, cell_h;
    int cols, rows;
    int first_char;
    int gap_x;             // spacing added between glyphs when drawing
    int draw_w, draw_h;    // size each glyph is drawn at (defaults to cell_w/cell_h)
} SkinFont;

// File browser modal layout. All values default to the current built-in look.
typedef struct {
    int header_h;          // top bar with "OPEN AUDIO" + path row
    int footer_h;          // bottom strip with OPEN/CANCEL
    int row_h;             // height of each entry row
    SDL_Rect open_btn;     // explicit override; if w==0, derived from window size
    SDL_Rect cancel_btn;
} SkinFb;

// Settings modal layout.
typedef struct {
    int header_h;
    int tab_h;
    int footer_h;
    int row_h;
    SDL_Rect close_btn;
} SkinSet;

// EQ panel layout.
typedef struct {
    int slider_top;
    int slider_bottom;
    int slider_w;
    int track_w;
    int label_y;           // y of frequency labels under each slider
    int readout_y;         // y of dB readouts
    int title_y;           // y of "EQUALIZER" label
    SDL_Rect onoff_btn;
    SDL_Rect flat_btn;
    SDL_Rect back_btn;
} SkinEq;

// Playlist panel layout knobs.
typedef struct {
    int header_h;
    int row_h;
} SkinPl;

typedef struct Skin {
    char name[64];
    char dir[260];

    int window_w, window_h;
    int compact_h;         // window height when playlist is hidden
    int modal_w, modal_h;  // window size while file-browser / settings is open

    SDL_Texture* bg_tex;
    int bg_w, bg_h;
    SDL_Texture* btn_sheet;     // optional shared transport-button sheet

    SkinButton buttons[BTN_COUNT];

    SkinElement title;
    SkinElement time_disp;
    SkinElement viz;
    SkinElement pos_slider;
    SkinElement vol_slider;
    SkinElement drag_region;
    SkinElement playlist_rect;

    SDL_Color theme_bg;
    SDL_Color theme_panel;
    SDL_Color theme_accent;
    SDL_Color theme_text;

    SkinFont font;
    SkinFb   fb;
    SkinSet  set;
    SkinEq   eq;
    SkinPl   pl;
} Skin;

bool skin_load(Skin* skin, SDL_Renderer* ren, const char* ini_path);
void skin_default(Skin* skin);
void skin_destroy(Skin* skin);

int skin_button_at(const Skin* skin, int x, int y);

#endif
