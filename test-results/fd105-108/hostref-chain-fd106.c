// FD-106 measurement: host answers for sendmsg control chains past
// the first cmsg header -- trailing slack shapes, second headers of
// each kind, cumulative rights counts, alignment stepping.
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>

static int recv_fds(int rx) {
    char data[64]; char ctl[8192];
    struct iovec iov = { data, sizeof(data) };
    struct msghdr m; memset(&m, 0, sizeof(m));
    m.msg_iov = &iov; m.msg_iovlen = 1;
    m.msg_control = ctl; m.msg_controllen = sizeof(ctl);
    ssize_t r = recvmsg(rx, &m, MSG_DONTWAIT);
    if (r < 0) return -1;
    int n = 0;
    for (struct cmsghdr* c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c))
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
            n += (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
    return n;
}

// Build a control area from a spec of headers: each entry is
// {level, type, len, nfds}; headers are laid out at CMSG_ALIGN steps.
struct H { int level; int type; size_t len; int nfds; };
static void run(const char* name, struct H* hs, int nh, size_t room, int type) {
    int sv[2]; socketpair(AF_UNIX, type, 0, sv);
    static char ctlbuf[16384]; memset(ctlbuf, 0, sizeof(ctlbuf));
    int allfds[600]; int nall = 0;
    for (int i = 0; i < 600; i++) allfds[i] = open("/dev/null", O_RDONLY);
    size_t off = 0;
    for (int i = 0; i < nh; i++) {
        memcpy(ctlbuf + off + 0, &hs[i].len, sizeof(size_t));
        memcpy(ctlbuf + off + 8, &hs[i].level, 4);
        memcpy(ctlbuf + off + 12, &hs[i].type, 4);
        if (hs[i].type == SCM_RIGHTS) {
            for (int j = 0; j < hs[i].nfds; j++)
                memcpy(ctlbuf + off + 16 + 4 * j, &allfds[nall++], 4);
        } else if (hs[i].type == SCM_CREDENTIALS) {
            int cred[3] = { (int)getpid(), (int)getuid(), (int)getgid() };
            memcpy(ctlbuf + off + 16, cred, 12);
        }
        off += (hs[i].len + 7) & ~(size_t)7;
    }
    char good[16] = "0123456789ABCDEF";
    struct iovec iov = { good, 16 };
    struct msghdr m; memset(&m, 0, sizeof(m));
    m.msg_iov = &iov; m.msg_iovlen = 1;
    m.msg_control = ctlbuf; m.msg_controllen = room;
    errno = 0; ssize_t r = sendmsg(sv[0], &m, 0); int e = errno;
    int arrived = (r >= 0) ? recv_fds(sv[1]) : -2;
    printf("%-44s ret=%zd errno=%d(%s) fds=%d\n", name, r, r < 0 ? e : 0,
           r < 0 ? strerror(e) : "-", arrived);
    for (int i = 0; i < 600; i++) close(allfds[i]);
    close(sv[0]); close(sv[1]);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    { struct H h[] = {{SOL_SOCKET, SCM_RIGHTS, 20, 1}};
      run("K1 rights(1) room=CMSG_SPACE+8 slack8", h, 1, 24 + 8, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_RIGHTS, 20, 1}};
      run("K2 rights(1) room=20+16 slack16zero", h, 1, 36, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_RIGHTS, 20, 1},{SOL_SOCKET, SCM_RIGHTS, 20, 1}};
      run("K3 rights(1)+rights(1)", h, 2, 48, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_RIGHTS, 20, 1},{SOL_SOCKET, SCM_CREDENTIALS, 28, 0}};
      run("K4 rights(1)+creds(28)", h, 2, 24 + 32, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_RIGHTS, 20, 1},{SOL_SOCKET, 999, 16, 0}};
      run("K5 rights(1)+SOL_SOCKET type999", h, 2, 24 + 16, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_CREDENTIALS, 28, 0}};
      run("K6 creds(28) only", h, 1, 32, SOCK_STREAM); }
    { struct H h[] = {{IPPROTO_IP, 0, 16, 0}};
      run("K7 level=IPPROTO_IP len16 only", h, 1, 16, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_RIGHTS, 20, 1},{SOL_SOCKET, SCM_RIGHTS, 8, 0}};
      run("K8 rights(1)+hdr len8", h, 2, 24 + 16, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_RIGHTS, 20, 1},{SOL_SOCKET, SCM_RIGHTS, 4096, 1}};
      run("K9 rights(1)+hdr len4096>room", h, 2, 48, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_RIGHTS, 20, 1},{IPPROTO_IP, 0, 16, 0}};
      run("K10 rights(1)+level IP len16", h, 2, 24 + 16, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_RIGHTS, 16 + 200 * 4, 200},{SOL_SOCKET, SCM_RIGHTS, 16 + 100 * 4, 100}};
      run("K11 rights(200)+rights(100)=300", h, 2, (16 + 200 * 4 + 7 & ~7) + 16 + 100 * 4, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_RIGHTS, 16 + 200 * 4, 200},{SOL_SOCKET, SCM_RIGHTS, 16 + 53 * 4, 53}};
      run("K11b rights(200)+rights(53)=253", h, 2, (16 + 200 * 4 + 7 & ~7) + 16 + 53 * 4, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_RIGHTS, 20, 1},{SOL_SOCKET, SCM_RIGHTS, 18, 0}};
      run("K14 rights(1)+rights len18 (0 fds)", h, 2, 24 + 24, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_CREDENTIALS, 16, 0}};
      run("K13 creds len16 (short)", h, 1, 16, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_RIGHTS, 16, 0},{SOL_SOCKET, SCM_RIGHTS, 20, 1}};
      run("K16 rights(0)+rights(1)", h, 2, 16 + 24, SOCK_STREAM); }
    { struct H h[] = {{SOL_SOCKET, SCM_RIGHTS, 20, 1},{SOL_SOCKET, SCM_RIGHTS, 20, 1}};
      run("K3d rights(1)+rights(1) DGRAM", h, 2, 48, SOCK_DGRAM); }
    return 0;
}
