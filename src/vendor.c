#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_JACK
#define MA_NO_PULSEAUDIO
#define MA_NO_ALSA
#define MA_NO_AAUDIO
#define MA_NO_OPENSL
#include "../vendor/miniaudio.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO_WRITE
#include "../vendor/stb_image.h"
