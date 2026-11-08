#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <fcntl.h>
#ifdef __aarch64__
#define NR_pwritev 70
#define NR_pwritev2 286
#define NR_pwrite64 SYS_pwrite64
#else
#define NR_pwritev 362
#define NR_pwritev2 393
#define NR_pwrite64 SYS_pwrite64
#endif
static char buf[64];
static struct iovec iov = { buf, 4 };
static int p[2];
static void t(const char* n, long nr, int fd, int cnt, long long off, int flags){
  errno=0;
  long r;
  if (nr == NR_pwrite64) r = syscall(nr, fd, buf, 4, (long)off);
  else if (nr == NR_pwritev) r = syscall(nr, fd, &iov, cnt, (long)off, (long)(off>>32));
  else r = syscall(nr, fd, &iov, cnt, (long)off, (long)(off>>32), flags);
  int e = errno;
  printf("%-40s -> %ld errno=%d (%s)\n", n, r, e, strerror(e));
}
int main(void){
  if(pipe(p)) return 1;
  memcpy(buf, "wxyz", 4);
  t("pwrite64 pipe off=-2", NR_pwrite64, p[1], 1, -2, 0);
  t("pwritev pipe READ end off=0", NR_pwritev, p[0], 1, 0, 0);
  t("pwritev2 pipe APPEND off=0", NR_pwritev2, p[1], 1, 0, 0x10);
  t("pwritev2 pipe APPEND off=-1", NR_pwritev2, p[1], 1, -1, 0x10);
  fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
  char out[16] = {0};
  ssize_t n = read(p[0], out, 16);
  if (n < 0) { printf("%-40s -> read errno=%d (%s)\n", "drain", errno, strerror(errno)); return 0; }
  printf("%-40s -> %zd bytes '%s'\n", "drain", n, out);
  return 0;
}
