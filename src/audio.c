#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/miniaudio.h"

#ifdef _WIN32
#include <windows.h>
static wchar_t* audio_utf8_to_w(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t* w = (wchar_t*)malloc(sizeof(wchar_t) * n);
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}
#endif

struct Audio {
    ma_device device;
    ma_decoder decoder;
    bool device_inited;
    bool decoder_inited;

    bool playing;
    bool finished_flag;

    float volume;
    char path[520];

    float capture[VIZ_SAMPLES];
    int capture_pos;
    ma_mutex mutex;
    ma_uint64 length_frames;   // cached on load (avoids per-frame decoder access)

    Eq eq;
};

static void data_callback(ma_device* dev, void* output, const void* input, ma_uint32 frames) {
    (void)input;
    Audio* a = (Audio*)dev->pUserData;
    float* out = (float*)output;
    ma_uint32 channels = dev->playback.channels;
    size_t total = (size_t)frames * channels;
    memset(out, 0, total * sizeof(float));

    if (!a->decoder_inited || !a->playing) return;

    ma_uint64 read = 0;
    ma_mutex_lock(&a->mutex);                                   // guard decoder vs. main-thread seek/load
    ma_decoder_read_pcm_frames(&a->decoder, out, frames, &read);
    ma_mutex_unlock(&a->mutex);

    float v = a->volume;
    for (size_t i = 0; i < (size_t)read * channels; i++) out[i] *= v;

    eq_process(&a->eq, out, (int)read, (int)channels);

    ma_mutex_lock(&a->mutex);
    int pos = a->capture_pos;
    for (ma_uint64 i = 0; i < read; i++) {
        float mono = 0.f;
        for (ma_uint32 c = 0; c < channels; c++) mono += out[i * channels + c];
        if (channels) mono /= (float)channels;
        a->capture[pos] = mono;
        pos = (pos + 1) % VIZ_SAMPLES;
    }
    a->capture_pos = pos;
    ma_mutex_unlock(&a->mutex);

    if (read < frames) {
        a->playing = false;
        a->finished_flag = true;
    }
}

Audio* audio_create(void) {
    Audio* a = (Audio*)calloc(1, sizeof(Audio));
    if (!a) return NULL;
    a->volume = 0.7f;
    ma_mutex_init(&a->mutex);

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 48000;
    cfg.dataCallback = data_callback;
    cfg.pUserData = a;

    if (ma_device_init(NULL, &cfg, &a->device) != MA_SUCCESS) {
        fprintf(stderr, "audio: ma_device_init failed\n");
        ma_mutex_uninit(&a->mutex);
        free(a);
        return NULL;
    }
    a->device_inited = true;
    eq_init(&a->eq, (int)a->device.sampleRate);
    if (ma_device_start(&a->device) != MA_SUCCESS) {
        fprintf(stderr, "audio: ma_device_start failed\n");
    }
    return a;
}

Eq* audio_get_eq(Audio* a) { return &a->eq; }

void audio_destroy(Audio* a) {
    if (!a) return;
    if (a->device_inited) ma_device_uninit(&a->device);
    if (a->decoder_inited) ma_decoder_uninit(&a->decoder);
    ma_mutex_uninit(&a->mutex);
    free(a);
}

bool audio_load(Audio* a, const char* path) {
    a->playing = false;            // stop the callback touching the decoder first
    a->finished_flag = false;
    ma_mutex_lock(&a->mutex);
    if (a->decoder_inited) {
        ma_decoder_uninit(&a->decoder);
        a->decoder_inited = false;
    }
    ma_mutex_unlock(&a->mutex);

    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, a->device.playback.channels,
                                                  a->device.sampleRate);
#ifdef _WIN32
    // Path comes in as UTF-8 (from SDL drag-drop, file dialog, argv). The narrow
    // ma_decoder_init_file uses fopen, which on Windows is ANSI — Unicode names fail.
    wchar_t* wpath = audio_utf8_to_w(path);
    ma_result mr = MA_ERROR;
    if (wpath) {
        mr = ma_decoder_init_file_w(wpath, &dc, &a->decoder);
        free(wpath);
    }
    if (mr != MA_SUCCESS) {
        fprintf(stderr, "audio: cannot decode %s\n", path);
        return false;
    }
