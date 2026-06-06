#include "lyrics.h"
#include "tags.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
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

static char *read_all(const char *path) {
    FILE *f = open_rb(path);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4L * 1024 * 1024) { fclose(f); return NULL; }
    char *b = (char *)malloc(sz + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    b[sz] = 0; fclose(f);
    if ((unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF)
        memmove(b, b + 3, (size_t)sz - 2);  // strip UTF-8 BOM
    return b;
}

static void add_line(Lyrics *l, double t, const char *text) {
    if (l->count >= LYRICS_MAX) return;
    while (*text == ' ') text++;
    LrcLine *ln = &l->lines[l->count++];
    ln->t = t;
    snprintf(ln->text, sizeof(ln->text), "%s", text);
}

static double parse_time(const char *s, const char *end) {
    const char *colon = (const char *)memchr(s, ':', end - s);
    if (!colon) return -1;
    int mm = atoi(s), ss = atoi(colon + 1);
    double frac = 0;
    const char *dot = (const char *)memchr(colon, '.', end - colon);
    if (dot) frac = atof(dot);
    return mm * 60.0 + ss + frac;
}

static void each_line(char *buf, void (*fn)(Lyrics *, char *), Lyrics *l) {
    char *p = buf;
    while (*p) {
        char *nl = strpbrk(p, "\r\n");
        char line[400];
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (len > 399) len = 399;
        memcpy(line, p, len); line[len] = 0;
        fn(l, line);
        if (!nl) break;
        p = nl; while (*p == '\r' || *p == '\n') p++;
    }
}

static void lrc_line(Lyrics *l, char *line) {
    char *q = line; double times[16]; int nt = 0;
    while (*q == '[') {
        char *close = strchr(q, ']'); if (!close) break;
        if (isdigit((unsigned char)q[1])) {
            double t = parse_time(q + 1, close);
            if (t >= 0 && nt < 16) times[nt++] = t;
        }
        q = close + 1;
    }
    if (nt > 0) { for (int i = 0; i < nt; i++) add_line(l, times[i], q); l->synced = true; }
}

static void txt_line(Lyrics *l, char *line) { add_line(l, -1, line); }

static int cmp_line(const void *a, const void *b) {
    double ta = ((const LrcLine *)a)->t, tb = ((const LrcLine *)b)->t;
    return (ta < tb) ? -1 : (ta > tb) ? 1 : 0;
}

static void sibling(const char *path, const char *ext, char *out, int cap) {
    snprintf(out, cap, "%s", path);
    char *dot = strrchr(out, '.');
    char *s1 = strrchr(out, '/'), *s2 = strrchr(out, '\\'), *s = s2 > s1 ? s2 : s1;
    if (dot && (!s || dot > s)) *dot = 0;
    size_t n = strlen(out);
    snprintf(out + n, cap - (int)n, "%s", ext);
}

void lyrics_load(const char *audio_path, Lyrics *out) {
    memset(out, 0, sizeof(*out));
    char path[800];

    sibling(audio_path, ".lrc", path, sizeof(path));
    char *buf = read_all(path);
    if (buf) {
        each_line(buf, lrc_line, out);
        free(buf);
        if (out->synced && out->count > 0) { qsort(out->lines, out->count, sizeof(LrcLine), cmp_line); return; }
        out->count = 0; out->synced = false;
    }

    sibling(audio_path, ".txt", path, sizeof(path));
    buf = read_all(path);
    if (buf) { each_line(buf, txt_line, out); free(buf); if (out->count > 0) return; out->count = 0; }

    static char big[16384];
    if (tags_lyrics(audio_path, big, sizeof(big))) each_line(big, txt_line, out);
}

int lyrics_active(const Lyrics *l, double t) {
    if (!l->synced) return -1;
    int idx = -1;
    for (int i = 0; i < l->count; i++) {
        if (l->lines[i].t <= t + 0.001) idx = i;
        else break;
    }
    return idx;
}

// ---------------- online fetch (lrclib.net) ----------------
#ifdef _WIN32

static CRITICAL_SECTION g_cs;
static int  g_cs_init = 0;
static volatile LONG g_gen = 0;       // newest request id
static LONG g_result_gen = -1;        // request id whose result is stored
static LONG g_consumed_gen = -1;
static char g_result[16384];
static int  g_result_synced;

typedef struct { LONG gen; char query[1280]; } FetchReq;

static void url_encode(const char *s, char *out, int cap) {
    static const char *hex = "0123456789ABCDEF";
    int o = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && o < cap - 4; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c;
        else { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; }
    }
    out[o] = 0;
}

