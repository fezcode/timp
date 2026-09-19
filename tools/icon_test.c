// Renderer contract checks across title-bar, taskbar and macOS bundle sizes.
#include "icon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const int sizes[] = {16, 24, 32, 48, 64, 128, 256, 512, 1024};
    for (size_t n = 0; n < sizeof(sizes) / sizeof(sizes[0]); n++) {
        int size = sizes[n], partial = 0;
        size_t bytes = (size_t)size * size * 4;
        uint8_t *buf = malloc(bytes + 16);
        if (!buf) return 1;
        memset(buf, 0xa5, bytes + 16);
        icon_render_rgba(buf + 8, size);
        for (int i = 0; i < 8; i++)
            if (buf[i] != 0xa5 || buf[bytes + 8 + i] != 0xa5) return 1;
        const uint8_t *rgba = buf + 8;
        if (rgba[3] != 0 || rgba[(size_t)(size * (size / 2) + size / 2) * 4 + 3] != 255) return 1;
        for (size_t i = 0; i < bytes; i += 4) {
            if (rgba[i+3] > 0 && rgba[i+3] < 255) partial++;
            if (rgba[i+3] == 0 && (rgba[i] || rgba[i+1] || rgba[i+2])) return 1;
        }
        if (!partial) return 1;
        free(buf);
        printf("ok: %dpx bounds, transparency, opacity and edge coverage\n", size);
    }
    return 0;
}
