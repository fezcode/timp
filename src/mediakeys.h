#ifndef TIMP_MEDIAKEYS_H
#define TIMP_MEDIAKEYS_H

// System-wide media-key support (Windows). Runs a small background thread that
// registers the transport hotkeys; the main loop polls for pending actions.
enum { MK_NONE = 0, MK_PLAYPAUSE, MK_STOP, MK_PREV, MK_NEXT };

void mediakeys_start(void);
int  mediakeys_poll(void);   // returns one MK_* action and clears it

#endif
