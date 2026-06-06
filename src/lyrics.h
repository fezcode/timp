#ifndef TIMP_LYRICS_H
#define TIMP_LYRICS_H

#include <stdbool.h>

#define LYRICS_MAX 800

typedef struct { double t; char text[200]; } LrcLine;   // t < 0 → unsynced line
typedef struct { LrcLine lines[LYRICS_MAX]; int count; bool synced; } Lyrics;

// Loads lyrics for an audio file. Looks for a sibling .lrc (synced) first, then
// .txt, then embedded ID3/Vorbis lyrics. out->count is 0 if none were found.
void lyrics_load(const char *audio_path, Lyrics *out);

// Index of the current line for synced lyrics at playback time t, or -1.
int lyrics_active(const Lyrics *l, double t);

// Online fallback (lrclib.net) — runs on a background thread so the UI never
// blocks. Start a request for the current track; later poll for the result.
void lyrics_fetch_start(const char *artist, const char *title, const char *album, int duration_sec);
// If a result for the latest request arrived, parses it into out and returns
// true (out may be empty → the server had no match). Otherwise returns false.
bool lyrics_fetch_poll(Lyrics *out);

#endif
