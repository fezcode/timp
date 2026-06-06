#include "mediakeys.h"

#ifdef _WIN32
#include <windows.h>

static volatile LONG g_action = MK_NONE;

// Hotkey ids deliberately equal the MK_* action codes, so msg.wParam maps directly.
static DWORD WINAPI mk_thread(LPVOID p) {
    (void)p;
    RegisterHotKey(NULL, MK_PLAYPAUSE, 0, VK_MEDIA_PLAY_PAUSE);
    RegisterHotKey(NULL, MK_STOP,      0, VK_MEDIA_STOP);
    RegisterHotKey(NULL, MK_PREV,      0, VK_MEDIA_PREV_TRACK);
    RegisterHotKey(NULL, MK_NEXT,      0, VK_MEDIA_NEXT_TRACK);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY) InterlockedExchange(&g_action, (LONG)msg.wParam);
    }
    return 0;
}

void mediakeys_start(void) {
    HANDLE h = CreateThread(NULL, 0, mk_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
}
int mediakeys_poll(void) { return (int)InterlockedExchange(&g_action, MK_NONE); }

#else
void mediakeys_start(void) {}
int  mediakeys_poll(void) { return 0; }
#endif
