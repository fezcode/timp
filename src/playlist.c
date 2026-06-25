#include "playlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---------- shuffle order ----------
static void order_ensure_cap(Playlist* p, int need) {
    if (p->order_cap >= need) return;
    int cap = p->order_cap ? p->order_cap : 16;
    while (cap < need) cap *= 2;
    p->order = (int*)realloc(p->order, sizeof(int) * cap);
    p->order_cap = cap;
}

// How a stored index moves when paths[from] is relocated to slot `to`. Mirrors the
// index fixup playlist_move applies, so the shuffle order keeps pointing at the
// same songs after a drag-reorder.
static int remap_after_move(int v, int from, int to) {
    if (v == from)                 return to;
    if (from < v && v <= to)       return v - 1;
    if (to   <= v && v <  from)    return v + 1;
    return v;
}

// Point the cursor at whichever slot currently holds the playing track, so
// playback continues from there instead of restarting the shuffle walk.
static void order_point_at_current(Playlist* p) {
    p->order_pos = (p->order_count > 0) ? 0 : -1;
    for (int i = 0; i < p->order_count; i++)
        if (p->order[i] == p->index) { p->order_pos = i; break; }
}

// Build a fresh random permutation of [0..count). The current track is left
// wherever it lands and the cursor points at it (so the song keeps playing).
static void order_build(Playlist* p) {
    if (p->count <= 0) { p->order_count = 0; p->order_pos = -1; return; }
    order_ensure_cap(p, p->count);
    for (int i = 0; i < p->count; i++) p->order[i] = i;
    for (int i = p->count - 1; i > 0; i--) {       // Fisher-Yates
        int j = rand() % (i + 1);
        int t = p->order[i]; p->order[i] = p->order[j]; p->order[j] = t;
    }
    p->order_count = p->count;
    order_point_at_current(p);
}

void playlist_init(Playlist* p) {
    memset(p, 0, sizeof(*p));
    p->index = -1;
    p->order_pos = -1;
    static int seeded = 0;
    if (!seeded) { srand((unsigned)time(NULL)); seeded = 1; }
}

void playlist_clear(Playlist* p) {
    for (int i = 0; i < p->count; i++) free(p->paths[i]);
    p->count = 0;
    p->index = -1;
    p->order_count = 0;
    p->order_pos = -1;
    p->name[0] = 0;
    p->dirty = false;
}

void playlist_free(Playlist* p) {
    playlist_clear(p);
    free(p->paths);  p->paths = NULL;  p->cap = 0;
    free(p->order);  p->order = NULL;  p->order_cap = 0;
}

