#include "art.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// File-local stb_image (STATIC → no symbol clash with raylib's own copy).
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../vendor/stb_image.h"

#ifdef _WIN32
#include <windows.h>
static FILE *open_rb(const char *path) {
    int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (n <= 0) return fopen(path, "rb");
    wchar_t *w = (wchar_t *)malloc(sizeof(wchar_t) * n);
    if (!w) return fopen(path, "rb");
    MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n);
    FILE *f = _wfopen(w, L"rb");
    free(w);
    return f;
}
#else
static FILE *open_rb(const char *path) { return fopen(path, "rb"); }
#endif

static unsigned rd_be32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}
static unsigned rd_be24(const unsigned char *p) {
    return ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | p[2];
}
static unsigned syncsafe(const unsigned char *p) {
    return ((unsigned)(p[0] & 0x7f) << 21) | ((unsigned)(p[1] & 0x7f) << 14) |
           ((unsigned)(p[2] & 0x7f) << 7) | (p[3] & 0x7f);
}

// ---- ID3v2 APIC (MP3) ----
static bool extract_id3(FILE *f, unsigned char **od, int *os) {
    unsigned char h[10];
    rewind(f);
    if (fread(h, 1, 10, f) != 10) return false;
    if (memcmp(h, "ID3", 3) != 0) return false;
    int ver = h[3];
    unsigned size = syncsafe(h + 6);
    if (size == 0 || size > 64u * 1024 * 1024) return false;
    unsigned char *buf = (unsigned char *)malloc(size);
    if (!buf) return false;
    if (fread(buf, 1, size, f) != size) { free(buf); return false; }

    bool found = false;
    unsigned pos = 0;
    while (pos + 10 <= size) {
        if (buf[pos] == 0) break;  // padding
        unsigned fsz = (ver >= 4) ? syncsafe(buf + pos + 4) : rd_be32(buf + pos + 4);
        if (fsz == 0 || pos + 10 + fsz > size) break;
        if (memcmp(buf + pos, "APIC", 4) == 0) {
            const unsigned char *p = buf + pos + 10;
            const unsigned char *end = p + fsz;
            unsigned char enc = *p++;          // text encoding
            while (p < end && *p) p++;          // MIME type (latin1)
            if (p < end) p++;                   // null
            if (p < end) p++;                   // picture type
            if (enc == 1 || enc == 2) {         // UTF-16 description → double-null
                while (p + 1 < end && (p[0] || p[1])) p += 2;
                if (p + 1 < end) p += 2;
            } else {                            // latin1/UTF-8 description → single null
                while (p < end && *p) p++;
                if (p < end) p++;
            }
            int dlen = (int)(end - p);
            if (dlen > 0) {
                unsigned char *data = (unsigned char *)malloc(dlen);
                if (data) { memcpy(data, p, dlen); *od = data; *os = dlen; found = true; }
            }
            break;
        }
        pos += 10 + fsz;
    }
    free(buf);
    return found;
}

// ---- FLAC PICTURE block ----
static bool extract_flac(FILE *f, unsigned char **od, int *os) {
    unsigned char marker[4];
    rewind(f);
    if (fread(marker, 1, 4, f) != 4) return false;
    if (memcmp(marker, "fLaC", 4) != 0) return false;
    for (;;) {
        unsigned char bh[4];
        if (fread(bh, 1, 4, f) != 4) return false;
        int last = bh[0] & 0x80;
        int type = bh[0] & 0x7f;
        unsigned len = rd_be24(bh + 1);
        if (type == 6) {  // PICTURE
            if (len < 32 || len > 64u * 1024 * 1024) return false;
            unsigned char *blk = (unsigned char *)malloc(len);
            if (!blk) return false;
            if (fread(blk, 1, len, f) != len) { free(blk); return false; }
            unsigned p = 4;  // skip picture type (u32)
            if (p + 4 > len) { free(blk); return false; }
            unsigned mlen = rd_be32(blk + p); p += 4;
            if ((unsigned long)p + mlen + 4 > len) { free(blk); return false; }
            p += mlen;
            unsigned dlen2 = rd_be32(blk + p); p += 4;
            if ((unsigned long)p + dlen2 + 20 > len) { free(blk); return false; }
            p += dlen2;
            p += 16;  // width,height,depth,colors
            unsigned datalen = rd_be32(blk + p); p += 4;
            if ((unsigned long)p + datalen <= len && datalen > 0) {
                unsigned char *data = (unsigned char *)malloc(datalen);
                if (data) { memcpy(data, blk + p, datalen); *od = data; *os = (int)datalen; free(blk); return true; }
            }
            free(blk);
            return false;
        }
        if (fseek(f, (long)len, SEEK_CUR) != 0) return false;
        if (last) break;
    }
    return false;
}

bool art_load_rgba(const char *path, unsigned char **rgba, int *w, int *h) {
    *rgba = NULL;
    FILE *f = open_rb(path);
    if (!f) return false;
    unsigned char *enc = NULL;
    int enc_size = 0;
    bool ok = extract_id3(f, &enc, &enc_size);
    if (!ok) ok = extract_flac(f, &enc, &enc_size);
    fclose(f);
    if (!ok || !enc) return false;

    int comp = 0;
    unsigned char *px = stbi_load_from_memory(enc, enc_size, w, h, &comp, 4);
    free(enc);
    if (!px) return false;
    *rgba = px;
    return true;
}

bool art_decode_file(const char *path, unsigned char **rgba, int *w, int *h) {
    *rgba = NULL;
    FILE *f = open_rb(path);
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 64L * 1024 * 1024) { fclose(f); return false; }
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return false; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return false; }
    fclose(f);
    int comp = 0;
    unsigned char *px = stbi_load_from_memory(buf, (int)sz, w, h, &comp, 4);
    free(buf);
    if (!px) return false;
    *rgba = px;
    return true;
}
