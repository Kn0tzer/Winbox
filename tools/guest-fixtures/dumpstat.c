/* FD-85 continuation fixture: report size and content statistics of the
 * raw stream files an ALSA "file" plugin dump captured in the guest
 * (see fd85b diag3: ~/.asoundrc wrapped the default PCM in a file
 * plugin, so these bytes are exactly what the audio client handed to
 * libasound, before any plug conversion). Interprets the bytes as
 * Float32 stereo and as S16 stereo and prints both, plus the first
 * Float32 values, so the true format identifies itself.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void stat_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("dumpstat: %s MISSING\n", path);
        fflush(stdout);
        return;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n > 0 ? n : 1);
    if (n > 0 && fread(b, 1, n, f) != (size_t)n) {
        printf("dumpstat: %s SHORTREAD\n", path);
        fclose(f);
        free(b);
        return;
    }
    fclose(f);
    long nz32 = 0;
    float peakf = 0;
    long nfloat = n / 4;
    float first[8] = {0};
    for (long i = 0; i < nfloat; i++) {
        float v;
        memcpy(&v, b + i * 4, 4);
        if (i < 8) first[i] = v;
        if (v != 0.0f) {
            nz32++;
            float a = v < 0 ? -v : v;
            if (a == a && a > peakf) peakf = a;
        }
    }
    long nz16 = 0;
    int peak16 = 0;
    long nshort = n / 2;
    for (long i = 0; i < nshort; i++) {
        short v;
        memcpy(&v, b + i * 2, 2);
        if (v) {
            nz16++;
            int a = v < 0 ? -(int)v : v;
            if (a > peak16) peak16 = a;
        }
    }
    printf("dumpstat: %s bytes=%ld f32samples=%ld f32nonzero=%ld f32peak=%.4f s16nonzero=%ld s16peak=%d first8f32=",
           path, n, nfloat, nz32, peakf, nz16, peak16);
    for (int i = 0; i < 8; i++) printf(" %.4f", first[i]);
    printf("\n");
    fflush(stdout);
    free(b);
}

int main(void) {
    stat_file("/home/username/alsa.raw");
    stat_file("/home/username/wine.raw");
    return 0;
}
