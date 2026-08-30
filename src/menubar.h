#ifndef TIMP_MENUBAR_H
#define TIMP_MENUBAR_H

#include <stdbool.h>
#include <stddef.h>

// menubar.c — Hisashi OS Window Layer (hoswl) bridge.
//
// Timp has no menu bar of its own; when Hisashi is running it gets one:
// File / Playback / Audio / View / Help are published to Hisashi's macOS-style
// menubar over the named pipe \\.\pipe\hoswl and the ids the user clicks come
// back to rl_main.c. Never blocks: while Hisashi isn't running it retries a
// connect every 2 s and everything else is a no-op.
// Protocol: Hisashi/docs/hoswl-protocol.md · client: src/hoswl.h (vendored copy
// of Hisashi/sdk/hoswl/hoswl.h — never edit it here, fix upstream).
// Same contract as pidi's and tivi's src/menubar.h.

// Everything that changes what the bar shows. rl_main.c fills one per frame;
// the menu is republished only when it differs from the last one sent.
typedef struct {
    bool loaded, playing, has_next, has_prev;
    bool shuffle;
    int  repeat;            // 0 off · 1 one · 2 all
    int  art_mode;          // 0 cover · 1 bars · 2 wave
    bool drawer_open;
    int  drawer_view;       // 0 playlist · 1 library · 2 queue
    bool eq_on, eq_panel, settings_open, lyrics_open;
    bool aot;
    int  side;              // 0 right · 1 left
    int  prev_mode;         // 0 smart · 1 direct
    bool muted;
    bool playlist_dirty;
    int  qcount;
} MenubarState;

void menubar_init(const char *app_version);   // once, before the first menubar_frame
void menubar_set_enabled(bool on);            // the "Hisashi menubar" setting; off disconnects
bool menubar_connected(void);

// Once per frame from the main loop: connect / republish when needed, and call
// on_click(id) for every item Hisashi reports clicked. Ids are the ones listed
// in menubar.c ("file.open", "pb.toggle", "viz.1", ...). Callbacks run right
// here on the main thread, never from inside the pipe code.
void menubar_frame(const MenubarState *st, void (*on_click)(const char *id));
void menubar_shutdown(void);

// The menu text (hoswl's line DSL) for a state — exposed for tools/menubar_test.c.
// Returns 0, or -1 when it does not fit in cap.
int  menubar_build_text(const MenubarState *st, char *out, size_t cap);

#endif
