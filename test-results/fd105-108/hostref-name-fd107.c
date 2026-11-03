// FD-107 measurement: recvmsg whose msg_name cannot be written --
// what the call answers, whether the data still lands, and what
// happens to msg_namelen / msg_flags / delivered control. Plus
// FD-105's recvfrom crossing-prefix shape.
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/un.h>

static const size_t PS = 4096;
static char pat[512];

static struct sockaddr_un mkaddr(const char* name, socklen_t* len) {
    struct sockaddr_un a; memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    a.sun_path[0] = 0; strcpy(a.sun_path + 1, name);
    *len = (socklen_t)(sizeof(a.sun_family) + 1 + strlen(name));
    return a;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (size_t i = 0; i < sizeof(pat); i++) pat[i] = (char)('a' + (i % 26));
    socklen_t sl, rl;
    struct sockaddr_un saddr = mkaddr("fd107snd", &sl);
    struct sockaddr_un raddr = mkaddr("fd107rcv", &rl);
    char* nonepg = mmap(NULL, PS, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ssize_t r; int e;

    // N1: named dgram sender -> recvmsg with msg_name on a PROT_NONE page
    {
        int tx = socket(AF_UNIX, SOCK_DGRAM, 0), rx = socket(AF_UNIX, SOCK_DGRAM, 0);
        bind(tx, (struct sockaddr*)&saddr, sl); bind(rx, (struct sockaddr*)&raddr, rl);
        sendto(tx, pat, 100, 0, (struct sockaddr*)&raddr, rl);
        char buf[128]; memset(buf, 0, sizeof(buf));
        struct iovec iov = { buf, 100 };
        struct msghdr m; memset(&m, 0, sizeof(m));
        m.msg_name = nonepg; m.msg_namelen = 128;
        m.msg_iov = &iov; m.msg_iovlen = 1;
        char ctl[256]; m.msg_control = ctl; m.msg_controllen = sizeof(ctl);
        m.msg_flags = 0x7777;
        errno = 0; r = recvmsg(rx, &m, 0); e = errno;
        printf("N1 bad name: ret=%zd errno=%d(%s) data100=%d namelen=%d flags=0x%x ctllen=%d\n",
               r, r < 0 ? e : 0, r < 0 ? strerror(e) : "-", memcmp(buf, pat, 100) == 0,
               (int)m.msg_namelen, m.msg_flags, (int)m.msg_controllen);
        close(tx); close(rx);
    }
    // N2: same but the sender also passes an fd; name still bad
    {
        int tx = socket(AF_UNIX, SOCK_DGRAM, 0), rx = socket(AF_UNIX, SOCK_DGRAM, 0);
        bind(tx, (struct sockaddr*)&saddr, sl); bind(rx, (struct sockaddr*)&raddr, rl);
        int fd0 = open("/dev/null", O_RDONLY);
        char sctl[64]; memset(sctl, 0, sizeof(sctl));
        struct msghdr sm; memset(&sm, 0, sizeof(sm));
        struct iovec siov = { pat, 16 };
        sm.msg_iov = &siov; sm.msg_iovlen = 1;
        sm.msg_control = sctl; sm.msg_controllen = CMSG_SPACE(sizeof(int));
        struct cmsghdr* c = CMSG_FIRSTHDR(&sm);
        c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c), &fd0, sizeof(int));
        sm.msg_name = (void*)&raddr; sm.msg_namelen = rl;
        sendmsg(tx, &sm, 0);
        char buf[128]; memset(buf, 0, sizeof(buf));
        struct iovec iov = { buf, 16 };
        struct msghdr m; memset(&m, 0, sizeof(m));
        m.msg_name = nonepg; m.msg_namelen = 128;
        m.msg_iov = &iov; m.msg_iovlen = 1;
        char ctl[256]; memset(ctl, 0xEE, sizeof(ctl));
        m.msg_control = ctl; m.msg_controllen = sizeof(ctl);
        errno = 0; r = recvmsg(rx, &m, 0); e = errno;
        int gotfd = -1;
        struct cmsghdr* rc = CMSG_FIRSTHDR(&m);
        if (rc && rc->cmsg_type == SCM_RIGHTS) memcpy(&gotfd, CMSG_DATA(rc), 4);
        int fdok = gotfd >= 0 && fcntl(gotfd, F_GETFD) >= 0;
        printf("N2 bad name + rights: ret=%zd errno=%d(%s) data16=%d namelen=%d ctllen=%d fd_installed=%d\n",
               r, r < 0 ? e : 0, r < 0 ? strerror(e) : "-", memcmp(buf, pat, 16) == 0,
               (int)m.msg_namelen, (int)m.msg_controllen, fdok);
        close(tx); close(rx); close(fd0); if (gotfd >= 0) close(gotfd);
    }
    // N3: control case -- writable name receives the sender address
    {
        int tx = socket(AF_UNIX, SOCK_DGRAM, 0), rx = socket(AF_UNIX, SOCK_DGRAM, 0);
        bind(tx, (struct sockaddr*)&saddr, sl); bind(rx, (struct sockaddr*)&raddr, rl);
        sendto(tx, pat, 16, 0, (struct sockaddr*)&raddr, rl);
        char buf[128]; struct sockaddr_un got; memset(&got, 0, sizeof(got));
        struct iovec iov = { buf, 16 };
        struct msghdr m; memset(&m, 0, sizeof(m));
        m.msg_name = &got; m.msg_namelen = sizeof(got);
        m.msg_iov = &iov; m.msg_iovlen = 1;
        errno = 0; r = recvmsg(rx, &m, 0); e = errno;
        printf("N3 good name: ret=%zd errno=%d namelen=%d name=%s data16=%d\n",
               r, r < 0 ? e : 0, (int)m.msg_namelen,
               (m.msg_namelen > 2 && got.sun_path[0] == 0) ? got.sun_path + 1 : "?",
               memcmp(buf, pat, 16) == 0);
        close(tx); close(rx);
    }
    // N4: unnamed sender (socketpair dgram) + bad name pointer: nothing to copy
    {
        int sv[2]; socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
        send(sv[0], pat, 16, 0);
        char buf[128];
        struct iovec iov = { buf, 16 };
        struct msghdr m; memset(&m, 0, sizeof(m));
        m.msg_name = nonepg; m.msg_namelen = 128;
        m.msg_iov = &iov; m.msg_iovlen = 1;
        errno = 0; r = recvmsg(sv[1], &m, 0); e = errno;
        printf("N4 unnamed sender + bad name: ret=%zd errno=%d(%s) namelen=%d data16=%d\n",
               r, r < 0 ? e : 0, r < 0 ? strerror(e) : "-", (int)m.msg_namelen, memcmp(buf, pat, 16) == 0);
        close(sv[0]); close(sv[1]);
    }
    // N5 (FD-105): recvfrom stream, buffer crossing into an unmapped page
    {
        char* base = mmap(NULL, PS * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        munmap(base + PS, PS);
        int sv[2]; socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        send(sv[0], pat, 200, 0);
        errno = 0; r = recvfrom(sv[1], base + 4000, 200, 0, NULL, NULL); e = errno;
        printf("N5 recvfrom stream crossing: ret=%zd errno=%d(%s) prefix96=%d\n",
               r, r < 0 ? e : 0, r < 0 ? strerror(e) : "-", memcmp(base + 4000, pat, 96) == 0);
        close(sv[0]); close(sv[1]); munmap(base, PS);
    }
    // N6 (FD-105): recvfrom dgram [buf good] but len spans into unmapped tail
    {
        char* base = mmap(NULL, PS * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        munmap(base + PS, PS);
        int sv[2]; socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
        send(sv[0], pat, 100, 0);
        errno = 0; r = recvfrom(sv[1], base + 4050, 100, 0, NULL, NULL); e = errno;
        printf("N6 recvfrom dgram crossing(46 good): ret=%zd errno=%d(%s) prefix46=%d\n",
               r, r < 0 ? e : 0, r < 0 ? strerror(e) : "-", memcmp(base + 4050, pat, 46) == 0);
        close(sv[0]); close(sv[1]); munmap(base, PS);
    }
    munmap(nonepg, PS);
    return 0;
}
