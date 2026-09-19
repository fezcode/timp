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

typedef struct {
    char name[256];
    ma_device_id id;
} AudioDeviceEntry;

struct Audio {
    ma_context context;
    ma_device device;
    ma_decoder decoder;
    bool context_inited;
    bool device_inited;
    bool decoder_inited;

    AudioDeviceEntry devices[AUDIO_MAX_DEVICES];
    int  device_count;
    char device_name[256];   // "" = system default

    bool playing;
    bool finished_flag;

    float volume;
    float fade;            // sleep-timer fade-out gain, multiplied onto volume
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

    float v = a->volume * a->fade;
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

// Open (or reopen) the playback device. `id` NULL means the system default.
// The EQ is re-initialised at the new sample rate with its gains carried over,
// since eq_init() clears the struct.
static bool audio_open_device(Audio* a, const ma_device_id* id) {
    float gains[EQ_BANDS];
    bool  eq_on = false;
    if (a->device_inited) {                      // carry the EQ across a switch
        eq_on = eq_is_enabled(&a->eq);
        for (int i = 0; i < EQ_BANDS; i++) gains[i] = eq_get_gain(&a->eq, i);
    } else {
        for (int i = 0; i < EQ_BANDS; i++) gains[i] = 0.f;
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.pDeviceID = (ma_device_id*)id;
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 48000;
    cfg.dataCallback = data_callback;
    cfg.pUserData = a;

    if (ma_device_init(a->context_inited ? &a->context : NULL, &cfg, &a->device) != MA_SUCCESS) {
        a->device_inited = false;
        return false;
    }
    a->device_inited = true;

    eq_init(&a->eq, (int)a->device.sampleRate);
    for (int i = 0; i < EQ_BANDS; i++) eq_set_gain(&a->eq, i, gains[i]);
    eq_set_enabled(&a->eq, eq_on);

    if (ma_device_start(&a->device) != MA_SUCCESS) {
        fprintf(stderr, "audio: ma_device_start failed\n");
        return false;
    }
    return true;
}

Audio* audio_create(void) {
    Audio* a = (Audio*)calloc(1, sizeof(Audio));
    if (!a) return NULL;
    a->volume = 0.7f;
    a->fade = 1.0f;
    ma_mutex_init(&a->mutex);

    // An explicit context is what makes the devices enumerable: ma_device_init
    // would otherwise spin up a private one we cannot ask for a device list.
    if (ma_context_init(NULL, 0, NULL, &a->context) == MA_SUCCESS) {
        a->context_inited = true;
        audio_refresh_devices(a);
    }

    if (!audio_open_device(a, NULL)) {
        fprintf(stderr, "audio: ma_device_init failed\n");
        if (a->context_inited) ma_context_uninit(&a->context);
        ma_mutex_uninit(&a->mutex);
        free(a);
        return NULL;
    }
    return a;
}

Eq* audio_get_eq(Audio* a) { return &a->eq; }

void audio_destroy(Audio* a) {
    if (!a) return;
    if (a->device_inited) ma_device_uninit(&a->device);
    if (a->decoder_inited) ma_decoder_uninit(&a->decoder);
    if (a->context_inited) ma_context_uninit(&a->context);
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

void audio_set_fade(Audio* a, float f) {
    if (f < 0.f) f = 0.f;
    if (f > 1.f) f = 1.f;
    a->fade = f;
}

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

// ---- output device selection --------------------------------------------

void audio_refresh_devices(Audio* a) {
    a->device_count = 0;
    if (!a->context_inited) return;

    ma_device_info* infos = NULL;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&a->context, &infos, &count, NULL, NULL) != MA_SUCCESS) return;

    // Copy out: miniaudio owns `infos` and recycles it on the next enumeration.
    for (ma_uint32 i = 0; i < count && a->device_count < AUDIO_MAX_DEVICES; i++) {
        AudioDeviceEntry* e = &a->devices[a->device_count++];
        snprintf(e->name, sizeof(e->name), "%s", infos[i].name);
        e->id = infos[i].id;
    }
}

int audio_device_count(const Audio* a) { return a->device_count; }

const char* audio_device_name(const Audio* a, int i) {
    if (i < 0 || i >= a->device_count) return NULL;
    return a->devices[i].name;
}

const char* audio_current_device(const Audio* a) { return a->device_name; }

bool audio_set_device(Audio* a, const char* name) {
    bool want_default = (name == NULL || name[0] == 0);

    int idx = -1;
    if (!want_default) {
        for (int i = 0; i < a->device_count; i++) {
            if (!strcmp(a->devices[i].name, name)) { idx = i; break; }
        }
        if (idx < 0) {                       // re-enumerate once: it may be newly plugged in
            audio_refresh_devices(a);
            for (int i = 0; i < a->device_count; i++) {
                if (!strcmp(a->devices[i].name, name)) { idx = i; break; }
            }
        }
    }

    const char* target = (idx >= 0) ? a->devices[idx].name : "";
    if (!strcmp(target, a->device_name) && a->device_inited) return idx >= 0 || want_default;

    // Remember what was playing: the decoder is tied to the device's channel
    // count and sample rate, so a switch may have to rebuild it.
    bool   was_playing = a->playing;
    double pos = audio_position_seconds(a);
    char   path[520];
    snprintf(path, sizeof(path), "%s", a->path);
    bool   had_track = a->decoder_inited;
    ma_uint32 old_channels = a->device_inited ? a->device.playback.channels : 0;
    ma_uint32 old_rate     = a->device_inited ? a->device.sampleRate : 0;

    a->playing = false;                      // park the callback before it loses its device
    if (a->device_inited) {
        ma_device_uninit(&a->device);        // joins the audio thread
        a->device_inited = false;
    }

    const ma_device_id* id = (idx >= 0) ? &a->devices[idx].id : NULL;
    bool ok = audio_open_device(a, id);
    if (!ok && idx >= 0) {                   // refused: don't leave the app mute
        fprintf(stderr, "audio: cannot open '%s', falling back to the default device\n", name);
        if (a->device_inited) { ma_device_uninit(&a->device); a->device_inited = false; }
        ok = audio_open_device(a, NULL);
        idx = -1;
    }
    snprintf(a->device_name, sizeof(a->device_name), "%s", (idx >= 0) ? a->devices[idx].name : "");

    if (had_track && a->device_inited &&
        (a->device.playback.channels != old_channels || a->device.sampleRate != old_rate)) {
        audio_load(a, path);                 // rebuild the decoder at the new format
    }
    if (had_track && a->decoder_inited) {
        audio_seek_seconds(a, pos);
        if (was_playing) audio_play(a);
    }
    return ok && (idx >= 0 || want_default);
}
