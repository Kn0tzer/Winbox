/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"

#include "ksocket.h"
#include "kstat.h"
#include "kscheduler.h"
#include "kunixsocket.h"
#include "knativesocket.h"
#include "knetlink.h"

#include <string.h>
#include <stdio.h>
#include <mutex>
#include <set>
#include <tuple>

static std::mutex g_socketCreateWarningMutex;
static std::set<std::tuple<U32, U32, U32>> g_warnedSocketCreateErrors;

void kwarnSocketCreateFailure(KThread* thread, U32 domain, U32 type, U32 protocol, S32 result, const char* operation) {
    if (result >= 0) return;
    const S32 error = -result;
    if (error != K_EAFNOSUPPORT && error != K_EPROTONOSUPPORT && error != K_EINVAL) return;
    {
        std::lock_guard<std::mutex> lock(g_socketCreateWarningMutex);
        if (!g_warnedSocketCreateErrors.insert(std::make_tuple(domain, type, protocol)).second) return;
    }
    const char* exe = "<unknown>";
    if (thread && thread->process) {
        if (thread->process->exe.length()) exe = thread->process->exe.c_str();
        else if (thread->process->name.length()) exe = thread->process->name.c_str();
    }
    kwarn_fmt("guest %s failed: domain=%u type=0x%x protocol=%u errno=%d exe=%s",
              operation ? operation : "socket", domain, type, protocol, error, exe);
}

BString socketAddressName(KMemory* memory, U32 address, U32 len) {
    if (!address) {
        return B("");
    }
    U16 family = memory->readw(address);
    if (family == K_AF_UNIX) {
        U32 sLen = (U32)memory->strlen(address + 2);
        BString result(sLen + 1, 0);
        memory->memcpy(result.str(), address + 2, sLen+1);
        return result;
    } else if (family == K_AF_NETLINK) {
        BString result;
        result.sprintf("port %u", memory->readd(address + 4));
        return result;
    } else if (family == K_AF_INET) {
        BString result;
        result.sprintf("AF_INET %u.%u.%u.%u:%u", memory->readb(address + 4), memory->readb(address + 5), memory->readb(address + 6), memory->readb(address + 7), memory->readb(address + 3) | (((U32)memory->readb(address + 2)) << 8));
        return result;
    }
    return B("Unknown address family");
}

U32 ksocket(U32 domain, U32 type, U32 protocol) {
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_GUEST_X64)
    // Offline by default. The launcher sets this host-only flag together with
    // a fixed same-origin SOCKFS relay before libc reads the environment.
    // Guest TCP destinations must never become direct host-port WebSockets.
    if (domain != K_AF_UNIX && domain != K_AF_NETLINK) {
        static const bool relayEnabled = [] {
            const char* value = getenv("BW64_NETWORK_RELAY");
            return value && strcmp(value, "1") == 0;
        }();
        const bool guestLoopbackUdp =
            domain == K_AF_INET && type == K_SOCK_DGRAM && (protocol == 0 || protocol == 17);
        if (!guestLoopbackUdp &&
            (!relayEnabled || domain != K_AF_INET || type != K_SOCK_STREAM || (protocol != 0 && protocol != 6))) {
            return -K_EAFNOSUPPORT;
        }
    }
#endif
    if (domain==K_AF_UNIX) {
        std::shared_ptr<KUnixSocketObject> kSocket = std::make_shared<KUnixSocketObject>(domain, type, protocol);
        KFileDescriptorPtr result = KThread::currentThread()->process->allocFileDescriptor(kSocket, K_O_RDWR, 0, -1, 0);
        return result->handle;
    } else if (domain == K_AF_NETLINK) {
        std::shared_ptr<KNetLinkObject> kSocket = std::make_shared<KNetLinkObject>(domain, type, protocol);
        KFileDescriptorPtr result = KThread::currentThread()->process->allocFileDescriptor(kSocket, K_O_RDWR, 0, -1, 0);
        return result->handle;
    } else if (domain == K_AF_INET) {
        std::shared_ptr<KNativeSocketObject> s = std::make_shared<KNativeSocketObject>(domain, type, protocol);
        if (s->error) {
            return s->error;
        } else {
            KFileDescriptorPtr result = KThread::currentThread()->process->allocFileDescriptor(s, K_O_RDWR, 0, -1, 0);
            return result->handle;
        }
    }
    return -K_EAFNOSUPPORT;
}

#define IS_NOT_SOCKET(fd) (fd->kobject->type!=KTYPE_NATIVE_SOCKET && fd->kobject->type!=KTYPE_UNIX_SOCKET)

