// Test for src/menubar.c — the Hisashi menubar tree built from a MenubarState.
// Pure: no window, no pipe, no audio (hoswl's offline compile step only).
//
// build + run (MSYS2 MinGW gcc, from the repo root):
//   gcc -O2 -std=c11 -Isrc src/menubar.c tools/menubar_test.c -o build/menubar_test.exe && build/menubar_test.exe
#include "menubar.h"
#include "hoswl.h"   // hoswl_compile_menu_text (implementation lives in menubar.c)

#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond, what) do { printf("%s %s\n", (cond) ? "ok:  " : "FAIL:", what); if (!(cond)) fails++; } while (0)

int main(void) {
    static char text[16384], json[65536], err[160];
    MenubarState s = { 0 };

    // Nothing loaded, empty queue.
    CHECK(menubar_build_text(&s, text, sizeof text) == 0, "build with nothing loaded");
    CHECK(strstr(text, " pb.toggle|Play|Space|d\n") != NULL, "Play disabled while nothing is loaded");
    CHECK(strstr(text, " file.save|Save Playlist||d\n") != NULL, "Save Playlist disabled for an empty queue");
    CHECK(strstr(text, " file.clear|Clear Playlist||d\n") != NULL, "Clear Playlist disabled for an empty queue");
    CHECK(strstr(text, "  viz.0|Album Art||x\n") != NULL, "Album Art visualizer checked by default");
    CHECK(strstr(text, "  side.0|Right||x\n") != NULL, "drawer side Right by default");
    CHECK(hoswl_compile_menu_text(text, json, sizeof json, err, sizeof err) == 0, "DSL compiles to JSON");

    // Playing, dirty queue of 3, shuffle + repeat all, EQ on, bars visualizer, muted.
    s.loaded = true; s.playing = true; s.has_next = true; s.shuffle = true; s.repeat = 2; s.art_mode = 1;
    s.eq_on = true; s.muted = true; s.playlist_dirty = true; s.qcount = 3; s.side = 1; s.prev_mode = 1;
    s.drawer_open = true; s.aot = true;
    CHECK(menubar_build_text(&s, text, sizeof text) == 0, "build while playing");
    CHECK(strstr(text, " pb.toggle|Pause|Space|\n") != NULL, "Play row reads Pause while playing");
    CHECK(strstr(text, " pb.next|Next||\n") != NULL, "Next enabled with a following track");
    CHECK(strstr(text, " file.save|Save Playlist||\n") != NULL, "Save Playlist enabled when dirty");
    CHECK(strstr(text, " pb.shuffle|Shuffle|S|x\n") != NULL, "shuffle checked");
    CHECK(strstr(text, "  repeat.2|All||x\n") != NULL, "repeat all checked");
    CHECK(strstr(text, " audio.mute|Mute||x\n") != NULL, "mute checked");
    CHECK(strstr(text, " audio.eq|Equalizer||x\n") != NULL, "equalizer checked");
    CHECK(strstr(text, "  viz.1|Spectrum Bars||x\n") != NULL, "bars visualizer checked");
    CHECK(strstr(text, " view.drawer|Playlist Drawer|Q|x\n") != NULL, "drawer checked");
    CHECK(strstr(text, "  side.1|Left||x\n") != NULL, "drawer side Left checked");
    CHECK(strstr(text, "  prev.1|Direct||x\n") != NULL, "direct prev checked");
    CHECK(strstr(text, " view.aot|Always on Top|T|x\n") != NULL, "always on top checked");
    CHECK(hoswl_compile_menu_text(text, json, sizeof json, err, sizeof err) == 0, "DSL while playing compiles to JSON");
    CHECK(strstr(json, "\"id\":\"pb.repeat\",\"label\":\"Repeat\",\"items\":[") != NULL, "repeat is a submenu in the JSON");

    char tiny[64];
    CHECK(menubar_build_text(&s, tiny, sizeof tiny) == -1, "tiny buffer rejected");

    printf(fails ? "FAILED (%d)\n" : "OK\n", fails);
    return fails ? 1 : 0;
}
