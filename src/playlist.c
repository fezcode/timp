#include "playlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---------- play queue ----------
static void order_ensure_cap(Playlist* p, int need) {
    if (p->order_cap >= need) return;
    int cap = p->order_cap ? p->order_cap : 16;
    while (cap < need) cap *= 2;
    p->order = (int*)realloc(p->order, sizeof(int) * cap);
    p->order_cap = cap;
}

// How a stored index moves when paths[from] is relocated to slot `to`. Mirrors the
// index fixup playlist_move applies, so the queue keeps pointing at the same songs
// after a drag-reorder.
static int remap_after_move(int v, int from, int to) {
    if (v == from)                 return to;
    if (from < v && v <= to)       return v - 1;
    if (to   <= v && v <  from)    return v + 1;
    return v;
}

// Point the cursor at whichever queue slot currently holds the playing track, so
// playback continues from there instead of restarting the queue walk.
static void order_point_at_current(Playlist* p) {
    p->order_pos = (p->order_count > 0) ? 0 : -1;
    for (int i = 0; i < p->order_count; i++)
        if (p->order[i] == p->index) { p->order_pos = i; break; }
}

// Queue mirrors the playlist (shuffle off): identity order, cursor on the
// current song.
static void queue_identity(Playlist* p) {
    p->queue_edited = false;
    if (p->count <= 0) { p->order_count = 0; p->order_pos = -1; return; }
    order_ensure_cap(p, p->count);
    for (int i = 0; i < p->count; i++) p->order[i] = i;
    p->order_count = p->count;
    order_point_at_current(p);
}

// Shuffled queue with `anchor` first: the anchor song plays now, then every other
// song exactly once in random order (Fisher-Yates over positions 1..count-1).
static void queue_shuffle_anchored(Playlist* p, int anchor) {
    p->queue_edited = false;
    if (p->count <= 0) { p->order_count = 0; p->order_pos = -1; return; }
    if (anchor < 0 || anchor >= p->count) anchor = 0;
    order_ensure_cap(p, p->count);
    int w = 0;
    p->order[w++] = anchor;
    for (int i = 0; i < p->count; i++) if (i != anchor) p->order[w++] = i;
    for (int i = p->count - 1; i > 1; i--) {
        int j = 1 + rand() % i;   // uniform in [1..i]; slot 0 (the anchor) never moves
        int t = p->order[i]; p->order[i] = p->order[j]; p->order[j] = t;
    }
    p->order_count = p->count;
    p->order_pos = 0;
    p->index = anchor;
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
    p->queue_edited = false;
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
    // New songs join the tail of the queue in both modes (keeps identity intact
    // when shuffle is off, and guarantees the song still gets played when on).
    order_ensure_cap(p, p->order_count + 1);
    p->order[p->order_count++] = p->count - 1;
    if (p->order_pos < 0) p->order_pos = 0;
    p->dirty = true;
}

