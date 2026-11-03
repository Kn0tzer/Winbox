/* FP/store bisection probe (FD-85 payload lane): pure Win32 console, no
 * audio. Each test prints its own verdict. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static double sabs(const short *s, int n) { double a = 0; int i; for (i = 0; i < n; i++) { double v = s[i]; a += v < 0 ? -v : v; } return a; }
static double dabs(const double *s, int n) { double a = 0; int i; for (i = 0; i < n; i++) { double v = s[i]; a += v < 0 ? -v : v; } return a; }

int main(void) {
    const int N = 26460;
    short *sb;
    double *db;
    int i;
    double acc;
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    /* T1: libm values straight to printf (no memory stores) */
    printf("FP T1: sin(1.0)=%.6f sin(0.5)=%.6f cos(1.0)=%.6f sqrt(2.0)=%.6f\n",
           sin(1.0), sin(0.5), cos(1.0), sqrt(2.0));

    sb = (short *)malloc((size_t)N * sizeof(short));
    db = (double *)malloc((size_t)N * sizeof(double));
    printf("FP T0: sb=%p db=%p\n", (void *)sb, (void *)db);

    /* T2: constant short stores */
    for (i = 0; i < N; i++) sb[i] = 12000;
    printf("FP T2: const-short absum=%.1f head=%d\n", sabs(sb, N), sb[100]);

    /* T3: integer pattern stores */
    for (i = 0; i < N; i++) sb[i] = (short)(i % 2000 - 1000);
    printf("FP T3: int-pattern absum=%.1f head=%d\n", sabs(sb, N), sb[1500]);

    /* T4: the failing shape - sin -> double*amp -> short store */
    for (i = 0; i < N; i++) sb[i] = (short)(sin(2.0 * 3.141592653589793 * 440.0 * i / 44100.0) * 12000.0);
    printf("FP T4: sin-short absum=%.1f s100=%d s1000=%d s5000=%d\n", sabs(sb, N), sb[100], sb[1000], sb[5000]);

    /* T5: sin into a double buffer */
    for (i = 0; i < N; i++) db[i] = sin(i * 0.01);
    printf("FP T5: sin-double absum=%.2f d100=%.4f\n", dabs(db, N), db[100]);

    /* T6: register-only accumulation of sin (no stores) */
    acc = 0;
    for (i = 0; i < N; i++) acc += sin(i * 0.01);
    printf("FP T6: sin-regsum=%.2f\n", acc);

    /* T7: double multiply-add stores (no libm) */
    for (i = 0; i < N; i++) db[i] = i * 0.5 + 1.25;
    printf("FP T7: muladd-double absum=%.1f d100=%.2f\n", dabs(db, N), db[100]);

    /* T8: float (single precision) sinf into short */
    for (i = 0; i < N; i++) sb[i] = (short)(sinf(i * 0.01f) * 12000.0f);
    printf("FP T8: sinf-short absum=%.1f s100=%d\n", sabs(sb, N), sb[100]);

    printf("FP: DONE\n");
    return 0;
}
