/* FP bisection #6 (FD-85 payload lane): record the accumulator trajectory
 * into an array during the loop; print afterwards. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static double db[1000];
static double traj[1000];
static double sinv[1000];

int main(void) {
    int i;
    double acc;
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 0; i < 1000; i++) db[i] = i * 0.01;

    acc = 0;
    for (i = 0; i < 1000; i++) {
        double s = sin(db[i]);
        sinv[i] = s;
        acc += s;
        traj[i] = acc;
    }
    printf("FP6 final acc=%.4f (true 184.1776)\n", acc);
    for (i = 0; i < 130; i += 10)
        printf("FP6 i=%d sin=%.4f traj=%.4f\n", i, sinv[i], traj[i]);
    printf("FP6 traj[99]=%.4f traj[199]=%.4f traj[499]=%.4f traj[999]=%.4f\n",
           traj[99], traj[199], traj[499], traj[999]);
    printf("FP6 sinv[100]=%.4f sinv[500]=%.4f sinv[999]=%.4f\n", sinv[100], sinv[500], sinv[999]);
    printf("FP6: DONE\n");
    return 0;
}
