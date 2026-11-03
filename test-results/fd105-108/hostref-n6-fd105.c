// Re-check of the FD-105 dgram crossing case with a byte dump: is
// the writable prefix really absent after the faulted recvfrom?
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/mman.h>
static const size_t PS = 4096;
static char pat[512];
int main(void) {
    for (size_t i = 0; i < sizeof(pat); i++) pat[i] = (char)('a' + (i % 26));
    char* base = mmap(NULL, PS * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    munmap(base + PS, PS);
    memset(base, 0xEE, PS);
    int sv[2]; socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
    send(sv[0], pat, 100, 0);
    errno = 0; ssize_t r = recvfrom(sv[1], base + 4050, 100, 0, NULL, NULL); int e = errno;
    printf("ret=%zd errno=%d(%s)\nfirst16: ", r, r < 0 ? e : 0, r < 0 ? strerror(e) : "-");
    for (int i = 0; i < 16; i++) printf("%02x ", (unsigned char)base[4050 + i]);
    printf("\nwant16:  ");
    for (int i = 0; i < 16; i++) printf("%02x ", (unsigned char)pat[i]);
    printf("\nbytes 40..46: ");
    for (int i = 40; i < 46; i++) printf("%02x ", (unsigned char)base[4050 + i]);
    printf(" (want %02x %02x %02x %02x %02x %02x)\n",
           (unsigned char)pat[40], (unsigned char)pat[41], (unsigned char)pat[42],
           (unsigned char)pat[43], (unsigned char)pat[44], (unsigned char)pat[45]);
    // and the stream twin for contrast
    memset(base, 0xEE, PS);
    int sw[2]; socketpair(AF_UNIX, SOCK_STREAM, 0, sw);
    send(sw[0], pat, 100, 0);
    errno = 0; r = recvfrom(sw[1], base + 4050, 100, 0, NULL, NULL); e = errno;
    printf("stream ret=%zd errno=%d first16match=%d\n", r, r < 0 ? e : 0, memcmp(base + 4050, pat, 46) == 0);
    return 0;
}
