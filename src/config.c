#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <SDL2/SDL.h>

// Settings always live in the platform user-data dir (e.g. %APPDATA%\Timp\ on
// Windows, ~/.local/share/Timp/ on Linux), which SDL_GetPrefPath creates for
// us. Independent of the install location, so a read-only install dir such as
// Program Files works the same for admin and standard users.
static void config_path(char* out, size_t cap) {
    char* pref = SDL_GetPrefPath("Timp", "Timp");
    if (pref) {
        snprintf(out, cap, "%sconfig.ini", pref);  // SDL_GetPrefPath includes trailing sep
        SDL_free(pref);
    } else {
        snprintf(out, cap, "config.ini");  // last resort: current directory
    }
}

void config_default(WhConfig* c) {
    c->always_on_top = false;
    c->playlist_visible = true;
    c->current_theme = 0;
    c->skin_path[0] = 0;
}

bool config_load(WhConfig* c) {
    config_default(c);
    char path[1024];
    config_path(path, sizeof(path));
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char key[64]; int val;
        if (sscanf(line, " %63[^= \t] = %d", key, &val) == 2) {
            if      (strcmp(key, "always_on_top") == 0)    c->always_on_top    = val != 0;
            else if (strcmp(key, "playlist_visible") == 0) c->playlist_visible = val != 0;
            else if (strcmp(key, "current_theme") == 0)    c->current_theme    = val;
        }
        // skin_path is a string — separate sscanf because the int variant rejects it.
        char sval[300];
        if (sscanf(line, " %63[^= \t] = %299[^\r\n]", key, sval) == 2) {
            if (strcmp(key, "skin_path") == 0) snprintf(c->skin_path, sizeof(c->skin_path), "%s", sval);
        }
    }
    fclose(f);
    return true;
}

bool config_save(const WhConfig* c) {
    char path[1024];
    config_path(path, sizeof(path));
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "# Timp config - edited by the app, but human-readable.\n");
    fprintf(f, "always_on_top=%d\n",    c->always_on_top ? 1 : 0);
    fprintf(f, "playlist_visible=%d\n", c->playlist_visible ? 1 : 0);
    fprintf(f, "current_theme=%d\n",    c->current_theme);
    if (c->skin_path[0]) fprintf(f, "skin_path=%s\n", c->skin_path);
    fclose(f);
    return true;
}
