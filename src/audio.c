#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include "../vendor/miniaudio.h"

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
    SDL_mutex* mutex;
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
    ma_decoder_read_pcm_frames(&a->decoder, out, frames, &read);

    float v = a->volume;
    for (size_t i = 0; i < (size_t)read * channels; i++) out[i] *= v;

    SDL_LockMutex(a->mutex);
    int pos = a->capture_pos;
    for (ma_uint64 i = 0; i < read; i++) {
        float mono = 0.f;
        for (ma_uint32 c = 0; c < channels; c++) mono += out[i * channels + c];
        if (channels) mono /= (float)channels;
        a->capture[pos] = mono;
        pos = (pos + 1) % VIZ_SAMPLES;
    }
    a->capture_pos = pos;
    SDL_UnlockMutex(a->mutex);

    if (read < frames) {
        a->playing = false;
        a->finished_flag = true;
    }
}

Audio* audio_create(void) {
    Audio* a = (Audio*)calloc(1, sizeof(Audio));
    if (!a) return NULL;
    a->volume = 0.7f;
    a->mutex = SDL_CreateMutex();

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 48000;
    cfg.dataCallback = data_callback;
    cfg.pUserData = a;

    if (ma_device_init(NULL, &cfg, &a->device) != MA_SUCCESS) {
        fprintf(stderr, "audio: ma_device_init failed\n");
        SDL_DestroyMutex(a->mutex);
        free(a);
        return NULL;
    }
    a->device_inited = true;
    if (ma_device_start(&a->device) != MA_SUCCESS) {
        fprintf(stderr, "audio: ma_device_start failed\n");
    }
    return a;
}

void audio_destroy(Audio* a) {
    if (!a) return;
    if (a->device_inited) ma_device_uninit(&a->device);
    if (a->decoder_inited) ma_decoder_uninit(&a->decoder);
    if (a->mutex) SDL_DestroyMutex(a->mutex);
    free(a);
}

bool audio_load(Audio* a, const char* path) {
    if (a->decoder_inited) {
        ma_decoder_uninit(&a->decoder);
        a->decoder_inited = false;
    }
    a->playing = false;
    a->finished_flag = false;

    ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, a->device.playback.channels,
                                                  a->device.sampleRate);
    if (ma_decoder_init_file(path, &dc, &a->decoder) != MA_SUCCESS) {
        fprintf(stderr, "audio: cannot decode %s\n", path);
        return false;
    }
    a->decoder_inited = true;
    snprintf(a->path, sizeof(a->path), "%s", path);
    return true;
}

void audio_play(Audio* a) {
    if (!a->decoder_inited) return;
    if (a->finished_flag) {
        ma_decoder_seek_to_pcm_frame(&a->decoder, 0);
        a->finished_flag = false;
    }
    a->playing = true;
}

void audio_pause(Audio* a) { a->playing = false; }

void audio_stop(Audio* a) {
    a->playing = false;
    if (a->decoder_inited) ma_decoder_seek_to_pcm_frame(&a->decoder, 0);
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
    ma_decoder_get_cursor_in_pcm_frames((ma_decoder*)&a->decoder, &cursor);
    return (double)cursor / (double)a->decoder.outputSampleRate;
}

double audio_length_seconds(const Audio* a) {
    if (!a->decoder_inited) return 0.0;
    ma_uint64 total = 0;
    if (ma_decoder_get_length_in_pcm_frames((ma_decoder*)&a->decoder, &total) != MA_SUCCESS) return 0.0;
    return (double)total / (double)a->decoder.outputSampleRate;
}

void audio_seek_seconds(Audio* a, double seconds) {
    if (!a->decoder_inited) return;
    if (seconds < 0) seconds = 0;
    ma_uint64 frame = (ma_uint64)(seconds * (double)a->decoder.outputSampleRate);
    ma_decoder_seek_to_pcm_frame(&a->decoder, frame);
    a->finished_flag = false;
}

int audio_snapshot_waveform(Audio* a, float* out, int n) {
    if (n > VIZ_SAMPLES) n = VIZ_SAMPLES;
    SDL_LockMutex(a->mutex);
    int pos = a->capture_pos;
    int start = (pos - n + VIZ_SAMPLES) % VIZ_SAMPLES;
    for (int i = 0; i < n; i++) {
        out[i] = a->capture[(start + i) % VIZ_SAMPLES];
    }
    SDL_UnlockMutex(a->mutex);
    return n;
}

const char* audio_current_path(const Audio* a) {
    return a->decoder_inited ? a->path : NULL;
}
