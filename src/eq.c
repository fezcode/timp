#include "eq.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const float DEFAULT_FREQS[EQ_BANDS] = {
    60.f, 170.f, 310.f, 600.f, 1000.f,
    3000.f, 6000.f, 12000.f, 14000.f, 16000.f
};

static void recompute_band(EqBand* band, int sample_rate) {
    float A = powf(10.0f, band->gain_db / 40.0f);
    float omega = 2.0f * (float)M_PI * band->frequency / (float)sample_rate;
    float sn = sinf(omega), cs = cosf(omega);
    float alpha = sn / (2.0f * band->q);

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cs;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cs;
    float a2 = 1.0f - alpha / A;

    band->b0 = b0 / a0;
    band->b1 = b1 / a0;
    band->b2 = b2 / a0;
    band->a1 = a1 / a0;
    band->a2 = a2 / a0;
}

void eq_init(Eq* eq, int sample_rate) {
    memset(eq, 0, sizeof(*eq));
    eq->sample_rate = sample_rate;
    eq->enabled = false;
    for (int i = 0; i < EQ_BANDS; i++) {
        eq->bands[i].frequency = DEFAULT_FREQS[i];
        eq->bands[i].q = 1.41f;
        eq->bands[i].gain_db = 0.0f;
        recompute_band(&eq->bands[i], sample_rate);
    }
}

void eq_set_enabled(Eq* eq, bool on) {
    if (on && !eq->enabled) eq_reset_state(eq);
    eq->enabled = on;
}

bool eq_is_enabled(const Eq* eq) { return eq->enabled; }

void eq_set_gain(Eq* eq, int band, float gain_db) {
    if (band < 0 || band >= EQ_BANDS) return;
    if (gain_db < -12.f) gain_db = -12.f;
    if (gain_db >  12.f) gain_db =  12.f;
    eq->bands[band].gain_db = gain_db;
    recompute_band(&eq->bands[band], eq->sample_rate);
}

float eq_get_gain(const Eq* eq, int band) {
    if (band < 0 || band >= EQ_BANDS) return 0.f;
    return eq->bands[band].gain_db;
}

float eq_get_frequency(const Eq* eq, int band) {
    if (band < 0 || band >= EQ_BANDS) return 0.f;
    return eq->bands[band].frequency;
}

void eq_flat(Eq* eq) {
    for (int i = 0; i < EQ_BANDS; i++) {
        eq->bands[i].gain_db = 0.0f;
        recompute_band(&eq->bands[i], eq->sample_rate);
    }
    eq_reset_state(eq);
}

void eq_reset_state(Eq* eq) {
    for (int i = 0; i < EQ_BANDS; i++) {
        for (int c = 0; c < EQ_MAX_CHANNELS; c++) {
            eq->bands[i].x1[c] = eq->bands[i].x2[c] = 0.f;
            eq->bands[i].y1[c] = eq->bands[i].y2[c] = 0.f;
        }
    }
}

void eq_process(Eq* eq, float* samples, int num_frames, int num_channels) {
    if (!eq->enabled) return;
    if (num_channels > EQ_MAX_CHANNELS) num_channels = EQ_MAX_CHANNELS;
    for (int c = 0; c < num_channels; c++) {
        for (int b = 0; b < EQ_BANDS; b++) {
            EqBand* band = &eq->bands[b];
            if (band->gain_db == 0.0f) continue;  // pass-through optimization
            float b0 = band->b0, b1 = band->b1, b2 = band->b2;
            float a1 = band->a1, a2 = band->a2;
            float x1 = band->x1[c], x2 = band->x2[c];
            float y1 = band->y1[c], y2 = band->y2[c];
            for (int n = 0; n < num_frames; n++) {
                float x = samples[n * num_channels + c];
                float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                samples[n * num_channels + c] = y;
                x2 = x1; x1 = x;
                y2 = y1; y1 = y;
            }
            band->x1[c] = x1; band->x2[c] = x2;
            band->y1[c] = y1; band->y2[c] = y2;
        }
    }
}
