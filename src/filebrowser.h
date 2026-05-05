#ifndef TIMP_FILEBROWSER_H
#define TIMP_FILEBROWSER_H

#include <stdbool.h>
#include <SDL2/SDL.h>
#include "skin.h"

#define FB_MAX_ENTRIES 4096
#define FB_PATH_MAX 520
#define FB_MAX_PICKS 256

typedef struct {
    char name[260];
    bool is_dir;
    bool selected;
} FbEntry;

typedef struct {
    bool open;
    char cwd[FB_PATH_MAX];
    FbEntry* entries;
    int n_entries;
    int cap_entries;
    int hover;
    int anchor;          // last clicked row, for shift-range
    int scroll;
    int rows_visible;

    bool result_ready;
    char result_paths[FB_MAX_PICKS][FB_PATH_MAX];
    int  result_count;

    // Path entry: when active, cwd row becomes a text field (Ctrl-L or click on it).
    bool path_edit;
    char path_buf[FB_PATH_MAX];
} FileBrowser;

void fb_init(FileBrowser* fb);
void fb_destroy(FileBrowser* fb);

void fb_show(FileBrowser* fb);
void fb_close(FileBrowser* fb);

int  fb_result_count(const FileBrowser* fb);
const char* fb_result_path(const FileBrowser* fb, int i);
void fb_clear_result(FileBrowser* fb);

void fb_handle_event(FileBrowser* fb, const SDL_Event* e, const Skin* skin);
void fb_render(FileBrowser* fb, SDL_Renderer* ren, const Skin* skin);

#endif
