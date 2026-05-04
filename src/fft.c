#include "fft.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FFT_MAX 2048

static void fft_in_place(float* re, float* im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / (float)len;
        float wre = cosf(ang), wim = sinf(ang);
        int half = len / 2;
        for (int i = 0; i < n; i += len) {
            float cre = 1.0f, cim = 0.0f;
            for (int k = 0; k < half; k++) {
                int a = i + k;
                int b = a + half;
                float ure = cre * re[b] - cim * im[b];
                float uim = cre * im[b] + cim * re[b];
                re[b] = re[a] - ure;
                im[b] = im[a] - uim;
                re[a] += ure;
                im[a] += uim;
                float tmp = cre * wre - cim * wim;
                cim = cre * wim + cim * wre;
                cre = tmp;
            }
        }
    }
}

void fft_magnitudes(const float* time_samples, int n, float* mags) {
    if (n > FFT_MAX) n = FFT_MAX;
    static float re[FFT_MAX];
    static float im[FFT_MAX];
    for (int i = 0; i < n; i++) {
        float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(n - 1)));
        re[i] = time_samples[i] * w;
        im[i] = 0.0f;
    }
    fft_in_place(re, im, n);
    int half = n / 2;
    float scale = 2.0f / (float)n;
    for (int i = 0; i < half; i++) {
        mags[i] = sqrtf(re[i] * re[i] + im[i] * im[i]) * scale;
    }
}

void fft_log_bands(const float* mags_in, int n_in, float* bands_out, int n_out) {
    if (n_in <= 1 || n_out <= 0) {
        memset(bands_out, 0, sizeof(float) * n_out);
        return;
    }
    float min_lo = 1.0f;
    float max_hi = (float)(n_in - 1);
    float lmin = logf(min_lo);
    float lmax = logf(max_hi);
    float max_v = 0.0f;
    for (int b = 0; b < n_out; b++) {
        float t0 = (float)b / (float)n_out;
        float t1 = (float)(b + 1) / (float)n_out;
        int lo = (int)expf(lmin + (lmax - lmin) * t0);
        int hi = (int)expf(lmin + (lmax - lmin) * t1);
        if (hi <= lo) hi = lo + 1;
        if (hi > n_in) hi = n_in;
        float sum = 0.0f;
        int cnt = 0;
        for (int i = lo; i < hi; i++) { sum += mags_in[i]; cnt++; }
        bands_out[b] = cnt ? sum / (float)cnt : 0.0f;
        if (bands_out[b] > max_v) max_v = bands_out[b];
    }
    // soft normalize against a floor so quiet sections don't go to full scale
    float norm = max_v > 0.05f ? max_v : 0.05f;
    for (int b = 0; b < n_out; b++) {
        float v = bands_out[b] / norm;
        // log-y for perceptual feel
        v = (v > 0.0f) ? logf(1.0f + 9.0f * v) / logf(10.0f) : 0.0f;
        if (v > 1.0f) v = 1.0f;
        bands_out[b] = v;
    }
}
