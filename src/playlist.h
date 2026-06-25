#ifndef TIMP_PLAYLIST_H
#define TIMP_PLAYLIST_H

#include <stdbool.h>

typedef struct Playlist {
    char** paths;
    int count;
    int cap;
    int index;

    bool shuffle;
    bool loop;

    // Fixed shuffle order: a permutation of [0..count). While shuffle is on,
    // next/prev move `order_pos` through this array and order[order_pos]==index.
    // The order is built once when shuffle is toggled on (the current track keeps
    // playing wherever it lands); newly-added tracks append to the end; remove/move
    // keep it a valid permutation with the cursor still on the current song. The
    // end of the list wraps the *same* order when loop is on (no re-shuffle).
    int* order;
    int  order_count;
    int  order_cap;
    int  order_pos;   // cursor into order[]; order[order_pos] == index

    // Saved-playlist bookkeeping for the drawer's Save button.
    char name[128];   // display name (file stem); empty when untitled
    bool dirty;       // list content changed since the last save/load
} Playlist;

void playlist_init(Playlist* p);
void playlist_free(Playlist* p);
void playlist_clear(Playlist* p);

void playlist_add(Playlist* p, const char* path);
// Removes the entry at idx. Returns true if the currently-playing track was removed.
bool playlist_remove(Playlist* p, int idx);
// Moves the entry at `from` so that it ends up at index `to`. Updates the
// current-track index so the same song stays current after the reorder.
void playlist_move(Playlist* p, int from, int to);

const char* playlist_current(const Playlist* p);
bool playlist_has_next(const Playlist* p);
bool playlist_has_prev(const Playlist* p);

const char* playlist_next(Playlist* p);
const char* playlist_prev(Playlist* p);
void playlist_set_index(Playlist* p, int i);

int playlist_count(const Playlist* p);
int playlist_index(const Playlist* p);

void playlist_set_shuffle(Playlist* p, bool on);
void playlist_set_loop(Playlist* p, bool on);
bool playlist_shuffle(const Playlist* p);
bool playlist_loop(const Playlist* p);
// Rebuild the shuffle order from scratch (no-op when shuffle is off). The current
// track keeps playing where it lands in the new order.
void playlist_reshuffle(Playlist* p);

// Saved-playlist name + dirty flag (drives the drawer's Save button).
const char* playlist_name(const Playlist* p);
void playlist_set_name(Playlist* p, const char* name);
bool playlist_dirty(const Playlist* p);
void playlist_mark_clean(Playlist* p);

#endif
