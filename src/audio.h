#ifndef TIMP_AUDIO_H
#define TIMP_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#include "eq.h"

#define VIZ_SAMPLES 1024

typedef struct Audio Audio;

Eq* audio_get_eq(Audio* a);

Audio* audio_create(void);
void audio_destroy(Audio* a);

bool audio_load(Audio* a, const char* path);
void audio_unload(Audio* a);   // fully release the decoder (audio_is_loaded → false)
void audio_play(Audio* a);
void audio_pause(Audio* a);
void audio_stop(Audio* a);
bool audio_is_loaded(const Audio* a);
bool audio_is_playing(const Audio* a);
bool audio_finished(const Audio* a);  // true once when track has ended

void audio_set_volume(Audio* a, float v);  // 0..1
float audio_get_volume(const Audio* a);

// An extra output gain multiplied on top of the volume (0..1), used by the
// sleep timer's fade-out. Kept apart from the volume so the slider and the
// persisted setting never see it.
void audio_set_fade(Audio* a, float f);

double audio_position_seconds(const Audio* a);
double audio_length_seconds(const Audio* a);
void audio_seek_seconds(Audio* a, double seconds);

// Snapshot the most recent VIZ_SAMPLES mono samples into out[0..n-1].
// Returns the number of samples copied.
int audio_snapshot_waveform(Audio* a, float* out, int n);

const char* audio_current_path(const Audio* a);

// ---- output device selection --------------------------------------------
// Names are UTF-8 and are what gets persisted (indices shift when hardware is
// plugged or unplugged). Index -1 / a NULL name means the system default.

#define AUDIO_MAX_DEVICES 32

void        audio_refresh_devices(Audio* a);          // re-enumerate; call before listing
int         audio_device_count(const Audio* a);
const char* audio_device_name(const Audio* a, int i);

// Switch playback to the named device, keeping the loaded track and its
// position. NULL or "" selects the system default. Falls back to the default
// when the name is unknown or the device refuses to open; returns false then.
bool        audio_set_device(Audio* a, const char* name);

// Name of the device in use, or "" while on the system default.
const char* audio_current_device(const Audio* a);

#endif
