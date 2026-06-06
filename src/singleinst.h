#ifndef TIMP_SINGLEINST_H
#define TIMP_SINGLEINST_H

#include <stdbool.h>

// Single-instance support (Windows). The first process to launch "wins" and owns
// a named mutex + a hidden message-only window. Any later process forwards its
// song path(s) to the winner and exits, so there is only ever one Timp window.
//
// Off Windows these degrade to: acquire() always true, the rest no-ops — i.e.
// the app behaves exactly as before (every launch is its own instance).

// Returns true if this is the FIRST instance (we now own the app). Returns false
// if another Timp is already running.
bool singleinst_acquire(void);

// (Second instance) Ask the running instance to play this UTF-8 path. Also brings
// the running instance to the foreground. Returns true if it was delivered.
bool singleinst_send_file(const char *utf8_path);

// (Second instance) No song to hand over — just ask the running instance to
// raise its window. Returns true if delivered.
bool singleinst_send_focus(void);

// (First instance) Begin listening for forwarded paths / focus pings.
void singleinst_listen_start(void);

// (First instance, main loop) Dequeue the next forwarded path (UTF-8) into out.
// Returns true and fills out when a path was pending, false when the queue empty.
bool singleinst_poll_file(char *out, int cap);

// (First instance, main loop) Returns true once if a focus request is pending.
bool singleinst_poll_focus(void);

#endif
