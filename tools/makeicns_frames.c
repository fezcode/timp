// Standalone generator for the macOS .icns pipeline. Renders the shared
// procedural icon (icon.h — analytically anti-aliased, crisp at any size) to a
// PAM (P7 RGB_ALPHA) file; bundle-macos.sh converts the frames to PNG with
// ffmpeg and packs them with iconutil.
//
//   makeicns_frames <size> <out.pam>
#include "icon.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: makeicns_frames <size> <out.pam>\n"); return 1; }
    int size = atoi(argv[1]);
    if (size < 16 || size > 1024) { fprintf(stderr, "size out of range\n"); return 1; }

    uint8_t *rgba = (uint8_t *)malloc((size_t)size * size * 4);
    if (!rgba) return 1;
    icon_render_rgba(rgba, size);

    FILE *f = fopen(argv[2], "wb");
    if (!f) { free(rgba); return 1; }
    fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", size, size);
    fwrite(rgba, 1, (size_t)size * size * 4, f);
    fclose(f);
    free(rgba);
    return 0;
}
