#ifndef TIMP_CONFIG_H
#define TIMP_CONFIG_H

#include <stdbool.h>

typedef struct {
    bool always_on_top;
    bool playlist_visible;
    int  current_theme;
    char skin_path[300];   // empty = pick default at startup
} WhConfig;

void config_default(WhConfig* c);
bool config_load(WhConfig* c);
bool config_save(const WhConfig* c);

#endif
