#!/bin/sh
# Build Timp on macOS — produces build/timp.
# Mirrors build.ps1: raylib via pkg-config (Homebrew), vendored miniaudio +
# stb_image in vendor/ (fetched by setup.ps1 / curl). macOS feature parity
# lives in the *_mac.m backends (NSOpenPanel dialogs, MPRemoteCommandCenter
# media keys, window rounding); single-instance and online lyrics use the
# POSIX branches (Unix socket, libcurl).
set -e
cd "$(dirname "$0")"

if [ ! -f vendor/miniaudio.h ] || [ ! -f vendor/stb_image.h ]; then
    echo 'fetching vendored deps'
    mkdir -p vendor
    curl -fsSL -o vendor/miniaudio.h https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h
    curl -fsSL -o vendor/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
fi

CFLAGS="-O2 -Wall -Wextra -Wno-unused-parameter -std=c11 -Isrc -Ivendor $(pkg-config --cflags raylib)"
LIBS="$(pkg-config --libs raylib) -lcurl -lm -lpthread \
      -framework Cocoa -framework MediaPlayer -framework UniformTypeIdentifiers"

mkdir -p build
SRCS="rl_main audio art osdialog tags lyrics rlconfig mediakeys singleinst fft eq playlist playlistio vendor_ma menubar"
OBJC_SRCS="osdialog_mac mediakeys_mac"

OBJS=""
for s in $SRCS; do
    echo "compiling $s"
    cc $CFLAGS -c "src/$s.c" -o "build/$s.o"
    OBJS="$OBJS build/$s.o"
done
for s in $OBJC_SRCS; do
    echo "compiling $s (objc)"
    cc $CFLAGS -fobjc-arc -c "src/$s.m" -o "build/$s.o"
    OBJS="$OBJS build/$s.o"
done

echo 'linking build/timp'
cc $OBJS -o build/timp $LIBS
echo "built $(pwd)/build/timp"
