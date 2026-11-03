// FD-106 part 3: import-phase order between an unreadable control
// area (-EFAULT) and an over-cap iovlen (-EMSGSIZE), and between a
// malformed chain (-EINVAL) and a bad fd value inside a rights array
// (-EBADF).
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/mman.h>
static char good[16] = "0123456789ABCDEF";
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    int sv[2]; ssize_t r; int e;
    // iovlen 1025 + control PROT_NONE
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    { char* pg = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      static struct iovec many[1025];
      for (int i = 0; i < 1025; i++) { many[i].iov_base = good; many[i].iov_len = 0; }
      struct msghdr m; memset(&m, 0, sizeof(m));
      m.msg_iov = many; m.msg_iovlen = 1025; m.msg_control = pg; m.msg_controllen = 32;
      errno = 0; r = sendmsg(sv[0], &m, 0); e = errno;
      printf("%-44s ret=%zd errno=%d(%s)\n", "iovlen1025 + control PROT_NONE", r, r < 0 ? e : 0, r < 0 ? strerror(e) : "-");
      munmap(pg, 4096); }
    close(sv[0]); close(sv[1]);
    // malformed second header (len 8) + bad fd value in first rights header
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    { char ctl[64]; memset(ctl, 0, sizeof(ctl));
      size_t L = 20; int lvl = SOL_SOCKET, typ = SCM_RIGHTS;
      memcpy(ctl + 0, &L, 8); memcpy(ctl + 8, &lvl, 4); memcpy(ctl + 12, &typ, 4);
      int badfd = 9999; memcpy(ctl + 16, &badfd, 4);
      size_t L2 = 8; memcpy(ctl + 24, &L2, 8); memcpy(ctl + 32, &lvl, 4); memcpy(ctl + 36, &typ, 4);
      struct iovec iov = { good, 16 };
      struct msghdr m; memset(&m, 0, sizeof(m));
      m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = ctl; m.msg_controllen = 40;
      errno = 0; r = sendmsg(sv[0], &m, 0); e = errno;
      printf("%-44s ret=%zd errno=%d(%s)\n", "badfd in hdr1 + len8 hdr2", r, r < 0 ? e : 0, r < 0 ? strerror(e) : "-"); }
    close(sv[0]); close(sv[1]);
    // bad fd in second rights header + clean first header
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    { char ctl[64]; memset(ctl, 0, sizeof(ctl));
      size_t L = 20; int lvl = SOL_SOCKET, typ = SCM_RIGHTS;
      int fd0 = open("/dev/null", O_RDONLY);
      memcpy(ctl + 0, &L, 8); memcpy(ctl + 8, &lvl, 4); memcpy(ctl + 12, &typ, 4); memcpy(ctl + 16, &fd0, 4);
      memcpy(ctl + 24, &L, 8); memcpy(ctl + 32, &lvl, 4); memcpy(ctl + 36, &typ, 4);
      int badfd = 9999; memcpy(ctl + 40, &badfd, 4);
      struct iovec iov = { good, 16 };
      struct msghdr m; memset(&m, 0, sizeof(m));
      m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = ctl; m.msg_controllen = 44;
      errno = 0; r = sendmsg(sv[0], &m, 0); e = errno;
      printf("%-44s ret=%zd errno=%d(%s)\n", "good hdr1 + badfd in hdr2", r, r < 0 ? e : 0, r < 0 ? strerror(e) : "-");
      close(fd0); }
    close(sv[0]); close(sv[1]);
    return 0;
}
