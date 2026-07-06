// icon.h — Timp's procedural app icon (pure C, no raylib/SDL deps).
// The mark is the app's own signature fallback artwork in miniature: a
// rounded-square album tile with the plum→rose gradient, the soft glow
// circle, and a dark play mark — the "now playing" screen as an icon.
// Single source of truth for the runtime window icon (rl_main.c) and the
// embedded .ico (tools/makeicon.c); edges are analytically anti-aliased,
// so it renders crisp at any size (16 → 256 px).
#ifndef TIMP_ICON_H
#define TIMP_ICON_H

#include <stdint.h>
#include <math.h>

// palette sampled from the app itself (examples/main.png)
#define ICON_TOP_R   85.0f   // gradient top — deep plum
#define ICON_TOP_G   44.0f
#define ICON_TOP_B   96.0f
#define ICON_BOT_R  188.0f   // gradient bottom — rose
#define ICON_BOT_G   86.0f
#define ICON_BOT_B  126.0f
#define ICON_GLO_R  218.0f   // soft glow circle — warm light rose
#define ICON_GLO_G  142.0f
#define ICON_GLO_B  146.0f
#define ICON_MRK_R   16.0f   // play mark — espresso (the app background)
#define ICON_MRK_G   13.0f
#define ICON_MRK_B   10.0f

static float icon__clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static float icon__smooth(float e0, float e1, float x) {
    float t = icon__clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
static float icon__lerp(float a, float b, float t) { return a + (b - a) * t; }

// signed distance to a rounded rectangle centered at (cx,cy)
static float icon__sd_round_rect(float px, float py, float cx, float cy,
                                 float hx, float hy, float r) {
    float qx = fabsf(px - cx) - (hx - r);
    float qy = fabsf(py - cy) - (hy - r);
    float ax = qx > 0.0f ? qx : 0.0f;
    float ay = qy > 0.0f ? qy : 0.0f;
    float outside = sqrtf(ax * ax + ay * ay);
    float inside  = (qx > qy ? qx : qy);
    if (inside > 0.0f) inside = 0.0f;
    return outside + inside - r;
}

// exact signed distance to a triangle (iq), negative inside
static float icon__sd_triangle(float px, float py,
                               float ax, float ay, float bx, float by,
                               float cx, float cy) {
    float e0x = bx - ax, e0y = by - ay;
    float e1x = cx - bx, e1y = cy - by;
    float e2x = ax - cx, e2y = ay - cy;
    float v0x = px - ax, v0y = py - ay;
    float v1x = px - bx, v1y = py - by;
    float v2x = px - cx, v2y = py - cy;
    float t0 = icon__clampf((v0x * e0x + v0y * e0y) / (e0x * e0x + e0y * e0y), 0.0f, 1.0f);
    float t1 = icon__clampf((v1x * e1x + v1y * e1y) / (e1x * e1x + e1y * e1y), 0.0f, 1.0f);
    float t2 = icon__clampf((v2x * e2x + v2y * e2y) / (e2x * e2x + e2y * e2y), 0.0f, 1.0f);
    float q0x = v0x - e0x * t0, q0y = v0y - e0y * t0;
    float q1x = v1x - e1x * t1, q1y = v1y - e1y * t1;
    float q2x = v2x - e2x * t2, q2y = v2y - e2y * t2;
    float s  = (e0x * e2y - e0y * e2x) < 0.0f ? -1.0f : 1.0f;
    float d  = q0x * q0x + q0y * q0y;
    float sd = s * (v0x * e0y - v0y * e0x);
    float d1 = q1x * q1x + q1y * q1y;
    float s1 = s * (v1x * e1y - v1y * e1x);
    float d2 = q2x * q2x + q2y * q2y;
    float s2 = s * (v2x * e2y - v2y * e2x);
    if (d1 < d) d = d1;
    if (d2 < d) d = d2;
    if (s1 < sd) sd = s1;
    if (s2 < sd) sd = s2;
    return -sqrtf(d) * (sd < 0.0f ? -1.0f : 1.0f);
}

// Render the icon into a tightly-packed size*size*4 RGBA8888 buffer
// (straight alpha; outside the rounded tile is fully transparent).
static void icon_render_rgba(uint8_t *rgba, int size) {
    const float S = (float)size;

    // tile: near-full-bleed rounded square (Win11-style corner radius)
    const float inset  = 0.015f;
    const float half   = 0.5f - inset;
    const float radius = 0.20f;

    // glow circle + play mark share a center a touch above the middle,
    // like the soft circle in the generated cover art
    const float gcx = 0.50f, gcy = 0.46f, gr = 0.345f;

    // play triangle: nudged right of center so it reads optically centered,
    // and grown slightly at taskbar sizes so it stays legible
    const float ms  = (size <= 32) ? 1.12f : 1.0f;
    const float tcx = 0.542f, tcy = 0.46f;
    const float thw = 0.1675f * ms, thh = 0.1825f * ms;
    const float tx0 = tcx - thw, tty = tcy - thh, tby = tcy + thh, ttx = tcx + thw;
    const float tround = 0.034f;   // corner rounding of the mark

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            const float px = ((float)x + 0.5f) / S;
            const float py = ((float)y + 0.5f) / S;

            // tile coverage (1px analytic feather)
            const float dt = icon__sd_round_rect(px, py, 0.5f, 0.5f, half, half, radius);
            const float cover = icon__clampf(0.5f - dt * S, 0.0f, 1.0f);

            uint8_t *out = rgba + ((size_t)y * (size_t)size + (size_t)x) * 4;
            if (cover <= 0.0f) { out[0] = out[1] = out[2] = out[3] = 0; continue; }

            // vertical plum→rose gradient with a whisper of diagonal drift
            const float t = icon__clampf((0.80f * py + 0.20f * px - 0.03f) / 0.94f, 0.0f, 1.0f);
            float r = icon__lerp(ICON_TOP_R, ICON_BOT_R, t);
            float g = icon__lerp(ICON_TOP_G, ICON_BOT_G, t);
            float b = icon__lerp(ICON_TOP_B, ICON_BOT_B, t);

            // soft glow circle — lighter rose, gradient still ghosting through
            const float dc = sqrtf((px - gcx) * (px - gcx) + (py - gcy) * (py - gcy));
            const float glow = (1.0f - icon__smooth(gr - 0.16f, gr + 0.06f, dc)) * 0.62f;
            r = icon__lerp(r, ICON_GLO_R, glow);
            g = icon__lerp(g, ICON_GLO_G, glow);
            b = icon__lerp(b, ICON_GLO_B, glow);

            // dark play mark with rounded corners
            const float dm = icon__sd_triangle(px, py, tx0, tty, tx0, tby, ttx, tcy) - tround;
            const float mark = icon__clampf(0.5f - dm * S, 0.0f, 1.0f);
            r = icon__lerp(r, ICON_MRK_R, mark);
            g = icon__lerp(g, ICON_MRK_G, mark);
            b = icon__lerp(b, ICON_MRK_B, mark);

            out[0] = (uint8_t)(r + 0.5f);
            out[1] = (uint8_t)(g + 0.5f);
            out[2] = (uint8_t)(b + 0.5f);
            out[3] = (uint8_t)(cover * 255.0f + 0.5f);
        }
    }
}

#endif // TIMP_ICON_H
