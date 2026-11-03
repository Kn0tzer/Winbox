// FD-106 measurement, part 2: credential length predicate, and the
// import order -- the host copies the whole control area before it
// validates the chain, so unreadable control bytes answer -EFAULT
// ahead of every -EINVAL the chain would produce.
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <netinet/in.h>

static char good[16] = "0123456789ABCDEF";

static void report(const char* name, ssize_t r, int e) {
    printf("%-46s ret=%zd errno=%d(%s)\n", name, r, r < 0 ? e : 0, r < 0 ? strerror(e) : "-");
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    int sv[2]; ssize_t r; int e;
    // creds length predicate: 20 / 24 / 28 / 32
    for (int clen = 20; clen <= 32; clen += 4) {
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        char ctl[64]; memset(ctl, 0, sizeof(ctl));
        size_t L = (size_t)clen; int lvl = SOL_SOCKET, typ = SCM_CREDENTIALS;
        memcpy(ctl + 0, &L, 8); memcpy(ctl + 8, &lvl, 4); memcpy(ctl + 12, &typ, 4);
        int cred[3] = { (int)getpid(), (int)getuid(), (int)getgid() };
        memcpy(ctl + 16, cred, 12);
        struct iovec iov = { good, 16 };
        struct msghdr m; memset(&m, 0, sizeof(m));
        m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = ctl; m.msg_controllen = 32;
        errno = 0; r = sendmsg(sv[0], &m, 0); e = errno;
        char name[64]; snprintf(name, sizeof(name), "creds len=%d room=32", clen);
        report(name, r, e);
        close(sv[0]); close(sv[1]);
    }
    // K17: valid rights header, controllen reaches into a PROT_NONE page
    {
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        char* pg = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        mprotect(pg + 4096, 4096, PROT_NONE);
        char* ctl = pg + 4096 - 32; // header at ctl, controllen 64 crosses into PROT_NONE at +32
        memset(ctl, 0, 32);
        size_t L = 20; int lvl = SOL_SOCKET, typ = SCM_RIGHTS;
        memcpy(ctl + 0, &L, 8); memcpy(ctl + 8, &lvl, 4); memcpy(ctl + 12, &typ, 4);
        int fd0 = open("/dev/null", O_RDONLY); memcpy(ctl + 16, &fd0, 4);
        struct iovec iov = { good, 16 };
        struct msghdr m; memset(&m, 0, sizeof(m));
        m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = ctl; m.msg_controllen = 64;
        errno = 0; r = sendmsg(sv[0], &m, 0); e = errno;
        report("K17 valid rights, room crosses PROT_NONE", r, e);
        close(fd0); close(sv[0]); close(sv[1]); munmap(pg, 8192);
    }
    // K18: malformed first header (len 8) + later control bytes unreadable
    {
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        char* pg = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        mprotect(pg + 4096, 4096, PROT_NONE);
        char* ctl = pg + 4096 - 32;
        memset(ctl, 0, 32);
        size_t L = 8; int lvl = SOL_SOCKET, typ = SCM_RIGHTS;
        memcpy(ctl + 0, &L, 8); memcpy(ctl + 8, &lvl, 4); memcpy(ctl + 12, &typ, 4);
        struct iovec iov = { good, 16 };
        struct msghdr m; memset(&m, 0, sizeof(m));
        m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = ctl; m.msg_controllen = 64;
        errno = 0; r = sendmsg(sv[0], &m, 0); e = errno;
        report("K18 len8 header + room crosses PROT_NONE", r, e);
        close(sv[0]); close(sv[1]); munmap(pg, 8192);
    }
    // K19: control entirely unreadable, controllen 8 (< header size)
    {
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        char* pg = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        struct iovec iov = { good, 16 };
        struct msghdr m; memset(&m, 0, sizeof(m));
        m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = pg; m.msg_controllen = 8;
        errno = 0; r = sendmsg(sv[0], &m, 0); e = errno;
        report("K19 control PROT_NONE room=8", r, e);
        close(sv[0]); close(sv[1]); munmap(pg, 4096);
    }
    // K20: control entirely unreadable, controllen 64
    {
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        char* pg = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        struct iovec iov = { good, 16 };
        struct msghdr m; memset(&m, 0, sizeof(m));
        m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = pg; m.msg_controllen = 64;
        errno = 0; r = sendmsg(sv[0], &m, 0); e = errno;
        report("K20 control PROT_NONE room=64", r, e);
        close(sv[0]); close(sv[1]); munmap(pg, 4096);
    }
    // K21: first header unknown SOL_SOCKET type alone
    {
        socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        char ctl[32]; memset(ctl, 0, sizeof(ctl));
        size_t L = 16; int lvl = SOL_SOCKET, typ = 999;
        memcpy(ctl + 0, &L, 8); memcpy(ctl + 8, &lvl, 4); memcpy(ctl + 12, &typ, 4);
        struct iovec iov = { good, 16 };
        struct msghdr m; memset(&m, 0, sizeof(m));
        m.msg_iov = &iov; m.msg_iovlen = 1; m.msg_control = ctl; m.msg_controllen = 16;
        errno = 0; r = sendmsg(sv[0], &m, 0); e = errno;
        report("K21 SOL_SOCKET type999 alone", r, e);
        close(sv[0]); close(sv[1]);
    }
    return 0;
}