bool playlist_remove(Playlist* p, int idx) {
    if (idx < 0 || idx >= p->count) return false;
    bool was_current = (idx == p->index);
    free(p->paths[idx]);
    for (int i = idx; i < p->count - 1; i++) p->paths[i] = p->paths[i+1];
    p->count--;

    // Drop every queue entry of the removed song (an edited queue may hold it
    // several times) and renumber the rest. The cursor is tracked by position,
    // not by re-finding the song — duplicates make that ambiguous.
    int w = 0, newpos = -1, before = 0;
    for (int r = 0; r < p->order_count; r++) {
        int v = p->order[r];
        if (v == idx) continue;
        if (v > idx) v--;
        if (r == p->order_pos) newpos = w;
        if (r <  p->order_pos) before = w + 1;   // where the entry after the cursor now sits
        p->order[w++] = v;
    }
    p->order_count = w;

    if (p->count == 0) {
        p->index = -1;
        p->order_pos = -1;
    } else if (!was_current) {
        if (p->index > idx) p->index--;
        if (newpos >= 0) p->order_pos = newpos;
        else order_point_at_current(p);           // shouldn't happen; keep the cursor sane
    } else if (p->order_count > 0) {
        // The playing song went away: continue from the entry that followed it
        // (or wrap/stop at the tail when it was last).
        p->order_pos = (before < p->order_count) ? before : (p->loop ? 0 : p->order_count - 1);
        p->index = p->order[p->order_pos];
    } else {
        // Queue held only the removed song — rebuild it for the current mode.
        if (p->index >= p->count) p->index = p->count - 1;
        playlist_rebuild_queue(p);
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
    if (p->shuffle || p->queue_edited) {   // custom/shuffled queue keeps its play sequence; entries renumber
        for (int i = 0; i < p->order_count; i++)
            p->order[i] = remap_after_move(p->order[i], from, to);
    } else {                               // pristine queue mirrors the playlist's new order
        queue_identity(p);
    }
    p->dirty = true;
}

const char* playlist_current(const Playlist* p) {
    if (p->index < 0 || p->index >= p->count) return NULL;
    return p->paths[p->index];
}

bool playlist_has_next(const Playlist* p) {
    if (p->order_count == 0) return false;
    return (p->order_pos + 1 < p->order_count) || p->loop;
}

bool playlist_has_prev(const Playlist* p) {
    if (p->order_count == 0) return false;
    return (p->order_pos > 0) || p->loop;
}

const char* playlist_next(Playlist* p) {
    if (p->order_count == 0) return NULL;
    if (p->order_pos + 1 < p->order_count) p->order_pos++;
    else if (p->loop)                      p->order_pos = 0;
    else return NULL;
    p->index = p->order[p->order_pos];
    return p->paths[p->index];
}

const char* playlist_prev(Playlist* p) {
    if (p->order_count == 0) return NULL;
    if (p->order_pos > 0) p->order_pos--;
    else if (p->loop)     p->order_pos = p->order_count - 1;
    else return NULL;
    p->index = p->order[p->order_pos];
    return p->paths[p->index];
}

void playlist_play_index(Playlist* p, int i) {
    if (i < 0 || i >= p->count) return;
    p->index = i;
    // A playlist click starts a fresh listening session: the queue rebuilds and
    // any manual queue edits are discarded.
    if (p->shuffle) queue_shuffle_anchored(p, i);   // clicked song first, rest reshuffled
    else            queue_identity(p);              // playlist order, cursor on i
}

void playlist_queue_jump(Playlist* p, int pos) {
    if (pos < 0 || pos >= p->order_count) return;
    p->order_pos = pos;
    p->index = p->order[pos];
}

int playlist_queue_at(const Playlist* p, int pos) {
    if (pos < 0 || pos >= p->order_count) return -1;
    return p->order[pos];
}

int playlist_queue_pos(const Playlist* p) { return p->order_pos; }
int playlist_queue_count(const Playlist* p) { return p->order_count; }

void playlist_queue_insert_next(Playlist* p, int idx) {
    if (idx < 0 || idx >= p->count) return;
    order_ensure_cap(p, p->order_count + 1);
    int at = (p->order_pos >= 0) ? p->order_pos + 1 : 0;
    if (at > p->order_count) at = p->order_count;
    for (int i = p->order_count; i > at; i--) p->order[i] = p->order[i - 1];
    p->order[at] = idx;
    p->order_count++;
    if (p->order_pos < 0) { p->order_pos = 0; p->index = idx; }   // queue was empty — it becomes current
    p->queue_edited = true;   // a queue tweak, not a playlist change: dirty stays untouched
}

bool playlist_queue_remove(Playlist* p, int pos) {
    if (pos < 0 || pos >= p->order_count || pos == p->order_pos) return false;
    for (int i = pos; i < p->order_count - 1; i++) p->order[i] = p->order[i + 1];
    p->order_count--;
    if (pos < p->order_pos) p->order_pos--;
    p->queue_edited = true;
    return true;
}

void playlist_queue_move(Playlist* p, int from, int to) {
    if (from < 0 || from >= p->order_count) return;
    if (to < 0) to = 0;
    if (to >= p->order_count) to = p->order_count - 1;
    if (from == to) return;
    int moved = p->order[from];
    if (from < to) for (int i = from; i < to; i++) p->order[i] = p->order[i + 1];
    else           for (int i = from; i > to; i--) p->order[i] = p->order[i - 1];
    p->order[to] = moved;
    // remap_after_move works on positions just as well as on indices
    p->order_pos = remap_after_move(p->order_pos, from, to);
    p->queue_edited = true;
}

bool playlist_queue_edited(const Playlist* p) { return p->queue_edited; }

int playlist_count(const Playlist* p) { return p->count; }
int playlist_index(const Playlist* p) { return p->index; }

void playlist_set_shuffle(Playlist* p, bool on) {
    if (p->shuffle == on) return;
    p->shuffle = on;
    if (on) queue_shuffle_anchored(p, p->index);   // current song keeps playing, rest shuffled after it
    else    queue_identity(p);                     // queue snaps back to playlist order
}

void playlist_rebuild_queue(Playlist* p) {
    if (p->shuffle) queue_shuffle_anchored(p, p->index);
    else            queue_identity(p);
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
