#ifndef TIMP_SMTC_H
#define TIMP_SMTC_H

#include <stdbool.h>

// Windows System Media Transport Controls — the OS-level "now playing" session.
// Registering one is what makes Windows treat Timp as a media app: the track
// shows up in the volume/media flyout and on the lock screen, and the keyboard's
// transport keys are routed here by the system (only while we're the active
// session) instead of being grabbed globally.
//
// Every call below is safe from the UI thread: the WinRT objects live on a
// dedicated apartment thread and these just hand it work.

// Creates the session for `hwnd`. Returns false when WinRT/SMTC is unavailable,
// which is the caller's cue to fall back to the raw media-key hook.
bool smtc_start(void *hwnd);
void smtc_stop(void);

// Publishes the current track. `audio_path` is the song file (used to find its
// artwork); pass NULL for everything to clear the session.
void smtc_now_playing(const char *title, const char *artist, const char *album,
                      const char *audio_path);

// MK_STOPPED / MK_PLAYING / MK_PAUSED (see mediakeys.h).
void smtc_set_state(int state);

// Seconds. Drives the scrubber in the flyout; pushed at most a few times a second.
void smtc_set_timeline(double position, double duration);

// One pending MK_* transport action from the OS, cleared as it's read.
int smtc_poll(void);

#endif
