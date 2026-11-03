/* FP bisection #7 (FD-85 payload lane): is the sin return value lost at
 * the XMM0 -> memory store, at the reload, or later? */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static double db[1000];
static double sinv[1000];
static double sinv2[1000];
static volatile double vsink;

int main(void) {
    int i, bad = 0, bad2 = 0;
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 0; i < 1000; i++) db[i] = i * 0.01;

    for (i = 0; i < 1000; i++) {
        double s = sin(db[i]);
        double rb;
        sinv[i] = s;
        rb = sinv[i];
        if (rb != s) {
            if (bad < 8) printf("FP7 i=%d s=%.6f rb=%.6f\n", i, s, rb);
            bad++;
        }
        memcpy(&sinv2[i], &s, sizeof(double));
        if (sinv2[i] != s) bad2++;
        vsink = s;
    }
    printf("FP7 bad=%d bad2=%d\n", bad, bad2);
    printf("FP7 post: sinv[100]=%.4f sinv[500]=%.4f sinv[999]=%.4f\n", sinv[100], sinv[500], sinv[999]);
    printf("FP7 post2: sinv2[100]=%.4f sinv2[500]=%.4f sinv2[999]=%.4f\n", sinv2[100], sinv2[500], sinv2[999]);
    printf("FP7: DONE\n");
    return 0;
}
