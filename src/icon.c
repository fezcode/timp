#include "icon.h"
#include <math.h>

// Supersampling factor per axis for anti-aliasing.
#define ICON_SS 4

// Geometry in normalized [0,1] coordinates over the icon square.
static const double TILE_MARGIN = 0.06;
static const double TILE_RADIUS = 0.18;
static const double BAR_X0 = 0.20, BAR_X1 = 0.80;   // crossbar (top of the T)
static const double BAR_Y0 = 0.20, BAR_Y1 = 0.34;
static const double TRI_X0 = 0.40, TRI_X1 = 0.70;   // play triangle (the stem)
static const double TRI_YTOP = 0.34, TRI_YBOT = 0.82;

static int inside_rounded_tile(double px, double py) {
    const double x_min = TILE_MARGIN, x_max = 1.0 - TILE_MARGIN;
    const double y_min = TILE_MARGIN, y_max = 1.0 - TILE_MARGIN;
    const double cx = 0.5 * (x_min + x_max), cy = 0.5 * (y_min + y_max);
    const double halfw = 0.5 * (x_max - x_min), halfh = 0.5 * (y_max - y_min);
    double qx = fabs(px - cx) - (halfw - TILE_RADIUS);
    double qy = fabs(py - cy) - (halfh - TILE_RADIUS);
    if (qx < 0) qx = 0;
    if (qy < 0) qy = 0;
    return (sqrt(qx * qx + qy * qy) - TILE_RADIUS) <= 0.0;
}

static int inside_mark(double px, double py) {
    // Crossbar.
    if (px >= BAR_X0 && px <= BAR_X1 && py >= BAR_Y0 && py <= BAR_Y1)
        return 1;
    // Right-pointing play triangle: back edge vertical at TRI_X0, apex at TRI_X1.
    if (px >= TRI_X0 && px <= TRI_X1) {
        const double ymid = 0.5 * (TRI_YTOP + TRI_YBOT);
        const double t = (px - TRI_X0) / (TRI_X1 - TRI_X0); // 0 at back, 1 at apex
        const double top = TRI_YTOP + t * (ymid - TRI_YTOP);
        const double bot = TRI_YBOT + t * (ymid - TRI_YBOT);
        if (py >= top && py <= bot)
            return 1;
    }
    return 0;
}

void icon_render_rgba(uint8_t *rgba, int size,
                      uint8_t ar, uint8_t ag, uint8_t ab,
                      uint8_t tr, uint8_t tg, uint8_t tb,
                      int tile_bg) {
    const int ss = ICON_SS;
    const double inv = 1.0 / (double)size;
    const double subinv = 1.0 / (double)ss;
    const double nsamples = (double)(ss * ss);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int tile_hits = 0, mark_hits = 0;
            for (int sy = 0; sy < ss; sy++) {
                for (int sx = 0; sx < ss; sx++) {
                    const double px = (x + (sx + 0.5) * subinv) * inv;
                    const double py = (y + (sy + 0.5) * subinv) * inv;
                    if (tile_bg && inside_rounded_tile(px, py)) tile_hits++;
                    if (inside_mark(px, py)) mark_hits++;
                }
            }
            const double tile_a = tile_bg ? (tile_hits / nsamples) : 0.0;
            const double mark_a = mark_hits / nsamples;

            // Composite accent mark OVER tile (straight alpha).
            const double out_a = mark_a + tile_a * (1.0 - mark_a);
            double r = 0, g = 0, b = 0;
            if (out_a > 0.0) {
                const double tw = tile_a * (1.0 - mark_a);
                r = (ar * mark_a + tr * tw) / out_a;
                g = (ag * mark_a + tg * tw) / out_a;
                b = (ab * mark_a + tb * tw) / out_a;
            }

            uint8_t *p = rgba + ((size_t)y * size + x) * 4;
            p[0] = (uint8_t)(r + 0.5);
            p[1] = (uint8_t)(g + 0.5);
            p[2] = (uint8_t)(b + 0.5);
            p[3] = (uint8_t)(out_a * 255.0 + 0.5);
        }
    }
}
