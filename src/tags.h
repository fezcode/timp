#ifndef TIMP_TAGS_H
#define TIMP_TAGS_H

#include <stdbool.h>

typedef struct { char title[256]; char artist[256]; char album[256]; } Tags;

// Reads title/artist/album from ID3v2 (MP3) text frames or FLAC Vorbis comments.
// Fields are UTF-8 (empty if absent). Returns true if at least one was found.
// Unicode-safe on Windows.
bool tags_read(const char *path, Tags *out);

#endif
