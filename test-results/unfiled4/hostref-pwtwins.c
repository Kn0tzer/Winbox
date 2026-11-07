#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#ifdef __aarch64__
#define NR_pwritev 70
#define NR_pwrite64_SYSCALL SYS_pwrite64
#else
#define NR_pwritev 334
#endif
#ifndef SYS_pwritev2
#define SYS_pwritev2 393
#endif
static char buf[64];
static struct iovec iov = { buf, 4 };
int main(void){
  int p[2]; if(pipe(p)) return 1;
  memcpy(buf, "wxyz", 4);
  errno=0; long r = syscall(SYS_pwrite64, p[1], buf, 4, 0L); int e=errno;
  printf("%-38s -> %ld errno=%d (%s)\n", "pwrite64 pipe off=0", r, e, strerror(e));
  errno=0; r = syscall(NR_pwritev, p[1], &iov, 1, 0L, 0L); e=errno;
  printf("%-38s -> %ld errno=%d (%s)\n", "pwritev pipe off=0", r, e, strerror(e));
  errno=0; r = syscall(NR_pwritev, p[1], &iov, 2000, 0L, 0L); e=errno;
  printf("%-38s -> %ld errno=%d (%s)\n", "pwritev pipe cnt=2000 off=0", r, e, strerror(e));
  errno=0; r = syscall(SYS_pwritev2, p[1], &iov, 1, 0L, 0L, 0); e=errno;
  printf("%-38s -> %ld errno=%d (%s)\n", "pwritev2 pipe off=0 flags=0", r, e, strerror(e));
  errno=0; r = syscall(SYS_pwritev2, p[1], &iov, 1, -1L, -1L, 0); e=errno;
  printf("%-38s -> %ld errno=%d (%s)\n", "pwritev2 pipe off=-1 (pos mode)", r, e, strerror(e));
  errno=0; r = syscall(SYS_pwrite64, p[0], buf, 4, 0L); e=errno;
  printf("%-38s -> %ld errno=%d (%s)\n", "pwrite64 pipe READ end off=0", r, e, strerror(e));
  char out[8] = {0};
  ssize_t n = read(p[0], out, 8);
  printf("%-38s -> %zd bytes '%s'\n", "drain after all attempts", n, out);
  return 0;
}
