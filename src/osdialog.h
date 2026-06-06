#ifndef TIMP_OSDIALOG_H
#define TIMP_OSDIALOG_H

// Opens the native OS "open audio files" dialog (multi-select). Invokes on_file
// once per chosen file with a UTF-8 path. Returns the number of files chosen
// (0 if cancelled). Windows-only for now; a stub elsewhere.
int os_open_audio_files(void (*on_file)(const char *utf8_path, void *ud), void *ud);

// Clip the OS window to a rounded rectangle (corner radius in px). hwnd is the
// raylib GetWindowHandle() pointer. No-op off Windows.
void os_round_window(void *hwnd, int w, int h, int radius);

#endif

