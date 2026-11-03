/* fpprobe16: split call form (import thunk vs function pointer) against
 * sin's internal path (range path arg 2.0 vs polynomial path arg 0.5).
 * Args come from volatile variables so nothing folds.
 * sin(2.0)=0.909297, sin(0.5)=0.479426. */
#include <stdio.h>
#include <windows.h>
#include <math.h>

static volatile double arg20 = 2.0;
static volatile double arg05 = 0.5;
static double oT20[1000], oP20[1000], oT05[1000], oP05[1000];
typedef double (__cdecl *sinfn)(double);

int main(void) {
    HMODULE m = GetModuleHandleA("msvcrt.dll");
    sinfn psin = (sinfn)GetProcAddress(m, "sin");

    for (int i = 0; i < 1000; i++) oT20[i] = sin(arg20);
    printf("FP16 T20 thunk  range sample=%.6f\n", oT20[500]);

    for (int i = 0; i < 1000; i++) oP20[i] = psin(arg20);
    printf("FP16 P20 ptr    range sample=%.6f\n", oP20[500]);

    for (int i = 0; i < 1000; i++) oT05[i] = sin(arg05);
    printf("FP16 T05 thunk  poly  sample=%.6f\n", oT05[500]);

    for (int i = 0; i < 1000; i++) oP05[i] = psin(arg05);
    printf("FP16 P05 ptr    poly  sample=%.6f\n", oP05[500]);

    printf("FP16: DONE\n");
    return 0;
}
