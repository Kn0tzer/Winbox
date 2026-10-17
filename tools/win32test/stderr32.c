#include <stdio.h>
int main(void) {
    fprintf(stderr, "STDERR32: HELLO-STDERR\n"); fflush(stderr);
    fprintf(stdout, "STDERR32: HELLO-STDOUT\n"); fflush(stdout);
    return 42;
}
