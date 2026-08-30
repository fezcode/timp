// menubar.c — Hisashi OS Window Layer (hoswl) bridge. Contract in menubar.h.
//
// The whole menu tree is rebuilt as hoswl's line DSL from a MenubarState
// snapshot and pushed with one hoswl_set_menus() whenever the fingerprint of
// that snapshot moves (a few KB; cheaper than tracking per-item patches).
// Clicks come back as item ids through hoswl_poll() and are dispatched to
// rl_main.c's callback from inside menubar_frame — the main thread, never a
// pipe thread.
//
// hoswl.h is a verbatim copy of Hisashi/sdk/hoswl/hoswl.h. Never edit the
// copy; fix upstream and re-vendor.
#define HOSWL_IMPLEMENTATION
#include "hoswl.h"
#include "menubar.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static hoswl_t  g_h;
static char     g_version[64];
static bool     g_enabled = true;
static uint32_t g_fp;          // fingerprint of the last published menu
static bool     g_published;   // g_fp is meaningful

void menubar_init(const char *app_version) {
    snprintf(g_version, sizeof g_version, "%s", app_version ? app_version : "");
}

void menubar_set_enabled(bool on) {
    g_enabled = on;
    if (!on && g_h.inited) { hoswl_shutdown(&g_h); g_published = false; }   // sends "bye" if connected
}

bool menubar_connected(void) { return hoswl_connected(&g_h) != 0; }

// FNV-1a over every field that shows up in the menu text.
static uint32_t fingerprint(const MenubarState *s) {
    uint32_t h = 2166136261u;
    #define FNV(b) (h = (h ^ (uint32_t)(unsigned char)(b)) * 16777619u)
    const int fields[] = { s->loaded, s->playing, s->has_next, s->has_prev, s->shuffle, s->repeat,
                           s->art_mode, s->drawer_open, s->drawer_view, s->eq_on, s->eq_panel,
                           s->settings_open, s->lyrics_open, s->aot, s->side, s->prev_mode,
                           s->muted, s->playlist_dirty, s->qcount > 0 };
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) { FNV(fields[i]); FNV(fields[i] >> 8); }
    #undef FNV
    return h;
}

