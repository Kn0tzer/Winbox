#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#ifndef SYS_preadv2
#define SYS_preadv2 392
#endif
#ifndef RWF_HIPRI
#define RWF_HIPRI 0x1
#endif
static char buf[64];
static struct iovec iov = { buf, 16 };
static int pipefd[2];
static void refill(void){ const char* m="pipe"; if(write(pipefd[1],m,4)!=4) perror("write"); }
static void drain(void){ int fl=fcntl(pipefd[0],F_GETFL); fcntl(pipefd[0],F_SETFL,fl|O_NONBLOCK); char t[64]; while(read(pipefd[0],t,64)>0){} fcntl(pipefd[0],F_SETFL,fl); }
static void t(const char* n, long long off, int flags){
  drain(); refill();
  errno=0;
  long r = syscall(SYS_preadv2, pipefd[0], &iov, 1, (long)off, (long)(off>>32), flags);
  int e = errno;
  drain();
  printf("%-38s -> %ld errno=%d (%s)\n", n, r, e, strerror(e));
}

#ifdef __aarch64__
#define NR_preadv 69
#else
#define NR_preadv 361
#endif
static void t1(const char* n, int fd, long long off){
  char b2[64]; struct iovec v = { b2, 16 };
  errno=0;
  long r = syscall(NR_preadv, fd, &v, 1, (long)off, (long)(off>>32));
  int e = errno;
  printf("%-38s -> %ld errno=%d (%s)\n", n, r, e, strerror(e));
}

static void t2(const char* n, int fd, struct iovec* v, int cnt, long long off, int flags){
  errno=0;
  long r = syscall(SYS_preadv2, fd, v, cnt, (long)off, (long)(off>>32), flags);
  int e = errno;
  printf("%-38s -> %ld errno=%d (%s)\n", n, r, e, strerror(e));
}
static void t1b(const char* n, int fd, struct iovec* v, int cnt, long long off){
  errno=0;
  long r = syscall(NR_preadv, fd, v, cnt, (long)off, (long)(off>>32));
  int e = errno;
  printf("%-38s -> %ld errno=%d (%s)\n", n, r, e, strerror(e));
}
int main(void){
  if(pipe(pipefd)) return 1;
  t("preadv2 pipe off=-1 flags=0", -1, 0);
  t("preadv2 pipe off=-2 flags=0", -2, 0);
  t("preadv2 pipe off=0 flags=0", 0, 0);
  t("preadv2 pipe off=-1 flags=HIPRI", -1, RWF_HIPRI);
  refill();
  t1("preadv pipe off=-1", pipefd[0], -1);
  drain();
  t1("preadv pipe off=-2", pipefd[0], -2);
  t1b("preadv pipe cnt=2000 off=0", pipefd[0], &iov, 2000, 0);
  t1b("preadv pipe cnt=-1 off=0", pipefd[0], &iov, -1, 0);
  t1b("preadv pipe iov=NULL off=0", pipefd[0], NULL, 1, 0);
  t1b("preadv pipe WRITE end off=0", pipefd[1], &iov, 1, 0);
  t2("preadv2 pipe flags=0x80 off=0", pipefd[0], &iov, 1, 0, 0x80);
  refill();
  t2("preadv2 pipe flags=0x80 off=-1", pipefd[0], &iov, 1, -1, 0x80);
  drain();
  t2("preadv2 pipe cnt=2000 off=0", pipefd[0], &iov, 2000, 0, 0);
  t2("preadv2 pipe iov=NULL off=0", pipefd[0], NULL, 1, 0, 0);
  int df = open("/tmp/hostref-data.bin", O_RDONLY);
  t2("preadv2 file flags=0x80 off=0", df, &iov, 1, 0, 0x80);
  t1b("preadv file cnt=2000 off=0", df, &iov, 2000, 0);
  t1("preadv file off=-1", df, -1);
  t1("preadv file off=-2", df, -2);
  return 0;
}
