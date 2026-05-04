#ifndef WH_FFT_H
#define WH_FFT_H

// Real-input FFT magnitudes via radix-2 Cooley-Tukey.
// `n` must be a power of two, <= 2048. Applies a Hann window.
// Writes n/2 magnitude bins into `mags`.
void fft_magnitudes(const float* time_samples, int n, float* mags);

// Bucket `n_in` magnitude bins into `n_out` log-spaced bands (out is normalized 0..1).
void fft_log_bands(const float* mags_in, int n_in, float* bands_out, int n_out);

#endif
