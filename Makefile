UNAME_S := $(shell uname -s 2>/dev/null)

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -std=c11
LDFLAGS ?=
LDLIBS  ?=

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)

ifeq ($(SDL_LIBS),)
    SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
    SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
endif

CFLAGS  += $(SDL_CFLAGS) -Ivendor -Isrc -DICON_WITH_SDL
LDLIBS  += $(SDL_LIBS) -lm

# Platform-specific audio backend libs
ifeq ($(OS),Windows_NT)
    LDLIBS += -lole32 -lwinmm
else ifeq ($(UNAME_S),Linux)
    LDLIBS += -lpthread -ldl
else ifeq ($(UNAME_S),Darwin)
    LDLIBS += -framework CoreAudio -framework AudioToolbox -framework CoreFoundation
endif

BUILD := build
SRCS  := src/main.c src/audio.c src/skin.c src/ui.c src/ini.c src/font.c \
         src/playlist.c src/filebrowser.c src/fft.c src/eq.c \
         src/theme.c src/settings.c src/config.c src/icon.c src/vendor.c
OBJS  := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

ifeq ($(OS),Windows_NT)
    BIN := $(BUILD)/timp.exe
else
    BIN := $(BUILD)/timp
endif

.PHONY: all clean run

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

run: $(BIN)
	./$(BIN)
