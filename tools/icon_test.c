// Sanity checks for the procedural icon renderer (album-tile design):
// rounded plum→rose gradient tile, soft glow circle, dark play mark.
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
    icon_render_rgba(buf, size);

    // 1. Rounded tile cuts the corner -> fully transparent.
    CHECK(px(buf, size, 0, 0)[3] == 0, "top-left corner transparent");

    // 2. Top of the tile -> opaque deep plum (blue over green, not gray).
    const uint8_t *top = px(buf, size, size / 2, (int)(0.07 * size));
    CHECK(top[3] > 200, "top area is opaque");
    CHECK(top[2] > top[1] + 25 && top[0] > top[1], "top is plum");

    // 3. Bottom of the tile -> rose (red dominant, warmer than the top).
    const uint8_t *bot = px(buf, size, size / 2, (int)(0.91 * size));
    CHECK(bot[3] > 200 && bot[0] > 145 && bot[0] > bot[2] + 25, "bottom is rose");
    CHECK(bot[0] > top[0] + 40, "gradient darkens toward the top");

    // 4. Glow circle: inside-the-circle point (left of the mark) is clearly
    //    lighter than the same row near the tile edge.
    const int gy = (int)(0.46 * size);
    const uint8_t *in  = px(buf, size, (int)(0.28 * size), gy);
    const uint8_t *out = px(buf, size, (int)(0.10 * size), gy);
    CHECK(in[0] + in[1] + in[2] > out[0] + out[1] + out[2] + 60,
          "glow circle is lighter than the tile around it");

    // 5. Play mark interior -> dark espresso.
    const uint8_t *mk = px(buf, size, (int)(0.47 * size), gy);
    CHECK(mk[3] > 200 && mk[0] < 40 && mk[1] < 40 && mk[2] < 40,
          "play mark is dark");

    // 6. Anti-aliasing: the corner diagonal crosses partially-covered pixels.
    int partial = 0;
    for (int i = 0; i < 13; i++) {
        const uint8_t a = px(buf, size, i, i)[3];
        if (a > 8 && a < 247) partial++;
    }
    CHECK(partial >= 1, "corner arc is anti-aliased");

    free(buf);
    if (fails) { printf("\n%d checks FAILED\n", fails); return 1; }
    printf("\nall checks passed\n");
    return 0;
}
