/* fpprobe19: is raw x87 itself broken in a tight loop?
 * X: sinl (long double, pure x87: fldt/fsin/fstpt inside __sinl_internal)
 * Y: sin  (double wrapper around the same x87 core) -- control.
 * Z: fsin via inline asm on a double, minimal hand sequence.
 * sinl(0.5L) = 0.4794255386..., sin(0.5) = 0.4794255386. */
#include <stdio.h>
#include <math.h>

static volatile double a05 = 0.5;
static volatile long double a05l = 0.5L;
static long double outX[1000];
static double outY[1000];
static double outZ[1000];

int main(void) {
    for (int i = 0; i < 1000; i++) outX[i] = sinl(a05l);
    printf("FP19 X sinl sample=%.10f\n", (double)outX[500]);

    for (int i = 0; i < 1000; i++) outY[i] = sin(a05);
    printf("FP19 Y sin  sample=%.10f\n", outY[500]);

    for (int i = 0; i < 1000; i++) {
        double r;
        __asm__ volatile ("fldl %1\n\tfsin\n\tfstpl %0"
                          : "=m"(r) : "m"(a05));
        outZ[i] = r;
    }
    printf("FP19 Z asm  sample=%.10f\n", outZ[500]);

    printf("FP19: DONE\n");
    return 0;
}
