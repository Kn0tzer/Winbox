/* fpprobe13: pin down what makes sin() return 0 in a loop.
 * K2: out[i] = sin(half)      -- half is a volatile .data variable (0.5),
 *                                loaded from memory every iteration.
 * K3: out[i] = sin(0.5+i*0.0) -- argument computed in registers only.
 * K4: out[i] = sin(heap[i])   -- argument loaded from a malloc'd buffer
 *                                filled with 0.5.
 * Ground truth is the printed sample: sin(0.5) = 0.4794. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static volatile double half = 0.5;
static double out2[1000];
static double out3[1000];
static double out4[1000];

int main(void) {
    double *heap = (double *)malloc(1000 * sizeof(double));
    for (int i = 0; i < 1000; i++) heap[i] = 0.5;

    for (int i = 0; i < 1000; i++) out2[i] = sin(half);
    printf("FP13 K2 memarg sample=%.4f acc=%.4f\n", out2[500],
           out2[0] + out2[250] + out2[500] + out2[750] + out2[999]);

    for (int i = 0; i < 1000; i++) out3[i] = sin(0.5 + i * 0.0);
    printf("FP13 K3 regarg sample=%.4f acc=%.4f\n", out3[500],
           out3[0] + out3[250] + out3[500] + out3[750] + out3[999]);

    for (int i = 0; i < 1000; i++) out4[i] = sin(heap[i]);
    printf("FP13 K4 heaparg sample=%.4f acc=%.4f\n", out4[500],
           out4[0] + out4[250] + out4[500] + out4[750] + out4[999]);

    printf("FP13: DONE\n");
    return 0;
}
