/* FP bisection #10 (FD-87): cross-thread view. Main fills via sin loop and
 * counts zeros; a worker thread counts zeros in the same array; main
 * recounts after the wait. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static double db[1000];
static double sinv[1000];
static volatile long workerZeros = -1;

static int countZeros(void) {
    int i, z = 0;
    for (i = 0; i < 1000; i++) if (sinv[i] == 0.0) z++;
    return z;
}

static DWORD WINAPI worker(LPVOID p) {
    (void)p;
    workerZeros = countZeros();
    return 0;
}

int main(void) {
    int i, zMain1, zMain2;
    HANDLE th;

    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 0; i < 1000; i++) db[i] = i * 0.01;
    for (i = 0; i < 1000; i++) sinv[i] = sin(db[i]);

    zMain1 = countZeros();
    th = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (th) { WaitForSingleObject(th, 10000); CloseHandle(th); }
    zMain2 = countZeros();

    printf("FP10 main-before=%d worker=%ld main-after=%d\n", zMain1, workerZeros, zMain2);
    fflush(stdout);
    printf("FP10: DONE\n");
    return 0;
}
