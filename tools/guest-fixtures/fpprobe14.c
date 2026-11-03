/* fpprobe14: map the distortion. Print sin(x) result BITS for fixed
 * args, first as single calls, then from inside a tight loop. */
#include <stdio.h>
#include <string.h>
#include <math.h>

static volatile double args[6] = {0.0, 0.5, 1.0, 2.0, 5.0, -1.0};
static double results[6];
static unsigned long long bits(double d) { unsigned long long u; memcpy(&u, &d, 8); return u; }

int main(void) {
    for (int k = 0; k < 6; k++) {
        double r = sin(args[k]);
        printf("FP14 single arg=%.1f bits=%016llx val=%.6f\n", args[k], bits(r), r);
    }
    for (int i = 0; i < 2000; i++) {
        for (int k = 0; k < 6; k++) results[k] = sin(args[k]);
    }
    for (int k = 0; k < 6; k++) {
        printf("FP14 loop   arg=%.1f bits=%016llx val=%.6f\n", args[k], bits(results[k]), results[k]);
    }
    printf("FP14: DONE\n");
    return 0;
}
