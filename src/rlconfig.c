#include "rlconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

// Data home: %APPDATA%\fezcode\Timp — one Fezcode-owned tree for every app in
// the suite. config.ini and Playlists\ both live here. macOS mirrors it at
// ~/Library/Application Support/fezcode/Timp/, other Unix at ~/.config/fezcode/Timp/.
void rlconfig_data_dir(char *out, int cap) {
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata && *appdata) {
        char parent[512];
        snprintf(parent, sizeof(parent), "%s\\fezcode", appdata);
        CreateDirectoryA(parent, NULL);              // parent first — no mkdir -p on Win32
        snprintf(out, cap, "%s\\fezcode\\Timp", appdata);
        CreateDirectoryA(out, NULL);
    } else snprintf(out, cap, ".");
#else
    char base[512];
  #ifdef __APPLE__
    const char *home = getenv("HOME");
    if (!home || !*home) { snprintf(out, cap, "."); return; }
    snprintf(base, sizeof(base), "%s/Library/Application Support", home);
  #else
    const char *xdg  = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg)        snprintf(base, sizeof(base), "%s", xdg);
    else if (home && *home) snprintf(base, sizeof(base), "%s/.config", home);
    else { snprintf(out, cap, "."); return; }
  #endif
    char parent[600];
    snprintf(parent, sizeof(parent), "%s/fezcode", base); mkdir(parent, 0755);
    snprintf(out, cap, "%s/fezcode/Timp", base);          mkdir(out, 0755);
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
    c->prev_mode = 0;
    c->hisashi_menubar = true;
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
            else if (!strcmp(key, "prev_mode")) c->prev_mode = iv;
            else if (!strcmp(key, "hisashi_menubar")) c->hisashi_menubar = iv != 0;
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
    fprintf(f, "prev_mode=%d\n", c->prev_mode);
    fprintf(f, "hisashi_menubar=%d\n", c->hisashi_menubar ? 1 : 0);
    if (c->has_win_pos) { fprintf(f, "win_x=%d\n", c->win_x); fprintf(f, "win_y=%d\n", c->win_y); }
    fclose(f);
    return true;
}
