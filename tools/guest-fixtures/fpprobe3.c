/* FP bisection #3 (FD-85 payload lane): are double constants loaded from
 * the compiler's constant pool / .rdata served correctly in the 64-bit
 * guest, vs the same values in .data? */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static const double C_RDATA_001 = 0.01;          /* .rdata */
static const double C_RDATA_PI = 3.141592653589793;
static double C_DATA_001 = 0.01;                 /* .data */
static double C_DATA_PI = 3.141592653589793;
static const float CF_RDATA_001 = 0.01f;         /* .rdata float */
static float CF_DATA_001 = 0.01f;                /* .data float */

int main(void) {
    volatile double sink = 0;
    int i;
    double acc;
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("FP3 pool-literal: 0.01=%.20g pi=%.20g 44100.0=%.6f\n", 0.01, 3.141592653589793, 44100.0);
    printf("FP3 rdata: 0.01=%.20g pi=%.20g\n", C_RDATA_001, C_RDATA_PI);
    printf("FP3 data:  0.01=%.20g pi=%.20g\n", C_DATA_001, C_DATA_PI);
    printf("FP3 rdata-f: 0.01f=%.10g  data-f: 0.01f=%.10g\n", CF_RDATA_001, CF_DATA_001);

    acc = 0;
    for (i = 0; i < 1000; i++) acc += sin(i * C_RDATA_001);
    printf("FP3 loop-rdata: sum sin(i*0.01) i<1000 = %.4f (expect ~ -9.0..9.0 nonzero-ish, exact -4.78)\n", acc);
    acc = 0;
    for (i = 0; i < 1000; i++) acc += sin(i * C_DATA_001);
    printf("FP3 loop-data:  sum sin(i*0.01) i<1000 = %.4f\n", acc);
    acc = 0;
    for (i = 0; i < 1000; i++) acc += sin(i * 0.01);
    printf("FP3 loop-pool:  sum sin(i*0.01) i<1000 = %.4f\n", acc);

    sink = C_RDATA_001 + C_DATA_001;
    (void)sink;
    printf("FP3: DONE\n");
    return 0;
}
