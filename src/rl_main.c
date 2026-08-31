// Timp — raylib rewrite. Hi-Fi / Audiophile look.
// Graphics via raylib (supersampled for AA); audio via the project's miniaudio
// engine (audio.c): FLAC/MP3/OGG/WAV + Unicode paths. Reuses fft/eq/playlist.
#include "raylib.h"
#include "rlgl.h"
#include "audio.h"
#include "art.h"
#include "fft.h"
#include "eq.h"
#include "playlist.h"
#include "playlistio.h"
#include "osdialog.h"
#include "tags.h"
#include "lyrics.h"
#include "rlconfig.h"
#include "mediakeys.h"
#include "singleinst.h"
#include "menubar.h"
#include "icon.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define WW   440
#define WH   716
#define SS   2          // supersample factor
#define PAD  20
#define TBH  40         // top bar height
#define ARTS (WW - 2 * PAD)
#define DRAWER_W 320           // playlist drawer width (logical px); window grows by this
#define WMAXW (WW + DRAWER_W)  // render target width covers player + drawer
#define TIMP_VERSION "0.12.0"  // keep in sync with forge.toml

// ---------- palette ----------
static const Color BG0 = { 24, 21, 17, 255 };
static const Color BG1 = { 12, 11,  8, 255 };
static const Color TXT = { 236, 227, 207, 255 };
static const Color MUT = { 150, 139, 114, 255 };
static const Color TRK = { 44, 39, 30, 255 };
static const Color CARDBG = { 20, 18, 14, 255 };   // panel/card interior

// ---------- state ----------
static Audio    *g_audio = NULL;
static Texture2D g_cover;
static bool      g_has_cover = false;
static Color     g_accent = { 201, 164, 90, 255 };
static char      g_title[256] = "Drop a track to begin";
static char      g_meta[256]  = "";   // sized to match Tags.artist (avoids snprintf truncation)
static char      g_album[256] = "";   // published to the OS now-playing session only
static char      g_fmt[32]    = "";
static Playlist  g_pl;
static bool      g_show_queue = false;
static bool      g_show_eq = false;
static int       g_eq_drag = -1;
static bool      g_show_settings = false;
static bool      g_show_lyrics = false;
static bool      g_lyrics_fetching = false;
static int       g_lyrics_scroll = 0;
static Lyrics    g_lyrics;
static float     g_premute = 0.7f;
static int       g_repeat = 0;          // 0 off · 1 one · 2 all
static int       g_prev_mode = 0;       // 0 smart (>5s restarts, else prev) · 1 direct (always prev)
static int       g_queue_scroll = 0;
static int       g_art_mode = 0;        // 0 cover · 1 bars · 2 wave

// ---- playlist drawer (slides out the side, extending the window) ----
static int       g_side = 0;            // 0 = right · 1 = left (persisted)
static float     g_drawer_anim = 0.0f;  // 0 closed .. 1 fully open
static int       g_base_x = 0, g_base_y = 0;   // closed-window top-left
static int       g_drawer_view = 0;     // 0 = playlist · 1 = saved-playlist library · 2 = queue
static bool      g_naming = false;      // typing a name for a new playlist
static char      g_name_buf[PL_NAME_MAX] = "";
static bool      g_confirm_overwrite = false;  // "overwrite existing?" prompt
static char      g_saved_names[PL_MAX_SAVED][PL_NAME_MAX];
static int       g_saved_count = 0;
static char      g_search[64] = "";     // drawer search filter (cleared on tab switch)
static bool      g_search_focus = false;
static int      *g_filt = NULL;         // visible row → playlist index (tab 0) / queue pos (tab 2)
static int       g_filt_cap = 0;
static float     g_bars[64], g_peaks[64];
static const int NBARS = 48;
static bool      g_aot = false;           // always on top (persisted)
static bool      g_quit = false;          // the Hisashi menu's Quit; leaves the main loop like the × button
static bool      g_hisashi_menubar = true;  // publish the menus to Hisashi's menubar (persisted)
static char      g_data_dir[600];         // %APPDATA%\fezcode\Timp (config + Playlists)
static Font fTitle, fMeta, fSmall, fEye;

// hover animation values
enum { HV_OPEN, HV_QUEUE, HV_EQ, HV_SET, HV_LYR, HV_MIN, HV_CLOSE, HV_PLAY, HV_PREV, HV_NEXT, HV_SHUF, HV_REP, HV_ART, HV_N };
static float g_hv[HV_N];

// queue interaction
static double g_last_click_t = -1;
static int    g_last_click_idx = -1;
static int    g_q_press = -1, g_q_drag = -1, g_q_target = -1;
static int    g_q_drag_view = 0;        // which tab the drag started in (0 playlist · 2 queue)
static float  g_q_press_y = 0;

// context menu (right-click on a drawer row)
static bool    g_ctx_open = false;
static int     g_ctx_view = 0;          // tab the menu belongs to (0 playlist · 2 queue)
static int     g_ctx_target = -1;       // playlist index (tab 0) / queue pos (tab 2)
static Vector2 g_ctx_at = { 0, 0 };     // drawer-local anchor (where the user clicked)

