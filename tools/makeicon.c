// Standalone generator for assets/timp.ico. Reuses the SDL-free icon renderer.
// Writes a 4-frame ICO (16/32/48/256), each frame a 32-bit BGRA DIB.
#include "icon.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static void w16(FILE *f, uint16_t v) {
    fputc(v & 0xff, f);
    fputc((v >> 8) & 0xff, f);
}
static void w32(FILE *f, uint32_t v) {
    fputc(v & 0xff, f);        fputc((v >> 8) & 0xff, f);
    fputc((v >> 16) & 0xff, f); fputc((v >> 24) & 0xff, f);
}

// Bytes for one frame: BITMAPINFOHEADER + XOR(BGRA) + AND(1bpp, 4-byte padded).
static uint32_t frame_bytes(int size) {
    const uint32_t and_stride = (uint32_t)(((size + 31) / 32) * 4);
    return 40u + (uint32_t)size * (uint32_t)size * 4u
              + and_stride * (uint32_t)size;
}

static void write_frame(FILE *f, int size) {
    uint8_t *rgba = (uint8_t *)malloc((size_t)size * size * 4);
    // Default skin colors: accent green (80,255,130) on slate (28,36,46).
    icon_render_rgba(rgba, size, 80, 255, 130, 28, 36, 46, 1);

    // BITMAPINFOHEADER (height doubled for XOR + AND masks).
    w32(f, 40);
    w32(f, (uint32_t)size);
    w32(f, (uint32_t)(size * 2));
    w16(f, 1);
    w16(f, 32);
    w32(f, 0);                 // BI_RGB
    w32(f, 0);                 // biSizeImage (0 ok for BI_RGB)
    w32(f, 0); w32(f, 0);      // pixels-per-meter x/y
    w32(f, 0); w32(f, 0);      // clrUsed / clrImportant

    // XOR bitmap: bottom-up rows, BGRA per pixel.
    for (int y = size - 1; y >= 0; y--) {
        for (int x = 0; x < size; x++) {
            const uint8_t *p = rgba + ((size_t)y * size + x) * 4;
            fputc(p[2], f); // B
            fputc(p[1], f); // G
            fputc(p[0], f); // R
            fputc(p[3], f); // A
        }
    }

    // AND mask: all zero (alpha lives in the BGRA data), padded per row.
    const uint32_t and_stride = (uint32_t)(((size + 31) / 32) * 4);
    for (int y = 0; y < size; y++)
        for (uint32_t b = 0; b < and_stride; b++)
            fputc(0, f);

    free(rgba);
}

int main(int argc, char **argv) {
    const char *out = (argc > 1) ? argv[1] : "assets/timp.ico";
    const int sizes[] = { 16, 32, 48, 256 };
    const int n = (int)(sizeof(sizes) / sizeof(sizes[0]));

    FILE *f = fopen(out, "wb");
    if (!f) { fprintf(stderr, "cannot open %s\n", out); return 1; }

    // ICONDIR
    w16(f, 0);            // reserved
    w16(f, 1);            // type = 1 (icon)
    w16(f, (uint16_t)n);

    // ICONDIRENTRY for each frame.
    uint32_t offset = 6u + 16u * (uint32_t)n;
    for (int i = 0; i < n; i++) {
        const int s = sizes[i];
        const uint32_t bytes = frame_bytes(s);
        fputc(s >= 256 ? 0 : s, f);   // width  (0 means 256)
        fputc(s >= 256 ? 0 : s, f);   // height (0 means 256)
        fputc(0, f);                  // color count
        fputc(0, f);                  // reserved
        w16(f, 1);                    // planes
        w16(f, 32);                   // bit count
        w32(f, bytes);                // bytes in resource
        w32(f, offset);               // image offset
        offset += bytes;
    }

    // Frame payloads, same order.
    for (int i = 0; i < n; i++)
        write_frame(f, sizes[i]);

    fclose(f);
    printf("wrote %s (%d frames)\n", out, n);
    return 0;
}
