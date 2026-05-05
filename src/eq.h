#ifndef TIMP_EQ_H
#define TIMP_EQ_H

#include <stdbool.h>

#define EQ_BANDS 10
#define EQ_MAX_CHANNELS 2

typedef struct {
    float frequency;
    float gain_db;
    float q;

    // RBJ peaking biquad coefficients (a0 normalized to 1)
    float b0, b1, b2, a1, a2;

    // state per channel
    float x1[EQ_MAX_CHANNELS], x2[EQ_MAX_CHANNELS];
    float y1[EQ_MAX_CHANNELS], y2[EQ_MAX_CHANNELS];
} EqBand;

typedef struct {
    bool enabled;
    int sample_rate;
    EqBand bands[EQ_BANDS];
} Eq;

void eq_init(Eq* eq, int sample_rate);

void eq_set_enabled(Eq* eq, bool on);
bool eq_is_enabled(const Eq* eq);

void eq_set_gain(Eq* eq, int band, float gain_db);  // -12..+12
float eq_get_gain(const Eq* eq, int band);
float eq_get_frequency(const Eq* eq, int band);

void eq_flat(Eq* eq);
void eq_reset_state(Eq* eq);

// Process interleaved float samples in-place.
void eq_process(Eq* eq, float* samples, int num_frames, int num_channels);

#endif