// Rewrites wineserver socket paths for 32-bit processes to isolate them
// from 64-bit wineserver sockets. Both architectures compute the same
// server dir hash from WINEPREFIX, causing 32-bit Wine to connect to the
// 64-bit wineserver (which never responds to 32-bit protocol).
// For 32-bit: /tmp/wine-XXX/server-UID-HASH/socket -> /tmp/wine-XXX/server32-UID-HASH/socket
// For 64-bit: unchanged
static BString rewriteWineserverSocketPath(const BString& path, bool is64Bit) {
    if (is64Bit) {
        return path;
    }
    int idx = path.indexOf("/server-");
    if (idx < 0) {
        return path;
    }
    BString before = path.substr(0, idx + 7);
    BString after = path.substr(idx + 7);
    BString result = before + B("32") + after;
    return result;
}

static bool patchSocketPathForArch(KThread* thread, U32 address, U32 len, BString& outOriginal, BString& outPatched, U32& outNewLen) {
    outOriginal = B("");
    outPatched = B("");
    outNewLen = len;
    if (!address || len < 3) {
        return false;
    }
    KMemory* memory = thread->memory;
    U16 family = memory->readw(address);
    if (family != K_AF_UNIX) {
        return false;
    }
    U32 pathLen = (U32)memory->strlen(address + 2);
    if (pathLen == 0 || pathLen + 3 > len) {
        return false;
    }
    BString original(pathLen + 1, 0);
    memory->memcpy(original.str(), address + 2, pathLen + 1);
    BString patched = rewriteWineserverSocketPath(original, thread->process->is64Bit);
    if (patched == original) {
        return false;
    }
    if (patched.length() > 107) {
        return false;
    }
    outOriginal = original;
    outPatched = patched;
    outNewLen = 2 + patched.length() + 1;
    memory->memcpy(address + 2, patched.c_str(), patched.length() + 1);
    return true;
}

static void restoreSocketPath(KThread* thread, U32 address, const BString& original) {
    if (original.length() == 0) {
        return;
    }
    thread->memory->memcpy(address + 2, original.c_str(), original.length() + 1);
}

// Wine server protocol REQ id for create_event (from Wine 8.0 server/protocol.def)
#define WINE_REQ_CREATE_EVENT 30

// UTF-16LE bytes for "__wineboot_event" (16 chars * 2 bytes = 32 bytes)
static const U8 winebootEventNameUtf16[] = {
    0x5F, 0x00, 0x5F, 0x00, 0x77, 0x00, 0x69, 0x00,
    0x6E, 0x00, 0x65, 0x00, 0x62, 0x00, 0x6F, 0x00,
    0x6F, 0x00, 0x74, 0x00, 0x5F, 0x00, 0x65, 0x00,
    0x76, 0x00, 0x65, 0x00, 0x6E, 0x00, 0x74, 0x00
};

