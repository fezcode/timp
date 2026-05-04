#include "ini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char* trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char* end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = 0;
    return s;
}

static void push_entry(Ini* ini, const char* section, const char* key, const char* value) {
    if (ini->count == ini->cap) {
        ini->cap = ini->cap ? ini->cap * 2 : 32;
        ini->entries = (IniEntry*)realloc(ini->entries, sizeof(IniEntry) * ini->cap);
    }
    IniEntry* e = &ini->entries[ini->count++];
    snprintf(e->section, sizeof(e->section), "%s", section);
    snprintf(e->key, sizeof(e->key), "%s", key);
    snprintf(e->value, sizeof(e->value), "%s", value);
}

bool ini_load(Ini* ini, const char* path) {
    memset(ini, 0, sizeof(*ini));
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    char section[64] = "";
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char* s = trim(line);
        if (*s == 0 || *s == '#' || *s == ';') continue;
        if (*s == '[') {
            char* end = strchr(s, ']');
            if (!end) continue;
            *end = 0;
            snprintf(section, sizeof(section), "%s", s + 1);
            continue;
        }
        char* eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = trim(s);
        char* val = trim(eq + 1);
        push_entry(ini, section, key, val);
    }
    fclose(f);
    return true;
}

void ini_free(Ini* ini) {
    free(ini->entries);
    memset(ini, 0, sizeof(*ini));
}

const char* ini_get(const Ini* ini, const char* section, const char* key) {
    for (int i = 0; i < ini->count; i++) {
        const IniEntry* e = &ini->entries[i];
        if (strcmp(e->section, section) == 0 && strcmp(e->key, key) == 0) {
            return e->value;
        }
    }
    return NULL;
}

int ini_get_int(const Ini* ini, const char* section, const char* key, int dflt) {
    const char* v = ini_get(ini, section, key);
    if (!v) return dflt;
    return atoi(v);
}

bool ini_get_rect(const Ini* ini, const char* section, const char* key,
                  int* x, int* y, int* w, int* h) {
    const char* v = ini_get(ini, section, key);
    if (!v) return false;
    return sscanf(v, "%d,%d,%d,%d", x, y, w, h) == 4;
}

bool ini_get_color(const Ini* ini, const char* section, const char* key,
                   unsigned char* r, unsigned char* g, unsigned char* b) {
    const char* v = ini_get(ini, section, key);
    if (!v) return false;
    int rr, gg, bb;
    if (sscanf(v, "%d,%d,%d", &rr, &gg, &bb) != 3) return false;
    *r = (unsigned char)rr;
    *g = (unsigned char)gg;
    *b = (unsigned char)bb;
    return true;
}
