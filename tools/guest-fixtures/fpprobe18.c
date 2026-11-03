/* fpprobe18: dump the import stub's own bytes (the code the fixture's
 * call instruction actually jumps to) and the bit patterns of the
 * wrong results from a tight thunk loop. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <math.h>

static volatile double a05 = 0.5;
static volatile double a20 = 2.0;
static double out[1000];
static unsigned long long bits(double d) { unsigned long long u; memcpy(&u, &d, 8); return u; }

int main(void) {
    /* &sin in the importing module is the stub the call targets. */
    const unsigned char *stub = (const unsigned char *)&sin;
    printf("FP18 stub=%p bytes=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
           (const void *)stub,
           stub[0], stub[1], stub[2], stub[3], stub[4], stub[5],
           stub[6], stub[7], stub[8], stub[9], stub[10], stub[11]);

    for (int i = 0; i < 1000; i++) out[i] = sin(a05);
    printf("FP18 thunk05 bits=%016llx val=%.17g\n", bits(out[500]), out[500]);
    printf("FP18 thunk05 first bits=%016llx val=%.17g\n", bits(out[0]), out[0]);

    for (int i = 0; i < 1000; i++) out[i] = sin(a20);
    printf("FP18 thunk20 bits=%016llx val=%.17g\n", bits(out[500]), out[500]);
    printf("FP18 thunk20 first bits=%016llx val=%.17g\n", bits(out[0]), out[0]);

    printf("FP18: DONE\n");
    return 0;
}
