#include "mediakeys.h"

#ifdef _WIN32
#include <windows.h>
#include <stdbool.h>
#include "smtc.h"

static volatile LONG g_action = MK_NONE;
static HHOOK g_hook;
static bool  g_smtc;   // true → the OS delivers transport keys; no hook needed

// ---- fallback path (pre-WinRT Windows, or SMTC refused to start) ----
// A low-level keyboard hook catches the transport keys system-wide regardless of
// focus. It is deliberately the second choice: it fires whether or not Timp is
// the media app the user meant, so with SMTC available we don't install it at
// all and let the system arbitrate. We never consume the key (always
// CallNextHookEx), so the OS keeps its normal routing.
static LRESULT CALLBACK ll_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN)) {
        const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lparam;
        LONG act = MK_NONE;
        switch (k->vkCode) {
            case VK_MEDIA_PLAY_PAUSE: act = MK_PLAYPAUSE; break;
            case VK_MEDIA_STOP:       act = MK_STOP;      break;
            case VK_MEDIA_PREV_TRACK: act = MK_PREV;      break;
            case VK_MEDIA_NEXT_TRACK: act = MK_NEXT;      break;
            default: break;
        }
        if (act != MK_NONE) InterlockedExchange(&g_action, act);
    }
    return CallNextHookEx(g_hook, code, wparam, lparam);
}

// A WH_KEYBOARD_LL hook must live on a thread that pumps messages, so it runs on
// its own thread with a GetMessage loop.
static DWORD WINAPI mk_thread(LPVOID p) {
    (void)p;
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, ll_proc, GetModuleHandleW(NULL), 0);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) { /* pump so the hook keeps firing */ }
    if (g_hook) UnhookWindowsHookEx(g_hook);
    return 0;
}

void mediakeys_start(void *native_window) {
    g_smtc = smtc_start(native_window);
    if (g_smtc) return;
    HANDLE h = CreateThread(NULL, 0, mk_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
}
void mediakeys_shutdown(void) { if (g_smtc) smtc_stop(); }

int mediakeys_poll(void) {
    if (g_smtc) return smtc_poll();
    return (int)InterlockedExchange(&g_action, MK_NONE);
}

void mediakeys_now_playing(const char *title, const char *artist, const char *album,
                           const char *audio_path) {
    if (g_smtc) smtc_now_playing(title, artist, album, audio_path);
}
void mediakeys_set_state(int state)                          { if (g_smtc) smtc_set_state(state); }
void mediakeys_set_timeline(double position, double duration) { if (g_smtc) smtc_set_timeline(position, duration); }

#elif !defined(__APPLE__)   /* macOS lives in mediakeys_mac.m */
void mediakeys_start(void *native_window) { (void)native_window; }
void mediakeys_shutdown(void) {}
int  mediakeys_poll(void) { return 0; }
void mediakeys_now_playing(const char *title, const char *artist, const char *album,
                           const char *audio_path) {
    (void)title; (void)artist; (void)album; (void)audio_path;
}
void mediakeys_set_state(int state) { (void)state; }
void mediakeys_set_timeline(double position, double duration) { (void)position; (void)duration; }
#endif
