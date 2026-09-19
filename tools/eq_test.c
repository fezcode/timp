// Test for src/eq.c — the preset table and the gains-derived preset lookup.
// Pure: no audio device, no window.
//
// build + run (MSYS2 MinGW gcc, from the repo root):
//   gcc -O2 -std=c11 -Isrc src/eq.c tools/eq_test.c -o build/eq_test.exe && build/eq_test.exe
#include "eq.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond, what) do { printf("%s %s\n", (cond) ? "ok:  " : "FAIL:", what); if (!(cond)) fails++; } while (0)

int main(void) {
    Eq eq;
    eq_init(&eq, 48000);

    CHECK(eq_preset_count() >= 2, "there is more than one preset");
    CHECK(eq_preset_name(0) != NULL && !strcmp(eq_preset_name(0), "Flat"), "preset 0 is Flat");
    CHECK(eq_preset_name(-1) == NULL, "a negative index has no name");
    CHECK(eq_preset_name(eq_preset_count()) == NULL, "an index past the end has no name");

    // A fresh EQ is flat, which is preset 0.
    CHECK(eq_preset_match(&eq) == 0, "a fresh EQ matches Flat");

    // Every preset round-trips: apply it, and the lookup finds it again.
    for (int p = 0; p < eq_preset_count(); p++) {
        eq_preset_apply(&eq, p);
        int got = eq_preset_match(&eq);
        if (got != p) printf("     preset %d (%s) matched as %d\n", p, eq_preset_name(p), got);
        CHECK(got == p, eq_preset_name(p));
    }

    // Nudging one band drops out of the preset and into Custom.
    eq_preset_apply(&eq, 1);
    eq_set_gain(&eq, 3, eq_get_gain(&eq, 3) + 2.0f);
    CHECK(eq_preset_match(&eq) == -1, "a moved band reads as Custom");

    // eq_flat() lands back on Flat rather than on Custom.
    eq_flat(&eq);
    CHECK(eq_preset_match(&eq) == 0, "flattening returns to Flat");

    // Applying a preset leaves the enabled flag and the band frequencies alone.
    eq_set_enabled(&eq, true);
    float f0 = eq_get_frequency(&eq, 0), f9 = eq_get_frequency(&eq, EQ_BANDS - 1);
    eq_preset_apply(&eq, 2);
    CHECK(eq_is_enabled(&eq), "a preset does not switch the EQ off");
    CHECK(eq_get_frequency(&eq, 0) == f0 && eq_get_frequency(&eq, EQ_BANDS - 1) == f9,
          "a preset does not move the band frequencies");

    // Out-of-range indices are ignored, not clamped onto a neighbour.
    eq_preset_apply(&eq, 2);
    eq_preset_apply(&eq, -1);
    eq_preset_apply(&eq, eq_preset_count());
    CHECK(eq_preset_match(&eq) == 2, "an out-of-range preset changes nothing");

    // Gains stay inside the slider range the UI draws.
    for (int p = 0; p < eq_preset_count(); p++) {
        eq_preset_apply(&eq, p);
        bool in_range = true;
        for (int b = 0; b < EQ_BANDS; b++) {
            float g = eq_get_gain(&eq, b);
            if (g < -12.f || g > 12.f) in_range = false;
        }
        CHECK(in_range, "preset gains stay within -12..+12 dB");
    }

    printf(fails ? "FAILED (%d)\n" : "OK\n", fails);
    return fails ? 1 : 0;
}
