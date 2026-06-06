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

double audio_position_seconds(const Audio* a);
double audio_length_seconds(const Audio* a);
void audio_seek_seconds(Audio* a, double seconds);

// Snapshot the most recent VIZ_SAMPLES mono samples into out[0..n-1].
// Returns the number of samples copied.
int audio_snapshot_waveform(Audio* a, float* out, int n);

const char* audio_current_path(const Audio* a);

#endif
