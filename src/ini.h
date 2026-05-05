#ifndef TIMP_INI_H
#define TIMP_INI_H

#include <stdbool.h>

typedef struct IniEntry {
    char section[64];
    char key[64];
    char value[256];
} IniEntry;

typedef struct Ini {
    IniEntry* entries;
    int count;
    int cap;
} Ini;

bool ini_load(Ini* ini, const char* path);
void ini_free(Ini* ini);

const char* ini_get(const Ini* ini, const char* section, const char* key);
int ini_get_int(const Ini* ini, const char* section, const char* key, int dflt);
bool ini_get_rect(const Ini* ini, const char* section, const char* key,
                  int* x, int* y, int* w, int* h);
bool ini_get_color(const Ini* ini, const char* section, const char* key,
                   unsigned char* r, unsigned char* g, unsigned char* b);

#endif
