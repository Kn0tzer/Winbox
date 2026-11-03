/* FP bisection #4 (FD-85 payload lane): separate the argument computation,
 * the call repetition, and the accumulation. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static double args4[4] = {1.0, 0.5, 2.0, 100.0};
static double db[1000];

int main(void) {
    int i;
    double acc, t;
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    /* A: argument computation only, no call */
    for (i = 0; i < 1000; i++) db[i] = i * 0.01;
    printf("FP4 A: db[100]=%.4f db[999]=%.4f (expect 1.0, 9.99)\n", db[100], db[999]);

    /* B: call loop over precomputed arguments */
    acc = 0;
    for (i = 0; i < 1000; i++) acc += sin(db[i]);
    printf("FP4 B: sum sin(db[i]) = %.4f (expect ~ -4.78)\n", acc);

    /* C: 1000 calls, memory args only, no computation */
    acc = 0;
    for (i = 0; i < 1000; i++) acc += sin(args4[i % 4]);
    printf("FP4 C: sum sin(args4[i%%4]) x1000 = %.2f (expect 250*(0.8415+0.4794+0.9093-0.5064)=430.95)\n", acc);

    /* D: one inline-computed call, no loop */
    t = 100 * 0.01;
    printf("FP4 D: t=%.20g sin(t)=%.6f sin(100*0.01)=%.6f\n", t, sin(t), sin(100 * 0.01));

    /* E: short loop, print every value */
    for (i = 95; i < 105; i++)
        printf("FP4 E: i=%d arg=%.4f sin=%.6f\n", i, i * 0.01, sin(i * 0.01));

    printf("FP4: DONE\n");
    return 0;
}
