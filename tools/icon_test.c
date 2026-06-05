#include "icon.h"
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", (msg)); fails++; } \
    else         { printf("ok:   %s\n", (msg)); } \
} while (0)

static const uint8_t *px(const uint8_t *buf, int size, int x, int y) {
    return buf + ((size_t)y * size + x) * 4;
}

int main(void) {
    const int size = 64;
    uint8_t *buf = calloc((size_t)size * size, 4);
    // accent green (80,255,130) on slate tile (28,36,46), with tile background.
    icon_render_rgba(buf, size, 80, 255, 130, 28, 36, 46, 1);

    // 1. Rounded tile cuts the corner -> fully transparent.
    CHECK(px(buf, size, 0, 0)[3] == 0, "top-left corner transparent");

    // 2. Above the crossbar but inside the tile -> opaque tile color (low green).
    const uint8_t *top = px(buf, size, size / 2, (int)(0.12 * size));
    CHECK(top[3] > 200 && top[1] < 120, "above-bar area is opaque tile");

    // 3. Crossbar row, centered -> opaque accent green (high green, low red).
    const int by = (int)(0.27 * size);
    const uint8_t *bar = px(buf, size, size / 2, by);
    CHECK(bar[3] > 200 && bar[1] > 200 && bar[0] < 160, "crossbar is accent green");

    // 4. Left of the triangle back-edge, below the bar -> tile, not accent.
    const int my = (int)(0.60 * size);
    const uint8_t *left = px(buf, size, (int)(0.25 * size), my);
    CHECK(left[3] > 200 && left[1] < 120, "left of stem is tile, not accent");

    // 5. Inside the triangle body -> accent green.
    const uint8_t *tri = px(buf, size, (int)(0.50 * size), my);
    CHECK(tri[3] > 200 && tri[1] > 200 && tri[0] < 160, "triangle body is accent green");

    // 6. Transparent-bg mode: tile area is empty, but the mark is still drawn.
    uint8_t *buf2 = calloc((size_t)size * size, 4);
    icon_render_rgba(buf2, size, 80, 255, 130, 28, 36, 46, 0);
    CHECK(px(buf2, size, (int)(0.12 * size), (int)(0.12 * size))[3] == 0,
          "transparent-bg: tile area is transparent");
    const uint8_t *mk = px(buf2, size, size / 2, by);
    CHECK(mk[3] > 200 && mk[1] > 200, "transparent-bg: mark still drawn");

    free(buf);
    free(buf2);
    if (fails) { printf("\n%d checks FAILED\n", fails); return 1; }
    printf("\nall checks passed\n");
    return 0;
}
