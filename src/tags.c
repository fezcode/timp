#include "tags.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static FILE *open_rb(const char *path) {
    int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (n <= 0) return fopen(path, "rb");
    wchar_t *w = (wchar_t *)malloc(sizeof(wchar_t) * n);
    if (!w) return fopen(path, "rb");
    MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n);
    FILE *f = _wfopen(w, L"rb"); free(w); return f;
}
#else
static FILE *open_rb(const char *p) { return fopen(p, "rb"); }
#endif

static unsigned be32(const unsigned char *p) { return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static unsigned be24(const unsigned char *p) { return ((unsigned)p[0] << 16) | (p[1] << 8) | p[2]; }
static unsigned le32(const unsigned char *p) { return ((unsigned)p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0]; }
static unsigned syncsafe(const unsigned char *p) { return ((unsigned)(p[0] & 0x7f) << 21) | ((p[1] & 0x7f) << 14) | ((p[2] & 0x7f) << 7) | (p[3] & 0x7f); }

static int ci_eq(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
    }
    return 1;
}

// Decode an ID3v2 text payload (given encoding + raw bytes) into UTF-8.
static void decode_text(unsigned char enc, const unsigned char *p, int len, char *dst, int cap) {
    dst[0] = 0;
    char *o = dst, *end = dst + cap;
    if (enc == 0) {                              // latin1 → utf8
        for (int i = 0; i < len && p[i]; i++) {
            unsigned char c = p[i];
            if (c < 0x80) { if (o < end - 1) *o++ = c; }
            else if (o < end - 2) { *o++ = (char)(0xC0 | (c >> 6)); *o++ = (char)(0x80 | (c & 0x3f)); }
        }
    } else if (enc == 3) {                        // utf8
        for (int i = 0; i < len && p[i]; i++) if (o < end - 1) *o++ = (char)p[i];
    } else {                                      // utf-16 (1=BOM, 2=BE)
        int be = (enc == 2), i = 0;
        if (enc == 1 && len >= 2) {
            if (p[0] == 0xFF && p[1] == 0xFE) { be = 0; i = 2; }
            else if (p[0] == 0xFE && p[1] == 0xFF) { be = 1; i = 2; }
        }
        for (; i + 1 < len; i += 2) {
            unsigned u = be ? ((p[i] << 8) | p[i + 1]) : ((p[i + 1] << 8) | p[i]);
            if (u == 0) break;
            if (u < 0x80) { if (o < end - 1) *o++ = (char)u; }
            else if (u < 0x800) { if (o < end - 2) { *o++ = (char)(0xC0 | (u >> 6)); *o++ = (char)(0x80 | (u & 0x3f)); } }
            else if (o < end - 3) { *o++ = (char)(0xE0 | (u >> 12)); *o++ = (char)(0x80 | ((u >> 6) & 0x3f)); *o++ = (char)(0x80 | (u & 0x3f)); }
        }
    }
    *o = 0;
}

static void id3_text(const unsigned char *p, int len, char *dst, int cap) {
    dst[0] = 0;
    if (len < 1) return;
    decode_text(p[0], p + 1, len - 1, dst, cap);
}

static bool read_id3(FILE *f, Tags *t) {
    unsigned char h[10]; rewind(f);
    if (fread(h, 1, 10, f) != 10 || memcmp(h, "ID3", 3)) return false;
    int ver = h[3]; unsigned size = syncsafe(h + 6);
    if (size == 0 || size > 16u * 1024 * 1024) return false;
    unsigned char *buf = (unsigned char *)malloc(size);
    if (!buf) return false;
    if (fread(buf, 1, size, f) != size) { free(buf); return false; }
    bool any = false; unsigned pos = 0;
    while (pos + 10 <= size) {
        if (buf[pos] == 0) break;
        unsigned fsz = (ver >= 4) ? syncsafe(buf + pos + 4) : be32(buf + pos + 4);
        if (fsz == 0 || pos + 10 + fsz > size) break;
        const unsigned char *fr = buf + pos + 10;
        if (!memcmp(buf + pos, "TIT2", 4))      { id3_text(fr, fsz, t->title, sizeof(t->title));  if (t->title[0]) any = true; }
        else if (!memcmp(buf + pos, "TPE1", 4)) { id3_text(fr, fsz, t->artist, sizeof(t->artist)); if (t->artist[0]) any = true; }
        else if (!memcmp(buf + pos, "TALB", 4)) { id3_text(fr, fsz, t->album, sizeof(t->album));  if (t->album[0]) any = true; }
        pos += 10 + fsz;
    }
    free(buf);
    return any;
}

static void match_comment(const char *c, int len, Tags *t) {
    const char *eq = (const char *)memchr(c, '=', len);
    if (!eq) return;
    int klen = (int)(eq - c), vlen = len - klen - 1;
    const char *val = eq + 1;
    char *dst = NULL; int cap = 0;
    if (klen == 5 && ci_eq(c, "TITLE", 5))       { dst = t->title;  cap = sizeof(t->title); }
    else if (klen == 6 && ci_eq(c, "ARTIST", 6)) { dst = t->artist; cap = sizeof(t->artist); }
    else if (klen == 5 && ci_eq(c, "ALBUM", 5))  { dst = t->album;  cap = sizeof(t->album); }
    if (dst && dst[0] == 0) { int n = vlen < cap - 1 ? vlen : cap - 1; if (n < 0) n = 0; memcpy(dst, val, n); dst[n] = 0; }
}

static bool read_flac(FILE *f, Tags *t) {
    unsigned char m[4]; rewind(f);
    if (fread(m, 1, 4, f) != 4 || memcmp(m, "fLaC", 4)) return false;
    for (;;) {
        unsigned char bh[4]; if (fread(bh, 1, 4, f) != 4) break;
        int last = bh[0] & 0x80, type = bh[0] & 0x7f; unsigned len = be24(bh + 1);
        if (type == 4) {  // VORBIS_COMMENT (little-endian lengths)
            if (len > 4u * 1024 * 1024) break;
            unsigned char *b = (unsigned char *)malloc(len);
            if (!b) break;
            if (fread(b, 1, len, f) != len) { free(b); break; }
            unsigned p = 0;
            if (p + 4 <= len) { unsigned vl = le32(b + p); p += 4 + vl; }
            if (p + 4 <= len) {
                unsigned cnt = le32(b + p); p += 4;
                for (unsigned i = 0; i < cnt && p + 4 <= len; i++) {
                    unsigned cl = le32(b + p); p += 4;
                    if (p + cl > len) break;
                    match_comment((const char *)(b + p), (int)cl, t);
                    p += cl;
                }
            }
            free(b);
            return t->title[0] || t->artist[0] || t->album[0];
        } else if (fseek(f, (long)len, SEEK_CUR)) break;
        if (last) break;
    }
    return false;
}

bool tags_read(const char *path, Tags *out) {
    memset(out, 0, sizeof(*out));
    FILE *f = open_rb(path);
    if (!f) return false;
    bool ok = read_id3(f, out);
    if (!ok) ok = read_flac(f, out);
    fclose(f);
    return ok;
}
