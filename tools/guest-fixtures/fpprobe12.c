/* fpprobe12: split the failing loop's pieces.
 * K: outK[i] = sin(0.5)        -- constant argument, no array loads.
 * L: outL[i] = id(db[i])       -- array load + store, local call, no sin.
 * M: outM[i] = sin(db[i])      -- the failing shape (control).
 * If K is wrong, sin itself breaks in a loop. If K is right and M is
 * wrong, the db[i] load feeding sin is the broken piece. If L is wrong,
 * the load/store-with-calls shape is broken independent of sin. */
#include <stdio.h>
#include <math.h>

static double db[1000];
static double outK[1000];
static double outL[1000];
static double outM[1000];

static double id(double v) { return v; }

static int badK(void) { int b = 0; for (int i = 0; i < 1000; i++) if (outK[i] != sin(0.5)) b++; return b; }
static int badL(void) { int b = 0; for (int i = 0; i < 1000; i++) if (outL[i] != db[i]) b++; return b; }
static int badM(void) { int b = 0; for (int i = 0; i < 1000; i++) if (outM[i] != sin(db[i])) b++; return b; }

int main(void) {
    for (int i = 0; i < 1000; i++) db[i] = i * 0.01;

    for (int i = 0; i < 1000; i++) outK[i] = sin(0.5);
    printf("FP12 K constarg bad=%d sample=%.4f\n", badK(), outK[500]);

    for (int i = 0; i < 1000; i++) outL[i] = id(db[i]);
    printf("FP12 L loadstore bad=%d sample=%.4f\n", badL(), outL[500]);

    for (int i = 0; i < 1000; i++) outM[i] = sin(db[i]);
    printf("FP12 M sinload bad=%d sample=%.4f\n", badM(), outM[500]);

    printf("FP12: DONE\n");
    return 0;
}
