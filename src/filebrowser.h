#ifndef WH_FILEBROWSER_H
#define WH_FILEBROWSER_H

#include <stdbool.h>
#include <SDL2/SDL.h>
#include "skin.h"

#define FB_MAX_ENTRIES 4096
#define FB_PATH_MAX 520

typedef struct {
    char name[260];
    bool is_dir;
} FbEntry;

typedef struct {
    bool open;
    char cwd[FB_PATH_MAX];
    FbEntry* entries;
    int n_entries;
    int cap_entries;
    int hover;
    int scroll;          // first visible row
    int rows_visible;    // computed at render time

    // result
    bool result_ready;
    char result_path[FB_PATH_MAX];
} FileBrowser;

void fb_init(FileBrowser* fb);
void fb_destroy(FileBrowser* fb);

void fb_show(FileBrowser* fb);
void fb_close(FileBrowser* fb);

// Returns true if a file was selected this frame; result_path holds it.
bool fb_take_result(FileBrowser* fb, char* out, int out_size);

void fb_handle_event(FileBrowser* fb, const SDL_Event* e, const Skin* skin);
void fb_render(FileBrowser* fb, SDL_Renderer* ren, const Skin* skin);

#endif
