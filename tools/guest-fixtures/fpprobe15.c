/* fpprobe15: how does the call reach sin, and does sin's code change?
 * T: loop calling sin through the import thunk.
 * P: loop calling sin through a GetProcAddress function pointer.
 * Also prints the resolved sin address and its first 8 bytes before
 * and after the loops. sin(2.0) = 0.909297 is the probe value. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <math.h>

static double outT[1000];
static double outP[1000];
typedef double (__cdecl *sinfn)(double);

static void dumpbytes(const char *tag, const unsigned char *p) {
    printf("FP15 %s bytes=%02x %02x %02x %02x %02x %02x %02x %02x\n",
           tag, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
}

int main(void) {
    HMODULE m = GetModuleHandleA("msvcrt.dll");
    sinfn psin = (sinfn)GetProcAddress(m, "sin");
    printf("FP15 sinaddr=%p single=%.6f\n", (void *)psin, sin(2.0));
    dumpbytes("before", (const unsigned char *)psin);

    for (int i = 0; i < 1000; i++) outT[i] = sin(2.0);
    printf("FP15 T thunk sample=%.6f\n", outT[500]);
    dumpbytes("aftermid", (const unsigned char *)psin);

    for (int i = 0; i < 1000; i++) outP[i] = psin(2.0);
    printf("FP15 P ptr sample=%.6f\n", outP[500]);
    dumpbytes("after", (const unsigned char *)psin);

    printf("FP15: DONE\n");
    return 0;
}
