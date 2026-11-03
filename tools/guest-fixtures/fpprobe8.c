/* FP bisection #8 (FD-87): what in the loop protects or destroys?
 * Each section fills its OWN bss array via sin() calls, with a different
 * extra call interleaved. Results printed after each section; section A
 * is re-read at the very end. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

typedef double (*id_fn)(double);

static double db[1000];
static double outA[1000], outB[1000], outC[1000], outD[1000], outE[1000];

static void report(const char *name, double *out, double acc) {
    printf("FP8 %s acc=%.4f out[500]=%.4f out[999]=%.4f\n", name, acc, out[500], out[999]);
    fflush(stdout);
}

int main(void) {
    int i;
    double acc;
    HMODULE hd;
    id_fn help_id = NULL;
    LARGE_INTEGER li;

    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 0; i < 1000; i++) db[i] = i * 0.01;

    hd = LoadLibraryA("fphelp.dll");
    if (hd) help_id = (id_fn)(void *)GetProcAddress(hd, "fphelp_id");
    printf("FP8 help_id=%p\n", (void *)help_id);
    fflush(stdout);

    /* A: plain sin loop (FD-87 baseline shape) */
    acc = 0;
    for (i = 0; i < 1000; i++) { double s = sin(db[i]); outA[i] = s; acc += s; }
    report("A plain-sin", outA, acc);

    /* B: sin + GetTickCount per iteration */
    acc = 0;
    for (i = 0; i < 1000; i++) { double s = sin(db[i]); outB[i] = s; acc += s; (void)GetTickCount(); }
    report("B +GetTickCount", outB, acc);

    /* C: sin + Sleep(0) per iteration */
    acc = 0;
    for (i = 0; i < 1000; i++) { double s = sin(db[i]); outC[i] = s; acc += s; Sleep(0); }
    report("C +Sleep0", outC, acc);

    /* D: sin + own-DLL call per iteration */
    acc = 0;
    for (i = 0; i < 1000; i++) { double s = sin(db[i]); if (help_id) s = help_id(s); outD[i] = s; acc += s; }
    report("D +owndll", outD, acc);

    /* E: sin + QueryPerformanceCounter per iteration */
    acc = 0;
    for (i = 0; i < 1000; i++) { double s = sin(db[i]); outE[i] = s; acc += s; (void)QueryPerformanceCounter(&li); }
    report("E +QPC", outE, acc);

    /* Re-read A long after its loop */
    printf("FP8 A recheck out[500]=%.4f out[999]=%.4f\n", outA[500], outA[999]);
    fflush(stdout);
    printf("FP8: DONE\n");
    return 0;
}
