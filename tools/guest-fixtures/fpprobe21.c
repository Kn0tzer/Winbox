/* fpprobe21: dump the raw FNSAVE image at each stage of the x87 sequence,
 * fresh and after a corrupting loop, to see exactly which stage lies.
 * Image layout (64-bit env): 7 dwords env (cw, sw, tag, ...), then eight
 * 10-byte slots (signif qword + signExp word). We print all 27 dwords. */
#include <stdio.h>
#include <string.h>
#include <math.h>

static volatile double a05 = 0.5;
static unsigned char buf[108];
static double results[16];

static void dumpimg(const char *tag) {
    const unsigned *d = (const unsigned *)buf;
    printf("FP21 %s sw=%04x tag=%04x slots:", tag,
           (unsigned)((const unsigned short *)buf)[2],
           (unsigned)((const unsigned short *)buf)[4]);
    for (int n = 0; n < 8; n++) {
        const unsigned char *s = buf + 28 + n * 10;
        unsigned long long sig;
        unsigned short se;
        memcpy(&sig, s, 8);
        memcpy(&se, s + 8, 2);
        printf(" [%04x:%016llx]", se, sig);
    }
    printf("\n");
}

static void seq_load(void) {
    __asm__ volatile ("fldl %0\n\tfnsave %1"
                      :: "m"(a05), "m"(*(unsigned char(*)[108])buf) : "memory");
}
static void seq_load_sin(void) {
    __asm__ volatile ("fldl %0\n\tfsin\n\tfnsave %1"
                      :: "m"(a05), "m"(*(unsigned char(*)[108])buf) : "memory");
}
static double seq_full(void) {
    double r;
    __asm__ volatile ("fldl %1\n\tfsin\n\tfstpl %0"
                      : "=m"(r) : "m"(a05));
    return r;
}

int main(void) {
    seq_load();
    dumpimg("A-load-fresh");
    seq_load_sin();
    dumpimg("B-loadsin-fresh");
    printf("FP21 full-fresh=%.17g\n", seq_full());

    for (int i = 0; i < 10; i++) results[i] = seq_full();
    printf("FP21 loop:");
    for (int i = 0; i < 10; i++) printf(" %.17g", results[i]);
    printf("\n");

    seq_load();
    dumpimg("C-load-afterloop");
    seq_load_sin();
    dumpimg("D-loadsin-afterloop");
    printf("FP21 full-after=%.17g\n", seq_full());

    printf("FP21: DONE\n");
    return 0;
}