static int http_get(const wchar_t *host, const wchar_t *path, char *out, int cap) {
    int total = 0; out[0] = 0;
    HINTERNET hS = WinHttpOpen(L"Timp/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return 0;
    WinHttpSetTimeouts(hS, 5000, 5000, 5000, 10000);  // resolve/connect/send/receive
    HINTERNET hC = WinHttpConnect(hS, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hC) {
        HINTERNET hR = WinHttpOpenRequest(hC, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (hR) {
            if (WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hR, NULL)) {
                DWORD avail;
                do {
                    avail = 0; WinHttpQueryDataAvailable(hR, &avail);
                    if (avail) {
                        if (total + (int)avail > cap - 1) avail = cap - 1 - total;
                        DWORD rd = 0;
                        if (avail > 0 && WinHttpReadData(hR, out + total, avail, &rd)) total += (int)rd;
                    }
                } while (avail > 0 && total < cap - 1);
            }
            WinHttpCloseHandle(hR);
        }
        WinHttpCloseHandle(hC);
    }
    WinHttpCloseHandle(hS);
    out[total] = 0;
    return total;
}

// Extract a JSON string value for `key` (handles \n \t \" \\ \/ \uXXXX). Returns
// false if the key is absent or its value is null.
static bool json_str(const char *json, const char *key, char *out, int cap) {
    char pat[64]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *k = strstr(json, pat); if (!k) return false;
    const char *p = k + strlen(pat);
    while (*p && *p != ':') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return false;  // null or other
    p++;
    int o = 0;
    while (*p && *p != '"' && o < cap - 4) {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case 'n': out[o++] = '\n'; break;
                case 't': out[o++] = '\t'; break;
                case 'r': break;
                case '/': out[o++] = '/'; break;
                case '\\': out[o++] = '\\'; break;
                case '"': out[o++] = '"'; break;
                case 'u': {
                    int cp = 0;
                    for (int i = 0; i < 4 && p[1]; i++) {
                        p++; char c = *p; cp <<= 4;
                        if (c >= '0' && c <= '9') cp |= c - '0';
                        else if (c >= 'a' && c <= 'f') cp |= c - 'a' + 10;
                        else if (c >= 'A' && c <= 'F') cp |= c - 'A' + 10;
                    }
                    if (cp < 0x80) out[o++] = (char)cp;
                    else if (cp < 0x800) { out[o++] = (char)(0xC0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 0x3f)); }
                    else { out[o++] = (char)(0xE0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3f)); out[o++] = (char)(0x80 | (cp & 0x3f)); }
                } break;
                default: out[o++] = *p; break;
            }
            if (*p) p++;
        } else out[o++] = *p++;
    }
    out[o] = 0;
    return true;
}

static DWORD WINAPI fetch_thread(LPVOID arg) {
    FetchReq *req = (FetchReq *)arg;
    char pathA[2048]; snprintf(pathA, sizeof(pathA), "/api/get?%s", req->query);
    wchar_t pathW[2048];
    MultiByteToWideChar(CP_UTF8, 0, pathA, -1, pathW, 2048);

    char *body = (char *)malloc(1 << 17);  // 128 KB
    if (!body) { free(req); return 0; }
    http_get(L"lrclib.net", pathW, body, 1 << 17);

    char text[16384]; int synced = 0; text[0] = 0;
    if (json_str(body, "syncedLyrics", text, sizeof(text)) && text[0]) synced = 1;
    else if (json_str(body, "plainLyrics", text, sizeof(text))) synced = 0;
    else text[0] = 0;
    free(body);

    EnterCriticalSection(&g_cs);
    if (req->gen == g_gen) {  // still the most recent request
        snprintf(g_result, sizeof(g_result), "%s", text);
        g_result_synced = synced;
        g_result_gen = req->gen;
    }
    LeaveCriticalSection(&g_cs);
    free(req);
    return 0;
}

void lyrics_fetch_start(const char *artist, const char *title, const char *album, int duration_sec) {
    if (!title || !title[0]) return;
    if (!g_cs_init) { InitializeCriticalSection(&g_cs); g_cs_init = 1; }
    LONG gen = InterlockedIncrement(&g_gen);
    FetchReq *req = (FetchReq *)malloc(sizeof(FetchReq));
    if (!req) return;
    req->gen = gen;
    char ea[512], et[512], el[512];
    url_encode(artist ? artist : "", ea, sizeof(ea));
    url_encode(title, et, sizeof(et));
    url_encode(album ? album : "", el, sizeof(el));
    snprintf(req->query, sizeof(req->query),
             "artist_name=%s&track_name=%s&album_name=%s&duration=%d", ea, et, el, duration_sec);
    HANDLE h = CreateThread(NULL, 0, fetch_thread, req, 0, NULL);
    if (h) CloseHandle(h); else free(req);
}

bool lyrics_fetch_poll(Lyrics *out) {
    if (!g_cs_init) return false;
    char local[16384]; int synced = 0; bool fresh = false;
    EnterCriticalSection(&g_cs);
    if (g_result_gen == g_gen && g_result_gen != g_consumed_gen) {
        g_consumed_gen = g_result_gen;
        memcpy(local, g_result, sizeof(local));
        synced = g_result_synced;
        fresh = true;
    }
    LeaveCriticalSection(&g_cs);
    if (!fresh) return false;

    memset(out, 0, sizeof(*out));
    if (local[0]) {
        if (synced) {
            each_line(local, lrc_line, out);
            if (out->synced && out->count > 0) qsort(out->lines, out->count, sizeof(LrcLine), cmp_line);
            else { memset(out, 0, sizeof(*out)); each_line(local, txt_line, out); }
        } else each_line(local, txt_line, out);
    }
    return true;
}

#else
void lyrics_fetch_start(const char *a, const char *t, const char *al, int d) { (void)a;(void)t;(void)al;(void)d; }
bool lyrics_fetch_poll(Lyrics *out) { (void)out; return false; }
#endif
