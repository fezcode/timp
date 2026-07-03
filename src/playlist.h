#ifndef TIMP_PLAYLIST_H
#define TIMP_PLAYLIST_H

#include <stdbool.h>

typedef struct Playlist {
    // The playlist proper: the user's songs in the user's order. Shuffle never
    // reorders this — only an explicit drag/move does.
    char** paths;
    int count;
    int cap;
    int index;        // currently-playing entry (index into paths); -1 when empty

    bool shuffle;
    bool loop;

    // The play queue: a list of playlist indices that playback walks in sequence
    // (order[order_pos] == index). Rebuilt as a permutation — shuffle off →
    // identity, so the queue mirrors the playlist; shuffle on → an anchor song
    // first (the one the user clicked, or the one playing when shuffle was
    // switched on) with every other song shuffled once after it, so a full pass
    // plays everything exactly once. Manual edits (insert/remove/reorder) may
    // then introduce duplicates and holes; queue_edited tracks that. The end of
    // the queue wraps the *same* order when loop is on (no re-shuffle).
    int* order;
    int  order_count;
    int  order_cap;
    int  order_pos;      // cursor into order[]; order[order_pos] == index
    bool queue_edited;   // manually customized since the last rebuild

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

// "The user clicked song i in the playlist": shuffle on → i becomes the head of
// a freshly shuffled queue (all other songs once, in random order, after it);
// shuffle off → the cursor just moves to i in the playlist-order queue.
void playlist_play_index(Playlist* p, int i);

// Jump the cursor to queue position pos without changing the queue order.
void playlist_queue_jump(Playlist* p, int pos);
int  playlist_queue_at(const Playlist* p, int pos);  // playlist index at queue pos; -1 if out of range
int  playlist_queue_pos(const Playlist* p);          // cursor position in the queue; -1 when empty
int  playlist_queue_count(const Playlist* p);        // queue length (≠ count once edited)

// Manual queue edits. They never touch the playlist itself, and are discarded
// the next time the queue rebuilds (playlist click / shuffle toggle / load).
void playlist_queue_insert_next(Playlist* p, int idx);  // idx plays right after the current song; duplicates fine
bool playlist_queue_remove(Playlist* p, int pos);       // refuses the playing entry (pos == order_pos)
void playlist_queue_move(Playlist* p, int from, int to);
bool playlist_queue_edited(const Playlist* p);

int playlist_count(const Playlist* p);
int playlist_index(const Playlist* p);

void playlist_set_shuffle(Playlist* p, bool on);
void playlist_set_loop(Playlist* p, bool on);
bool playlist_shuffle(const Playlist* p);
bool playlist_loop(const Playlist* p);
// Rebuild the queue for the current mode: identity when shuffle is off, a fresh
// current-song-first shuffle when on. (Used after loading a saved playlist.)
void playlist_rebuild_queue(Playlist* p);

// Saved-playlist name + dirty flag (drives the drawer's Save button).
const char* playlist_name(const Playlist* p);
void playlist_set_name(Playlist* p, const char* name);
bool playlist_dirty(const Playlist* p);
void playlist_mark_clean(Playlist* p);

#endif
