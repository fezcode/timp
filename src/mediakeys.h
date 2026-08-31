#ifndef TIMP_MEDIAKEYS_H
#define TIMP_MEDIAKEYS_H

// System media-transport integration.
//
// Timp registers itself as the OS's "now playing" app — Windows SMTC, macOS
// MPNowPlayingInfoCenter — which does two things at once: the current track
// shows up wherever the system displays media (the Windows volume/media flyout,
// the macOS Now Playing widget), and the keyboard's transport keys are routed
// to us *by the system*, only while we're the session it considers current,
// instead of being intercepted globally behind everyone else's back.
//
// The app publishes state through the setters and drains actions with
// mediakeys_poll() once a frame.

// Actions coming back from the OS.
enum { MK_NONE = 0, MK_PLAYPAUSE, MK_STOP, MK_PREV, MK_NEXT, MK_PLAY, MK_PAUSE };

// Playback state we publish to the OS.
enum { MK_STOPPED = 0, MK_PLAYING, MK_PAUSED };

// Call once, after the window exists. `native_window` is the HWND on Windows
// (SMTC sessions are per-window); ignored elsewhere.
void mediakeys_start(void *native_window);
void mediakeys_shutdown(void);

int  mediakeys_poll(void);   // returns one MK_* action and clears it

// The track on air. `audio_path` is the song file, used to find its cover art;
// pass NULL for every argument when nothing is loaded.
void mediakeys_now_playing(const char *title, const char *artist, const char *album,
                           const char *audio_path);
void mediakeys_set_state(int state);                            // MK_STOPPED/PLAYING/PAUSED
void mediakeys_set_timeline(double position, double duration);  // seconds

#endif