static char* dupstr(const char* s) {
    size_t n = strlen(s) + 1;
    char* d = (char*)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

void playlist_add(Playlist* p, const char* path) {
    if (!path) return;
    if (p->count == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 8;
        p->paths = (char**)realloc(p->paths, sizeof(char*) * p->cap);
    }
    p->paths[p->count++] = dupstr(path);
    if (p->index < 0) p->index = 0;
    if (p->shuffle) {                     // append the new track to the tail of the order
        order_ensure_cap(p, p->order_count + 1);
        p->order[p->order_count++] = p->count - 1;
        if (p->order_pos < 0) p->order_pos = 0;
    }
    p->dirty = true;
}

bool playlist_remove(Playlist* p, int idx) {
    if (idx < 0 || idx >= p->count) return false;
    bool was_current = (idx == p->index);
    free(p->paths[idx]);
    for (int i = idx; i < p->count - 1; i++) p->paths[i] = p->paths[i+1];
    p->count--;
    if (p->count == 0)              p->index = -1;
    else if (p->index >= p->count)  p->index = p->count - 1;
    else if (p->index > idx)        p->index--;

    if (p->shuffle) {                     // drop idx and renumber the entries above it
        int w = 0;
        for (int r = 0; r < p->order_count; r++) {
            int v = p->order[r];
            if (v == idx) continue;
            if (v > idx) v--;
            p->order[w++] = v;
        }
        p->order_count = w;
        order_point_at_current(p);
    }
    p->dirty = true;
    return was_current;
}

void playlist_move(Playlist* p, int from, int to) {
    if (from < 0 || from >= p->count) return;
    if (to < 0) to = 0;
    if (to >= p->count) to = p->count - 1;
    if (from == to) return;

    char* moved = p->paths[from];
    if (from < to) for (int i = from; i < to; i++) p->paths[i] = p->paths[i + 1];
    else           for (int i = from; i > to; i--) p->paths[i] = p->paths[i - 1];
    p->paths[to] = moved;

    // Keep the current track's identity stable across the reorder.
    p->index = remap_after_move(p->index, from, to);
    if (p->shuffle) {                     // renumber the order; the cursor slot is unchanged
        for (int i = 0; i < p->order_count; i++)
            p->order[i] = remap_after_move(p->order[i], from, to);
    }
    p->dirty = true;
}

const char* playlist_current(const Playlist* p) {
    if (p->index < 0 || p->index >= p->count) return NULL;
    return p->paths[p->index];
}

bool playlist_has_next(const Playlist* p) {
    if (p->count == 0) return false;
    if (p->shuffle && p->order_count > 0) {
        if (p->order_pos + 1 < p->order_count) return true;
        return p->loop;
    }
    if (p->index + 1 < p->count) return true;
    return p->loop;
}

bool playlist_has_prev(const Playlist* p) {
    if (p->count == 0) return false;
    if (p->shuffle && p->order_count > 0) {
        if (p->order_pos > 0) return true;
        return p->loop;
    }
    if (p->index > 0) return true;
    return p->loop;
}

const char* playlist_next(Playlist* p) {
    if (p->count == 0) return NULL;
    if (p->shuffle && p->order_count > 0) {
        if (p->order_pos + 1 < p->order_count) p->index = p->order[++p->order_pos];
        else if (p->loop)                      { p->order_pos = 0; p->index = p->order[0]; }
        else return NULL;
    } else if (p->index + 1 < p->count) {
        p->index++;
    } else if (p->loop) {
        p->index = 0;
    } else return NULL;
    return p->paths[p->index];
}

const char* playlist_prev(Playlist* p) {
    if (p->count == 0) return NULL;
    if (p->shuffle && p->order_count > 0) {
        if (p->order_pos > 0) p->index = p->order[--p->order_pos];
        else if (p->loop)     { p->order_pos = p->order_count - 1; p->index = p->order[p->order_pos]; }
        else return NULL;
    } else if (p->index > 0) {
        p->index--;
    } else if (p->loop) {
        p->index = p->count - 1;
    } else return NULL;
    return p->paths[p->index];
}

void playlist_set_index(Playlist* p, int i) {
    if (i < 0 || i >= p->count) return;
    p->index = i;
    // A manual jump just repositions the cursor inside the existing shuffle order.
    if (p->shuffle && p->order_count > 0) order_point_at_current(p);
}

int playlist_count(const Playlist* p) { return p->count; }
int playlist_index(const Playlist* p) { return p->index; }

void playlist_set_shuffle(Playlist* p, bool on) {
    if (p->shuffle == on) return;
    p->shuffle = on;
    if (on) order_build(p);
    else { p->order_count = 0; p->order_pos = -1; }
}
void playlist_reshuffle(Playlist* p) {
    if (p->shuffle) order_build(p);
}
void playlist_set_loop(Playlist* p, bool on) { p->loop = on; }
bool playlist_shuffle(const Playlist* p) { return p->shuffle; }
bool playlist_loop(const Playlist* p) { return p->loop; }

const char* playlist_name(const Playlist* p) { return p->name; }
void playlist_set_name(Playlist* p, const char* name) {
    if (!name) { p->name[0] = 0; return; }
    snprintf(p->name, sizeof(p->name), "%s", name);
}
bool playlist_dirty(const Playlist* p) { return p->dirty; }
void playlist_mark_clean(Playlist* p) { p->dirty = false; }
