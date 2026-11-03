/* FP bisection #9 (FD-87): visibility timeline. Fill via sin loop, then
 * count exact-zero entries (sin(i*0.01) is 0 only at i=0) at several
 * points, each count computed call-free BEFORE the event it follows. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static double db[1000];
static double sinv[1000];

static int countZeros(void) {
    int i, z = 0;
    for (i = 0; i < 1000; i++) if (sinv[i] == 0.0) z++;
    return z;
}

int main(void) {
    int i, z;
    double s;

    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 0; i < 1000; i++) db[i] = i * 0.01;
    for (i = 0; i < 1000; i++) sinv[i] = sin(db[i]);

    z = countZeros();
    printf("FP9 zeros right after loop (no prior printf): %d\n", z);
    fflush(stdout);

    s = sin(0.5);
    z = countZeros();
    printf("FP9 zeros after one more sin call (s=%.4f): %d\n", s, z);
    fflush(stdout);

    printf("FP9 a plain printf line\n");
    fflush(stdout);
    z = countZeros();
    printf("FP9 zeros after a plain printf: %d\n", z);
    fflush(stdout);

    Sleep(1);
    z = countZeros();
    printf("FP9 zeros after Sleep(1): %d\n", z);
    fflush(stdout);

    printf("FP9: DONE\n");
    return 0;
}
