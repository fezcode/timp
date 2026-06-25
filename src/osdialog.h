#ifndef TIMP_OSDIALOG_H
#define TIMP_OSDIALOG_H

// Opens the native OS "open audio files" dialog (multi-select). Invokes on_file
// once per chosen file with a UTF-8 path. Returns the number of files chosen
// (0 if cancelled). Windows-only for now; a stub elsewhere.
int os_open_audio_files(void (*on_file)(const char *utf8_path, void *ud), void *ud);

// Clip the OS window to a rounded rectangle (corner radius in px). hwnd is the
// raylib GetWindowHandle() pointer. No-op off Windows.
void os_round_window(void *hwnd, int w, int h, int radius);

// Returns the process arguments as UTF-8 strings (out[0] = program path). On
// Windows the real Unicode command line is decoded, because the ANSI argv mangles
// non-ASCII paths (e.g. Turkish "İ"/"ı") and breaks opening those files. Off
// Windows it simply returns argv. The returned array lives for the process
// lifetime — do not free it.
char **os_args_utf8(int argc, char **argv, int *out_count);

// Restore + raise the given OS window to the foreground. hwnd is the raylib
// GetWindowHandle() pointer. No-op off Windows.
void os_focus_window(void *hwnd);

// Open the given folder (UTF-8 path) in the system file manager (Explorer).
// No-op off Windows.
void os_reveal_dir(const char *utf8_path);

#endif

