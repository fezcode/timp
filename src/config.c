#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static void config_path(char* out, size_t cap) {
#ifdef _WIN32
    const char* base = getenv("APPDATA");
    if (!base) base = getenv("USERPROFILE");
    if (!base) { snprintf(out, cap, "whamp.ini"); return; }
    char dir[512];
    snprintf(dir, sizeof(dir), "%s\\WHamp", base);
    _mkdir(dir);
    snprintf(out, cap, "%s\\config.ini", dir);
#else
    const char* home = getenv("HOME");
    if (!home) { snprintf(out, cap, "whamp.ini"); return; }
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.config", home);
    mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/.config/whamp", home);
    mkdir(dir, 0755);
    snprintf(out, cap, "%s/.config/whamp/config.ini", home);
#endif
}

void config_default(WhConfig* c) {
    c->always_on_top = false;
    c->playlist_visible = true;
    c->current_theme = 0;
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
    }
    fclose(f);
    return true;
}

bool config_save(const WhConfig* c) {
    char path[1024];
    config_path(path, sizeof(path));
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "# WHamp config — edited by the app, but human-readable.\n");
    fprintf(f, "always_on_top=%d\n",    c->always_on_top ? 1 : 0);
    fprintf(f, "playlist_visible=%d\n", c->playlist_visible ? 1 : 0);
    fprintf(f, "current_theme=%d\n",    c->current_theme);
    fclose(f);
    return true;
}