#else
    if (ma_decoder_init_file(path, &dc, &a->decoder) != MA_SUCCESS) {
        fprintf(stderr, "audio: cannot decode %s\n", path);
        return false;
    }
#endif
    a->decoder_inited = true;
    a->length_frames = 0;          // cache length once (playing==false → callback idle)
    ma_decoder_get_length_in_pcm_frames(&a->decoder, &a->length_frames);
    snprintf(a->path, sizeof(a->path), "%s", path);
    return true;
}

void audio_unload(Audio* a) {
    a->playing = false;
    a->finished_flag = false;
    ma_mutex_lock(&a->mutex);
    if (a->decoder_inited) { ma_decoder_uninit(&a->decoder); a->decoder_inited = false; }
    a->length_frames = 0;
    a->path[0] = 0;
    ma_mutex_unlock(&a->mutex);
}

void audio_play(Audio* a) {
    if (!a->decoder_inited) return;
    if (a->finished_flag) {
        ma_mutex_lock(&a->mutex);
        ma_decoder_seek_to_pcm_frame(&a->decoder, 0);
        ma_mutex_unlock(&a->mutex);
        a->finished_flag = false;
    }
    a->playing = true;
}

void audio_pause(Audio* a) { a->playing = false; }

void audio_stop(Audio* a) {
    a->playing = false;
    if (a->decoder_inited) {
        ma_mutex_lock(&a->mutex);
        ma_decoder_seek_to_pcm_frame(&a->decoder, 0);
        ma_mutex_unlock(&a->mutex);
    }
    a->finished_flag = false;
}

bool audio_is_loaded(const Audio* a) { return a->decoder_inited; }
bool audio_is_playing(const Audio* a) { return a->playing; }

bool audio_finished(const Audio* a) {
    return a->finished_flag;
}

void audio_set_volume(Audio* a, float v) {
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    a->volume = v;
}
float audio_get_volume(const Audio* a) { return a->volume; }

double audio_position_seconds(const Audio* a) {
    if (!a->decoder_inited) return 0.0;
    ma_uint64 cursor = 0;
    ma_mutex_lock((ma_mutex*)&a->mutex);
    ma_decoder_get_cursor_in_pcm_frames((ma_decoder*)&a->decoder, &cursor);
    ma_mutex_unlock((ma_mutex*)&a->mutex);
    return (double)cursor / (double)a->decoder.outputSampleRate;
}

double audio_length_seconds(const Audio* a) {
    if (!a->decoder_inited) return 0.0;
    return (double)a->length_frames / (double)a->decoder.outputSampleRate;
}

void audio_seek_seconds(Audio* a, double seconds) {
    if (!a->decoder_inited) return;
    if (seconds < 0) seconds = 0;
    ma_uint64 frame = (ma_uint64)(seconds * (double)a->decoder.outputSampleRate);
    ma_mutex_lock(&a->mutex);
    ma_decoder_seek_to_pcm_frame(&a->decoder, frame);
    ma_mutex_unlock(&a->mutex);
    a->finished_flag = false;
}

int audio_snapshot_waveform(Audio* a, float* out, int n) {
    if (n > VIZ_SAMPLES) n = VIZ_SAMPLES;
    ma_mutex_lock(&a->mutex);
    int pos = a->capture_pos;
    int start = (pos - n + VIZ_SAMPLES) % VIZ_SAMPLES;
    for (int i = 0; i < n; i++) {
        out[i] = a->capture[(start + i) % VIZ_SAMPLES];
    }
    ma_mutex_unlock(&a->mutex);
    return n;
}

const char* audio_current_path(const Audio* a) {
    return a->decoder_inited ? a->path : NULL;
}
