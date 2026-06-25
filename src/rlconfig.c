#include "rlconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

void rlconfig_data_dir(char *out, int cap) {
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata && *appdata) {
        snprintf(out, cap, "%s\\Timp", appdata);
        CreateDirectoryA(out, NULL);
    } else snprintf(out, cap, ".");
#else
    const char *home = getenv("HOME");
    if (home && *home) snprintf(out, cap, "%s/.timp", home);
    else snprintf(out, cap, ".");
#endif
}

static void cfg_path(char *out, int cap) {
    char dir[512]; rlconfig_data_dir(dir, sizeof(dir));
#ifdef _WIN32
    snprintf(out, cap, "%s\\config.ini", dir);
#else
    snprintf(out, cap, "%s/config.ini", dir);
#endif
}

void rlconfig_defaults(RlConfig *c) {
    memset(c, 0, sizeof(*c));
    c->volume = 0.7f;
    c->always_on_top = false;
    c->eq_enabled = false;
    for (int i = 0; i < 10; i++) c->eq_gains[i] = 0.f;
    c->has_win_pos = false;
    c->playlist_side = 0;
}

bool rlconfig_load(RlConfig *c) {
    rlconfig_defaults(c);
    char path[600]; cfg_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64]; float fv; int iv;
        if (sscanf(line, " %63[^= ] = %f", key, &fv) == 2) {
            if (!strcmp(key, "volume")) c->volume = fv;
            else if (!strncmp(key, "eq", 2) && key[2] >= '0' && key[2] <= '9') {
                int b = atoi(key + 2);
                if (b >= 0 && b < 10) c->eq_gains[b] = fv;
            }
        }
        if (sscanf(line, " %63[^= ] = %d", key, &iv) == 2) {
            if (!strcmp(key, "always_on_top")) c->always_on_top = iv != 0;
            else if (!strcmp(key, "eq_enabled")) c->eq_enabled = iv != 0;
            else if (!strcmp(key, "win_x")) { c->win_x = iv; c->has_win_pos = true; }
            else if (!strcmp(key, "win_y")) c->win_y = iv;
            else if (!strcmp(key, "playlist_side")) c->playlist_side = iv;
        }
    }
    fclose(f);
    return true;
}

bool rlconfig_save(const RlConfig *c) {
    char path[600]; cfg_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "# Timp settings\n");
    fprintf(f, "volume=%.3f\n", c->volume);
    fprintf(f, "always_on_top=%d\n", c->always_on_top ? 1 : 0);
    fprintf(f, "eq_enabled=%d\n", c->eq_enabled ? 1 : 0);
    for (int i = 0; i < 10; i++) fprintf(f, "eq%d=%.2f\n", i, c->eq_gains[i]);
    fprintf(f, "playlist_side=%d\n", c->playlist_side);
    if (c->has_win_pos) { fprintf(f, "win_x=%d\n", c->win_x); fprintf(f, "win_y=%d\n", c->win_y); }
    fclose(f);
    return true;
}
