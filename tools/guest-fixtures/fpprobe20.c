/* fpprobe20: isolate FLD/FSTP m64 round-trip vs FSIN interaction.
 * RT: loop { r = arg } via fldl/fstpl only (no fsin) -- must equal arg.
 * SC: loop { r = sin(arg) } via fldl/fsin/fstpl for several args.
 * Prints full precision + bits. */
#include <stdio.h>
#include <string.h>
#include <math.h>

static volatile double args[5] = {0.5, 1.0, 3.0, -0.5, 0.25};
static double out[1000];
static unsigned long long bits(double d) { unsigned long long u; memcpy(&u, &d, 8); return u; }

static double rt(double a) {
    double r;
    __asm__ volatile ("fldl %1\n\tfstpl %0" : "=m"(r) : "m"(a));
    return r;
}
static double sc(double a) {
    double r;
    __asm__ volatile ("fldl %1\n\tfsin\n\tfstpl %0" : "=m"(r) : "m"(a));
    return r;
}

int main(void) {
    for (int k = 0; k < 5; k++) {
        for (int i = 0; i < 1000; i++) out[i] = rt(args[k]);
        printf("FP20 RT arg=%.2f out[0]=%.17g out[500]=%.17g bits500=%016llx\n",
               args[k], out[0], out[500], bits(out[500]));
    }
    for (int k = 0; k < 5; k++) {
        for (int i = 0; i < 1000; i++) out[i] = sc(args[k]);
        printf("FP20 SC arg=%.2f out[0]=%.17g out[500]=%.17g true=%.17g ratio=%.17g\n",
               args[k], out[0], out[500], sin(args[k]),
               out[500] / sin(args[k]));
    }
    printf("FP20: DONE\n");
    return 0;
}
