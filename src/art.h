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

// Same extraction as art_load_rgba, but hands back the *still-encoded* JPEG/PNG
// blob instead of decoding it. Callers that need to re-publish the artwork as a
// file (the OS now-playing thumbnail) want the original bytes, not pixels.
// On success returns true with *data malloc'd (free with free()) and *size set.
bool art_load_encoded(const char *path, unsigned char **data, int *size);

// Finds a cover image sitting next to `path` (cover.jpg, folder.png, ...) and
// writes its full path into out. Returns false when the folder has none.
bool art_find_dir_cover(const char *path, char *out, int outsz);

#endif
