/* FP bisection #5 (FD-85 payload lane): the accumulator across calls. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static double db[1000];
static double gacc; /* .data accumulator */

__attribute__((noinline)) static double id(double v) { return v; }
__attribute__((noinline)) static void noop(void) { }

int main(void) {
    int i;
    double acc;
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 0; i < 1000; i++) db[i] = i * 0.01;

    /* A: accumulate across a trivial local call */
    acc = 0;
    for (i = 0; i < 1000; i++) acc += id(db[i]);
    printf("FP5 A: sum id(db) = %.2f (expect 4995.00)\n", acc);

    /* A2: accumulate across an empty call */
    acc = 0;
    for (i = 0; i < 1000; i++) { noop(); acc += db[i]; }
    printf("FP5 A2: sum db with noop = %.2f (expect 4995.00)\n", acc);

    /* B: accumulate across fabs */
    acc = 0;
    for (i = 0; i < 1000; i++) acc += fabs(db[i] - 5.0);
    printf("FP5 B: sum fabs(db-5) = %.2f (expect ~2500.25)\n", acc);

    /* C: accumulator in .data across sin calls */
    gacc = 0;
    for (i = 0; i < 1000; i++) gacc += sin(db[i]);
    printf("FP5 C: gacc sum sin(db) = %.4f (expect ~ -4.78)\n", gacc);

    /* D: trajectory of a register accumulator across sin calls */
    acc = 0;
    for (i = 0; i < 1000; i++) {
        acc += sin(db[i]);
        if (i % 100 == 99) printf("FP5 D: after %d iters acc=%.4f\n", i + 1, acc);
    }
    printf("FP5 D: final acc=%.4f\n", acc);

    printf("FP5: DONE\n");
    return 0;
}
