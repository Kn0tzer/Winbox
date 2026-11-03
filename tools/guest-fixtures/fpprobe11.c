/* fpprobe11: granularity test.
 * Section M: iterations 0..499 do sin + Sleep(0); iterations 500..999 do
 * plain sin. If per-iteration syscalls are what save stores, the first
 * half survives and the second half is lost.
 * Section N: plain sin for all 1000, then ONE Sleep(0) + one
 * QueryPerformanceCounter + one printf at the very end, then verify.
 * If a late syscall rescues the data, N reads back clean; if the data
 * was written somewhere wrong, N stays zero. */
#include <stdio.h>
#include <math.h>
#include <windows.h>

static double db[1000];
static double outM[1000];
static double outN[1000];

static int count_bad(const double *out) {
    int bad = 0;
    for (int i = 0; i < 1000; i++) {
        double e = sin(db[i]);
        if (out[i] != e) bad++;
    }
    return bad;
}

int main(void) {
    for (int i = 0; i < 1000; i++) db[i] = i * 0.01;

    for (int i = 0; i < 1000; i++) {
        outM[i] = sin(db[i]);
        if (i < 500) Sleep(0);
    }
    int badM_first = 0, badM_second = 0;
    for (int i = 0; i < 1000; i++) {
        if (outM[i] != sin(db[i])) { if (i < 500) badM_first++; else badM_second++; }
    }
    printf("FP11 M first-half bad=%d second-half bad=%d\n", badM_first, badM_second);

    for (int i = 0; i < 1000; i++) outN[i] = sin(db[i]);
    Sleep(0);
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    printf("FP11 N about to verify\n");
    int badN = count_bad(outN);
    printf("FP11 N bad=%d outN[500]=%.4f\n", badN, outN[500]);
    printf("FP11: DONE\n");
    return 0;
}
