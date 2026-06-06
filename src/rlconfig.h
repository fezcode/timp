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
} RlConfig;

void rlconfig_defaults(RlConfig *c);
bool rlconfig_load(RlConfig *c);
bool rlconfig_save(const RlConfig *c);

#endif
