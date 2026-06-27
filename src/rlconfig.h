#ifndef TIMP_RLCONFIG_H
#define TIMP_RLCONFIG_H

#include <stdbool.h>

// Persisted per-user settings (stored next to the OS user-data dir).
typedef struct {
    float volume;
    bool  always_on_top;
    bool  eq_enabled;
    float eq_gains[10];
    int   win_x, win_y;
    bool  has_win_pos;
    int   playlist_side;   // 0 = right, 1 = left
    int   prev_mode;       // 0 = smart (restart if >5s, else prev) · 1 = direct (always prev)
} RlConfig;

void rlconfig_defaults(RlConfig *c);
bool rlconfig_load(RlConfig *c);
bool rlconfig_save(const RlConfig *c);

// Absolute path of the per-user data folder (Windows: %APPDATA%\Timp), creating
// it if needed. config.ini and the Playlists\ folder both live here.
void rlconfig_data_dir(char *out, int cap);

#endif