// ---------- helpers ----------
static float approach(float c, float t, float dt) { return c + (t - c) * (1.0f - expf(-dt * 16.0f)); }
static int   clampi(int v, int lo, int hi)   { return v < lo ? lo : (v > hi ? hi : v); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static Color clerp(Color a, Color b, float t) {
    t = clampf(t, 0.0f, 1.0f);
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
}
static Color alpha(Color c, unsigned char a) { c.a = a; return c; }

// raylib 5.5's DrawRectangleRoundedLines rounds its corners at a different radius
// than DrawRectangleRounded, so a fill never quite meets its border (the corners
// gap). DrawRectangleRoundedLinesEx matches the fill — route every rounded border
// through it so fills sit flush inside their outlines.
static void DrawRoundedBorder(Rectangle r, float roundness, int segments, Color c) {
    DrawRectangleRoundedLinesEx(r, roundness, segments, 1.0f, c);
}
// A smooth filled rounded rect (high segment count so corners aren't faceted).
static void rrFill(Rectangle r, float rn, Color c) { DrawRectangleRounded(r, rn, 32, c); }
// Filled rounded rect with a crisp, even border that always matches the fill:
// two concentric fills, because raylib 5.5's rounded-line funcs give corners that
// don't line up with DrawRectangleRounded. `fill` is the interior, `border` the ~1.2px ring.
static void rrBox(Rectangle r, float rn, Color fill, Color border) {
    float t = 1.2f;
    Rectangle in = { r.x + t, r.y + t, r.width - 2 * t, r.height - 2 * t };
    DrawRectangleRounded(r, rn, 32, border);
    DrawRectangleRounded(in, rn, 32, CARDBG);   // opaque base so a translucent fill doesn't let the border bleed through
    DrawRectangleRounded(in, rn, 32, fill);
}

static void round_corners(Image *img, int rad) {
    int w = img->width, h = img->height;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int inX = (x < rad) || (x >= w - rad), inY = (y < rad) || (y >= h - rad);
            if (!(inX && inY)) continue;
            int cx = (x < rad) ? rad : (w - 1 - rad), cy = (y < rad) ? rad : (h - 1 - rad);
            float dx = (float)(x - cx), dy = (float)(y - cy);
            if (dx * dx + dy * dy > (float)rad * rad) ImageDrawPixel(img, x, y, BLANK);
        }
}
static void soft_shadow(Rectangle r, float round, int spread, int a) {
    for (int i = spread; i >= 1; i--) {
        int aa = a * (spread - i + 1) / (spread * 3);
        DrawRectangleRounded((Rectangle){ r.x - i, r.y + 3, r.width + 2 * i, r.height + i }, round, 12, (Color){ 0, 0, 0, (unsigned char)aa });
    }
}
static const char *basename_of(const char *path) {
    const char *a = strrchr(path, '/'), *b = strrchr(path, '\\');
    const char *s = (b > a) ? b : a;
    return s ? s + 1 : path;
}
static Color accent_from_image(Image *im) {
    Color *px = LoadImageColors(*im);
    if (!px) return (Color){ 201, 164, 90, 255 };
    int n = im->width * im->height, step = n / 3000 + 1;
    long r = 0, g = 0, b = 0; int cnt = 0;
    for (int i = 0; i < n; i += step) { if (px[i].a < 16) continue; r += px[i].r; g += px[i].g; b += px[i].b; cnt++; }
    UnloadImageColors(px);
    if (!cnt) return (Color){ 201, 164, 90, 255 };
    Vector3 hsv = ColorToHSV((Color){ (unsigned char)(r / cnt), (unsigned char)(g / cnt), (unsigned char)(b / cnt), 255 });
    float s = hsv.y; if (s < 0.35f) s = 0.45f; if (s > 0.85f) s = 0.70f;
    return ColorFromHSV(hsv.x, s, 0.86f);
}
static void make_cover(const char *path) {
    const char *name = basename_of(path);
    unsigned hh = 5381;
    for (const char *p = name; *p; p++) hh = hh * 33u + (unsigned char)*p;
    float hue = (float)(hh % 360);
    int S = ARTS * SS;
    Image img = GenImageGradientLinear(S, S, 45, ColorFromHSV(hue, 0.45f, 0.34f), ColorFromHSV(fmodf(hue + 36, 360), 0.55f, 0.11f));
    ImageDrawCircleV(&img, (Vector2){ S * 0.32f, S * 0.30f }, (int)(S * 0.36f), ColorFromHSV(hue, 0.30f, 0.52f));
    round_corners(&img, 16 * SS);
    g_cover = LoadTextureFromImage(img);
    SetTextureFilter(g_cover, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
    g_has_cover = true;
    g_accent = ColorFromHSV(hue, 0.42f, 0.84f);
}
static bool dir_cover_rgba(const char *path, unsigned char **rgba, int *w, int *h) {
    char cand[800];
    return art_find_dir_cover(path, cand, (int)sizeof(cand)) && art_decode_file(cand, rgba, w, h);
}
static void set_cover(const char *path) {
    if (g_has_cover) { UnloadTexture(g_cover); g_has_cover = false; }
    unsigned char *rgba = NULL; int aw = 0, ah = 0;
    if (art_load_rgba(path, &rgba, &aw, &ah) || dir_cover_rgba(path, &rgba, &aw, &ah)) {
        Image im = { rgba, aw, ah, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        int side = aw < ah ? aw : ah;
        ImageCrop(&im, (Rectangle){ (aw - side) / 2.0f, (ah - side) / 2.0f, (float)side, (float)side });
        int S = ARTS * SS;
        ImageResize(&im, S, S);
        g_accent = accent_from_image(&im);
        round_corners(&im, 16 * SS);
        g_cover = LoadTextureFromImage(im);
        SetTextureFilter(g_cover, TEXTURE_FILTER_BILINEAR);
        UnloadImage(im);
        g_has_cover = true;
    } else make_cover(path);
}
static void set_meta_from_path(const char *path) {
    const char *name = basename_of(path), *dot = strrchr(name, '.');
    if (dot) { snprintf(g_fmt, sizeof(g_fmt), "%s", dot + 1); for (char *p = g_fmt; *p; p++) *p = (char)toupper((unsigned char)*p); }
    else g_fmt[0] = 0;

    Tags tg;
    if (tags_read(path, &tg) && tg.title[0]) {
        snprintf(g_title, sizeof(g_title), "%s", tg.title);
        snprintf(g_meta, sizeof(g_meta), "%s", tg.artist);
        snprintf(g_album, sizeof(g_album), "%s", tg.album);
    } else {
        snprintf(g_title, sizeof(g_title), "%s", name);
        char *d = strrchr(g_title, '.'); if (d) *d = 0;
        for (char *p = g_title; *p; p++) if (*p == '_') *p = ' ';
        g_meta[0] = 0;
        g_album[0] = 0;
    }
}
static void reset_now_playing(void) {
    if (g_audio) audio_unload(g_audio);
    snprintf(g_title, sizeof(g_title), "Drop a track to begin");
    g_meta[0] = 0; g_fmt[0] = 0; g_album[0] = 0;
    mediakeys_now_playing(NULL, NULL, NULL, NULL);
    g_lyrics.count = 0; g_lyrics.synced = false;
    g_accent = (Color){ 201, 164, 90, 255 };
    if (g_has_cover) { UnloadTexture(g_cover); g_has_cover = false; }
}

static void load_file(const char *path) {
    if (!path || !g_audio) return;
    if (audio_load(g_audio, path)) {
        audio_play(g_audio); set_meta_from_path(path); set_cover(path);
        mediakeys_now_playing(g_title, g_meta, g_album, path);
        lyrics_load(path, &g_lyrics); g_lyrics_scroll = 0; g_lyrics_fetching = false;
        if (g_lyrics.count == 0) {  // no local lyrics → try lrclib.net in the background
            lyrics_fetch_start(g_meta, g_title, "", (int)audio_length_seconds(g_audio));
            g_lyrics_fetching = true;
        }
    } else {
        snprintf(g_title, sizeof(g_title), "Can't open file");
        mediakeys_now_playing(NULL, NULL, NULL, NULL);   // don't leave the OS on the old track
    }
}
// Prev action shared by the on-screen button and the system media key.
// Smart mode (Spotify-like): once past the first 5s, prev restarts the current
// song; only within the first 5s does it step to the previous track.
// Direct mode: always step to the previous track.
static void do_prev(void) {
    bool loaded = g_audio && audio_is_loaded(g_audio);
    if (g_prev_mode == 0 && loaded && audio_position_seconds(g_audio) > 5.0) { audio_seek_seconds(g_audio, 0); return; }
    if (playlist_has_prev(&g_pl)) load_file(playlist_prev(&g_pl));
    else if (loaded) audio_seek_seconds(g_audio, 0);
}
static void queue_add_cb(const char *path, void *ud) { (void)ud; playlist_add(&g_pl, path); }
static void open_dialog(void) {
    bool wasLoaded = g_audio && audio_is_loaded(g_audio);
    int before = playlist_count(&g_pl);
    os_open_audio_files(queue_add_cb, NULL);
    if (!wasLoaded && playlist_count(&g_pl) > before) load_file(playlist_current(&g_pl));
}
static void drawer_save(const char *name) {
    if (name && name[0] && playlistio_save(&g_pl, name)) {
        playlist_set_name(&g_pl, name);
        playlist_mark_clean(&g_pl);
    }
}

// ---------- Hisashi menubar (hoswl) ----------
// Timp has no menu bar of its own; Hisashi's menubar gets one from menubar.c.
// Item ids are defined there. Runs from the main loop (menubar_frame), so every
// action below is the same code the keyboard / buttons run.
static void menu_click(const char *id) {
    if (g_naming || g_confirm_overwrite) return;   // a modal owns the input; Hisashi has already raised our window
    bool loaded = g_audio && audio_is_loaded(g_audio);
    bool playing = g_audio && audio_is_playing(g_audio);
    int n;
    #define IS(s) (strcmp(id, s) == 0)
    if      (IS("file.open"))     open_dialog();
    else if (IS("file.save")) {
        if (playlist_dirty(&g_pl) && playlist_count(&g_pl) > 0) {
            g_show_queue = true; if (g_drawer_view == 1) g_drawer_view = 0;   // the name prompt lives in the drawer
            if (playlist_name(&g_pl)[0]) { snprintf(g_name_buf, sizeof(g_name_buf), "%s", playlist_name(&g_pl)); g_confirm_overwrite = true; }
            else { g_naming = true; g_name_buf[0] = 0; }
        }
    }
    else if (IS("file.library"))  { g_show_queue = true; g_saved_count = playlistio_list(g_saved_names, PL_MAX_SAVED); g_drawer_view = 1; g_queue_scroll = 0; g_search_focus = false; }
    else if (IS("file.clear"))    { if (playlist_count(&g_pl) > 0) { playlist_clear(&g_pl); reset_now_playing(); g_queue_scroll = 0; g_search[0] = 0; } }
    else if (IS("file.folder"))   os_reveal_dir(g_data_dir);
    else if (IS("file.quit"))     g_quit = true;
    else if (IS("pb.toggle"))     { if (loaded) { if (playing) audio_pause(g_audio); else audio_play(g_audio); } }
    else if (IS("pb.stop"))       { if (loaded) audio_stop(g_audio); }
    else if (IS("pb.prev"))       do_prev();
    else if (IS("pb.next"))       { if (playlist_has_next(&g_pl)) load_file(playlist_next(&g_pl)); }
    else if (IS("pb.back"))       { if (loaded) audio_seek_seconds(g_audio, audio_position_seconds(g_audio) - 5); }
    else if (IS("pb.fwd"))        { if (loaded) audio_seek_seconds(g_audio, audio_position_seconds(g_audio) + 5); }
    else if (IS("pb.shuffle"))    playlist_set_shuffle(&g_pl, !playlist_shuffle(&g_pl));
    else if (sscanf(id, "repeat.%d", &n) == 1) { if (n >= 0 && n < 3) { g_repeat = n; playlist_set_loop(&g_pl, g_repeat == 2); } }
    else if (IS("audio.up"))      { if (g_audio) audio_set_volume(g_audio, audio_get_volume(g_audio) + 0.05f); }
    else if (IS("audio.down"))    { if (g_audio) audio_set_volume(g_audio, audio_get_volume(g_audio) - 0.05f); }
    else if (IS("audio.mute")) {
        if (g_audio) {
            if (audio_get_volume(g_audio) > 0.001f) { g_premute = audio_get_volume(g_audio); audio_set_volume(g_audio, 0); }
            else audio_set_volume(g_audio, g_premute);
        }
    }
    else if (IS("audio.eq"))      { if (g_audio) { Eq *e = audio_get_eq(g_audio); eq_set_enabled(e, !eq_is_enabled(e)); } }
    else if (IS("audio.eqflat"))  { if (g_audio) eq_flat(audio_get_eq(g_audio)); }
    else if (IS("audio.eqpanel")) { g_show_eq = !g_show_eq; if (g_show_eq) { g_show_settings = g_show_lyrics = false; } }
    else if (sscanf(id, "viz.%d", &n) == 1) { if (n >= 0 && n < 3) g_art_mode = n; }
    else if (IS("view.drawer"))   { g_show_queue = !g_show_queue; if (g_show_queue) { if (g_drawer_view == 1) g_drawer_view = 0; } else { g_naming = false; g_confirm_overwrite = false; } }
    else if (IS("view.lyrics"))   { g_show_lyrics = !g_show_lyrics; if (g_show_lyrics) { g_show_eq = g_show_settings = false; } }
    else if (IS("view.settings") || IS("help.about")) { g_show_settings = !g_show_settings; if (g_show_settings) { g_show_eq = g_show_lyrics = false; } }
    else if (IS("view.aot"))      { g_aot = !g_aot; if (g_aot) SetWindowState(FLAG_WINDOW_TOPMOST); else ClearWindowState(FLAG_WINDOW_TOPMOST); }
    else if (sscanf(id, "side.%d", &n) == 1) { if (n == 0 || n == 1) g_side = n; }
    else if (sscanf(id, "prev.%d", &n) == 1) { if (n == 0 || n == 1) g_prev_mode = n; }
    #undef IS
}

// ---------- drawer search + layout ----------
// Display name shown in drawer rows: basename, extension stripped, '_' → ' '.
static void row_display_name(const char *path, char *out, size_t cap) {
    snprintf(out, cap, "%s", basename_of(path));
    char *d = strrchr(out, '.'); if (d) *d = 0;
    for (char *q = out; *q; q++) if (*q == '_') *q = ' ';
}
// Case-insensitive (ASCII) substring test for the drawer search box.
static bool str_contains_ci(const char *hay, const char *needle) {
    if (!needle[0]) return true;
    size_t nl = strlen(needle);
    for (const char *h = hay; *h; h++) {
        size_t k = 0;
        while (k < nl && h[k] && tolower((unsigned char)h[k]) == tolower((unsigned char)needle[k])) k++;
        if (k == nl) return true;
    }
    return false;
}
static bool row_matches(const char *path, const char *filter) {
    if (!filter[0]) return true;
    char nm[256]; row_display_name(path, nm, sizeof(nm));
    return str_contains_ci(nm, filter);
}
// Fill g_filt with the rows the open tab shows after the search filter:
// playlist indices for tab 0, queue positions for tab 2. Returns the row count
// (the saved-playlist library is unfiltered).
static int drawer_filter_build(int view) {
    if (view == 1) return g_saved_count;
    int n = playlist_count(&g_pl), total = 0;
    if (g_filt_cap < n) {
        int cap = g_filt_cap ? g_filt_cap : 64;
        while (cap < n) cap *= 2;
        g_filt = (int *)realloc(g_filt, sizeof(int) * cap);
        g_filt_cap = cap;
    }
    if (view == 0) {
        for (int i = 0; i < n; i++)
            if (row_matches(g_pl.paths[i], g_search)) g_filt[total++] = i;
    } else {
        for (int pos = 0; pos < n; pos++) {
            int idx = playlist_queue_at(&g_pl, pos);
            if (idx >= 0 && row_matches(g_pl.paths[idx], g_search)) g_filt[total++] = pos;
        }
    }
    return total;
}
// Per-view header geometry. Recomputed after clicks so a tab switch renders with
// the right layout in the same frame it happens.
typedef struct { int headH, listTop, listBot, visible; float btnY; Rectangle search; } DLayout;
static DLayout drawer_layout(Rectangle dCard, int view) {
    DLayout L;
    L.headH   = (view == 0) ? 144 : (view == 2) ? 104 : 88;   // tabs+name+buttons+search · tabs+caption+search · library
    L.btnY    = dCard.y + ((view == 1) ? 44.0f : 76.0f);      // Save/Open (tab 0) · Back (library)
    L.search  = (Rectangle){ dCard.x + 12, dCard.y + ((view == 0) ? 110.0f : 68.0f), dCard.width - 24, 26 };
    L.listTop = (int)dCard.y + L.headH;
    L.listBot = (int)(dCard.y + dCard.height) - 12;
    L.visible = (L.listBot - L.listTop) / 34;
    return L;
}

// ---------- shared UTF-8 text entry (naming modal + search box) ----------
// fname_safe rejects the characters Windows forbids in filenames.
static void text_input_utf8(char *buf, size_t cap, bool fname_safe) {
    int ch;
    while ((ch = GetCharPressed()) != 0) {
        bool illegal = fname_safe && ch < 128 && strchr("\\/:*?\"<>|", ch) != NULL;
        if (ch >= 32 && ch < 0x250 && !illegal) {            // UTF-8 encode (covers Turkish)
            char enc[2]; int el;
            if (ch < 0x80) { enc[0] = (char)ch; el = 1; }
            else { enc[0] = (char)(0xC0 | (ch >> 6)); enc[1] = (char)(0x80 | (ch & 0x3F)); el = 2; }
            size_t L = strlen(buf);
            if (L + (size_t)el < cap - 1) { for (int k = 0; k < el; k++) buf[L + k] = enc[k]; buf[L + el] = 0; }
        }
    }
}
static void text_backspace_utf8(char *buf) {
    size_t L = strlen(buf);
    if (L > 0) { L--; while (L > 0 && ((unsigned char)buf[L] & 0xC0) == 0x80) L--; buf[L] = 0; }
}
static void draw_fit(Font f, const char *txt, Vector2 pos, float size, float sp, Color c, float maxw) {
    char buf[512]; snprintf(buf, sizeof(buf), "%s", txt);
    if (MeasureTextEx(f, buf, size, sp).x <= maxw) { DrawTextEx(f, buf, pos, size, sp, c); return; }
    for (int n = (int)strlen(buf); n > 1; n--) {
        buf[n - 1] = 0; char tmp[520]; snprintf(tmp, sizeof(tmp), "%s…", buf);
        if (MeasureTextEx(f, tmp, size, sp).x <= maxw) { DrawTextEx(f, tmp, pos, size, sp, c); return; }
    }
    DrawTextEx(f, buf, pos, size, sp, c);
}

// ---------- row context menu ----------
// Items for the open context menu. Returns the count; `en` marks clickable ones.
static int ctx_menu_items(const char **items, bool *en) {
    if (g_ctx_view == 0) {
        items[0] = "Add to queue"; en[0] = true;
        items[1] = "Remove";       en[1] = true;
        return 2;
    }
    items[0] = "Remove from queue";
    en[0] = (g_ctx_target != playlist_queue_pos(&g_pl));   // the playing entry stays
    return 1;
}
#define CTX_ITEM_H 30
static Rectangle ctx_menu_rect(Rectangle dCard) {
    const char *items[2]; bool en[2];
    int n = ctx_menu_items(items, en);
    Rectangle r = { g_ctx_at.x, g_ctx_at.y, 170, (float)(n * CTX_ITEM_H + 10) };
    if (r.x + r.width  > dCard.x + dCard.width  - 4) r.x = dCard.x + dCard.width  - 4 - r.width;
    if (r.y + r.height > dCard.y + dCard.height - 4) r.y = dCard.y + dCard.height - 4 - r.height;
    if (r.x < dCard.x + 4) r.x = dCard.x + 4;
    if (r.y < dCard.y + 4) r.y = dCard.y + 4;
    return r;
}
static Rectangle ctx_item_rect(Rectangle m, int i) {
    return (Rectangle){ m.x + 5, m.y + 5 + i * CTX_ITEM_H, m.width - 10, CTX_ITEM_H - 2 };
}

// Search field for the drawer tabs: magnifier, text/placeholder, caret, clear ×.
static void draw_search_box(Rectangle r, Vector2 dmp, int frame) {
    bool hov = CheckCollisionPointRec(dmp, r);
    rrBox(r, 0.35f, (Color){ 12, 11, 8, 255 }, g_search_focus ? alpha(g_accent, 170) : alpha(MUT, hov ? 160 : 90));
    float mx = r.x + 14, my = r.y + 12;
    Color mc = g_search[0] ? g_accent : alpha(MUT, 180);
    DrawRing((Vector2){ mx, my }, 3.4f, 4.8f, 0, 360, 20, mc);
    DrawLineEx((Vector2){ mx + 3.4f, my + 3.4f }, (Vector2){ mx + 7.0f, my + 7.0f }, 1.6f, mc);
    if (g_search[0]) {
        draw_fit(fSmall, g_search, (Vector2){ r.x + 27, r.y + 6 }, 14, 0.2f, TXT, r.width - 54);
        float xx = r.x + r.width - 14, xy = r.y + r.height / 2;
        Color xc = (hov && dmp.x >= r.x + r.width - 26) ? TXT : alpha(MUT, 170);
        DrawLineEx((Vector2){ xx - 4, xy - 4 }, (Vector2){ xx + 4, xy + 4 }, 1.6f, xc);
        DrawLineEx((Vector2){ xx + 4, xy - 4 }, (Vector2){ xx - 4, xy + 4 }, 1.6f, xc);
    } else if (!g_search_focus) {
        DrawTextEx(fSmall, "Search", (Vector2){ r.x + 27, r.y + 6 }, 14, 0.2f, alpha(MUT, 120));
    }
    if (g_search_focus && ((frame / 30) & 1) == 0) {
        Vector2 tw = MeasureTextEx(fSmall, g_search, 14, 0.2f);
        float cx = r.x + 28 + (tw.x < r.width - 54 ? tw.x : r.width - 54);
        DrawRectangle((int)cx, (int)r.y + 6, 1, 14, alpha(TXT, 220));
    }
}

// ---------- crisp vector transport icons ----------
static void ic_play(float cx, float cy, float r, Color c) {
    DrawTriangle((Vector2){ cx - r * 0.7f, cy - r }, (Vector2){ cx - r * 0.7f, cy + r }, (Vector2){ cx + r, cy }, c);
}
static void ic_pause(float cx, float cy, float r, Color c) {
    float w = r * 0.42f;
    DrawRectangleRounded((Rectangle){ cx - r * 0.62f, cy - r, w, 2 * r }, 0.6f, 6, c);
    DrawRectangleRounded((Rectangle){ cx + r * 0.20f, cy - r, w, 2 * r }, 0.6f, 6, c);
}
static void ic_prev(float cx, float cy, float r, Color c) {
    DrawRectangleRounded((Rectangle){ cx - r, cy - r, r * 0.28f, 2 * r }, 0.8f, 4, c);
    DrawTriangle((Vector2){ cx + r, cy - r }, (Vector2){ cx - r * 0.35f, cy }, (Vector2){ cx + r, cy + r }, c);
}
static void ic_next(float cx, float cy, float r, Color c) {
    DrawRectangleRounded((Rectangle){ cx + r - r * 0.28f, cy - r, r * 0.28f, 2 * r }, 0.8f, 4, c);
    DrawTriangle((Vector2){ cx - r, cy - r }, (Vector2){ cx - r, cy + r }, (Vector2){ cx + r * 0.35f, cy }, c);
}
static void ic_shuffle(float cx, float cy, float r, Color c) {
    float th = 2.2f;
    DrawLineEx((Vector2){ cx - r, cy - r * 0.55f }, (Vector2){ cx + r * 0.55f, cy + r * 0.55f }, th, c);
    DrawLineEx((Vector2){ cx - r, cy + r * 0.55f }, (Vector2){ cx + r * 0.55f, cy - r * 0.55f }, th, c);
    // arrow tips
    DrawTriangle((Vector2){ cx + r, cy - r * 0.55f }, (Vector2){ cx + r * 0.4f, cy - r * 0.95f }, (Vector2){ cx + r * 0.45f, cy - r * 0.15f }, c);
    DrawTriangle((Vector2){ cx + r, cy + r * 0.55f }, (Vector2){ cx + r * 0.45f, cy + r * 0.15f }, (Vector2){ cx + r * 0.4f, cy + r * 0.95f }, c);
}
static void ic_repeat(float cx, float cy, float r, Color c) {
    DrawRing((Vector2){ cx, cy }, r - 2.2f, r, 35, 320, 32, c);
    DrawTriangle((Vector2){ cx + r * 0.95f, cy - r * 0.75f }, (Vector2){ cx + r * 0.35f, cy - r * 0.55f }, (Vector2){ cx + r * 0.95f, cy + r * 0.05f }, c);
}

#ifdef _WIN32
// Declared by hand (windows.h clashes with raylib names); lives in shell32.
__declspec(dllimport) long __stdcall SetCurrentProcessExplicitAppUserModelID(const wchar_t *app_id);
#endif

// Platform UI fonts — Segoe UI on Windows; SF Pro for text on macOS (the
// variable SFNS.ttf loads at its Regular default, so the semibold title falls
// back to Arial Bold, the closest glyf-outline weight stb_truetype can read);
// DejaVu on Linux. First existing candidate wins.
#ifdef _WIN32
static const char *UI_SEMIBOLD[] = { "C:/Windows/Fonts/seguisb.ttf" };
static const char *UI_REGULAR[]  = { "C:/Windows/Fonts/segoeui.ttf" };
#elif defined(__APPLE__)
static const char *UI_SEMIBOLD[] = { "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
                                     "/System/Library/Fonts/Supplemental/Tahoma Bold.ttf",
                                     "/System/Library/Fonts/Supplemental/Trebuchet MS Bold.ttf" };
static const char *UI_REGULAR[]  = { "/System/Library/Fonts/SFNS.ttf",
                                     "/System/Library/Fonts/Supplemental/Arial.ttf",
                                     "/System/Library/Fonts/Supplemental/Tahoma.ttf" };
#else
static const char *UI_SEMIBOLD[] = { "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                                     "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf" };
static const char *UI_REGULAR[]  = { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                     "/usr/share/fonts/TTF/DejaVuSans.ttf" };
#endif

static Font load_ui_font(const char **cands, int n, int px, int *cps, int cpc) {
    for (int i = 0; i < n; i++)
        if (FileExists(cands[i])) return LoadFontEx(cands[i], px, cps, cpc);
    return LoadFontEx(cands[0], px, cps, cpc);   // missing everywhere → raylib default
}

int main(int argc, char **argv) {
#ifdef _WIN32
    // Stable taskbar identity. Without it the Win11 taskbar keys the app by exe
    // path and can serve a stale cached icon from whatever lived there before
    // (the pre-raylib Timp had no embedded icon at all → blank taskbar icon).
    SetCurrentProcessExplicitAppUserModelID(L"Fezcode.Timp");
#endif

    // Decode the real Unicode args (Windows argv is ANSI — Turkish "İ"/"ı" etc.
    // arrive mangled and fail to open). args[0] is the program, args[1..] paths.
    int argn = 0;
    char **args = os_args_utf8(argc, argv, &argn);

    // Single instance: if a Timp is already running, hand it our song(s) (or just
    // raise it) and bow out before creating a window or grabbing the audio device.
    if (!singleinst_acquire()) {
        bool sent = false;
        for (int i = 1; i < argn; i++) if (singleinst_send_file(args[i])) sent = true;
        if (!sent) singleinst_send_focus();
        return 0;
    }
    singleinst_listen_start();
    os_open_files_handler_install();   // Finder "Open With" → the same queue

    playlist_init(&g_pl);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_UNDECORATED);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WW, WH, "Timp");
    int refresh = GetMonitorRefreshRate(GetCurrentMonitor());
    SetTargetFPS(refresh > 0 ? refresh : 60);
    SetExitKey(0);
    os_round_window(GetWindowHandle(), WW, WH, 16);

    g_audio = audio_create();

    // Glyph coverage: ASCII + Latin-1 + Latin Extended-A/B covers Turkish
    // (ı ş ğ ç ö ü İ) and Western/Central-European accents; plus punctuation.
    static int cps[640]; int cpc = 0;
    for (int c = 0x20; c <= 0x24F; c++) cps[cpc++] = c;
    static const int extra[] = { 0x2026, 0x2022, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D };
    for (unsigned i = 0; i < sizeof(extra) / sizeof(extra[0]); i++) cps[cpc++] = extra[i];

    const int nsb = (int)(sizeof(UI_SEMIBOLD) / sizeof(UI_SEMIBOLD[0]));
    const int nrg = (int)(sizeof(UI_REGULAR) / sizeof(UI_REGULAR[0]));
    fTitle = load_ui_font(UI_SEMIBOLD, nsb, 30 * SS, cps, cpc);
    fMeta  = load_ui_font(UI_REGULAR,  nrg, 16 * SS, cps, cpc);
    fSmall = load_ui_font(UI_REGULAR,  nrg, 14 * SS, cps, cpc);
    fEye   = load_ui_font(UI_REGULAR,  nrg, 12 * SS, cps, cpc);
    SetTextureFilter(fTitle.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(fMeta.texture,  TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(fSmall.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(fEye.texture,   TEXTURE_FILTER_BILINEAR);

    // Wide enough to hold the player plus the fully-extended drawer; we blit only
    // the currently-visible width to the (resizable) window each frame.
    RenderTexture2D target = LoadRenderTexture(WMAXW * SS, WH * SS);
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);

    // app/taskbar icon — the shared procedural mark (icon.h), same artwork as
    // the embedded .ico; several sizes so title bar / taskbar / Alt-Tab stay crisp
    {
        static const int is[] = { 16, 24, 32, 48, 64 };
        const int n = (int)(sizeof(is) / sizeof(is[0]));
        Image icons[sizeof(is) / sizeof(is[0])];
        for (int i = 0; i < n; i++) {
            icons[i] = (Image){ MemAlloc(is[i] * is[i] * 4), is[i], is[i], 1,
                                PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            icon_render_rgba(icons[i].data, is[i]);
        }
        SetWindowIcons(icons, n);
        for (int i = 0; i < n; i++) UnloadImage(icons[i]);
    }

    mediakeys_start(GetWindowHandle());

    // restore persisted settings
    RlConfig cfg; rlconfig_load(&cfg);
    g_aot = cfg.always_on_top;
    g_side = (cfg.playlist_side == 1) ? 1 : 0;
    g_prev_mode = (cfg.prev_mode == 1) ? 1 : 0;
    g_hisashi_menubar = cfg.hisashi_menubar;
    menubar_init(TIMP_VERSION);
    menubar_set_enabled(g_hisashi_menubar);
    if (g_audio) {
        audio_set_volume(g_audio, cfg.volume);
        Eq *e0 = audio_get_eq(g_audio);
        eq_set_enabled(e0, cfg.eq_enabled);
        for (int i = 0; i < EQ_BANDS; i++) eq_set_gain(e0, i, cfg.eq_gains[i]);
    }
    if (g_aot) SetWindowState(FLAG_WINDOW_TOPMOST);
    if (cfg.has_win_pos) SetWindowPosition(cfg.win_x, cfg.win_y);

    if (argn > 1) { for (int i = 1; i < argn; i++) playlist_add(&g_pl, args[i]); load_file(playlist_current(&g_pl)); }
    if (getenv("TIMP_QUEUE")) g_show_queue = true;
    if (getenv("TIMP_VIEW")) g_drawer_view = clampi(atoi(getenv("TIMP_VIEW")), 0, 2);   // 0 playlist · 2 queue (test/override)
    if (getenv("TIMP_SHUF")) playlist_set_shuffle(&g_pl, true);                         // (test/override)
    if (getenv("TIMP_FIND")) snprintf(g_search, sizeof(g_search), "%s", getenv("TIMP_FIND"));   // (test/override)
    if (getenv("TIMP_CTX")) {                                                                   // (test/override) open the row context menu
        g_ctx_open = true; g_ctx_view = (g_drawer_view == 2) ? 2 : 0;
        g_ctx_target = atoi(getenv("TIMP_CTX"));
        g_ctx_at = (Vector2){ 120, 300 };
    }
    if (getenv("TIMP_SIDE")) g_side = atoi(getenv("TIMP_SIDE")) ? 1 : 0;   // 0 right · 1 left (test/override)
    if (getenv("TIMP_EQ")) g_show_eq = true;
    if (getenv("TIMP_SET")) g_show_settings = true;
    if (getenv("TIMP_LYR")) g_show_lyrics = true;

    bool dragging = false; Vector2 dragGrab = { 0 };
    bool vol_drag = false;
    bool pos_drag = false; float scrub_t = 0;
    int shot_frame = getenv("TIMP_SHOT") ? 60 : -1, frame = 0;

    // The window's closed top-left; the drawer grows the window from here.
    { Vector2 wp0 = GetWindowPosition(); g_base_x = (int)wp0.x; g_base_y = (int)wp0.y; }

    char *dataDir = g_data_dir; rlconfig_data_dir(g_data_dir, sizeof g_data_dir);   // %APPDATA%\fezcode\Timp (config + Playlists)

    while (!WindowShouldClose() && !g_quit) {
        float dt = GetFrameTime();
        // ---- drawer slide + window geometry ----
        g_drawer_anim = approach(g_drawer_anim, g_show_queue ? 1.0f : 0.0f, dt);
        if (g_drawer_anim < 0.0008f) g_drawer_anim = 0.0f;
        if (g_drawer_anim > 0.9992f) g_drawer_anim = 1.0f;
        int dw         = (int)(DRAWER_W * g_drawer_anim + 0.5f);  // visible drawer width
        int curW       = WW + dw;                                 // current logical width
        int playerOffX = (g_side == 1) ? DRAWER_W : 0;            // player origin in the target
        int drawerOffX = (g_side == 1) ? 0 : WW;                  // drawer origin in the target
        int blitX      = (g_side == 1) ? (DRAWER_W - dw) : 0;     // left edge of the visible slice
        int playerShift = (g_side == 1) ? dw : 0;                 // screen->player-local x shift
        int drawerShift = (g_side == 1) ? (dw - DRAWER_W) : WW;   // screen->drawer-local x shift
        bool loaded  = g_audio && audio_is_loaded(g_audio);
        bool playing = g_audio && audio_is_playing(g_audio);

        if (loaded && audio_finished(g_audio)) {
            if (g_repeat == 1) audio_play(g_audio);                 // repeat one → replay
            else if (playlist_has_next(&g_pl)) load_file(playlist_next(&g_pl));  // loop=all wraps
            else audio_stop(g_audio);
        }
        // system-wide media keys
        switch (mediakeys_poll()) {
            case MK_PLAYPAUSE: if (loaded) { if (playing) audio_pause(g_audio); else audio_play(g_audio); } break;
            case MK_PLAY:      if (loaded && !playing) audio_play(g_audio); break;
            case MK_PAUSE:     if (loaded && playing) audio_pause(g_audio); break;
            case MK_STOP:      if (loaded) audio_stop(g_audio); break;
            case MK_PREV:      do_prev(); break;
            case MK_NEXT:      if (playlist_has_next(&g_pl)) load_file(playlist_next(&g_pl)); break;
            default: break;
        }
        if (lyrics_fetch_poll(&g_lyrics)) g_lyrics_fetching = false;
        // ---- Hisashi menubar: publish the current state, run any clicks ----
        { MenubarState ms = { 0 };
          ms.loaded = loaded; ms.playing = playing;
          ms.has_next = playlist_has_next(&g_pl); ms.has_prev = playlist_has_prev(&g_pl);
          ms.shuffle = playlist_shuffle(&g_pl); ms.repeat = g_repeat; ms.art_mode = g_art_mode;
          ms.drawer_open = g_show_queue; ms.drawer_view = g_drawer_view;
          ms.eq_on = g_audio && eq_is_enabled(audio_get_eq(g_audio)); ms.eq_panel = g_show_eq;
          ms.settings_open = g_show_settings; ms.lyrics_open = g_show_lyrics;
          ms.aot = g_aot; ms.side = g_side; ms.prev_mode = g_prev_mode;
          ms.muted = g_audio && audio_get_volume(g_audio) <= 0.001f;
          ms.playlist_dirty = playlist_dirty(&g_pl); ms.qcount = playlist_count(&g_pl);
          menubar_frame(&ms, menu_click); }
        // songs forwarded from a second launch → append to the queue and play the
        // first newly-added one (single-instance "append & play").
        {
            char fwd[4096];
            int firstNew = -1;
            while (singleinst_poll_file(fwd, sizeof fwd)) {
                playlist_add(&g_pl, fwd);
                if (firstNew < 0) firstNew = playlist_count(&g_pl) - 1;
            }
            if (firstNew >= 0) {
                playlist_play_index(&g_pl, firstNew);
                load_file(playlist_current(&g_pl));
            }
            if (singleinst_poll_focus()) os_focus_window(GetWindowHandle());
        }
        // drag-drop APPENDS to the queue (keeps what's already there)
        if (IsFileDropped()) {
            FilePathList d = LoadDroppedFiles();
            bool wasLoaded = loaded;
            int before = playlist_count(&g_pl);
            for (unsigned i = 0; i < d.count; i++) playlist_add(&g_pl, d.paths[i]);
            UnloadDroppedFiles(d);
            if (!wasLoaded && playlist_count(&g_pl) > before) load_file(playlist_current(&g_pl));
        }

        Vector2 rawmp = GetMousePosition();
        Vector2 mp  = { rawmp.x - playerShift, rawmp.y };   // player-local mouse (rects live in [0,WW])
        Vector2 dmp = { rawmp.x - drawerShift, rawmp.y };   // drawer-local mouse (rects live in [0,DRAWER_W])
        // window drag from the empty part of the top bar (player, or the open drawer's top strip)
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mp.y < TBH && mp.x > 74 && mp.x < WW - 66) { dragging = true; dragGrab = mp; }
        else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && dw > 0 && dmp.y < TBH && dmp.x > 0 && dmp.x < DRAWER_W) { dragging = true; dragGrab = mp; }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) dragging = false;
        if (dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Vector2 wp = GetWindowPosition();
            SetWindowPosition((int)(wp.x + mp.x - dragGrab.x), (int)(wp.y + mp.y - dragGrab.y));
            Vector2 wp2 = GetWindowPosition();                 // track the closed-window anchor
            g_base_x = (int)wp2.x + (g_side == 1 ? dw : 0);
            g_base_y = (int)wp2.y;
        } else {
            // Keep the OS window sized/positioned to the drawer slide. Right side grows
            // rightward (X fixed); left side grows leftward (X moves so the player stays put).
            int wantX = (g_side == 1) ? g_base_x - dw : g_base_x;
            if (GetScreenWidth() != curW) SetWindowSize(curW, WH);
            Vector2 wp2 = GetWindowPosition();
            if ((int)wp2.x != wantX || (int)wp2.y != g_base_y) SetWindowPosition(wantX, g_base_y);
        }

        // ---- layout ----
        Rectangle artR  = { PAD, TBH + 6, ARTS, ARTS };
        int infoY   = (int)(artR.y + artR.height) + 16;
        int titleY  = infoY + 18, metaY = titleY + 38;
        int specY   = metaY + 30, specH = 34;
        int barY    = specY + specH + 14, timesY = barY + 10, transY = timesY + 38;
        int volY    = transY + 44;
        Rectangle barRect = { PAD, (float)barY, WW - 2 * PAD, 4 };
        int mid = WW / 2;
        Rectangle playR  = { mid - 30, (float)transY - 30, 60, 60 };
        Rectangle prevR  = { mid - 92, (float)transY - 22, 44, 44 };
        Rectangle nextR  = { mid + 48, (float)transY - 22, 44, 44 };
        Rectangle shufR  = { PAD - 2, (float)transY - 16, 32, 32 };
        Rectangle repR   = { WW - PAD - 30, (float)transY - 16, 32, 32 };
        Rectangle openR  = { PAD, 12, 24, 22 };
        Rectangle queueR = { PAD + 30, 12, 24, 22 };
        Rectangle eqR    = { PAD + 60, 12, 26, 22 };
        Rectangle setR   = { PAD + 92, 12, 24, 22 };
        Rectangle lyrR   = { PAD + 122, 12, 24, 22 };
        Rectangle minR   = { WW - 58, 12, 22, 22 };
        Rectangle closeR = { WW - 32, 12, 22, 22 };
        Rectangle volRect = { (float)(PAD + 24), (float)volY, (float)(WW - 2 * PAD - 24), 6 };
        int qcount = playlist_count(&g_pl);
        // ---- drawer layout (logical coords within its own [0,DRAWER_W] strip) ----
        Rectangle dCard    = { 12, TBH + 8, DRAWER_W - 24, WH - (TBH + 8) - 16 };
        const int dRowH    = 34;
        DLayout   dl       = drawer_layout(dCard, g_drawer_view);
        int       dTotal   = (g_show_queue || dw > 0) ? drawer_filter_build(g_drawer_view) : 0;   // rows the open tab shows
        Rectangle dTabPlR  = { dCard.x + 12,  dCard.y + 10, 88, 26 };              // Playlist | Queue tabs
        Rectangle dTabQR   = { dCard.x + 104, dCard.y + 10, 88, 26 };
        Rectangle dSaveR   = { dCard.x + 12, dl.btnY, 84, 28 };                    // Save button
        Rectangle dOpenR   = { dCard.x + dCard.width - 96, dl.btnY, 84, 28 };      // Open library / Back
        Rectangle dClearR  = { dCard.x + dCard.width - 28, dCard.y + 12, 20, 20 }; // small Clear (×-all)
        Rectangle dYesR    = { dCard.x + dCard.width / 2 - 96, dCard.y + dCard.height / 2 + 8, 90, 30 };  // overwrite confirm
        Rectangle dNoR     = { dCard.x + dCard.width / 2 + 6,  dCard.y + dCard.height / 2 + 8, 90, 30 };
        // EQ panel geometry (lives in the art square)
        int eqTop = (int)artR.y + 60, eqBot = (int)(artR.y + artR.height) - 48;
        float eqSp = artR.width / 10.0f;
        Rectangle onR   = { artR.x + artR.width - 150, artR.y + 12, 56, 22 };
        Rectangle flatR = { artR.x + artR.width - 86, artR.y + 12, 70, 22 };
        Eq *eq = g_audio ? audio_get_eq(g_audio) : NULL;

        // ---- hover targets ----
        g_hv[HV_OPEN]  = approach(g_hv[HV_OPEN],  CheckCollisionPointRec(mp, openR), dt);
        g_hv[HV_QUEUE] = approach(g_hv[HV_QUEUE], CheckCollisionPointRec(mp, queueR), dt);
        g_hv[HV_EQ]    = approach(g_hv[HV_EQ],    CheckCollisionPointRec(mp, eqR), dt);
        g_hv[HV_SET]   = approach(g_hv[HV_SET],   CheckCollisionPointRec(mp, setR), dt);
        g_hv[HV_LYR]   = approach(g_hv[HV_LYR],   CheckCollisionPointRec(mp, lyrR), dt);
        g_hv[HV_MIN]   = approach(g_hv[HV_MIN],   CheckCollisionPointRec(mp, minR), dt);
        g_hv[HV_CLOSE] = approach(g_hv[HV_CLOSE], CheckCollisionPointRec(mp, closeR), dt);
        g_hv[HV_PLAY]  = approach(g_hv[HV_PLAY],  CheckCollisionPointRec(mp, playR), dt);
        g_hv[HV_PREV]  = approach(g_hv[HV_PREV],  CheckCollisionPointRec(mp, prevR), dt);
        g_hv[HV_NEXT]  = approach(g_hv[HV_NEXT],  CheckCollisionPointRec(mp, nextR), dt);
        g_hv[HV_SHUF]  = approach(g_hv[HV_SHUF],  CheckCollisionPointRec(mp, shufR), dt);
        g_hv[HV_REP]   = approach(g_hv[HV_REP],   CheckCollisionPointRec(mp, repR), dt);
        g_hv[HV_ART]   = approach(g_hv[HV_ART],   !g_show_eq && !g_show_settings && !g_show_lyrics && CheckCollisionPointRec(mp, artR), dt);

        // ---- context menu: a left press while open either acts or dismisses (and is swallowed) ----
        bool ctxSwallow = false;
        if (g_ctx_open && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            ctxSwallow = true;
            Rectangle m = ctx_menu_rect(dCard);
            const char *items[2]; bool en[2];
            int n = ctx_menu_items(items, en);
            for (int i = 0; i < n; i++) {
                if (!en[i] || !CheckCollisionPointRec(dmp, ctx_item_rect(m, i))) continue;
                if (g_ctx_view == 0 && i == 0) playlist_queue_insert_next(&g_pl, g_ctx_target);
                else if (g_ctx_view == 0 && i == 1) {
                    bool removedCur = playlist_remove(&g_pl, g_ctx_target);
                    if (removedCur) { const char *pp = playlist_current(&g_pl); if (pp) load_file(pp); else reset_now_playing(); }
                } else if (g_ctx_view == 2) playlist_queue_remove(&g_pl, g_ctx_target);
                break;
            }
            g_ctx_open = false;
        }
        // right-click a row in the playlist/queue tab → open the menu there
        if (g_show_queue && dw > 0 && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !g_naming && !g_confirm_overwrite) {
            g_ctx_open = false;
            if ((g_drawer_view == 0 || g_drawer_view == 2) &&
                CheckCollisionPointRec(dmp, (Rectangle){ dCard.x, (float)dl.listTop, dCard.width, (float)(dl.visible * dRowH) })) {
                int row = g_queue_scroll + ((int)dmp.y - dl.listTop) / dRowH;
                if (row >= 0 && row < dTotal) {
                    g_ctx_open = true; g_ctx_view = g_drawer_view; g_ctx_target = g_filt[row];
                    g_ctx_at = dmp;
                }
            }
        }

        // ---- clicks ----
        if (!ctxSwallow && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mp, closeR)) break;
            else if (CheckCollisionPointRec(mp, minR)) MinimizeWindow();
            else if (CheckCollisionPointRec(mp, openR)) open_dialog();
            else if (CheckCollisionPointRec(mp, queueR)) { g_show_queue = !g_show_queue; if (g_show_queue) { if (g_drawer_view == 1) g_drawer_view = 0; } else { g_naming = false; g_confirm_overwrite = false; } }
            else if (CheckCollisionPointRec(mp, eqR)) { g_show_eq = !g_show_eq; if (g_show_eq) { g_show_settings = g_show_lyrics = false; } }
            else if (CheckCollisionPointRec(mp, setR)) { g_show_settings = !g_show_settings; if (g_show_settings) { g_show_eq = g_show_lyrics = false; } }
            else if (CheckCollisionPointRec(mp, lyrR)) { g_show_lyrics = !g_show_lyrics; if (g_show_lyrics) { g_show_eq = g_show_settings = false; } }
            else if (g_show_settings && CheckCollisionPointRec(mp, artR)) {
                int ry0 = (int)artR.y + 50, rowH = 31;   // 7 rows must clear the DATA FOLDER block at +268
                int row = ((int)mp.y - ry0) / rowH;
                Rectangle foldBtn = { artR.x + 20, artR.y + 308, 130, 26 };
                if ((int)mp.y >= ry0 && row >= 0 && row < 7) {
                    if (row == 0) { g_aot = !g_aot; if (g_aot) SetWindowState(FLAG_WINDOW_TOPMOST); else ClearWindowState(FLAG_WINDOW_TOPMOST); }
                    else if (row == 1) playlist_set_shuffle(&g_pl, !playlist_shuffle(&g_pl));
                    else if (row == 2) { g_repeat = (g_repeat + 1) % 3; playlist_set_loop(&g_pl, g_repeat == 2); }
                    else if (row == 3) g_side ^= 1;                          // playlist drawer side
                    else if (row == 4) g_prev_mode ^= 1;                     // prev button behaviour
                    else if (row == 5 && g_audio) {
                        if (audio_get_volume(g_audio) > 0.001f) { g_premute = audio_get_volume(g_audio); audio_set_volume(g_audio, 0); }
                        else audio_set_volume(g_audio, g_premute);
                    }
                    else if (row == 6) { g_hisashi_menubar = !g_hisashi_menubar; menubar_set_enabled(g_hisashi_menubar); }
                }
                else if (CheckCollisionPointRec(mp, foldBtn)) os_reveal_dir(dataDir);
            }
            else if (g_show_eq && eq && CheckCollisionPointRec(mp, artR)) {
                if (CheckCollisionPointRec(mp, onR)) eq_set_enabled(eq, !eq_is_enabled(eq));
                else if (CheckCollisionPointRec(mp, flatR)) eq_flat(eq);
                else for (int b = 0; b < EQ_BANDS; b++) {
                    float cx = artR.x + eqSp * b + eqSp / 2;
                    if (mp.x >= cx - eqSp / 2 && mp.x < cx + eqSp / 2 && mp.y >= eqTop - 12 && mp.y <= eqBot + 12) {
                        g_eq_drag = b;
                        float tt = 1 - (mp.y - eqTop) / (float)(eqBot - eqTop); if (tt < 0) tt = 0; if (tt > 1) tt = 1;
                        eq_set_gain(eq, b, tt * 24 - 12);
                        break;
                    }
                }
            }
            else if (!g_show_eq && !g_show_settings && !g_show_lyrics && CheckCollisionPointRec(mp, artR)) g_art_mode = (g_art_mode + 1) % 3;
            else if (loaded && CheckCollisionPointRec(mp, playR)) { if (playing) audio_pause(g_audio); else audio_play(g_audio); }
            else if (CheckCollisionPointRec(mp, prevR)) do_prev();
            else if (CheckCollisionPointRec(mp, nextR)) { if (playlist_has_next(&g_pl)) load_file(playlist_next(&g_pl)); }
            else if (CheckCollisionPointRec(mp, shufR)) playlist_set_shuffle(&g_pl, !playlist_shuffle(&g_pl));
            else if (CheckCollisionPointRec(mp, repR)) { g_repeat = (g_repeat + 1) % 3; playlist_set_loop(&g_pl, g_repeat == 2); }
            else if (loaded && CheckCollisionPointRec(mp, (Rectangle){ barRect.x - 6, barRect.y - 11, barRect.width + 12, 26 })) {
                pos_drag = true;
                scrub_t = (mp.x - barRect.x) / barRect.width; if (scrub_t < 0) scrub_t = 0; if (scrub_t > 1) scrub_t = 1;
            }
            else if (CheckCollisionPointRec(mp, (Rectangle){ volRect.x - 6, volRect.y - 9, volRect.width + 12, 24 })) vol_drag = true;
        }

        // ---- drawer clicks (dmp is drawer-local; rects live in [0,DRAWER_W]) ----
        if (!ctxSwallow && g_show_queue && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (g_confirm_overwrite) {
                if (CheckCollisionPointRec(dmp, dYesR)) { drawer_save(g_name_buf); g_confirm_overwrite = false; }
                else if (CheckCollisionPointRec(dmp, dNoR)) { g_confirm_overwrite = false; }
            } else if (g_naming) {
                if (CheckCollisionPointRec(dmp, dYesR)) {
                    if (g_name_buf[0]) {
                        if (playlistio_exists(g_name_buf)) { g_naming = false; g_confirm_overwrite = true; }
                        else { drawer_save(g_name_buf); g_naming = false; }
                    }
                } else if (CheckCollisionPointRec(dmp, dNoR)) g_naming = false;      // cancel
            } else if (g_drawer_view == 1) {                                        // saved-playlist library
                if (CheckCollisionPointRec(dmp, dOpenR)) g_drawer_view = 0;         // Back
                else if (CheckCollisionPointRec(dmp, (Rectangle){ dCard.x, (float)dl.listTop, dCard.width, (float)(dl.visible * dRowH) })) {
                    int idx = g_queue_scroll + ((int)dmp.y - dl.listTop) / dRowH;
                    if (idx >= 0 && idx < g_saved_count && playlistio_load(&g_pl, g_saved_names[idx]) >= 0) {
                        playlist_rebuild_queue(&g_pl);
                        load_file(playlist_current(&g_pl));
                        g_drawer_view = 0; g_queue_scroll = 0;
                    }
                }
            } else {                                                                // playlist / queue tabs
                bool onSearch = CheckCollisionPointRec(dmp, dl.search);
                if (onSearch && g_search[0] && dmp.x >= dl.search.x + dl.search.width - 26) g_search[0] = 0;  // clear ×
                g_search_focus = onSearch;   // focus follows the press; clicks elsewhere unfocus
                if (CheckCollisionPointRec(dmp, dTabPlR)) {
                    if (g_drawer_view != 0) { g_drawer_view = 0; g_search[0] = 0; g_search_focus = false; g_queue_scroll = 0; g_last_click_idx = -1; }
                } else if (CheckCollisionPointRec(dmp, dTabQR)) {
                    if (g_drawer_view != 2) {
                        g_drawer_view = 2; g_search[0] = 0; g_search_focus = false; g_last_click_idx = -1;
                        g_queue_scroll = playlist_queue_pos(&g_pl) - 1;   // open with the current song in view (clamped below)
                        if (g_queue_scroll < 0) g_queue_scroll = 0;
                    }
                } else if (g_drawer_view == 2 && CheckCollisionPointRec(dmp, (Rectangle){ dCard.x, (float)dl.listTop, dCard.width, (float)(dl.visible * dRowH) })) {
                    int row = g_queue_scroll + ((int)dmp.y - dl.listTop) / dRowH;
                    if (row >= 0 && row < dTotal) {
                        int pos = g_filt[row];
                        if (dmp.x >= dCard.x + dCard.width - 30 && dmp.x <= dCard.x + dCard.width - 4) {
                            playlist_queue_remove(&g_pl, pos);   // remove × (the playing entry refuses)
                        } else {
                            double now = GetTime();
                            if (g_last_click_idx == pos && now - g_last_click_t < 0.35) {   // double-click → jump; queue order untouched
                                playlist_queue_jump(&g_pl, pos); load_file(playlist_current(&g_pl));
                                g_last_click_t = -1;
                            } else {
                                g_last_click_t = now; g_last_click_idx = pos;
                                if (!g_search[0]) { g_q_press = pos; g_q_press_y = dmp.y; g_q_drag_view = 2; }   // drag-reorder on the unfiltered list
                            }
                        }
                    }
                } else if (g_drawer_view == 0 && CheckCollisionPointRec(dmp, dCard)) {
                    if (qcount > 0 && CheckCollisionPointRec(dmp, dClearR)) { playlist_clear(&g_pl); reset_now_playing(); g_queue_scroll = 0; g_search[0] = 0; }
                    else if (CheckCollisionPointRec(dmp, dSaveR) && playlist_dirty(&g_pl) && qcount > 0) {
                        if (playlist_name(&g_pl)[0]) { snprintf(g_name_buf, sizeof(g_name_buf), "%s", playlist_name(&g_pl)); g_confirm_overwrite = true; }
                        else { g_naming = true; g_name_buf[0] = 0; }
                    }
                    else if (CheckCollisionPointRec(dmp, dOpenR)) { g_saved_count = playlistio_list(g_saved_names, PL_MAX_SAVED); g_drawer_view = 1; g_queue_scroll = 0; g_search_focus = false; }
                    else if (CheckCollisionPointRec(dmp, (Rectangle){ dCard.x, (float)dl.listTop, dCard.width, (float)(dl.visible * dRowH) })) {
                        int row = g_queue_scroll + ((int)dmp.y - dl.listTop) / dRowH;
                        if (row >= 0 && row < dTotal) {
                            int idx = g_filt[row];
                            if (dmp.x >= dCard.x + dCard.width - 30 && dmp.x <= dCard.x + dCard.width - 4) {  // remove ×
                                bool removedCur = playlist_remove(&g_pl, idx);
                                if (removedCur) { const char *p = playlist_current(&g_pl); if (p) load_file(p); else reset_now_playing(); }
                            } else {
                                double now = GetTime();
                                if (g_last_click_idx == idx && now - g_last_click_t < 0.35) {   // double-click → play (shuffle on: this song first, rest reshuffled)
                                    playlist_play_index(&g_pl, idx); load_file(playlist_current(&g_pl));
                                    g_last_click_t = -1;
                                } else {
                                    g_last_click_t = now; g_last_click_idx = idx;
                                    if (!g_search[0]) { g_q_press = idx; g_q_press_y = dmp.y; g_q_drag_view = 0; }  // drag-reorder only on the unfiltered list
                                }
                            }
                        }
                    }
                }
            }
        } else if (!ctxSwallow && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_search_focus = false;

        // drag-to-reorder (unfiltered lists only, so row == playlist index / queue pos)
        if (g_q_press >= 0 && g_q_drag < 0 && fabsf(mp.y - g_q_press_y) > 6) g_q_drag = g_q_press;
        if (g_q_drag >= 0) {
            int t = g_queue_scroll + ((int)dmp.y - dl.listTop) / dRowH;
            int lim = (g_q_drag_view == 2) ? playlist_queue_count(&g_pl) : qcount;
            t = clampi(t, 0, lim - 1);
            g_q_target = t;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (g_q_drag >= 0 && g_q_target >= 0 && g_q_target != g_q_drag) {
                if (g_q_drag_view == 2) playlist_queue_move(&g_pl, g_q_drag, g_q_target);
                else                    playlist_move(&g_pl, g_q_drag, g_q_target);
            }
            g_q_press = g_q_drag = g_q_target = -1;
            vol_drag = false;
        }
        if (vol_drag && g_audio) { float v = (mp.x - volRect.x) / volRect.width; if (v < 0) v = 0; if (v > 1) v = 1; audio_set_volume(g_audio, v); }
        if (pos_drag) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                scrub_t = (mp.x - barRect.x) / barRect.width; if (scrub_t < 0) scrub_t = 0; if (scrub_t > 1) scrub_t = 1;
            } else {  // released → commit the seek (one seek, no decoder thrash)
                if (loaded) audio_seek_seconds(g_audio, scrub_t * audio_length_seconds(g_audio));
                pos_drag = false;
            }
        }
        if (g_eq_drag >= 0) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && eq) {
                float tt = 1 - (mp.y - eqTop) / (float)(eqBot - eqTop); if (tt < 0) tt = 0; if (tt > 1) tt = 1;
                eq_set_gain(eq, g_eq_drag, tt * 24 - 12);
            } else g_eq_drag = -1;
        }

        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            if (g_show_queue) {
                g_queue_scroll -= (int)wheel; int maxs = dTotal - dl.visible; if (maxs < 0) maxs = 0;
                g_queue_scroll = clampi(g_queue_scroll, 0, maxs);
            } else if (g_show_lyrics && !g_lyrics.synced) {
                g_lyrics_scroll -= (int)(wheel * 28);
                int maxs = g_lyrics.count * 24 - (ARTS - 80); if (maxs < 0) maxs = 0;
                g_lyrics_scroll = clampi(g_lyrics_scroll, 0, maxs);
            } else if (g_audio) audio_set_volume(g_audio, audio_get_volume(g_audio) + wheel * 0.05f);
        }
        // ---- text entry: Save-flow name, or the open tab's search box ----
        if (!g_show_queue) { g_search_focus = false; g_ctx_open = false; }
        if (g_ctx_open && IsKeyPressed(KEY_ESCAPE)) g_ctx_open = false;
        if (g_naming) {
            text_input_utf8(g_name_buf, sizeof(g_name_buf), true);
            if (IsKeyPressed(KEY_BACKSPACE)) text_backspace_utf8(g_name_buf);
            if (IsKeyPressed(KEY_ENTER) && g_name_buf[0]) {
                if (playlistio_exists(g_name_buf)) { g_naming = false; g_confirm_overwrite = true; }
                else { drawer_save(g_name_buf); g_naming = false; }
            }
            if (IsKeyPressed(KEY_ESCAPE)) g_naming = false;
        } else if (g_confirm_overwrite) {
            if (IsKeyPressed(KEY_ENTER))  { drawer_save(g_name_buf); g_confirm_overwrite = false; }
            if (IsKeyPressed(KEY_ESCAPE)) g_confirm_overwrite = false;
        } else if (g_search_focus) {
            text_input_utf8(g_search, sizeof(g_search), false);
            if (IsKeyPressed(KEY_BACKSPACE)) text_backspace_utf8(g_search);
            if (IsKeyPressed(KEY_ESCAPE)) { if (g_search[0]) g_search[0] = 0; else g_search_focus = false; }
            if (IsKeyPressed(KEY_ENTER)) g_search_focus = false;
        }
        if (!g_naming && !g_confirm_overwrite && !g_search_focus) {
        if (IsKeyPressed(KEY_SPACE) && loaded) { if (playing) audio_pause(g_audio); else audio_play(g_audio); }
        if (loaded && IsKeyPressed(KEY_RIGHT)) audio_seek_seconds(g_audio, audio_position_seconds(g_audio) + 5);
        if (loaded && IsKeyPressed(KEY_LEFT))  audio_seek_seconds(g_audio, audio_position_seconds(g_audio) - 5);
        if (g_audio && IsKeyPressed(KEY_UP))   audio_set_volume(g_audio, audio_get_volume(g_audio) + 0.05f);
        if (g_audio && IsKeyPressed(KEY_DOWN)) audio_set_volume(g_audio, audio_get_volume(g_audio) - 0.05f);
        if (IsKeyPressed(KEY_Q)) { g_show_queue = !g_show_queue; if (g_show_queue) { if (g_drawer_view == 1) g_drawer_view = 0; } else { g_naming = false; g_confirm_overwrite = false; } }
        if (IsKeyPressed(KEY_E)) { g_show_eq = !g_show_eq; if (g_show_eq) { g_show_settings = g_show_lyrics = false; } }
        if (IsKeyPressed(KEY_G)) { g_show_settings = !g_show_settings; if (g_show_settings) { g_show_eq = g_show_lyrics = false; } }
        if (IsKeyPressed(KEY_Y)) { g_show_lyrics = !g_show_lyrics; if (g_show_lyrics) { g_show_eq = g_show_settings = false; } }
        if (IsKeyPressed(KEY_O)) open_dialog();
        if (IsKeyPressed(KEY_S)) playlist_set_shuffle(&g_pl, !playlist_shuffle(&g_pl));
        if (IsKeyPressed(KEY_L)) { g_repeat = (g_repeat + 1) % 3; playlist_set_loop(&g_pl, g_repeat == 2); }
        if (IsKeyPressed(KEY_T)) { g_aot = !g_aot; if (g_aot) SetWindowState(FLAG_WINDOW_TOPMOST); else ClearWindowState(FLAG_WINDOW_TOPMOST); }
        }  // end !g_naming && !g_confirm_overwrite && !g_search_focus

        // Clicks/typing above may have switched tabs or changed the list/filter —
        // recompute the drawer layout + visible rows so this frame draws the new state.
        if (g_show_queue || dw > 0) {
            dl = drawer_layout(dCard, g_drawer_view);
            dTotal = drawer_filter_build(g_drawer_view);
            dSaveR.y = dOpenR.y = dl.btnY;
            int maxs = dTotal - dl.visible; if (maxs < 0) maxs = 0;
            g_queue_scroll = clampi(g_queue_scroll, 0, maxs);
        }
        qcount = playlist_count(&g_pl);

        // ---- spectrum data ----
        float samp[512];
        if (g_audio) audio_snapshot_waveform(g_audio, samp, 512); else for (int i = 0; i < 512; i++) samp[i] = 0;
        float mags[256], bands[64];
        fft_magnitudes(samp, 512, mags);
        fft_log_bands(mags, 256, bands, NBARS);
        for (int i = 0; i < NBARS; i++) {
            float tg = bands[i];
            g_bars[i] += (tg - g_bars[i]) * (tg > g_bars[i] ? 0.55f : 0.14f);
            if (g_bars[i] >= g_peaks[i]) g_peaks[i] = g_bars[i];
            else { g_peaks[i] -= 0.012f; if (g_peaks[i] < 0) g_peaks[i] = 0; }
        }

        // ===== draw (into supersampled target) =====
        BeginTextureMode(target);
        ClearBackground(BG1);
        rlPushMatrix();
        rlScalef((float)SS, (float)SS, 1.0f);

        // ================= playlist drawer (extends out the side) =================
        if (dw > 0) {
            rlPushMatrix();
            rlTranslatef((float)drawerOffX, 0, 0);
            DrawRectangleGradientV(0, 0, DRAWER_W, WH, BG0, BG1);                                  // strip matches player bg
            DrawRectangle((g_side == 1) ? DRAWER_W - 1 : 0, 0, 1, WH, (Color){ 0, 0, 0, 70 });     // seam
            rrBox(dCard, 0.05f, CARDBG, (Color){ 255, 255, 255, 16 });

            if (g_confirm_overwrite) {
                const char *q1 = "Overwrite playlist";
                char q2[PL_NAME_MAX + 4]; snprintf(q2, sizeof(q2), "\"%s\"?", g_name_buf);
                Vector2 w1 = MeasureTextEx(fMeta, q1, 16, 0.3f), w2 = MeasureTextEx(fMeta, q2, 16, 0.3f);
                DrawTextEx(fMeta, q1, (Vector2){ dCard.x + (dCard.width - w1.x) / 2, dCard.y + dCard.height / 2 - 46 }, 16, 0.3f, TXT);
                DrawTextEx(fMeta, q2, (Vector2){ dCard.x + (dCard.width - w2.x) / 2, dCard.y + dCard.height / 2 - 22 }, 16, 0.3f, alpha(g_accent, 230));
                bool hy = CheckCollisionPointRec(dmp, dYesR), hn = CheckCollisionPointRec(dmp, dNoR);
                rrFill(dYesR, 0.4f, hy ? g_accent : alpha(g_accent, 110));
                Vector2 yw = MeasureTextEx(fSmall, "Overwrite", 13, 0.3f);
                DrawTextEx(fSmall, "Overwrite", (Vector2){ dYesR.x + (dYesR.width - yw.x) / 2, dYesR.y + 8 }, 13, 0.3f, hy ? BG1 : TXT);
                rrBox(dNoR, 0.4f, CARDBG, hn ? alpha(TXT, 220) : alpha(MUT, 150));
                Vector2 nw = MeasureTextEx(fSmall, "Cancel", 13, 0.3f);
                DrawTextEx(fSmall, "Cancel", (Vector2){ dNoR.x + (dNoR.width - nw.x) / 2, dNoR.y + 8 }, 13, 0.3f, hn ? TXT : MUT);
            } else if (g_naming) {
                // centered naming modal — its own field + Save/Cancel, so nothing overlaps
                const char *lbl = "Name this playlist";
                Vector2 lw = MeasureTextEx(fMeta, lbl, 16, 0.3f);
                DrawTextEx(fMeta, lbl, (Vector2){ dCard.x + (dCard.width - lw.x) / 2, dCard.y + dCard.height / 2 - 58 }, 16, 0.3f, TXT);
                Rectangle fld = { dCard.x + 22, dCard.y + dCard.height / 2 - 30, dCard.width - 44, 34 };
                rrBox(fld, 0.3f, (Color){ 12, 11, 8, 255 }, alpha(g_accent, 170));
                draw_fit(fMeta, g_name_buf, (Vector2){ fld.x + 12, fld.y + 8 }, 18, 0.3f, TXT, fld.width - 26);
                Vector2 tw = MeasureTextEx(fMeta, g_name_buf, 18, 0.3f);
                float caretx = fld.x + 13 + (tw.x < fld.width - 26 ? tw.x : fld.width - 26);
                if (((frame / 30) & 1) == 0) DrawRectangle((int)caretx, (int)(fld.y + 9), 2, 18, alpha(TXT, 220));
                bool active = (g_name_buf[0] != 0);
                bool hy = CheckCollisionPointRec(dmp, dYesR), hn = CheckCollisionPointRec(dmp, dNoR);
                rrFill(dYesR, 0.4f, active ? (hy ? g_accent : alpha(g_accent, 130)) : alpha(MUT, 45));
                Vector2 yw = MeasureTextEx(fSmall, "Save", 13, 0.3f);
                DrawTextEx(fSmall, "Save", (Vector2){ dYesR.x + (dYesR.width - yw.x) / 2, dYesR.y + 8 }, 13, 0.3f, active ? BG1 : alpha(MUT, 120));
                rrBox(dNoR, 0.4f, CARDBG, hn ? alpha(TXT, 220) : alpha(MUT, 150));
                Vector2 nw = MeasureTextEx(fSmall, "Cancel", 13, 0.3f);
                DrawTextEx(fSmall, "Cancel", (Vector2){ dNoR.x + (dNoR.width - nw.x) / 2, dNoR.y + 8 }, 13, 0.3f, hn ? TXT : MUT);
            } else {
                if (g_drawer_view != 1) {
                    // --- tabs: Playlist | Queue ---
                    Rectangle tabs[2] = { dTabPlR, dTabQR };
                    const char *tlbl[2] = { "Playlist", "Queue" };
                    int tview[2] = { 0, 2 };
                    for (int i = 0; i < 2; i++) {
                        bool active = (g_drawer_view == tview[i]);
                        bool ht = CheckCollisionPointRec(dmp, tabs[i]);
                        if (active) rrBox(tabs[i], 0.4f, alpha(g_accent, 40), alpha(g_accent, 200));
                        else if (ht) rrFill(tabs[i], 0.4f, (Color){ 255, 255, 255, 12 });
                        Vector2 tw = MeasureTextEx(fSmall, tlbl[i], 14, 0.3f);
                        DrawTextEx(fSmall, tlbl[i], (Vector2){ tabs[i].x + (tabs[i].width - tw.x) / 2, tabs[i].y + 5 }, 14, 0.3f,
                                   active ? g_accent : (ht ? TXT : MUT));
                    }
                    // count (+ clear × on the playlist tab) at the right of the tab row
                    char cnt[16]; snprintf(cnt, sizeof(cnt), "%d", qcount);
                    Vector2 cw = MeasureTextEx(fSmall, cnt, 13, 0.5f);
                    DrawTextEx(fSmall, cnt, (Vector2){ dClearR.x - 10 - cw.x, dCard.y + 14 }, 13, 0.5f, alpha(MUT, 150));
                    if (g_drawer_view == 0 && qcount > 0) {
                        bool hc = CheckCollisionPointRec(dmp, dClearR);
                        float cx2 = dClearR.x + dClearR.width / 2.0f, cy2 = dClearR.y + dClearR.height / 2.0f;
                        Color cc = hc ? alpha(g_accent, 230) : alpha(MUT, 160);
                        DrawLineEx((Vector2){ cx2 - 5, cy2 - 5 }, (Vector2){ cx2 + 5, cy2 + 5 }, 1.7f, cc);
                        DrawLineEx((Vector2){ cx2 + 5, cy2 - 5 }, (Vector2){ cx2 - 5, cy2 + 5 }, 1.7f, cc);
                    }
                    draw_search_box(dl.search, dmp, frame);
                }
                // --- per-view header ---
                if (g_drawer_view == 0) {
                    const char *pname = playlist_name(&g_pl);
                    const char *title = pname[0] ? pname : "Untitled playlist";
                    draw_fit(fMeta, title, (Vector2){ dCard.x + 14, dCard.y + 46 }, 17, 0.3f, pname[0] ? TXT : alpha(TXT, 175), dCard.width - 28);
                    if (playlist_dirty(&g_pl)) {
                        Vector2 tw = MeasureTextEx(fMeta, title, 17, 0.3f);
                        float dx = dCard.x + 18 + (tw.x < dCard.width - 42 ? tw.x : dCard.width - 42);
                        DrawCircle((int)dx, (int)(dCard.y + 55), 3, g_accent);
                    }
                    bool sActive = (playlist_dirty(&g_pl) && qcount > 0);
                    bool hs = CheckCollisionPointRec(dmp, dSaveR);
                    rrFill(dSaveR, 0.4f, sActive ? (hs ? g_accent : alpha(g_accent, 130)) : alpha(MUT, 45));
                    Vector2 sw = MeasureTextEx(fSmall, "Save", 14, 0.3f);
                    DrawTextEx(fSmall, "Save", (Vector2){ dSaveR.x + (dSaveR.width - sw.x) / 2, dSaveR.y + 7 }, 14, 0.3f, sActive ? BG1 : alpha(MUT, 120));
                    bool ho = CheckCollisionPointRec(dmp, dOpenR);
                    rrBox(dOpenR, 0.4f, CARDBG, ho ? alpha(TXT, 220) : alpha(MUT, 150));
                    Vector2 ow2 = MeasureTextEx(fSmall, "Open", 14, 0.3f);
                    DrawTextEx(fSmall, "Open", (Vector2){ dOpenR.x + (dOpenR.width - ow2.x) / 2, dOpenR.y + 7 }, 14, 0.3f, ho ? TXT : MUT);
                } else if (g_drawer_view == 2) {
                    DrawTextEx(fEye, "UP NEXT", (Vector2){ dCard.x + 14, dCard.y + 47 }, 11, 2.0f, alpha(g_accent, 190));
                    char sh[48];
                    snprintf(sh, sizeof(sh), "%s%s", playlist_shuffle(&g_pl) ? "shuffle on" : "playlist order",
                             playlist_queue_edited(&g_pl) ? " \xc2\xb7 edited" : "");
                    Vector2 shw = MeasureTextEx(fSmall, sh, 12, 0.3f);
                    DrawTextEx(fSmall, sh, (Vector2){ dCard.x + dCard.width - 14 - shw.x, dCard.y + 46 }, 12, 0.3f, alpha(MUT, 160));
                } else {
                    DrawTextEx(fMeta, "Saved playlists", (Vector2){ dCard.x + 14, dCard.y + 13 }, 17, 0.3f, alpha(TXT, 175));
                    bool ho = CheckCollisionPointRec(dmp, dOpenR);
                    rrBox(dOpenR, 0.4f, CARDBG, ho ? alpha(TXT, 220) : alpha(MUT, 150));
                    Vector2 ow2 = MeasureTextEx(fSmall, "< Back", 14, 0.3f);
                    DrawTextEx(fSmall, "< Back", (Vector2){ dOpenR.x + (dOpenR.width - ow2.x) / 2, dOpenR.y + 7 }, 14, 0.3f, ho ? TXT : MUT);
                }
                // --- list area ---
                if (g_drawer_view == 0) {
                    int cur = playlist_index(&g_pl);
                    for (int r = 0; r < dl.visible; r++) {
                        int rowi = g_queue_scroll + r;
                        if (rowi >= dTotal) break;
                        int idx = g_filt[rowi];
                        float ry = (float)(dl.listTop + r * dRowH);
                        bool isCur = (idx == cur);
                        bool hov = CheckCollisionPointRec(dmp, (Rectangle){ dCard.x, ry, dCard.width, (float)dRowH });
                        bool isDrag = (g_q_drag_view == 0 && idx == g_q_drag);
                        Rectangle row = { dCard.x + 6, ry, dCard.width - 12, (float)dRowH - 4 };
                        if (isCur)        DrawRectangleRounded(row, 0.35f, 6, alpha(g_accent, 32));
                        else if (isDrag)  DrawRectangleRounded(row, 0.35f, 6, (Color){ 255, 255, 255, 22 });
                        else if (hov)     DrawRectangleRounded(row, 0.35f, 6, (Color){ 255, 255, 255, 12 });
                        char num[16]; snprintf(num, sizeof(num), "%d", idx + 1);
                        DrawTextEx(fSmall, num, (Vector2){ dCard.x + 14, ry + 9 }, 13, 1.0f, isCur ? g_accent : alpha(MUT, 140));
                        char nm[256]; row_display_name(g_pl.paths[idx], nm, sizeof(nm));
                        draw_fit(fMeta, nm, (Vector2){ dCard.x + 40, ry + 7 }, 15, 0.2f, isCur ? TXT : alpha(TXT, 205), dCard.width - 80);
                        if (hov || isDrag) {
                            float xx = dCard.x + dCard.width - 17, xy = ry + (dRowH - 4) / 2.0f;
                            Color xc = alpha(TXT, 210);
                            DrawLineEx((Vector2){ xx - 5, xy - 5 }, (Vector2){ xx + 5, xy + 5 }, 1.7f, xc);
                            DrawLineEx((Vector2){ xx + 5, xy - 5 }, (Vector2){ xx - 5, xy + 5 }, 1.7f, xc);
                        }
                    }
                    if (g_q_drag >= 0 && g_q_drag_view == 0 && g_q_target >= 0 && g_q_target >= g_queue_scroll && g_q_target < g_queue_scroll + dl.visible) {
                        float ly = (float)(dl.listTop + (g_q_target - g_queue_scroll) * dRowH);
                        DrawRectangleRounded((Rectangle){ dCard.x + 6, ly - 1, dCard.width - 12, 2 }, 1, 4, g_accent);
                    }
                    if (dTotal > dl.visible) {
                        float tH = (float)(dl.visible * dRowH);
                        DrawRectangleRounded((Rectangle){ dCard.x + dCard.width - 5, (float)dl.listTop + tH * g_queue_scroll / dTotal, 3, tH * dl.visible / dTotal }, 1, 4, alpha(g_accent, 120));
                    }
                    if (qcount == 0 || dTotal == 0) {
                        const char *e = (qcount == 0) ? "Drop or open songs" : "No matches";
                        Vector2 ew = MeasureTextEx(fMeta, e, 15, 0.3f);
                        DrawTextEx(fMeta, e, (Vector2){ dCard.x + (dCard.width - ew.x) / 2, dCard.y + dCard.height / 2 - 8 }, 15, 0.3f, alpha(MUT, 130));
                    }
                } else if (g_drawer_view == 2) {
                    int qpos = playlist_queue_pos(&g_pl);
                    for (int r = 0; r < dl.visible; r++) {
                        int rowi = g_queue_scroll + r;
                        if (rowi >= dTotal) break;
                        int pos = g_filt[rowi];
                        int idx = playlist_queue_at(&g_pl, pos);
                        if (idx < 0) continue;
                        float ry = (float)(dl.listTop + r * dRowH);
                        bool isCur = (pos == qpos);
                        bool past  = (pos < qpos);   // already played this pass — dimmed
                        bool hov = CheckCollisionPointRec(dmp, (Rectangle){ dCard.x, ry, dCard.width, (float)dRowH });
                        bool isDrag = (g_q_drag_view == 2 && pos == g_q_drag);
                        Rectangle row = { dCard.x + 6, ry, dCard.width - 12, (float)dRowH - 4 };
                        if (isCur)        DrawRectangleRounded(row, 0.35f, 6, alpha(g_accent, 32));
                        else if (isDrag)  DrawRectangleRounded(row, 0.35f, 6, (Color){ 255, 255, 255, 22 });
                        else if (hov)     DrawRectangleRounded(row, 0.35f, 6, (Color){ 255, 255, 255, 12 });
                        char num[16]; snprintf(num, sizeof(num), "%d", pos + 1);
                        DrawTextEx(fSmall, num, (Vector2){ dCard.x + 14, ry + 9 }, 13, 1.0f, isCur ? g_accent : alpha(MUT, past ? 90 : 140));
                        char nm[256]; row_display_name(g_pl.paths[idx], nm, sizeof(nm));
                        draw_fit(fMeta, nm, (Vector2){ dCard.x + 40, ry + 7 }, 15, 0.2f, isCur ? TXT : alpha(TXT, past ? 120 : 205), dCard.width - 80);
                        if ((hov || isDrag) && !isCur) {   // remove × (the playing entry can't be removed)
                            float xx = dCard.x + dCard.width - 17, xy = ry + (dRowH - 4) / 2.0f;
                            Color xc = alpha(TXT, 210);
                            DrawLineEx((Vector2){ xx - 5, xy - 5 }, (Vector2){ xx + 5, xy + 5 }, 1.7f, xc);
                            DrawLineEx((Vector2){ xx + 5, xy - 5 }, (Vector2){ xx - 5, xy + 5 }, 1.7f, xc);
                        }
                    }
                    if (g_q_drag >= 0 && g_q_drag_view == 2 && g_q_target >= 0 && g_q_target >= g_queue_scroll && g_q_target < g_queue_scroll + dl.visible) {
                        float ly = (float)(dl.listTop + (g_q_target - g_queue_scroll) * dRowH);
                        DrawRectangleRounded((Rectangle){ dCard.x + 6, ly - 1, dCard.width - 12, 2 }, 1, 4, g_accent);
                    }
                    if (dTotal > dl.visible) {
                        float tH = (float)(dl.visible * dRowH);
                        DrawRectangleRounded((Rectangle){ dCard.x + dCard.width - 5, (float)dl.listTop + tH * g_queue_scroll / dTotal, 3, tH * dl.visible / dTotal }, 1, 4, alpha(g_accent, 120));
                    }
                    if (qcount == 0 || dTotal == 0) {
                        const char *e = (qcount == 0) ? "Queue is empty" : "No matches";
                        Vector2 ew = MeasureTextEx(fMeta, e, 15, 0.3f);
                        DrawTextEx(fMeta, e, (Vector2){ dCard.x + (dCard.width - ew.x) / 2, dCard.y + dCard.height / 2 - 8 }, 15, 0.3f, alpha(MUT, 130));
                    }
                } else {
                    for (int r = 0; r < dl.visible; r++) {
                        int idx = g_queue_scroll + r;
                        if (idx >= g_saved_count) break;
                        float ry = (float)(dl.listTop + r * dRowH);
                        bool hov = CheckCollisionPointRec(dmp, (Rectangle){ dCard.x, ry, dCard.width, (float)dRowH });
                        Rectangle row = { dCard.x + 6, ry, dCard.width - 12, (float)dRowH - 4 };
                        if (hov) DrawRectangleRounded(row, 0.35f, 6, (Color){ 255, 255, 255, 12 });
                        bool isOpen = (playlist_name(&g_pl)[0] && strcmp(playlist_name(&g_pl), g_saved_names[idx]) == 0);
                        DrawCircle((int)(dCard.x + 18), (int)(ry + dRowH / 2.0f - 2), 3, isOpen ? g_accent : alpha(MUT, 170));
                        draw_fit(fMeta, g_saved_names[idx], (Vector2){ dCard.x + 32, ry + 7 }, 15, 0.2f, isOpen ? g_accent : alpha(TXT, 210), dCard.width - 46);
                    }
                    if (g_saved_count == 0) {
                        const char *e = "No saved playlists";
                        Vector2 ew = MeasureTextEx(fMeta, e, 15, 0.3f);
                        DrawTextEx(fMeta, e, (Vector2){ dCard.x + (dCard.width - ew.x) / 2, dCard.y + dCard.height / 2 - 8 }, 15, 0.3f, alpha(MUT, 130));
                    }
                    if (g_saved_count > dl.visible) {
                        float tH = (float)(dl.visible * dRowH);
                        DrawRectangleRounded((Rectangle){ dCard.x + dCard.width - 5, (float)dl.listTop + tH * g_queue_scroll / g_saved_count, 3, tH * dl.visible / g_saved_count }, 1, 4, alpha(g_accent, 120));
                    }
                }
            }
            // ---- row context menu (drawn above everything in the drawer) ----
            if (g_ctx_open && !g_naming && !g_confirm_overwrite && g_drawer_view == g_ctx_view) {
                Rectangle m = ctx_menu_rect(dCard);
                soft_shadow(m, 0.25f, 8, 120);
                rrBox(m, 0.25f, (Color){ 30, 27, 21, 255 }, alpha(MUT, 130));
                const char *items[2]; bool en[2];
                int n = ctx_menu_items(items, en);
                for (int i = 0; i < n; i++) {
                    Rectangle ir = ctx_item_rect(m, i);
                    bool hi = en[i] && CheckCollisionPointRec(dmp, ir);
                    if (hi) DrawRectangleRounded(ir, 0.35f, 6, alpha(g_accent, 36));
                    DrawTextEx(fSmall, items[i], (Vector2){ ir.x + 12, ir.y + 7 }, 14, 0.2f, en[i] ? (hi ? TXT : alpha(TXT, 215)) : alpha(MUT, 110));
                }
            }
            rlPopMatrix();
        }

        // ===================== player =====================
        rlPushMatrix();
        rlTranslatef((float)playerOffX, 0, 0);
        DrawRectangleGradientV(0, 0, WW, WH, BG0, BG1);

        // ---- art / visualizer ----
        if (g_show_eq) {
            rrBox(artR, 0.05f, CARDBG, (Color){ 255, 255, 255, 14 });
            bool on = eq && eq_is_enabled(eq);
            DrawTextEx(fEye, "EQUALIZER", (Vector2){ artR.x + 16, artR.y + 18 }, 12, 3.0f, alpha(g_accent, 205));
            rrFill(onR, 0.4f, on ? g_accent : TRK);
            const char *onl = on ? "ON" : "OFF";
            Vector2 ow = MeasureTextEx(fSmall, onl, 13, 0.5f);
            DrawTextEx(fSmall, onl, (Vector2){ onR.x + (onR.width - ow.x) / 2, onR.y + 4 }, 13, 0.5f, on ? BG1 : TXT);
            rrBox(flatR, 0.4f, CARDBG, alpha(MUT, 180));
            Vector2 fw2 = MeasureTextEx(fSmall, "FLAT", 13, 0.5f);
            DrawTextEx(fSmall, "FLAT", (Vector2){ flatR.x + (flatR.width - fw2.x) / 2, flatR.y + 4 }, 13, 0.5f, MUT);
            int midY = (eqTop + eqBot) / 2;
            for (int b = 0; b < EQ_BANDS; b++) {
                float cx = artR.x + eqSp * b + eqSp / 2;
                DrawRectangleRounded((Rectangle){ cx - 2.5f, (float)eqTop, 5, (float)(eqBot - eqTop) }, 1, 6, TRK);
                DrawRectangle((int)(cx - 7), midY, 14, 1, alpha(MUT, 110));
                float g = eq ? eq_get_gain(eq, b) : 0;
                int ty = eqTop + (int)((1 - (g + 12) / 24) * (eqBot - eqTop));
                Color fc = on ? g_accent : alpha(g_accent, 110);
                if (ty < midY) DrawRectangleRounded((Rectangle){ cx - 2.5f, (float)ty, 5, (float)(midY - ty) }, 1, 4, fc);
                else           DrawRectangleRounded((Rectangle){ cx - 2.5f, (float)midY, 5, (float)(ty - midY) }, 1, 4, fc);
                DrawRectangleRounded((Rectangle){ cx - 9, (float)ty - 3, 18, 6 }, 0.6f, 6, g_eq_drag == b ? WHITE : TXT);
                char fl[8]; float fq = eq ? eq_get_frequency(eq, b) : 0;
                if (fq >= 1000) snprintf(fl, sizeof(fl), "%dK", (int)(fq / 1000)); else snprintf(fl, sizeof(fl), "%d", (int)fq);
                Vector2 flv = MeasureTextEx(fEye, fl, 11, 0.5f);
                DrawTextEx(fEye, fl, (Vector2){ cx - flv.x / 2, (float)eqBot + 8 }, 11, 0.5f, alpha(TXT, 170));
                if (g_eq_drag == b) {
                    char db[8]; snprintf(db, sizeof(db), "%+d", (int)(g + (g >= 0 ? 0.5f : -0.5f)));
                    Vector2 dv = MeasureTextEx(fEye, db, 11, 0);
                    DrawTextEx(fEye, db, (Vector2){ cx - dv.x / 2, (float)eqTop - 16 }, 11, 0, g_accent);
                }
            }
        } else if (g_show_settings) {
            rrBox(artR, 0.05f, CARDBG, (Color){ 255, 255, 255, 14 });
            DrawTextEx(fEye, "SETTINGS", (Vector2){ artR.x + 16, artR.y + 16 }, 12, 3.0f, alpha(g_accent, 205));
            const char *labels[7] = { "Always on top", "Shuffle", "Repeat", "Playlist side", "Prev button", "Mute", "Hisashi menubar" };
            bool st[7] = { g_aot, playlist_shuffle(&g_pl), playlist_loop(&g_pl), false, false, g_audio ? audio_get_volume(g_audio) <= 0.001f : false,
                           g_hisashi_menubar };
            int ry0 = (int)artR.y + 50, rowH = 31;   // 7 rows must clear the DATA FOLDER block at +268
            for (int i = 0; i < 7; i++) {
                float ry = (float)(ry0 + i * rowH);
                bool hov = CheckCollisionPointRec(mp, (Rectangle){ artR.x, ry, artR.width, (float)rowH });
                if (hov) DrawRectangleRounded((Rectangle){ artR.x + 6, ry, artR.width - 12, (float)rowH - 8 }, 0.3f, 6, (Color){ 255, 255, 255, 10 });
                DrawTextEx(fMeta, labels[i], (Vector2){ artR.x + 20, ry + 8 }, 16, 0.3f, TXT);
                float tx = artR.x + artR.width - 66, ty = ry + (rowH - 24) / 2.0f;
                if (i == 2) {  // repeat: 3-state pill (Off / One / All)
                    const char *rm[3] = { "Off", "One", "All" };
                    bool ron = g_repeat != 0;
                    rrBox((Rectangle){ tx, ty, 46, 24 }, 0.5f, ron ? alpha(g_accent, 55) : TRK, ron ? g_accent : alpha(MUT, 120));
                    Vector2 mw = MeasureTextEx(fSmall, rm[g_repeat], 13, 0.3f);
                    DrawTextEx(fSmall, rm[g_repeat], (Vector2){ tx + (46 - mw.x) / 2, ty + 4 }, 13, 0.3f, ron ? g_accent : MUT);
                } else if (i == 3) {  // playlist side: Left / Right pill
                    const char *sd = (g_side == 1) ? "Left" : "Right";
                    rrBox((Rectangle){ tx, ty, 46, 24 }, 0.5f, alpha(g_accent, 55), g_accent);
                    Vector2 mw = MeasureTextEx(fSmall, sd, 13, 0.3f);
                    DrawTextEx(fSmall, sd, (Vector2){ tx + (46 - mw.x) / 2, ty + 4 }, 13, 0.3f, g_accent);
                } else if (i == 4) {  // prev button: Smart / Direct pill
                    const char *pm = (g_prev_mode == 0) ? "Smart" : "Direct";
                    float pw = 56, px = artR.x + artR.width - 20 - pw;
                    rrBox((Rectangle){ px, ty, pw, 24 }, 0.5f, alpha(g_accent, 55), g_accent);
                    Vector2 mw = MeasureTextEx(fSmall, pm, 13, 0.3f);
                    DrawTextEx(fSmall, pm, (Vector2){ px + (pw - mw.x) / 2, ty + 4 }, 13, 0.3f, g_accent);
                } else {
                    rrFill((Rectangle){ tx, ty, 46, 24 }, 1, st[i] ? g_accent : TRK);
                    float kx = st[i] ? tx + 46 - 13 : tx + 13;
                    DrawCircle((int)kx, (int)(ty + 12), 9, st[i] ? BG1 : alpha(TXT, 210));
                }
            }
            // data folder (config + saved playlists) + reveal button
            DrawTextEx(fEye, "DATA FOLDER", (Vector2){ artR.x + 20, artR.y + 268 }, 11, 2.0f, alpha(g_accent, 190));
            draw_fit(fSmall, dataDir, (Vector2){ artR.x + 20, artR.y + 286 }, 13, 0.2f, alpha(MUT, 205), artR.width - 40);
            Rectangle foldBtn = { artR.x + 20, artR.y + 308, 130, 26 };
            bool hf = CheckCollisionPointRec(mp, foldBtn);
            rrBox(foldBtn, 0.4f, CARDBG, hf ? alpha(TXT, 220) : alpha(MUT, 150));
            Vector2 fw3 = MeasureTextEx(fSmall, "Open folder", 13, 0.3f);
            DrawTextEx(fSmall, "Open folder", (Vector2){ foldBtn.x + (foldBtn.width - fw3.x) / 2, foldBtn.y + 6 }, 13, 0.3f, hf ? TXT : MUT);
            DrawTextEx(fSmall, "Playlists live here", (Vector2){ foldBtn.x + foldBtn.width + 12, foldBtn.y + 6 }, 12, 0.2f, alpha(MUT, 130));
            DrawTextEx(fSmall, "Timp v" TIMP_VERSION "  \xc2\xb7  raylib edition", (Vector2){ artR.x + 20, artR.y + artR.height - 54 }, 13, 0.3f, alpha(TXT, 200));
            DrawTextEx(fEye, "SPACE PLAY   Q QUEUE   E EQ   G SETTINGS", (Vector2){ artR.x + 20, artR.y + artR.height - 30 }, 10, 1.0f, alpha(MUT, 160));
        } else if (g_show_lyrics) {
            rrBox(artR, 0.05f, CARDBG, (Color){ 255, 255, 255, 14 });
            DrawTextEx(fEye, "LYRICS", (Vector2){ artR.x + 16, artR.y + 18 }, 12, 3.0f, alpha(g_accent, 205));
            if (g_lyrics.count == 0) {
                const char *msg = g_lyrics_fetching ? "Searching lyrics…" : "No lyrics found";
                Vector2 mw = MeasureTextEx(fMeta, msg, 16, 0.3f);
                DrawTextEx(fMeta, msg, (Vector2){ artR.x + (artR.width - mw.x) / 2, artR.y + artR.height / 2 - 10 }, 16, 0.3f, alpha(MUT, 150));
            } else if (g_lyrics.synced) {
                double lp = g_audio ? audio_position_seconds(g_audio) : 0;
                int active = lyrics_active(&g_lyrics, lp);
                int lineH = 28;
                float ctr = artR.y + artR.height / 2;
                for (int i = 0; i < g_lyrics.count; i++) {
                    float yy = ctr + (i - active) * lineH - 9;
                    if (yy < artR.y + 44 || yy > artR.y + artR.height - 28) continue;
                    bool act = (i == active);
                    Color c = act ? TXT : alpha(MUT, 120);
                    float sz = act ? 17 : 15;
                    Vector2 tw = MeasureTextEx(fMeta, g_lyrics.lines[i].text, sz, 0.2f);
                    if (tw.x <= artR.width - 24) DrawTextEx(fMeta, g_lyrics.lines[i].text, (Vector2){ artR.x + (artR.width - tw.x) / 2, yy }, sz, 0.2f, c);
                    else draw_fit(fMeta, g_lyrics.lines[i].text, (Vector2){ artR.x + 12, yy }, sz, 0.2f, c, artR.width - 24);
                }
            } else {
                int lineH = 24;
                float top = artR.y + 46 - g_lyrics_scroll;
                for (int i = 0; i < g_lyrics.count; i++) {
                    float yy = top + i * lineH;
                    if (yy < artR.y + 40 || yy > artR.y + artR.height - 12) continue;
                    draw_fit(fMeta, g_lyrics.lines[i].text, (Vector2){ artR.x + 18, yy }, 15, 0.2f, alpha(TXT, 210), artR.width - 36);
                }
            }
        } else if (g_art_mode == 0) {
            soft_shadow(artR, 0.08f, 10, 150);
            if (g_has_cover) DrawTexturePro(g_cover, (Rectangle){ 0, 0, (float)g_cover.width, (float)g_cover.height }, artR, (Vector2){ 0, 0 }, 0, WHITE);
            else DrawRectangleRounded(artR, 0.06f, 12, TRK);
            DrawRoundedBorder(artR, 0.06f, 12, alpha((Color){ 255, 255, 255, 255 }, (unsigned char)(16 + 40 * g_hv[HV_ART])));
        } else {
            DrawRectangleRounded(artR, 0.06f, 12, (Color){ 16, 14, 10, 255 });
            Rectangle vz = { artR.x + 14, artR.y + 14, artR.width - 28, artR.height - 28 };
            if (g_art_mode == 1) {  // big bars
                float bw = vz.width / NBARS;
                for (int i = 0; i < NBARS; i++) {
                    float h = g_bars[i]; if (h > 1) h = 1;
                    float bh = h * vz.height * 0.92f;
                    float x = vz.x + i * bw;
                    DrawRectangleGradientV((int)x, (int)(vz.y + vz.height - bh), (int)bw - 2, (int)bh, alpha(g_accent, 240), alpha(g_accent, 30));
                    float pk = g_peaks[i] > 1 ? 1 : g_peaks[i];
                    DrawRectangle((int)x, (int)(vz.y + vz.height - pk * vz.height * 0.92f) - 2, (int)bw - 2, 2, alpha((Color){ 255, 245, 220, 255 }, 200));
                }
            } else {  // waveform
                float cyf = vz.y + vz.height / 2;
                Vector2 prev = { vz.x, cyf };
                for (int x = 0; x <= (int)vz.width; x += 2) {
                    float s = samp[x * 512 / (int)vz.width]; if (s > 1) s = 1; if (s < -1) s = -1;
                    Vector2 cur = { vz.x + x, cyf - s * vz.height * 0.45f };
                    if (x > 0) DrawLineEx(prev, cur, 2.0f, g_accent);
                    prev = cur;
                }
            }
            DrawRoundedBorder(artR, 0.06f, 12, alpha((Color){ 255, 255, 255, 255 }, (unsigned char)(16 + 40 * g_hv[HV_ART])));
        }

        // ---- top bar buttons (with hover brighten) ----
        Color cClose = clerp(MUT, TXT, g_hv[HV_CLOSE]), cMin = clerp(MUT, TXT, g_hv[HV_MIN]);
        DrawLineEx((Vector2){ closeR.x + 5, closeR.y + 6 }, (Vector2){ closeR.x + 16, closeR.y + 17 }, 1.6f, cClose);
        DrawLineEx((Vector2){ closeR.x + 16, closeR.y + 6 }, (Vector2){ closeR.x + 5, closeR.y + 17 }, 1.6f, cClose);
        DrawLineEx((Vector2){ minR.x + 5, minR.y + 13 }, (Vector2){ minR.x + 16, minR.y + 13 }, 1.6f, cMin);
        Color cOpen = clerp(MUT, TXT, g_hv[HV_OPEN]);
        DrawRing((Vector2){ openR.x + 12, openR.y + 11 }, 7.5f, 9.0f, 0, 360, 30, cOpen);
        DrawLineEx((Vector2){ openR.x + 12, openR.y + 7 }, (Vector2){ openR.x + 12, openR.y + 15 }, 1.7f, cOpen);
        DrawLineEx((Vector2){ openR.x + 8, openR.y + 11 }, (Vector2){ openR.x + 16, openR.y + 11 }, 1.7f, cOpen);
        Color cQ = g_show_queue ? g_accent : clerp(MUT, TXT, g_hv[HV_QUEUE]);
        for (int i = 0; i < 3; i++) DrawLineEx((Vector2){ queueR.x + 4, queueR.y + 6 + i * 5 }, (Vector2){ queueR.x + 20, queueR.y + 6 + i * 5 }, 1.7f, cQ);
        // EQ button — little faders with knobs
        Color cE = g_show_eq ? g_accent : clerp(MUT, TXT, g_hv[HV_EQ]);
        for (int i = 0; i < 3; i++) {
            float bx = eqR.x + 5 + i * 8;
            DrawLineEx((Vector2){ bx, eqR.y + 4 }, (Vector2){ bx, eqR.y + 18 }, 1.7f, alpha(cE, 150));
            DrawCircle((int)bx, (int)(eqR.y + (i == 1 ? 8 : 14)), 2.2f, cE);
        }
        // settings gear
        Color cS = g_show_settings ? g_accent : clerp(MUT, TXT, g_hv[HV_SET]);
        Vector2 gc = { setR.x + 12, setR.y + 11 };
        for (int k = 0; k < 8; k++) {
            float a = k * (PI / 4.0f);
            DrawLineEx((Vector2){ gc.x + cosf(a) * 6, gc.y + sinf(a) * 6 }, (Vector2){ gc.x + cosf(a) * 9, gc.y + sinf(a) * 9 }, 2.0f, cS);
        }
        DrawRing(gc, 3.5f, 6.0f, 0, 360, 24, cS);
        // lyrics button — music note
        Color cL = g_show_lyrics ? g_accent : clerp(MUT, TXT, g_hv[HV_LYR]);
        DrawCircle((int)(lyrR.x + 8), (int)(lyrR.y + 16), 3, cL);
        DrawLineEx((Vector2){ lyrR.x + 10.5f, lyrR.y + 16 }, (Vector2){ lyrR.x + 10.5f, lyrR.y + 5 }, 1.8f, cL);
        DrawLineEx((Vector2){ lyrR.x + 10.5f, lyrR.y + 5 }, (Vector2){ lyrR.x + 17, lyrR.y + 7 }, 1.8f, cL);
        // centered wordmark
        Vector2 wmw = MeasureTextEx(fEye, "TIMP", 13, 5.0f);
        DrawTextEx(fEye, "TIMP", (Vector2){ (WW - wmw.x) / 2, 14 }, 13, 5.0f, alpha(MUT, 190));

        // ---- info ----
        if (loaded) DrawTextEx(fEye, playing ? "NOW PLAYING" : "PAUSED", (Vector2){ PAD, (float)infoY }, 12, 3.0f, alpha(g_accent, 205));
        draw_fit(fTitle, g_title, (Vector2){ PAD, (float)titleY }, 30, 0.3f, TXT, WW - 2 * PAD);
        DrawTextEx(fMeta, g_meta[0] ? g_meta : "Unknown Artist", (Vector2){ PAD, (float)metaY }, 16, 0.3f, g_meta[0] ? MUT : alpha(MUT, 150));
        if (g_fmt[0]) { Vector2 fw = MeasureTextEx(fSmall, g_fmt, 14, 1.0f); DrawTextEx(fSmall, g_fmt, (Vector2){ WW - PAD - fw.x, (float)metaY + 1 }, 14, 1.0f, alpha(g_accent, 220)); }

        // ---- mini spectrum ----
        {
            float bw = (float)(WW - 2 * PAD) / NBARS;
            for (int i = 0; i < NBARS; i++) {
                float h = g_bars[i]; if (h > 1) h = 1;
                int bh = (int)(specH * h), bx = (int)(PAD + i * bw), bwi = (int)bw - 2; if (bwi < 1) bwi = 1;
                if (bh > 0) DrawRectangleGradientV(bx, specY + specH - bh, bwi, bh, alpha(g_accent, 230), alpha(g_accent, 35));
                float pk = g_peaks[i] > 1 ? 1 : g_peaks[i];
                DrawRectangle(bx, specY + specH - (int)(specH * pk) - 1, bwi, 2, alpha((Color){ 255, 245, 220, 255 }, 160));
            }
        }

        // ---- progress ----
        DrawRectangleRounded(barRect, 1, 8, TRK);
        float len = loaded ? (float)audio_length_seconds(g_audio) : 0, pos = loaded ? (float)audio_position_seconds(g_audio) : 0;
        float t = pos_drag ? scrub_t : ((len > 0) ? pos / len : 0); if (t > 1) t = 1; if (t < 0) t = 0;
        float dispPos = pos_drag ? scrub_t * len : pos;
        DrawRectangleRounded((Rectangle){ barRect.x, barRect.y, barRect.width * t, 4 }, 1, 8, g_accent);
        DrawCircle((int)(barRect.x + barRect.width * t), (int)(barRect.y + 2), pos_drag ? 8 : 6, TXT);
        char tl[16], tr[16]; snprintf(tl, sizeof(tl), "%d:%02d", (int)dispPos / 60, (int)dispPos % 60);
        float rem = len - dispPos; if (rem < 0) rem = 0; snprintf(tr, sizeof(tr), "-%d:%02d", (int)rem / 60, (int)rem % 60);
        DrawTextEx(fSmall, tl, (Vector2){ PAD, (float)timesY }, 14, 1.0f, MUT);
        Vector2 rw = MeasureTextEx(fSmall, tr, 14, 1.0f);
        DrawTextEx(fSmall, tr, (Vector2){ WW - PAD - rw.x, (float)timesY }, 14, 1.0f, MUT);

        // ---- transport (hover scale + brighten) ----
        float cyf = (float)transY;
        float pr = 30 + 3 * g_hv[HV_PLAY];
        DrawCircle(mid, (int)cyf, pr + 4, alpha(g_accent, (unsigned char)(40 * g_hv[HV_PLAY])));  // hover glow
        DrawCircle(mid, (int)cyf, pr, clerp(g_accent, clerp(g_accent, WHITE, 0.25f), g_hv[HV_PLAY]));
        if (playing) ic_pause(mid, cyf, 12, BG1); else ic_play(mid + 1, cyf, 13, BG1);
        ic_prev(prevR.x + 22, prevR.y + 22, 11 + g_hv[HV_PREV], clerp(alpha(TXT, 210), TXT, g_hv[HV_PREV]));
        ic_next(nextR.x + 22, nextR.y + 22, 11 + g_hv[HV_NEXT], clerp(alpha(TXT, 210), TXT, g_hv[HV_NEXT]));
        ic_shuffle(shufR.x + 16, shufR.y + 16, 10, playlist_shuffle(&g_pl) ? g_accent : clerp(MUT, TXT, g_hv[HV_SHUF]));
        Color repCol = g_repeat ? g_accent : clerp(MUT, TXT, g_hv[HV_REP]);
        ic_repeat(repR.x + 16, repR.y + 16, 9, repCol);
        if (g_repeat == 1) {  // repeat-one badge
            Vector2 ow = MeasureTextEx(fEye, "1", 9, 0);
            DrawTextEx(fEye, "1", (Vector2){ repR.x + 16 - ow.x / 2, repR.y + 16 - 4.5f }, 9, 0, repCol);
        }

        // ---- volume ----
        float vol = g_audio ? audio_get_volume(g_audio) : 0.7f;
        float sx = (float)PAD, sy = (float)volY + 3;
        DrawRectangle((int)sx, (int)sy - 3, 5, 6, MUT);
        DrawTriangle((Vector2){ sx + 5, sy - 7 }, (Vector2){ sx + 5, sy + 7 }, (Vector2){ sx + 12, sy }, MUT);
        DrawRectangleRounded(volRect, 1, 6, TRK);
        DrawRectangleRounded((Rectangle){ volRect.x, volRect.y, volRect.width * vol, volRect.height }, 1, 6, (Color){ 190, 178, 150, 255 });
        DrawCircle((int)(volRect.x + volRect.width * vol), (int)(volRect.y + 3), vol_drag ? 6 : 5, TXT);

        rlPopMatrix();   // player translate
        rlPopMatrix();   // supersample scale
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLANK);
        // blit only the currently-visible slice (player + however much drawer is out)
        DrawTexturePro(target.texture,
                       (Rectangle){ (float)(blitX * SS), 0, (float)(curW * SS), -(float)(WH * SS) },
                       (Rectangle){ 0, 0, (float)curW, (float)WH }, (Vector2){ 0, 0 }, 0, WHITE);
        EndDrawing();

        // Publish where we are to the OS now-playing session (cheap: the backend
        // only forwards what actually changed).
        { bool ld = g_audio && audio_is_loaded(g_audio), pl = g_audio && audio_is_playing(g_audio);
          mediakeys_set_state(!ld ? MK_STOPPED : (pl ? MK_PLAYING : MK_PAUSED));
          mediakeys_set_timeline(ld ? audio_position_seconds(g_audio) : 0,
                                 ld ? audio_length_seconds(g_audio) : 0); }

        frame++;
        if (shot_frame > 0 && frame == shot_frame) TakeScreenshot("rl_shot.png");
        if (shot_frame > 0 && frame == shot_frame + 3) break;
    }

    // persist settings (window still valid for GetWindowPosition)
    RlConfig save; rlconfig_defaults(&save);
    if (g_audio) {
        save.volume = audio_get_volume(g_audio);
        Eq *e1 = audio_get_eq(g_audio);
        save.eq_enabled = eq_is_enabled(e1);
        for (int i = 0; i < EQ_BANDS; i++) save.eq_gains[i] = eq_get_gain(e1, i);
    }
    save.always_on_top = g_aot;
    save.playlist_side = g_side;
    save.prev_mode = g_prev_mode;
    save.hisashi_menubar = g_hisashi_menubar;
    save.win_x = g_base_x; save.win_y = g_base_y; save.has_win_pos = true;   // closed-window anchor
    rlconfig_save(&save);
    menubar_shutdown();   // "bye" to Hisashi
    mediakeys_shutdown();

    UnloadRenderTexture(target);
    if (g_has_cover) UnloadTexture(g_cover);
    if (g_audio) audio_destroy(g_audio);
    CloseWindow();
    playlist_free(&g_pl);
    free(g_filt);
    return 0;
}
