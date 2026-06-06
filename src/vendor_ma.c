// miniaudio implementation for the raylib build. We deliberately do NOT compile
// stb_image here — raylib already provides image loading, and art.c carries its
// own file-local stb_image for decoding embedded cover bytes.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_JACK
#define MA_NO_PULSEAUDIO
#define MA_NO_ALSA
#define MA_NO_AAUDIO
#define MA_NO_OPENSL
#include "../vendor/miniaudio.h"
