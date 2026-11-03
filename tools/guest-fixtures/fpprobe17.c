/* fpprobe17: what does the fixture's IAT slot for sin contain, and what
 * code does the thunk actually reach? sin(0.5) = 0.479426. */
#include <stdio.h>
#include <windows.h>
#include <math.h>

static volatile double arg05 = 0.5;
typedef double (__cdecl *sinfn)(double);
extern sinfn __imp_sin; /* the IAT slot itself: its content is the target */

static void dump(const char *tag, const unsigned char *p) {
    printf("FP17 %s tgt=%p bytes=%02x %02x %02x %02x %02x %02x %02x %02x\n",
           tag, (const void *)p,
           p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
}

int main(void) {
    HMODULE m = GetModuleHandleA("msvcrt.dll");
    sinfn psin = (sinfn)GetProcAddress(m, "sin");
    void *slot = (void *)__imp_sin;
    printf("FP17 gpa=%p slot=%p\n", (void *)psin, slot);
    dump("gpa", (const unsigned char *)psin);
    if (slot) dump("slot", (const unsigned char *)slot);

    for (int i = 0; i < 6; i++)
        printf("FP17 thunk[%d]=%.6f\n", i, sin(arg05));
    if (slot) dump("slot-after", (const unsigned char *)slot);

    for (int i = 0; i < 3; i++)
        printf("FP17 ptr[%d]=%.6f\n", i, psin(arg05));

    printf("FP17: DONE\n");
    return 0;
}