// Intercepts wineserver create_event requests for __wineboot_event in 32-bit
// processes and sets initial_state=1 so the event starts signaled.
// This unblocks 32-bit Wine threads waiting on the event, bypassing the need
// for a working 32-bit PE to signal it (32-bit PE execution is broken).
// Only affects 32-bit processes connected to wineserver32.
// Request layout (Wine 8.0): header(12) + access(4)@12 + manual_reset(4)@16
//   + initial_state(4)@20 + objattr(vararg)@24+
static void patchWinebootEventCreate(KThread* thread, std::shared_ptr<KSocketObject> s, U32 address) {
    // Only for 32-bit processes (64-bit wineboot signals the event itself)
    if (thread->process->is64Bit) {
        return;
    }
    // Only for wineserver32 sockets
    if (!strstr(s->destAddress.data, "server32-")) {
        return;
    }
    KMemory* memory = thread->memory;
    // Parse msghdr: msg_iov at +8, msg_iovlen at +12
    U32 iovAddr = memory->readd(address + 8);
    U32 iovLen = memory->readd(address + 12);
    if (!iovAddr || !iovLen || iovLen > 16) {
        return;
    }
    // Read first iovec (base + len)
    U32 firstBase = memory->readd(iovAddr);
    U32 firstLen = memory->readd(iovAddr + 4);
    if (!firstBase || firstLen < 24) {
        return;
    }
    // Check REQ id == create_event (30)
    U32 reqId = memory->readd(firstBase);
    if (reqId != WINE_REQ_CREATE_EVENT) {
        return;
    }
    // Scan iovec data for UTF-16LE "__wineboot_event" (up to 512 bytes)
    U8 scanBuf[512];
    U32 scanLen = 0;
    for (U32 i = 0; i < iovLen && scanLen < sizeof(scanBuf); i++) {
        U32 base = memory->readd(iovAddr + i * 8);
        U32 len = memory->readd(iovAddr + i * 8 + 4);
        if (!base || !len) continue;
        U32 toCopy = len;
        if (scanLen + toCopy > sizeof(scanBuf)) {
            toCopy = sizeof(scanBuf) - scanLen;
        }
        memory->memcpy(scanBuf + scanLen, base, toCopy);
        scanLen += toCopy;
    }
    bool found = false;
    for (U32 i = 0; i + sizeof(winebootEventNameUtf16) <= scanLen; i++) {
        if (memcmp(scanBuf + i, winebootEventNameUtf16, sizeof(winebootEventNameUtf16)) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        return;
    }
    // Set initial_state=1 at offset 20 from request start
    memory->writed(firstBase + 20, 1);
    klog("ksocket: patched __wineboot_event create_event initial_state=1 (32-bit)");
}


U32 kbind(KThread* thread, U32 socket, U32 address, U32 len) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    // Isolate 32-bit wineserver sockets from 64-bit ones (see rewriteWineserverSocketPath)
    BString origPath, patchedPath;
    U32 newLen = len;
    if (patchSocketPathForArch(thread, address, len, origPath, patchedPath, newLen)) {
        U32 result = s->bind(thread, fd, address, newLen);
        restoreSocketPath(thread, address, origPath);
        return result;
    }
    return s->bind(thread, fd, address, len);
}

U32 kconnect(KThread* thread, U32 socket, U32 address, U32 len) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    if (s->connected) {
        return -K_EISCONN;
    }
    // Isolate 32-bit wineserver sockets from 64-bit ones (see rewriteWineserverSocketPath)
    BString origPath, patchedPath;
    U32 newLen = len;
    if (patchSocketPathForArch(thread, address, len, origPath, patchedPath, newLen)) {
        U32 result = s->connect(thread, fd, address, newLen);
        restoreSocketPath(thread, address, origPath);
        return result;
    }
    return s->connect(thread, fd, address, len);
}

U32 klisten(KThread* thread, U32 socket, U32 backlog) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    return s->listen(thread, fd, backlog);
}

U32 kaccept(KThread* thread, U32 socket, U32 address, U32 len, U32 flags) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    if (!s->listening) {
        return -K_EINVAL;
    }
    return s->accept(thread, fd, address, len, flags);
}

U32 kgetsockname(KThread* thread, U32 socket, U32 address, U32 plen) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    return s->getsockname(thread, fd, address, plen);
}

U32 kgetpeername(KThread* thread, U32 socket, U32 address, U32 plen) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    return s->getpeername(thread, fd, address, plen);
}

// Create a connected AF_UNIX socket pair and return the two guest FDs via
// outFd1/outFd2 — WITHOUT touching guest memory. Both the 32-bit ksocketpair
// (writes the FDs into 32-bit KMemory) and the 64-bit path (writes into
// KMemory64) share this so the socket-object wiring lives in one place.
S32 ksocketpairFds(KThread* thread, U32 af, U32 type, U32 protocol, U32 flags, FD& outFd1, FD& outFd2) {
    FD fd1 = 0;
    FD fd2 = 0;
    KFileDescriptorPtr f1;
    KFileDescriptorPtr f2;
    std::shared_ptr<KUnixSocketObject> s1;
    std::shared_ptr<KUnixSocketObject> s2;

    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(thread->process->fdsMutex);

    if (af!=K_AF_UNIX) {
        return -K_EAFNOSUPPORT;
    }
    if (type!=K_SOCK_DGRAM && type!=K_SOCK_STREAM) {
        return -K_EINVAL;
    }
    fd1 = ksocket(af, type, protocol);
    fd2 = ksocket(af, type, protocol);
    f1 = thread->process->getFileDescriptor(fd1);
    f2 = thread->process->getFileDescriptor(fd2);
    s1 = std::dynamic_pointer_cast<KUnixSocketObject>(f1->kobject);
    s2 = std::dynamic_pointer_cast<KUnixSocketObject>(f2->kobject);

    s1->connection = s2;
    s2->connection = s1;
    s1->connected = true;
    s2->connected = true;
    f1->accessFlags = K_O_RDWR;
    f2->accessFlags = K_O_RDWR;

    if ((flags & K_O_CLOEXEC)!=0) {
        thread->process->fcntrl(thread, fd1, K_F_SETFD, FD_CLOEXEC);
        thread->process->fcntrl(thread, fd2, K_F_SETFD, FD_CLOEXEC);
    }
    if ((flags & K_O_NONBLOCK)!=0) {
        thread->process->fcntrl(thread, fd1, K_F_SETFL, K_O_NONBLOCK);
        thread->process->fcntrl(thread, fd2, K_F_SETFL, K_O_NONBLOCK);
    }
    if (flags & ~(K_O_CLOEXEC|K_O_NONBLOCK)) {
        kwarn_fmt("Unknow flags sent to pipe2: %X", flags);
    }
    outFd1 = fd1;
    outFd2 = fd2;
    return 0;
}

