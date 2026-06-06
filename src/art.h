#ifndef TIMP_ART_H
#define TIMP_ART_H

#include <stdbool.h>

// Extracts the embedded cover art from an audio file and decodes it to RGBA8.
// On success returns true with *rgba malloc'd (free with free()) and *w/*h set.
// Supports ID3v2 APIC frames (MP3) and FLAC PICTURE blocks; files without art
// (e.g. WAV) return false. Decoding uses a file-local stb_image (JPEG/PNG).
bool art_load_rgba(const char *path, unsigned char **rgba, int *w, int *h);

// Decodes a standalone image file (JPEG/PNG) to RGBA8 with the same file-local
// stb_image. Unicode-safe on Windows. Used for folder-level cover art.
bool art_decode_file(const char *path, unsigned char **rgba, int *w, int *h);

#endif
