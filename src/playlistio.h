#ifndef TIMP_PLAYLISTIO_H
#define TIMP_PLAYLISTIO_H

#include <stdbool.h>
#include "playlist.h"

#define PL_NAME_MAX  128   // max saved-playlist name length (bytes, UTF-8)
#define PL_MAX_SAVED 256   // cap on how many saved playlists we enumerate

// Absolute UTF-8 path of the folder where saved playlists live
// (<data_dir>\Playlists), creating it if needed.
void playlistio_dir(char *out, int cap);

// True if a saved playlist with this name already exists (drives the
// overwrite-vs-new decision).
bool playlistio_exists(const char *name);

// Write the playlist to <dir>\<name>.m3u8 — UTF-8, one absolute path per line.
// Returns true on success.
bool playlistio_save(const Playlist *p, const char *name);

// Replace the playlist's contents with <dir>\<name>.m3u8: clears p, adds each
// listed path, sets the playlist name, and marks it clean. Returns the number of
// tracks loaded, or -1 if the file could not be opened.
int playlistio_load(Playlist *p, const char *name);

// List saved playlist names (file stems, no extension), most-recently-saved
// first. Fills out[i] (each PL_NAME_MAX bytes). Returns the count (<= max).
int playlistio_list(char out[][PL_NAME_MAX], int max);

#endif