// 32-bit socketpair: build the pair, then write the two FDs into 32-bit guest
// memory at `socks`.
U32 ksocketpair(KThread* thread, U32 af, U32 type, U32 protocol, U32 socks, U32 flags) {
    FD fd1 = 0, fd2 = 0;
    S32 rc = ksocketpairFds(thread, af, type, protocol, flags, fd1, fd2);
    if (rc) return (U32)rc;
    thread->memory->writed(socks, fd1);
    thread->memory->writed(socks + 4, fd2);
    return 0;
}

U32 ksend(KThread* thread, U32 socket, U32 buffer, U32 len, U32 flags) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    return s->sendto(thread, fd, buffer, len, flags, 0, 0);
}

U32 krecv(KThread* thread, U32 socket, U32 buffer, U32 len, U32 flags) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    return s->recvfrom(thread, fd, buffer, len, flags, 0, 0);
}

U32 kshutdown(KThread* thread, U32 socket, U32 how) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    return s->shutdown(thread, fd, how);
}

U32 ksetsockopt(KThread* thread, U32 socket, U32 level, U32 name, U32 value, U32 len) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    return s->setsockopt(thread, fd, level, name, value, len);
}

U32 kgetsockopt(KThread* thread, U32 socket, U32 level, U32 name, U32 value, U32 len_address) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    return s->getsockopt(thread, fd, level, name, value, len_address);
}

#define K_SOL_SOCKET 1
#define K_SCM_RIGHTS 1

U32 ksendmmsg(KThread* thread, U32 socket, U32 address, U32 vlen, U32 flags) {
    U32 i;

    for (i=0;i<vlen;i++) {
        S32 result = (S32)ksendmsg(thread, socket, address+i*32, flags);
        if (result>=0) {
            thread->memory->writed(address+i*32+28, result);
        } else {
            return i;
        }
    }
    return vlen;
}

U32 ksendmsg(KThread* thread, U32 socket, U32 address, U32 flags) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    // Optimization: only check for wineboot event patch on 32-bit.
    // 64-bit processes never need this (wineboot signals the event itself).
    // This avoids the function call overhead on every 64-bit sendmsg.
    if (!thread->process->is64Bit) {
        patchWinebootEventCreate(thread, s, address);
    }
    return s->sendmsg(thread, fd, address, flags);
}

U32 krecvmsg(KThread* thread, U32 socket, U32 address, U32 flags) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    return s->recvmsg(thread, fd, address, flags);
}

// ssize_t sendto(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
U32 ksendto(KThread* thread, U32 socket, U32 message, U32 length, U32 flags, U32 dest_addr, U32 dest_len) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    return s->sendto(thread, fd, message, length, flags, dest_addr, dest_len);
}

// ssize_t recvfrom(int socket, void *restrict buffer, size_t length, int flags, struct sockaddr *restrict address, socklen_t *restrict address_len);
U32 krecvfrom(KThread* thread, U32 socket, U32 buffer, U32 length, U32 flags, U32 address, U32 address_len) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(socket);

    if (!fd) {
        return -K_EBADF;
    }
    if (IS_NOT_SOCKET(fd)) {
        return -K_ENOTSOCK;
    }
    std::shared_ptr<KSocketObject> s = std::dynamic_pointer_cast<KSocketObject>(fd->kobject);
    return s->recvfrom(thread, fd, buffer, length, flags, address, address_len);
}

bool isNativeSocket(KThread* thread, FD desc) {
    KFileDescriptorPtr fd = thread->process->getFileDescriptor(desc);

    if (!fd) {
        return false;
    }
    if (fd->kobject->type == KTYPE_NATIVE_SOCKET) {
        return true;
    }
    return false;
}
