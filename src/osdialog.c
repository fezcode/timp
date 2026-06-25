#include "osdialog.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>

static char *w_to_utf8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *s = (char *)malloc(n);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

int os_open_audio_files(void (*on_file)(const char *, void *), void *ud) {
    static wchar_t buf[16384];
    buf[0] = 0;
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Audio Files\0*.mp3;*.flac;*.wav;*.ogg;*.m4a;*.opus\0All Files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    ofn.lpstrTitle = L"Add music to Timp";
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
                OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return 0;

    int count = 0;
    wchar_t *dir = buf;
    wchar_t *first = buf + wcslen(buf) + 1;
    if (*first == 0) {
        // single selection: buf holds the full path
        char *u = w_to_utf8(buf);
        if (u) { on_file(u, ud); free(u); count++; }
    } else {
        // multi: dir \0 name1 \0 name2 \0 ... \0
        for (wchar_t *f = first; *f; f += wcslen(f) + 1) {
            wchar_t full[4096];
            swprintf(full, 4096, L"%ls\\%ls", dir, f);
            full[4095] = 0;
            char *u = w_to_utf8(full);
            if (u) { on_file(u, ud); free(u); count++; }
        }
    }
    return count;
}

void os_round_window(void *hwnd, int w, int h, int radius) {
    (void)w; (void)h; (void)radius;
    // Win11 DWM rounded corners — composited and anti-aliased (unlike a region
    // clip, which is hard-edged/jagged). 33 = DWMWA_WINDOW_CORNER_PREFERENCE,
    // 2 = DWMWCP_ROUND.
    DWORD pref = 2;
    DwmSetWindowAttribute((HWND)hwnd, 33, &pref, sizeof(pref));
}

char **os_args_utf8(int argc, char **argv, int *out_count) {
    (void)argc; (void)argv;
    int wc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wc);
    if (!wargv) { *out_count = 0; return NULL; }
    // Allocated once for the process lifetime; never freed (mirrors argv's lifetime).
    char **out = (char **)calloc((size_t)wc, sizeof(char *));
    if (!out) { LocalFree(wargv); *out_count = 0; return NULL; }
    for (int i = 0; i < wc; i++) out[i] = w_to_utf8(wargv[i]);
    LocalFree(wargv);
    *out_count = wc;
    return out;
}

void os_focus_window(void *hwnd) {
    HWND h = (HWND)hwnd;
    if (!h) return;
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    SetForegroundWindow(h);
    BringWindowToTop(h);
}

void os_reveal_dir(const char *utf8_path) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, NULL, 0);
    if (n <= 0) return;
    wchar_t *w = (wchar_t *)malloc(sizeof(wchar_t) * n);
    if (!w) return;
    MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, w, n);
    ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
    free(w);
}

#else
int os_open_audio_files(void (*on_file)(const char *, void *), void *ud) {
    (void)on_file; (void)ud;
    return 0;
}
void os_round_window(void *hwnd, int w, int h, int radius) {
    (void)hwnd; (void)w; (void)h; (void)radius;
}
char **os_args_utf8(int argc, char **argv, int *out_count) {
    *out_count = argc;
    return argv;
}
void os_focus_window(void *hwnd) { (void)hwnd; }
void os_reveal_dir(const char *utf8_path) { (void)utf8_path; }
#endif