// Row syntax: " id|Label|Key|flags" — flags: d disabled, x checked, c checkable-off.
int menubar_build_text(const MenubarState *s, char *text, size_t cap) {
    size_t o = 0;
    #define PUT(...) do { \
        int w_ = snprintf(text + o, cap - o, __VA_ARGS__); \
        if (w_ < 0 || (size_t)w_ >= cap - o) return -1; \
        o += (size_t)w_; \
    } while (0)
    #define RADIO(cond) ((cond) ? "x" : "c")
    const char *need = s->loaded ? "" : "d";   // rows that need a loaded track

    PUT("File\n");
    PUT(" file.open|Open Files…|O\n");
    PUT(" -\n");
    PUT(" file.save|Save Playlist||%s\n", s->playlist_dirty && s->qcount > 0 ? "" : "d");
    PUT(" file.library|Saved Playlists…\n");
    PUT(" file.clear|Clear Playlist||%s\n", s->qcount > 0 ? "" : "d");
    PUT(" -\n");
    PUT(" file.folder|Open Data Folder\n");
    PUT(" -\n");
    PUT(" file.quit|Quit Timp\n");

    PUT("Playback\n");
    PUT(" pb.toggle|%s|Space|%s\n", s->playing ? "Pause" : "Play", need);
    PUT(" pb.stop|Stop||%s\n", need);
    PUT(" -\n");
    PUT(" pb.prev|Previous||%s\n", s->loaded || s->has_prev ? "" : "d");
    PUT(" pb.next|Next||%s\n", s->has_next ? "" : "d");
    PUT(" -\n");
    PUT(" pb.back|Back 5 s|Left|%s\n", need);
    PUT(" pb.fwd|Forward 5 s|Right|%s\n", need);
    PUT(" -\n");
    PUT(" pb.shuffle|Shuffle|S|%s\n", RADIO(s->shuffle));
    PUT(" pb.repeat|Repeat|>\n");
    PUT("  repeat.0|Off||%s\n", RADIO(s->repeat == 0));
    PUT("  repeat.1|One||%s\n", RADIO(s->repeat == 1));
    PUT("  repeat.2|All||%s\n", RADIO(s->repeat == 2));

    PUT("Audio\n");
    PUT(" audio.up|Volume Up|Up\n");
    PUT(" audio.down|Volume Down|Down\n");
    PUT(" audio.mute|Mute||%s\n", RADIO(s->muted));
    PUT(" -\n");
    PUT(" audio.eq|Equalizer||%s\n", RADIO(s->eq_on));
    PUT(" audio.eqflat|Flatten Equalizer\n");
    PUT(" -\n");
    PUT(" audio.eqpanel|Equalizer Panel|E|%s\n", RADIO(s->eq_panel));

    PUT("View\n");
    PUT(" view.viz|Visualizer|>\n");
    PUT("  viz.0|Album Art||%s\n", RADIO(s->art_mode == 0));
    PUT("  viz.1|Spectrum Bars||%s\n", RADIO(s->art_mode == 1));
    PUT("  viz.2|Waveform||%s\n", RADIO(s->art_mode == 2));
    PUT(" -\n");
    PUT(" view.drawer|Playlist Drawer|Q|%s\n", RADIO(s->drawer_open));
    PUT(" view.lyrics|Lyrics|Y|%s\n", RADIO(s->lyrics_open));
    PUT(" view.settings|Settings|G|%s\n", RADIO(s->settings_open));
    PUT(" -\n");
    PUT(" view.aot|Always on Top|T|%s\n", RADIO(s->aot));
    PUT(" view.side|Drawer Side|>\n");
    PUT("  side.0|Right||%s\n", RADIO(s->side == 0));
    PUT("  side.1|Left||%s\n", RADIO(s->side == 1));
    PUT(" view.prevmode|Previous Button|>\n");
    PUT("  prev.0|Smart (restart after 5 s)||%s\n", RADIO(s->prev_mode == 0));
    PUT("  prev.1|Direct||%s\n", RADIO(s->prev_mode == 1));

    PUT("Help\n");
    PUT(" help.about|About Timp…\n");
    #undef RADIO
    #undef PUT
    return 0;
}

static void publish(const MenubarState *s) {
    static char text[16384];
    if (menubar_build_text(s, text, sizeof text) != 0) { fprintf(stderr, "hoswl: menu text too large, not published\n"); return; }
    if (hoswl_set_menus(&g_h, text) != 0) fprintf(stderr, "hoswl: menu rejected: %s\n", g_h.last_error);
}

void menubar_frame(const MenubarState *s, void (*on_click)(const char *id)) {
    if (!g_enabled) {
        if (g_h.inited) hoswl_shutdown(&g_h);
        g_published = false;
        return;
    }
    if (!g_h.inited) {
        if (hoswl_init(&g_h, "io.timp.timp", "Timp", g_version) != 0) return;
        g_published = false;
    }
    // hoswl_poll connects lazily (retrying every 2 s while Hisashi is absent)
    // and re-sends the cached menu on every reconnect by itself.
    const char *id;
    while ((id = hoswl_poll(&g_h)) != NULL) {
        char copy[HOSWL_ID_MAX];
        snprintf(copy, sizeof copy, "%s", id);   // the click may republish, which reuses the buffer
        on_click(copy);
        if (!g_h.inited) return;                 // the click switched the integration off
    }
    uint32_t fp = fingerprint(s);
    if (!g_published || fp != g_fp) {
        g_fp = fp; g_published = true;
        publish(s);
    }
}

void menubar_shutdown(void) {
    if (g_h.inited) hoswl_shutdown(&g_h);
    g_published = false;
}
