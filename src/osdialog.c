#include "osdialog.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>

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
    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius * 2, radius * 2);
    SetWindowRgn((HWND)hwnd, rgn, TRUE);  // window takes ownership of rgn
}

#else
int os_open_audio_files(void (*on_file)(const char *, void *), void *ud) {
    (void)on_file; (void)ud;
    return 0;
}
void os_round_window(void *hwnd, int w, int h, int radius) {
    (void)hwnd; (void)w; (void)h; (void)radius;
}
#endif
