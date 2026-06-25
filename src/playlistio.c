#include "playlistio.h"
#include "rlconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

static wchar_t *u2w(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc(sizeof(wchar_t) * n);
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}
static char *w2u(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = (char *)malloc(n);
    if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}
// fopen that handles Unicode (Turkish) names — the playlist name itself may carry
// İ/ı/ş/ğ, and ANSI fopen would mangle it.
static FILE *pl_fopen(const char *path, const char *mode) {
    wchar_t *w = u2w(path);
    if (w) {
        wchar_t wm[8]; int i = 0;
        for (; mode[i] && i < 7; i++) wm[i] = (wchar_t)mode[i];
        wm[i] = 0;
        FILE *f = _wfopen(w, wm);
        free(w);
        return f;
    }
    return fopen(path, mode);
}
#else
static FILE *pl_fopen(const char *path, const char *mode) { return fopen(path, mode); }
#endif

void playlistio_dir(char *out, int cap) {
    char data[600];
    rlconfig_data_dir(data, sizeof(data));
#ifdef _WIN32
    snprintf(out, cap, "%s\\Playlists", data);
    wchar_t *w = u2w(out);
    if (w) { CreateDirectoryW(w, NULL); free(w); }
#else
    snprintf(out, cap, "%s/Playlists", data);
#endif
}

static void pl_path(char *out, int cap, const char *name) {
    char dir[600];
    playlistio_dir(dir, sizeof(dir));
#ifdef _WIN32
    snprintf(out, cap, "%s\\%s.m3u8", dir, name);
#else
    snprintf(out, cap, "%s/%s.m3u8", dir, name);
#endif
}

bool playlistio_exists(const char *name) {
    char path[1024];
    pl_path(path, sizeof(path), name);
    FILE *f = pl_fopen(path, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

bool playlistio_save(const Playlist *p, const char *name) {
    char path[1024];
    pl_path(path, sizeof(path), name);
    FILE *f = pl_fopen(path, "w");
    if (!f) return false;
    fputs("#EXTM3U\n", f);
    for (int i = 0; i < p->count; i++) {
        fputs(p->paths[i], f);   // absolute UTF-8 path, works across drives
        fputc('\n', f);
    }
    fclose(f);
    return true;
}

int playlistio_load(Playlist *p, const char *name) {
    char path[1024];
    pl_path(path, sizeof(path), name);
    FILE *f = pl_fopen(path, "r");
    if (!f) return -1;
    playlist_clear(p);
    char line[4096];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        char *s = line;
        // tolerate a leading UTF-8 BOM on the first line of imported files
        if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == 0 || *s == '#') continue;   // blank line or #EXTM3U/#EXTINF comment
        playlist_add(p, s);
        n++;
    }
    fclose(f);
    playlist_set_name(p, name);
    playlist_mark_clean(p);
    return n;
}

int playlistio_list(char out[][PL_NAME_MAX], int max) {
#ifdef _WIN32
    char dir[600];
    playlistio_dir(dir, sizeof(dir));
    char pat[700];
    snprintf(pat, sizeof(pat), "%s\\*.m3u8", dir);
    wchar_t *wpat = u2w(pat);
    if (!wpat) return 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpat, &fd);
    free(wpat);
    if (h == INVALID_HANDLE_VALUE) return 0;

    struct { char name[PL_NAME_MAX]; unsigned long long t; } items[PL_MAX_SAVED];
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char *u = w2u(fd.cFileName);
        if (!u) continue;
        size_t L = strlen(u);
        if (L > 5) u[L - 5] = 0;   // drop the ".m3u8" extension
        if (n < PL_MAX_SAVED) {
            snprintf(items[n].name, PL_NAME_MAX, "%s", u);
            ULARGE_INTEGER ul;
            ul.LowPart  = fd.ftLastWriteTime.dwLowDateTime;
            ul.HighPart = fd.ftLastWriteTime.dwHighDateTime;
            items[n].t = ul.QuadPart;
            n++;
        }
        free(u);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    // newest first (selection sort — n is small)
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (items[j].t > items[i].t) {
                char tn[PL_NAME_MAX]; unsigned long long tt;
                memcpy(tn, items[i].name, PL_NAME_MAX); tt = items[i].t;
                memcpy(items[i].name, items[j].name, PL_NAME_MAX); items[i].t = items[j].t;
                memcpy(items[j].name, tn, PL_NAME_MAX); items[j].t = tt;
            }

    int outn = n < max ? n : max;
    // both are exactly PL_NAME_MAX and items[i].name is NUL-terminated above
    for (int i = 0; i < outn; i++) memcpy(out[i], items[i].name, PL_NAME_MAX);
    return outn;
#else
    (void)out; (void)max;
    return 0;
#endif
}
