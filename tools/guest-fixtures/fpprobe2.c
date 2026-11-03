/* FP bisection #2 (FD-85 payload lane): are runtime libm calls broken in
 * the 64-bit guest, and for which functions/call shapes? */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static double args[4] = {1.0, 0.5, 2.0, 100.0};
typedef double (*unary_fn)(double);
static volatile unary_fn sin_ptr = sin;

int main(void) {
    int k;
    unsigned long long bits;
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    /* A: runtime sin with memory args (cannot be folded) */
    for (k = 0; k < 4; k++)
        printf("FP2 A: sin(args[%d]=%.2f) = %.6f\n", k, args[k], sin(args[k]));

    /* B: fixture-side bit pun of the same memory */
    bits = *(unsigned long long *)&args[0];
    printf("FP2 B: bits(args[0]) = %llx (expect 3ff0000000000000)\n", bits);

    /* C: sin through a volatile function pointer */
    printf("FP2 C: sin_ptr(1.0) = %.6f\n", sin_ptr(1.0));

    /* D: other libm functions with memory args */
    printf("FP2 D: cos=%.6f exp=%.6f log=%.6f fabs=%.6f floor=%.6f\n",
           cos(args[0]), exp(args[1]), log(args[2]), fabs(-args[2]), floor(args[3]));

    /* E: repeat A[0] after the other calls (state drift?) */
    printf("FP2 E: sin(args[0]) again = %.6f\n", sin(args[0]));

    /* F: tan and atan (different impls again) */
    printf("FP2 F: tan=%.6f atan=%.6f asin=%.6f acos=%.6f\n",
           tan(args[1]), atan(args[0]), asin(args[1]), acos(args[1]));

    /* G: pow and sqrt with runtime args */
    printf("FP2 G: pow(2,10)=%.2f sqrt(args[3])=%.4f\n", pow(args[2], 10.0), sqrt(args[3]));

    printf("FP2: DONE\n");
    return 0;
}
