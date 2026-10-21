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

#include "knativesocket.h"
#include "ksocket.h"
#include "kstat.h"
#include "bufferaccess.h"
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <map>

#ifdef WIN32
#undef BOOL
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
static int winsock_intialized;
#define BOOL unsigned int
#pragma comment(lib, "Ws2_32.lib")
#undef min
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#ifdef __MACH__
#include <ifaddrs.h>
#include <net/if_dl.h>
#endif

void closesocket(int socket) { close(socket); }

#if !defined(TCP_KEEPIDLE) && defined(TCP_KEEPALIVE)
#define TCP_KEEPIDLE TCP_KEEPALIVE
#endif

#endif

namespace {
std::mutex g_nativeSocketPriorityMutex;
std::unordered_map<const KNativeSocketObject*, U32> g_nativeSocketPriorities;

void setNativeSocketPriority(const KNativeSocketObject* socket, U32 priority) {
    std::lock_guard<std::mutex> lock(g_nativeSocketPriorityMutex);
    g_nativeSocketPriorities[socket] = priority;
}

U32 getNativeSocketPriority(const KNativeSocketObject* socket) {
    std::lock_guard<std::mutex> lock(g_nativeSocketPriorityMutex);
    auto it = g_nativeSocketPriorities.find(socket);
    return it == g_nativeSocketPriorities.end() ? 0 : it->second;
}

void eraseNativeSocketPriority(const KNativeSocketObject* socket) {
    std::lock_guard<std::mutex> lock(g_nativeSocketPriorityMutex);
    g_nativeSocketPriorities.erase(socket);
}

std::mutex g_loopbackUdpMutex;
std::map<U16, std::weak_ptr<KNativeSocketObject>> g_loopbackUdpSockets;
U16 g_nextLoopbackUdpPort = 49152;
std::mutex g_loopbackTcpMutex;
std::map<U16, std::weak_ptr<KNativeSocketObject>> g_loopbackTcpSockets;
U16 g_nextLoopbackTcpPort = 32768;

bool isGuestLoopbackAddress(U32 address) {
    return address == 0 || (address >> 24) == 127;
}

bool browserNetworkRelayEnabled() {
    const char* value = getenv("BW64_NETWORK_RELAY");
    return value && strcmp(value, "1") == 0;
}
}

#define K_SIOCGIFHWADDR 0x8927
#define K_SIOCGIFCONF 0x8912
#define K_SIOCGIFFLAGS 0x8913
#define K_SIOCGIFMTU 0x8921

std::vector<std::shared_ptr<KNativeSocketObject>> waitingNativeSockets;
fd_set waitingReadset;
fd_set waitingWriteset;
fd_set waitingErrorset;
int maxSocketId;

BString socketAddressName(KMemory* memory, U32 address, U32 len);
#ifdef _DEBUG
#define LOG_SOCK if (0) klog_fmt
#else
#define LOG_SOCK if (0) klog_fmt
#endif

#ifdef BOXEDWINE_MULTI_THREADED
#include "knativethread.h"
static KNativeThread* checkWaitingNativeSocketsThread;
static BOXEDWINE_MUTEX checkWaitingNativeSocketsThreadMutex;
static BOXEDWINE_MUTEX waitingNodeMutex;
static std::atomic_bool checkWaitingNativeSocketsThreadDone{false};
static S32 nativeSocketPipe[2] = {-1, -1};
#endif

void updateWaitingList() {
    FD_ZERO(&waitingReadset);
    FD_ZERO(&waitingWriteset);
    FD_ZERO(&waitingErrorset);
    
#ifdef BOXEDWINE_MULTI_THREADED
    maxSocketId = nativeSocketPipe[0];
    FD_SET(nativeSocketPipe[0], &waitingReadset);
#else
    maxSocketId = 0;
#endif
    for (auto& s : waitingNativeSockets) {
        bool errorSet = false;
#ifndef BOXEDWINE_MSVC
        if (s->nativeSocket>=FD_SETSIZE) {
            kpanic_fmt("updateWaitingList %s socket is too large to select on", s->nativeSocket);
        }
#endif
        if (s->readingCond->parentsCount()) {
            FD_SET(s->nativeSocket, &waitingReadset);
            FD_SET(s->nativeSocket, &waitingErrorset);
            errorSet = true;
        }
        if (s->writingCond->parentsCount()) {
            FD_SET(s->nativeSocket, &waitingWriteset);
            if (!errorSet)
                FD_SET(s->nativeSocket, &waitingErrorset);
        }
        if (s->nativeSocket>maxSocketId)
            maxSocketId = s->nativeSocket;
    }
}

bool checkWaitingNativeSockets(int timeout) {
    struct timeval t;
    t.tv_sec = timeout >= 0 ? timeout / 1000 : 0;
    t.tv_usec = timeout >= 0 ? (timeout % 1000) * 1000 : 0;

#ifdef BOXEDWINE_MULTI_THREADED
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(waitingNodeMutex);
        updateWaitingList();
    }
    {
#else
    if (waitingNativeSockets.size()) {
        updateWaitingList();
        if (maxSocketId==0)
            return false;
#endif           
        int result = select(maxSocketId + 1, &waitingReadset, &waitingWriteset, &waitingErrorset, (timeout>=0?&t: nullptr));
        if (result > 0) {
#ifdef BOXEDWINE_MULTI_THREADED
            if (FD_ISSET(nativeSocketPipe[0], &waitingReadset)) {
                char buf = 0;
#ifdef __EMSCRIPTEN__
                ::read(nativeSocketPipe[0], &buf, 1);
#else
                ::recv(nativeSocketPipe[0], &buf, 1, 0);
#endif
                return true;
            }
#endif
            BOXEDWINE_CONDITION conditions[1024] = { };
            U32 conditionCount = 0;
            {
                BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(waitingNodeMutex);                

                for (auto& s : waitingNativeSockets) {
                    if ((FD_ISSET(s->nativeSocket, &waitingReadset) || FD_ISSET(s->nativeSocket, &waitingErrorset)) && s->readingCond->waitCount()) {
                        if (conditionCount<1024) conditions[conditionCount++]=s->readingCond;
                    }
                    if ((FD_ISSET(s->nativeSocket, &waitingWriteset) || FD_ISSET(s->nativeSocket, &waitingErrorset)) && s->writingCond->waitCount()) {
                        if (conditionCount<1024) conditions[conditionCount++]=s->writingCond;
                    }
                }
            }
            for (U32 i=0;i<conditionCount;i++) {
                BOXEDWINE_CONDITION& c = conditions[i];
                BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(c);
                BOXEDWINE_CONDITION_SIGNAL_ALL(c);
            }
        }
        return true;
    }
    return false;
}

void setNativeBlocking(int nativeSocket, bool blocking) {
#ifdef WIN32
    u_long mode = blocking?0:1;
    ioctlsocket(nativeSocket, FIONBIO, &mode);           
#else
    if (blocking)
        fcntl(nativeSocket, F_SETFL, fcntl(nativeSocket, F_GETFL, 0) & ~O_NONBLOCK);
    else
        fcntl(nativeSocket, F_SETFL, fcntl(nativeSocket, F_GETFL, 0) | O_NONBLOCK);
#endif
}

#ifdef BOXEDWINE_MULTI_THREADED
static int checkWaitingNativeSockets_thread(void *ptr) {
    while(!checkWaitingNativeSocketsThreadDone) {
        checkWaitingNativeSockets(1000);
    }
    return 0;
}

void startNativeSocketsThread() {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(checkWaitingNativeSocketsThreadMutex);
    if (!checkWaitingNativeSocketsThread) {
#ifdef __EMSCRIPTEN__
        // Emscripten has PIPEFS wakeups but its socketpair syscall is ENOSYS.
        // Leaving the zero-initialized pair in place repeatedly polls stdin
        // and skips every real socket's readiness notification.
        if (::pipe(nativeSocketPipe) != 0) {
            kpanic("Failed to create native socket monitor pipe");
        }
        if (nativeSocketPipe[0] >= FD_SETSIZE) {
            ::close(nativeSocketPipe[0]);
            ::close(nativeSocketPipe[1]);
            nativeSocketPipe[0] = nativeSocketPipe[1] = -1;
            kpanic("Native socket monitor pipe exceeds select descriptor limit");
        }
#else
        Platform::nativeSocketPair(nativeSocketPipe);
#endif
        setNativeBlocking(nativeSocketPipe[0], false);
        setNativeBlocking(nativeSocketPipe[1], false);
        checkWaitingNativeSocketsThread = KNativeThread::createAndStartThread(checkWaitingNativeSockets_thread, B("NativeSockeThread"), (void *)nullptr);
    }    
}

void stopNativeSocketsThread() {
    // KSystem::destroy joins every guest thread before stopping this monitor.
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(checkWaitingNativeSocketsThreadMutex);
    if (checkWaitingNativeSocketsThread) {
        checkWaitingNativeSocketsThreadDone = true;
        char buf = 0;
#ifdef __EMSCRIPTEN__
        if (nativeSocketPipe[1] >= 0) ::write(nativeSocketPipe[1], &buf, 1);
#else
        ::send(nativeSocketPipe[1], &buf, 1, 0);
#endif
        checkWaitingNativeSocketsThread->wait();
#ifdef __EMSCRIPTEN__
        ::close(nativeSocketPipe[0]);
        ::close(nativeSocketPipe[1]);
        nativeSocketPipe[0] = nativeSocketPipe[1] = -1;
#endif
        checkWaitingNativeSocketsThreadDone = false;
        checkWaitingNativeSocketsThread = nullptr;
    }
}
#endif

void addWaitingNativeSocket(const std::shared_ptr<KNativeSocketObject>& s) {
#ifdef BOXEDWINE_MULTI_THREADED
    const std::lock_guard<std::recursive_mutex> lifecycleLock(checkWaitingNativeSocketsThreadMutex);
#endif
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(waitingNodeMutex);
    for (auto& waitingSocket : waitingNativeSockets) {
        if (waitingSocket->nativeSocket == s->nativeSocket) {
            return;
        }
    }

    waitingNativeSockets.push_back(s);
#ifdef BOXEDWINE_MULTI_THREADED
    startNativeSocketsThread();
    char buf = 2;
#ifdef __EMSCRIPTEN__
    if (nativeSocketPipe[1] >= 0) ::write(nativeSocketPipe[1], &buf, 1);
#else
    ::send(nativeSocketPipe[1], &buf, 1, 0);
#endif
#endif
}

void removeWaitingSocket(S32 nativeSocket) {
#ifdef BOXEDWINE_MULTI_THREADED
    const std::lock_guard<std::recursive_mutex> lifecycleLock(checkWaitingNativeSocketsThreadMutex);
#endif
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(waitingNodeMutex);
        for (int i = 0; i < (int)waitingNativeSockets.size(); i++) {
            if (waitingNativeSockets[i]->nativeSocket == nativeSocket) {
                waitingNativeSockets.erase(waitingNativeSockets.begin() + i);
                break;
            }
        }
    }
#ifdef BOXEDWINE_MULTI_THREADED
    char buf = 3;
#ifdef __EMSCRIPTEN__
    if (nativeSocketPipe[1] >= 0) ::write(nativeSocketPipe[1], &buf, 1);
#else
    ::send(nativeSocketPipe[1], &buf, 1, 0);
#endif
#endif
}

S32 translateNativeSocketError(S32 nativeSocket, int error) {
    S32 result = 0;
#ifdef WIN32
    if (error == WSAEMFILE) return -K_EMFILE;
    if (error == WSAENOBUFS) return -K_ENOBUFS;
    if (error == WSAEALREADY) return -K_EALREADY;
    if (error == WSAENOTCONN) {
        result = -K_ENOTCONN;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "ENOTCONN", result);
    }
    else if (error == WSAEWOULDBLOCK) {
        result = -K_EWOULDBLOCK;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EWOULDBLOCK", result);
    }
    else if (error == WSAETIMEDOUT) {
        result = -K_ETIMEDOUT;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "ETIMEDOUT", result);
    }
    else if (error == WSAECONNRESET) {
        result = -K_ECONNRESET;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "ECONNRESET", result);
    }
    else if (error == WSAEDESTADDRREQ) {
        result = -K_EDESTADDRREQ;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EDESTADDRREQ", result);
    }
    else if (error == WSAEHOSTUNREACH) {
        result = -K_EHOSTUNREACH;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EHOSTUNREACH", result);
    }
    else if (error == WSAECONNREFUSED) {
        result = -K_ECONNREFUSED;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "ECONNREFUSED", result);
    }
    else if (error == WSAEISCONN) {
        result = -K_EISCONN;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EISCONN", result);
    }
    else if (error == WSAEMSGSIZE) {
        result = -K_EMSGSIZE;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EMSGSIZE", result);
    }
    else {
        result = -K_EIO;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EIO", result);
    }

#else
    if (error == EMFILE) return -K_EMFILE;
    if (error == ENFILE) return -K_ENFILE;
    if (error == ENOMEM) return -K_ENOMEM;
    if (error == ENOBUFS) return -K_ENOBUFS;
    if (error == EALREADY) return -K_EALREADY;
    if (error == ENOTCONN) {
        result = -K_ENOTCONN;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "ENOTCONN", error);
    }
    else if (error == EWOULDBLOCK) {
        result = -K_EWOULDBLOCK;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EWOULDBLOCK", error);
    }
    else if (error == ETIMEDOUT) {
        result = -K_ETIMEDOUT;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "ETIMEDOUT", error);
    }
    else if (error == ECONNRESET) {
        result = -K_ECONNRESET;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "ECONNRESET", error);
    }
    else if (error == EDESTADDRREQ) {
        result = -K_EDESTADDRREQ;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EDESTADDRREQ", error);
    }
    else if (error == EHOSTUNREACH) {
        result = -K_EHOSTUNREACH;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EHOSTUNREACH", error);
    }
    else if (error == EISCONN) {
        result = -K_EISCONN;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EISCONN", error);
    }
    else if (error == ECONNREFUSED) {
        result = -K_ECONNREFUSED;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "ECONNREFUSED", error);
    } else if (error == EMSGSIZE) {
        result = -K_EMSGSIZE;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EMSGSIZE", error);
    } else if (error == EAFNOSUPPORT) {
        result = -K_EAFNOSUPPORT;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EAFNOSUPPORT", error);
    } else if (error == EINPROGRESS) {
        result = -K_EINPROGRESS;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EINPROGRESS", error);
    } else if (error == EPERM) {
        result = -K_EPERM;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "EPERM", error);
    } else {
        result = -K_EIO;
        LOG_SOCK("  native socket: %x error %s(%x)", nativeSocket, "UKNOWN/EIO", error);
    }
#endif
    return result;
}

S32 handleNativeSocketError(const std::shared_ptr<KNativeSocketObject>& s, bool write) {
#ifdef WIN32
    S32 result = translateNativeSocketError(s->nativeSocket, WSAGetLastError());
#else
    S32 result = translateNativeSocketError(s->nativeSocket, errno);
#endif
    s->error = -result;
#ifndef BOXEDWINE_MULTI_THREADED
    if (result == -K_EWOULDBLOCK) {
        addWaitingNativeSocket(s);
        if (s->blocking) {
            if (write) {
                BOXEDWINE_CONDITION_LOCK(s->writingCond);
                BOXEDWINE_CONDITION_WAIT(s->writingCond);
                BOXEDWINE_CONDITION_UNLOCK(s->writingCond);
            } else {
                BOXEDWINE_CONDITION_LOCK(s->readingCond);
                BOXEDWINE_CONDITION_WAIT(s->readingCond);
                BOXEDWINE_CONDITION_UNLOCK(s->readingCond);
            }
        }
    }    
#endif
    return result;
}

KNativeSocketObject::KNativeSocketObject(U32 domain, U32 type, U32 protocol) : KSocketObject(KTYPE_NATIVE_SOCKET, domain, type, protocol), 
    connecting(false),
    readingCond(std::make_shared<BoxedWineCondition>(B("KNativeSocketObject::readingCond"))),
    writingCond(std::make_shared<BoxedWineCondition>(B("KNativeSocketObject::writingCond"))) {
#ifdef WIN32
    if (!winsock_intialized) {
        WSADATA wsaData;
        static_cast<void>(WSAStartup(0x0202, &wsaData));
        winsock_intialized=1;
    }
#endif
    U32 nativeType = 0;
    U32 nativeProtocol = 0;

    this->nativeSocket = -1;

    if (type == K_SOCK_STREAM) {
        nativeType = SOCK_STREAM;
    } else if (type == K_SOCK_DGRAM) {
        nativeType = SOCK_DGRAM;
    } else if (type == K_SOCK_RAW) {
        nativeType = SOCK_RAW;
    } else if (type == K_SOCK_RDM) {
        nativeType = SOCK_RDM;
    } else if (type == K_SOCK_SEQPACKET) {
        nativeType = SOCK_SEQPACKET;
    } else {
        this->error = -K_EPROTOTYPE;
        return;
    }
    if (protocol == 0) {
        nativeProtocol = IPPROTO_IP ;
    } else if (protocol == 1) {
        nativeProtocol = IPPROTO_ICMP;
    } else if (protocol == 2) {
        nativeProtocol = IPPROTO_IGMP;
    } else if (protocol == 6) {
        nativeProtocol = IPPROTO_TCP;
    } else if (protocol == 12) {
        nativeProtocol = IPPROTO_PUP;
    } else if (protocol == 17) {
        nativeProtocol = IPPROTO_UDP;
    } else if (protocol == 22) {
        nativeProtocol = IPPROTO_IDP;
    } else if (protocol == 255) {
        nativeProtocol = IPPROTO_RAW;
    } else {
        this->error = -K_EPROTOTYPE;
        nativeProtocol = IPPROTO_IP;
    }
#if defined(__EMSCRIPTEN__) && defined(BOXEDWINE_GUEST_X64)
    if (domain == K_AF_INET && type == K_SOCK_DGRAM && (protocol == 0 || protocol == 17)) {
        this->loopbackDatagram = true;
        return;
    }
    if (domain == K_AF_INET && type == K_SOCK_STREAM && (protocol == 0 || protocol == 6)) {
        this->loopbackStream = true;
        if (!browserNetworkRelayEnabled()) return;
    }
#endif
    this->nativeSocket = (S32)socket(AF_INET, nativeType, nativeProtocol);
    if (this->nativeSocket < 0) {
#ifdef WIN32
        this->error = translateNativeSocketError(this->nativeSocket, WSAGetLastError());
#else
        this->error = translateNativeSocketError(this->nativeSocket, errno);
#endif
        return;
    }
#ifndef BOXEDWINE_MSVC
    if (this->nativeSocket >= FD_SETSIZE) {
        closesocket(this->nativeSocket);
        this->nativeSocket = -1;
        this->error = -K_EMFILE;
        return;
    }
#endif
    LOG_SOCK("%x native socket: %x open nativeType=%x nativeProtocol=%x", KThread::currentThread()->id, nativeSocket, nativeType, nativeProtocol);
#ifndef BOXEDWINE_MULTI_THREADED
    setNativeBlocking(this->nativeSocket, false);
#endif
}

KNativeSocketObject::~KNativeSocketObject() {
    eraseNativeSocketPriority(this);
    if (this->loopbackStream) this->closeLoopbackStream();
    if (this->loopbackStream && this->loopbackPort) {
        std::lock_guard<std::mutex> lock(g_loopbackTcpMutex);
        auto it = g_loopbackTcpSockets.find(this->loopbackPort);
        if (it != g_loopbackTcpSockets.end()) {
            std::shared_ptr<KNativeSocketObject> registered = it->second.lock();
            if (!registered || registered.get() == this) g_loopbackTcpSockets.erase(it);
        }
    }
    if (this->loopbackDatagram && this->loopbackPort) {
        std::lock_guard<std::mutex> lock(g_loopbackUdpMutex);
        g_loopbackUdpSockets.erase(this->loopbackPort);
    }
    LOG_SOCK("native socket: %x close", nativeSocket);
    if (this->nativeSocket >= 0) {
        closesocket(this->nativeSocket);
        removeWaitingSocket(this->nativeSocket);
    }
    this->nativeSocket = 0;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->readingCond);
        BOXEDWINE_CONDITION_SIGNAL_ALL(this->readingCond);
    }
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->writingCond);
        BOXEDWINE_CONDITION_SIGNAL_ALL(this->writingCond);
    }
}

#ifdef WIN32
PIP_ADAPTER_ADDRESSES getAdapterAddresses() {
    DWORD rv, size;
    PIP_ADAPTER_ADDRESSES adapter_addresses;

    rv = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, NULL, &size);
    if (rv != ERROR_BUFFER_OVERFLOW) {
        fprintf(stderr, "GetAdaptersAddresses() failed...");
        return nullptr;
    }
    adapter_addresses = (PIP_ADAPTER_ADDRESSES)malloc(size);

    rv = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, NULL, adapter_addresses, &size);
    if (rv != ERROR_SUCCESS) {
        fprintf(stderr, "GetAdaptersAddresses() failed...");
        free(adapter_addresses);
        return nullptr;
    }
    return adapter_addresses;
}

PIP_ADAPTER_ADDRESSES getAdapterAddress(const BString& name) {
    static PIP_ADAPTER_ADDRESSES addresses;
    
    if (!addresses) {
        addresses = getAdapterAddresses();
    }
    if (!addresses) {
        return nullptr;
    }
    if (name == "lo") {
        PIP_ADAPTER_ADDRESSES address = addresses;
        while (address) {
            if (address->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                return address;
            }
            address = address->Next;
        }
    }
    return addresses;
}

#endif 
U16 emulatedFamilyFromHostFamily(U16 family) {
    switch (family) {
    case AF_INET:
        return K_AF_INET;
    case AF_INET6:
        return K_AF_INET6;
    default:
        return family; // hope for the best
    }
}

/*
struct sockaddr {
    short family;
    char data[14];
}

struct ifreq {
    char ifr_name[IFNAMSIZ]; // Interface name 
    union {
        struct sockaddr ifr_addr;
        struct sockaddr ifr_dstaddr;
        struct sockaddr ifr_broadaddr;
        struct sockaddr ifr_netmask;
        struct sockaddr ifr_hwaddr;
        short           ifr_flags;
        int             ifr_ifindex;
        int             ifr_metric;
        int             ifr_mtu;
        struct ifmap    ifr_map;
        char            ifr_slave[IFNAMSIZ];
        char            ifr_newname[IFNAMSIZ];
        char* ifr_data;
    };
};

struct ifconf {
    int                 ifc_len; // size of buffer 
    union {
        char* ifc_buf; // buffer address 
        struct ifreq* ifc_req; // array of structures 
    };
};
*/
#define K_IFNAMSIZ 16

U32 KNativeSocketObject::ioctl(KThread* thread, U32 request) {
    CPU* cpu = thread->cpu;
    if (request == 0x541b) {        
#ifdef WIN32
        u_long value=0;
        U32 result = ioctlsocket(this->nativeSocket, FIONREAD, &value);
#else        
        int value=0;
        U32 result = ::ioctl(this->nativeSocket, FIONREAD, &value);        
#endif
        LOG_SOCK("  native socket: %x FIONREAD %d", nativeSocket, (int)value);
        if (result != 0) {
            std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
            return handleNativeSocketError(t, true);
        }        
        this->error = 0;
        thread->memory->writed(IOCTL_ARG1, value);
        return 0;
    } else if (request == K_SIOCGIFCONF) {
        U32 address = IOCTL_ARG1;
        if (!address) {
            return -K_EFAULT;
        }
#ifdef WIN32
        INTERFACE_INFO interfaces[20];
        unsigned long bytes;

        if (this->nativeSocket == SOCKET_ERROR) {
            return -1;
        }

        if (WSAIoctl(this->nativeSocket, SIO_GET_INTERFACE_LIST, 0, 0, &interfaces, sizeof(interfaces), &bytes, 0, 0) == SOCKET_ERROR) {
            std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
            return handleNativeSocketError(t, false);
        }

        U32 count = bytes / sizeof(INTERFACE_INFO);
        thread->memory->writed(address, count * 40);
        U32 buf = thread->memory->readd(address + 4);
        for (U32 i = 0; i < count; i++) {
            struct sockaddr_in* addr = (struct sockaddr_in*)&(interfaces[i].iiAddress);
            if (addr->sin_family == AF_INET && (interfaces[i].iiFlags & IFF_LOOPBACK)) {
                thread->memory->memcpy(buf + 40 * i, "lo", 3);
            } else {
                BString s;
                s += "eth";
                s += i;
                thread->memory->memcpy(buf + 40 * i, s.c_str(), s.length() + 1);
            }
            thread->memory->memcpy(buf + 40 * i + 16, addr, 16); // 16 sizeof addr            
        }
        this->error = 0;
        return 0;
#else        
        U32 numifs = 64;

#ifdef SIOCGIFNUM
        ::ioctl(this->nativeSocket, SIOCGIFNUM, (char*)&numifs);
#endif
        struct ifreq* ifs = new struct ifreq[numifs];
        struct ifconf ifconf = { 0 };

        ifconf.ifc_buf = (char*)(ifs);
        ifconf.ifc_len = sizeof(struct ifreq) * numifs;

        U32 result = ::ioctl(this->nativeSocket, SIOCGIFCONF, &ifconf);
        if (result == 0xffffffff) {
            std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
            return handleNativeSocketError(t, false);
        }
        thread->memory->writed(address, ifconf.ifc_len);
        thread->memory->memcpy(thread->memory->readd(address + 4), ifs, ifconf.ifc_len);
        delete[] ifs;
        this->error = 0;
        return 0;
#endif
    } else if (request == 0x5421) {
        U32 address = IOCTL_ARG1;
#ifdef WIN32
        u_long value = thread->memory->readd(address);
        U32 result = ioctlsocket(this->nativeSocket, FIONBIO, &value);
#else        
        int value = thread->memory->readd(address);
        U32 result = ::ioctl(this->nativeSocket, FIONBIO, &value);
#endif
        LOG_SOCK("  native socket: %x FIONBIO %d", nativeSocket, (int)value);
        if (result != 0) {
            std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
            return handleNativeSocketError(t, true);
        }
        blocking = value == 0;
        thread->memory->writed(IOCTL_ARG1, value);
        this->error = 0;
        return 0;
    } else if (request == K_SIOCGIFHWADDR) {
        U32 address = IOCTL_ARG1;
        BString name = thread->memory->readString(address);
#ifdef WIN32
        if (name == "lo") {
            thread->memory->writew(address, domain);
            thread->memory->memset(address + 2, 0, 14);
            return 0;
        }
        PIP_ADAPTER_ADDRESSES addresses = getAdapterAddresses();
        if (!addresses) {
            std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
            return handleNativeSocketError(t, true);
        }
        PIP_ADAPTER_UNICAST_ADDRESS ua = addresses->FirstUnicastAddress;
        while (ua) {
            if (emulatedFamilyFromHostFamily(ua->Address.lpSockaddr->sa_family) == domain) {
                break;
            }
            ua = ua->Next;
        }
        if (!ua) {
            free(addresses);
            return -1;
        }
        thread->memory->writew(address, domain);
        thread->memory->memcpy(address + 2, addresses->PhysicalAddress, std::min((U32)14, (U32)addresses->PhysicalAddressLength));
        free(addresses);
        return 0;
#elif defined(__MACH__)
        struct ifaddrs *ifap, *ifaptr;
        unsigned char *ptr;

        if (getifaddrs(&ifap) == 0) {
            for(ifaptr = ifap; ifaptr != NULL; ifaptr = (ifaptr)->ifa_next) {
                if (((ifaptr)->ifa_addr)->sa_family == AF_LINK && !strcmp(ifaptr->ifa_name, "en0")) {
                    ptr = (unsigned char *)LLADDR((struct sockaddr_dl *)(ifaptr)->ifa_addr);
                    thread->memory->writew(address, domain);
                    thread->memory->memcpy(address + 2, ptr, 6);
                    return 0;
                }
            }
        }
        return -1;
#else
        struct ifreq ifr = { };
        thread->memory->memcpy(ifr.ifr_name, address, K_IFNAMSIZ);
        U32 result = ::ioctl(this->nativeSocket, SIOCGIFHWADDR, &ifr);
        if (result) {
            std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
            return handleNativeSocketError(t, true);
        }
        thread->memory->writew(address, emulatedFamilyFromHostFamily(ifr.ifr_hwaddr.sa_family));
        thread->memory->memcpy(address + 2, ifr.ifr_hwaddr.sa_data, 6);
        return 0;
#endif
    } else if (request == K_SIOCGIFFLAGS) {
        U32 address = IOCTL_ARG1;
        BString name = thread->memory->readString(address);
#ifdef WIN32
        if (name == "lo") {
            thread->memory->writew(address + K_IFNAMSIZ, 0x49); // IFF_UP | IFF_LOOPBACK | IFF_RUNNING
        } else {
            thread->memory->writew(address + K_IFNAMSIZ, 0x1043); // IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST
        }
        return 0;
#else
        struct ifreq ifr = { };
        thread->memory->memcpy(ifr.ifr_name, address, K_IFNAMSIZ);
        U32 result = ::ioctl(this->nativeSocket, SIOCGIFFLAGS, &ifr);
        if (result) {
            std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
            return handleNativeSocketError(t, true);
        }
        thread->memory->writew(address + K_IFNAMSIZ, ifr.ifr_flags);
        return 0;
#endif
    } else if (request == K_SIOCGIFMTU) {
        U32 address = IOCTL_ARG1;
        BString name = thread->memory->readString(address);
#ifdef WIN32
        PIP_ADAPTER_ADDRESSES addresses = getAdapterAddress(name);
        if (!addresses) {
            std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
            return handleNativeSocketError(t, true);
        }
        thread->memory->writew(address + K_IFNAMSIZ, (U16)addresses->Mtu);
        return 0;
#else
        struct ifreq ifr = { };
        thread->memory->memcpy(ifr.ifr_name, address, K_IFNAMSIZ);
        U32 result = ::ioctl(this->nativeSocket, SIOCGIFMTU, &ifr);
        if (result) {
            std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
            return handleNativeSocketError(t, true);
        }
        thread->memory->writew(address + K_IFNAMSIZ, ifr.ifr_mtu);
        return 0;
#endif
    } else {
        if (bw64RecoverEnabled()) {
            bw64RecoverLogOnce("ioctl.unsupported", "unsupported ioctl on file descriptor; returned ENOTTY");
        } else {
            kwarn_fmt("KNativeSocketObject::ioctl request=%x not implemented", request);
        }
    }
    return -K_ENOTTY;
}

S64 KNativeSocketObject::seek(S64 pos) {
    return -K_ESPIPE;
}

S64 KNativeSocketObject::length() {
    return -1;
}

S64 KNativeSocketObject::getPos() {
    return 0;
}

void KNativeSocketObject::setBlocking(bool blocking) {
    if (this->loopbackDatagram || (this->loopbackStream && (this->loopbackPort || this->loopbackStreamConnected || this->nativeSocket < 0))) { this->blocking = blocking; return; }

    this->blocking = blocking;
#ifdef BOXEDWINE_MULTI_THREADED
    setNativeBlocking(this->nativeSocket, blocking);
#endif
    LOG_SOCK("  native socket: %x set blocking %s", nativeSocket, blocking?"true":"false");
}

bool KNativeSocketObject::isBlocking() {
    return this->blocking;
}

void KNativeSocketObject::setAsync(bool isAsync) {
    if (isAsync)
        kpanic("KNativeSocketObject::setAsync not implemented yet");
}

bool KNativeSocketObject::isAsync() {
    return false;
}

KFileLock* KNativeSocketObject::getLock(KFileLock* lock) {
    kdebug("KNativeSocketObject::getLock not implemented yet");
    return nullptr;
}

U32 KNativeSocketObject::setLock(KFileLock* lock, bool wait) {
    kdebug("KNativeSocketObject::setLock not implemented yet");
    return -1;
}

bool KNativeSocketObject::supportsLocks() {
    return false;
}

bool KNativeSocketObject::isOpen() {
    if (this->loopbackDatagram || (this->loopbackStream && (this->loopbackPort || this->loopbackStreamConnected || this->nativeSocket < 0))) return true;

    /*
    fd_set          sready = {};
    struct timeval  nowait = { 0 };

    FD_ZERO(&sready);
    FD_SET(this->nativeSocket, &sready);

    ::select(this->nativeSocket + 1, nullptr, nullptr, &sready, &nowait);
    bool result = FD_ISSET(this->nativeSocket, &sready) != 0;
    if (result) {
        std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
        this->error = handleNativeSocketError(t, false);
        return false;
    }
    */
    return this->listening || this->connected || this->connecting;
}

bool KNativeSocketObject::isPriorityReadReady() {
    if (this->loopbackDatagram) {
        std::lock_guard<std::mutex> lock(this->loopbackPacketsMutex);
        return !this->loopbackPackets.empty();
    }
    if (this->loopbackStream && (this->loopbackPort || this->loopbackStreamConnected || this->nativeSocket < 0)) return this->isReadReady();

    fd_set          sready = {};
    struct timeval  nowait = { 0 };

    FD_ZERO(&sready);
    FD_SET(this->nativeSocket, &sready);

    ::select(this->nativeSocket + 1, nullptr, nullptr, &sready, &nowait);
    bool result = FD_ISSET(this->nativeSocket, &sready) != 0;
    if (result) {
        this->error = 0;
    }
    return result;
}

bool KNativeSocketObject::isReadReady() {
    if (this->loopbackDatagram) {
        std::lock_guard<std::mutex> lock(this->loopbackPacketsMutex);
        return !this->loopbackPackets.empty();
    }
    if (this->loopbackStream && (this->loopbackPort || this->loopbackStreamConnected || this->nativeSocket < 0)) {
        if (this->listening) {
            std::lock_guard<std::mutex> lock(this->loopbackStreamMutex);
            return !this->loopbackAcceptQueue.empty();
        }
        std::lock_guard<std::mutex> lock(this->loopbackStreamMutex);
        return !this->loopbackStreamBytes.empty() || this->loopbackStreamPeerEof || this->inClosed;
    }

    fd_set          sready = {};
    struct timeval  nowait = { 0 };

    FD_ZERO(&sready);
    FD_SET(this->nativeSocket, &sready);

    ::select(this->nativeSocket + 1, &sready, nullptr, nullptr, &nowait);
    bool result = FD_ISSET(this->nativeSocket, &sready) != 0;    
    if (result) {        
        LOG_SOCK("%x native socket: %x isReadReady result=%x", KThread::currentThread()->id, nativeSocket, result);
        this->error = 0;
    }
    return result;
}

bool KNativeSocketObject::isWriteReady() {
    if (this->loopbackDatagram) return true;
    if (this->loopbackStream && (this->loopbackPort || this->loopbackStreamConnected || this->nativeSocket < 0)) {
        if (this->connecting) return true;
        if (!this->loopbackStreamConnected || this->outClosed) return false;
        return !this->loopbackStreamPeer.expired();
    }

    fd_set          sready = {};
    struct timeval  nowait = { 0 };

    FD_ZERO(&sready);
    FD_SET(this->nativeSocket, &sready);

    ::select(this->nativeSocket + 1, nullptr, &sready, nullptr, &nowait);
    bool result = FD_ISSET(this->nativeSocket, &sready) != 0;
    return result;
}

void KNativeSocketObject::waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) {
    if (this->loopbackDatagram || (this->loopbackStream && (this->loopbackPort || this->loopbackStreamConnected || this->nativeSocket < 0))) {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(waitingNodeMutex);
        if (events & K_POLLIN) {
            BOXEDWINE_CONDITION_ADD_PARENT(this->readingCond, parentCondition);
        } else {
            BOXEDWINE_CONDITION_REMOVE_PARENT(this->readingCond, parentCondition);
        }
        if (events & K_POLLOUT) {
            BOXEDWINE_CONDITION_ADD_PARENT(this->writingCond, parentCondition);
        } else {
            BOXEDWINE_CONDITION_REMOVE_PARENT(this->writingCond, parentCondition);
        }
        return;
    }

#ifdef BOXEDWINE_MULTI_THREADED
    const std::lock_guard<std::recursive_mutex> lifecycleLock(checkWaitingNativeSocketsThreadMutex);
#endif
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(waitingNodeMutex);
    if (events & K_POLLIN) {
        BOXEDWINE_CONDITION_ADD_PARENT(this->readingCond, parentCondition);
    } else {
        BOXEDWINE_CONDITION_REMOVE_PARENT(this->readingCond, parentCondition);
    }
    if (events & K_POLLOUT) {
        BOXEDWINE_CONDITION_ADD_PARENT(this->writingCond, parentCondition);
    } else {
        BOXEDWINE_CONDITION_REMOVE_PARENT(this->writingCond, parentCondition);
    }
    if (!readingCond->parentsCount() && !writingCond->parentsCount()) {
        removeWaitingSocket(this->nativeSocket);
    } else {
        std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
        addWaitingNativeSocket(t);
    }
}

U32 KNativeSocketObject::writeNative(U8* buffer, U32 len) {
    if (this->loopbackDatagram) return -K_EOPNOTSUPP;
    if (this->loopbackStream && (this->loopbackStreamConnected || this->nativeSocket < 0)) return this->sendLoopbackStream(buffer, len, 0);

    S32 result = (S32)::send(this->nativeSocket, (const char*)buffer, len, 0);
    LOG_SOCK("native socket: %x writeNative len=%d result=%x", nativeSocket, len, result);
    if (result>=0) {            
        this->error = 0;
        return result;
    }
    std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
    return handleNativeSocketError(t, true);
}

U32 KNativeSocketObject::readNative(U8* buffer, U32 len) {
    if (this->loopbackDatagram) return -K_EOPNOTSUPP;
    if (this->loopbackStream && (this->loopbackStreamConnected || this->nativeSocket < 0)) return this->recvLoopbackStream(buffer, len, 0);

    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(readingCond);
    S32 result = (S32)::recv(this->nativeSocket, (char*)buffer, len, 0);

    LOG_SOCK("%x native socket: %x readNative len=%d result=%x", KThread::currentThread()->id, nativeSocket, len, result);
    if (result>=0) {  
        this->error = 0;
        return result;
    }
    std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
    return handleNativeSocketError(t, false);
}

U32 KNativeSocketObject::stat(KProcess* process, U32 address, bool is64) {
    KSystem::writeStat(process, B(""), address, is64, 1, 0, K_S_IFSOCK|K__S_IWRITE|K__S_IREAD, 0, 0, 4096, 0, 0, 1);
    return 0;
}

U32 KNativeSocketObject::map(KThread* thread, U32 address, U32 len, S32 prot, S32 flags, U64 off) {
    return 0;
}

bool KNativeSocketObject::canMap() {
    return false;
}

BString KNativeSocketObject::selfFd() {
    return B("socket:[")+this->nativeSocket+B("]"); // :TODO: should be inode
}

static void readSockAddrIn(struct sockaddr_in* addr, KMemory* memory, U32 address) {
    addr->sin_family = AF_INET;
    memory->memcpy(&addr->sin_port, address+2, 2);
    memory->memcpy(&addr->sin_addr, address+4, 4);
}
    
static void writeSockAddrIn(struct sockaddr_in* addr, KMemory* memory, U32 address) {
    memory->writew(address, AF_INET);
    memory->memcpy(address+2, &addr->sin_port, 2);
    memory->memcpy(address+4, &addr->sin_addr, 4);
}
    
U32 KNativeSocketObject::bindLoopback(U32 address, U16 port) {
    if (!this->loopbackDatagram || !isGuestLoopbackAddress(address)) {
        return -K_EADDRNOTAVAIL;
    }
    std::shared_ptr<KNativeSocketObject> self =
        std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
    std::lock_guard<std::mutex> lock(g_loopbackUdpMutex);
    if (this->loopbackPort) return -K_EINVAL;

    auto portAvailable = [](U16 candidate) {
        auto it = g_loopbackUdpSockets.find(candidate);
        if (it == g_loopbackUdpSockets.end()) return true;
        if (it->second.expired()) {
            g_loopbackUdpSockets.erase(it);
            return true;
        }
        return false;
    };
    if (!port) {
        for (U32 attempt = 0; attempt < 16384; ++attempt) {
            const U16 candidate = g_nextLoopbackUdpPort++;
            if (g_nextLoopbackUdpPort < 49152) g_nextLoopbackUdpPort = 49152;
            if (candidate && portAvailable(candidate)) {
                port = candidate;
                break;
            }
        }
        if (!port) return -K_EADDRINUSE;
    } else if (!portAvailable(port)) {
        return -K_EADDRINUSE;
    }

    this->loopbackAddress = address;
    this->loopbackPort = port;
    g_loopbackUdpSockets[port] = self;
    this->error = 0;
    return 0;
}

U32 KNativeSocketObject::bindLoopbackStream(U32 address, U16 port) {
    if (!this->loopbackStream || !isGuestLoopbackAddress(address)) return -K_EADDRNOTAVAIL;
    if (this->loopbackPort) return -K_EINVAL;
    std::shared_ptr<KNativeSocketObject> self =
        std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
    std::lock_guard<std::mutex> lock(g_loopbackTcpMutex);
    auto portAvailable = [](U16 candidate) {
        auto it = g_loopbackTcpSockets.find(candidate);
        if (it == g_loopbackTcpSockets.end()) return true;
        if (it->second.expired()) {
            g_loopbackTcpSockets.erase(it);
            return true;
        }
        return false;
    };
    if (!port) {
        for (U32 attempt = 0; attempt < 32768; ++attempt) {
            const U16 candidate = g_nextLoopbackTcpPort++;
            if (g_nextLoopbackTcpPort < 32768) g_nextLoopbackTcpPort = 32768;
            if (candidate && portAvailable(candidate)) {
                port = candidate;
                break;
            }
        }
        if (!port) return -K_EADDRINUSE;
    } else if (!portAvailable(port)) {
        // SO_REUSEADDR is remembered and reported like Linux. There is no
        // TIME_WAIT state in this local transport, so an active endpoint is
        // still unavailable.
        return -K_EADDRINUSE;
    }
    this->loopbackAddress = address;
    this->loopbackPort = port;
    g_loopbackTcpSockets[port] = self;
    this->error = 0;
    return 0;
}

U32 KNativeSocketObject::sendLoopbackStream(const U8* bytes, U32 length, U32 flags) {
    if (!this->loopbackStreamConnected) return -K_ENOTCONN;
    if (this->outClosed) return -K_EPIPE;
    if (flags & ~(K_MSG_OOB | K_MSG_NOSIGNAL | K_MSG_DONTWAIT)) {
        kwarn_fmt("loopback TCP send unsupported flags: %x", flags);
    }
    std::shared_ptr<KNativeSocketObject> peer = this->loopbackStreamPeer.lock();
    if (!peer) return -K_EPIPE;
    if (peer->inClosed) return -K_EPIPE;
    {
        std::lock_guard<std::mutex> lock(peer->loopbackStreamMutex);
        if (length) peer->loopbackStreamBytes.insert(peer->loopbackStreamBytes.end(), bytes, bytes + length);
    }
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(peer->readingCond);
    BOXEDWINE_CONDITION_SIGNAL_ALL(peer->readingCond);
    this->error = 0;
    return length;
}

U32 KNativeSocketObject::recvLoopbackStream(U8* bytes, U32 length, U32 flags) {
    if (!this->loopbackStreamConnected) return -K_ENOTCONN;
    if (flags & ~(K_MSG_OOB | K_MSG_PEEK | K_MSG_NOSIGNAL | K_MSG_DONTWAIT)) {
        kwarn_fmt("loopback TCP recv unsupported flags: %x", flags);
    }
    const bool peek = (flags & K_MSG_PEEK) != 0;
    const bool nonblocking = !this->blocking || (flags & K_MSG_DONTWAIT);
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->readingCond);
    for (;;) {
        bool available = false;
        {
            std::lock_guard<std::mutex> lock(this->loopbackStreamMutex);
            if (!this->loopbackStreamBytes.empty()) {
                const U32 copied = std::min<U32>(length, (U32)this->loopbackStreamBytes.size());
                for (U32 i = 0; i < copied; ++i) {
                    bytes[i] = peek ? this->loopbackStreamBytes[i] : this->loopbackStreamBytes.front();
                    if (!peek) this->loopbackStreamBytes.pop_front();
                }
                this->error = 0;
                return copied;
            }
            available = this->loopbackStreamPeerEof || this->inClosed;
        }
        if (available) {
            this->error = 0;
            return 0;
        }
        if (nonblocking) {
            this->error = K_EWOULDBLOCK;
            return -K_EWOULDBLOCK;
        }
        BOXEDWINE_CONDITION_WAIT(this->readingCond);
#ifdef BOXEDWINE_MULTI_THREADED
        KThread* current = KThread::currentThread();
        if (current && current->terminating) return -K_EINTR;
#endif
    }
}

void KNativeSocketObject::closeLoopbackStream() {
    if (this->outClosed) return;
    this->outClosed = true;
    std::shared_ptr<KNativeSocketObject> peer = this->loopbackStreamPeer.lock();
    if (peer) {
        peer->loopbackStreamPeerEof = true;
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(peer->readingCond);
        BOXEDWINE_CONDITION_SIGNAL_ALL(peer->readingCond);
    }
}

static U32 sendLoopbackStreamGuest(KNativeSocketObject* socket, KThread* thread,
                                   U32 message, U32 length, U32 flags) {
    KMemory* memory = thread->memory;
    if (length && !memory->canRead(message, length)) return -K_EFAULT;
    std::vector<U8> bytes(length);
    if (length) memory->memcpy(bytes.data(), message, length);
    return socket->sendLoopbackStream(bytes.data(), length, flags);
}

static U32 recvLoopbackStreamGuest(KNativeSocketObject* socket, KThread* thread,
                                   U32 buffer, U32 length, U32 flags) {
    KMemory* memory = thread->memory;
    if (length && !memory->canWrite(buffer, length)) return -K_EFAULT;
    std::vector<U8> bytes(length);
    U32 result = socket->recvLoopbackStream(bytes.data(), length, flags);
    if ((S32)result > 0) memory->memcpy(buffer, bytes.data(), result);
    return result;
}

U32 KNativeSocketObject::ensureLoopbackBound() {
    if (this->loopbackPort) return 0;
    return this->bindLoopback(0x7f000001u, 0);
}

U32 KNativeSocketObject::sendLoopbackBytes(KThread* thread, const U8* bytes, U32 length, U32 flags,
                                            const struct sockaddr_in* destination, bool hasDestination) {
    (void)thread;
    if (length > 65507) return -K_EMSGSIZE;
    if (flags & ~(K_MSG_OOB | K_MSG_NOSIGNAL | K_MSG_DONTWAIT)) {
        kwarn_fmt("loopback UDP sendto unsupported flags: %x", flags);
    }

    U32 destinationAddress = 0;
    U16 destinationPort = 0;
    if (hasDestination) {
        if (!destination || destination->sin_family != AF_INET) return -K_EAFNOSUPPORT;
        destinationAddress = ntohl(destination->sin_addr.s_addr);
        destinationPort = ntohs(destination->sin_port);
    } else if (this->connected) {
        destinationAddress = this->loopbackPeerAddress;
        destinationPort = this->loopbackPeerPort;
    } else {
        return -K_EDESTADDRREQ;
    }

    if (!isGuestLoopbackAddress(destinationAddress)) return -K_EHOSTUNREACH;
    if (!destinationAddress) destinationAddress = 0x7f000001u;
    U32 result = this->ensureLoopbackBound();
    if ((S32)result < 0) return result;
    if (!destinationPort) {
        this->error = 0;
        return length;
    }

    std::shared_ptr<KNativeSocketObject> target;
    {
        std::lock_guard<std::mutex> lock(g_loopbackUdpMutex);
        auto it = g_loopbackUdpSockets.find(destinationPort);
        if (it != g_loopbackUdpSockets.end()) {
            target = it->second.lock();
            if (!target) g_loopbackUdpSockets.erase(it);
        }
    }
    if (target && target->loopbackDatagram &&
        (!target->loopbackAddress || target->loopbackAddress == destinationAddress)) {
        LoopbackPacket packet;
        if (length) packet.bytes.assign(bytes, bytes + length);
        packet.sourceAddress = this->loopbackAddress ? this->loopbackAddress : 0x7f000001u;
        packet.sourcePort = this->loopbackPort;
        {
            std::lock_guard<std::mutex> lock(target->loopbackPacketsMutex);
            if (target->loopbackPackets.size() >= 1024) return -K_ENOBUFS;
            target->loopbackPackets.push_back(std::move(packet));
        }
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(target->readingCond);
        BOXEDWINE_CONDITION_SIGNAL_ALL(target->readingCond);
    }
    this->error = 0;
    return length;
}

U32 KNativeSocketObject::sendLoopbackTo(KThread* thread, U32 message, U32 length, U32 flags,
                                         U32 destinationAddress, U32 destinationLength) {
    KMemory* memory = thread->memory;
    if (length > 65507) return -K_EMSGSIZE;
    if (length && !memory->canRead(message, length)) return -K_EFAULT;
    std::vector<U8> bytes(length);
    if (length) memory->memcpy(bytes.data(), message, length);

    struct sockaddr_in destination = {};
    const bool hasDestination = destinationAddress != 0;
    if (hasDestination) {
        if (destinationLength < sizeof(destination)) return -K_EINVAL;
        if (memory->readw(destinationAddress) != K_AF_INET) return -K_EAFNOSUPPORT;
        readSockAddrIn(&destination, memory, destinationAddress);
    }
    return this->sendLoopbackBytes(thread, bytes.data(), length, flags,
                                   hasDestination ? &destination : nullptr, hasDestination);
}

U32 KNativeSocketObject::recvLoopbackFrom(KThread* thread, U32 buffer, U32 length, U32 flags,
                                           U32 address, U32 addressLength) {
    KMemory* memory = thread->memory;
    if (length && !memory->canWrite(buffer, length)) return -K_EFAULT;
    if (address && !memory->canWrite(address, sizeof(struct sockaddr_in))) return -K_EFAULT;
    if (addressLength && !memory->canWrite(addressLength, 4)) return -K_EFAULT;
    if (flags & ~(K_MSG_OOB | K_MSG_PEEK | K_MSG_NOSIGNAL | K_MSG_DONTWAIT)) {
        kwarn_fmt("loopback UDP recvfrom unsupported flags: %x", flags);
    }

    LoopbackPacket packet;
    const bool peek = (flags & K_MSG_PEEK) != 0;
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->readingCond);
    for (;;) {
        bool available = false;
        {
            std::lock_guard<std::mutex> lock(this->loopbackPacketsMutex);
            if (!this->loopbackPackets.empty()) {
                packet = this->loopbackPackets.front();
                if (!peek) this->loopbackPackets.pop_front();
                available = true;
            }
        }
        if (available) break;
        if (!this->blocking || (flags & K_MSG_DONTWAIT)) {
            this->error = K_EWOULDBLOCK;
            return -K_EWOULDBLOCK;
        }
        BOXEDWINE_CONDITION_WAIT(this->readingCond);
#ifdef BOXEDWINE_MULTI_THREADED
        KThread* current = KThread::currentThread();
        if (current && current->terminating) return -K_EINTR;
#endif
    }

    const U32 copied = std::min<U32>(length, (U32)packet.bytes.size());
    if (copied) memory->memcpy(buffer, packet.bytes.data(), copied);
    if (address) {
        struct sockaddr_in source = {};
        source.sin_family = AF_INET;
        source.sin_port = htons(packet.sourcePort);
        source.sin_addr.s_addr = htonl(packet.sourceAddress);
        writeSockAddrIn(&source, memory, address);
    }
    if (addressLength) memory->writed(addressLength, sizeof(struct sockaddr_in));
    this->error = 0;
    return copied;
}

U32 KNativeSocketObject::bind(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 len) {
    KMemory* memory = thread->memory;
    if (this->loopbackDatagram) {
        if (len < sizeof(struct sockaddr_in)) return -K_EINVAL;
        if (memory->readw(address) != K_AF_INET) return -K_EAFNOSUPPORT;
        struct sockaddr_in addr = {};
        readSockAddrIn(&addr, memory, address);
        return this->bindLoopback(ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port));
    }
    if (this->loopbackStream) {
        if (len < sizeof(struct sockaddr_in)) return -K_EINVAL;
        if (memory->readw(address) != K_AF_INET) return -K_EAFNOSUPPORT;
        struct sockaddr_in addr = {};
        readSockAddrIn(&addr, memory, address);
        if (isGuestLoopbackAddress(ntohl(addr.sin_addr.s_addr))) {
            return this->bindLoopbackStream(ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port));
        }
        if (this->nativeSocket < 0) return -K_EADDRNOTAVAIL;
    }



    U32 family = memory->readw(address);
    if (family == K_AF_INET) {
        if (fd->kobject->type!=KTYPE_NATIVE_SOCKET) {
            return -K_ENOTSOCK;
        }
        struct sockaddr_in addr = {0};
        readSockAddrIn(&addr, memory, address);
        int result = ::bind(this->nativeSocket, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
        LOG_SOCK("native socket: %x bind result=%x", nativeSocket, result);
        if (result == 0) {
            this->error = 0;
            return 0;
        }
        std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
        return handleNativeSocketError(t, false);
    }
    return -K_EAFNOSUPPORT;
}

U32 KNativeSocketObject::connect(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 len) {
    if (this->loopbackDatagram) {
        if (len < sizeof(struct sockaddr_in)) return -K_EINVAL;
        if (thread->memory->readw(address) != K_AF_INET) return -K_EAFNOSUPPORT;
        struct sockaddr_in peer = {};
        readSockAddrIn(&peer, thread->memory, address);
        U32 peerAddress = ntohl(peer.sin_addr.s_addr);
        if (!isGuestLoopbackAddress(peerAddress)) return -K_EHOSTUNREACH;
        U32 result = this->ensureLoopbackBound();
        if ((S32)result < 0) return result;
        this->loopbackPeerAddress = peerAddress ? peerAddress : 0x7f000001u;
        this->loopbackPeerPort = ntohs(peer.sin_port);
        this->connected = true;
        this->error = 0;
        return 0;
    }
    if (this->loopbackStream && len >= sizeof(struct sockaddr_in) &&
        thread->memory->readw(address) == K_AF_INET) {
        struct sockaddr_in peerAddress = {};
        readSockAddrIn(&peerAddress, thread->memory, address);
        U32 destination = ntohl(peerAddress.sin_addr.s_addr);
        U16 destinationPort = ntohs(peerAddress.sin_port);
        if (isGuestLoopbackAddress(destination)) {
            if (this->loopbackStreamConnected) return -K_EISCONN;
            if (!destinationPort) return -K_ECONNREFUSED;
            if (!this->loopbackPort) {
                U32 bound = this->bindLoopbackStream(0x7f000001u, 0);
                if ((S32)bound < 0) return bound;
            }
            std::shared_ptr<KNativeSocketObject> listener;
            {
                std::lock_guard<std::mutex> lock(g_loopbackTcpMutex);
                auto it = g_loopbackTcpSockets.find(destinationPort);
                if (it != g_loopbackTcpSockets.end()) {
                    listener = it->second.lock();
                    if (!listener) g_loopbackTcpSockets.erase(it);
                }
            }
            const U32 normalizedDestination = destination ? destination : 0x7f000001u;
            if (!listener || !listener->listening ||
                (listener->loopbackAddress && listener->loopbackAddress != normalizedDestination)) {
                this->error = K_ECONNREFUSED;
                return -K_ECONNREFUSED;
            }
            std::shared_ptr<KNativeSocketObject> accepted =
                std::make_shared<KNativeSocketObject>(this->domain, this->type, this->protocol);
            if (accepted->nativeSocket >= 0) {
                closesocket(accepted->nativeSocket);
                accepted->nativeSocket = -1;
            }
            accepted->loopbackStream = true;
            accepted->loopbackStreamConnected = true;
            accepted->connected = true;
            accepted->loopbackAddress = listener->loopbackAddress;
            accepted->loopbackPort = listener->loopbackPort;
            accepted->loopbackPeerAddress = this->loopbackAddress ? this->loopbackAddress : 0x7f000001u;
            accepted->loopbackPeerPort = this->loopbackPort;
            accepted->loopbackStreamPeer = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
            this->loopbackPeerAddress = normalizedDestination;
            this->loopbackPeerPort = destinationPort;
            this->loopbackStreamPeer = accepted;
            this->loopbackStreamConnected = true;
            this->connected = true;
            {
                std::lock_guard<std::mutex> lock(listener->loopbackStreamMutex);
                if (listener->loopbackAcceptQueue.size() >= std::max<U32>(1, listener->loopbackStreamBacklog)) {
                    this->loopbackStreamPeer.reset();
                    this->loopbackStreamConnected = false;
                    this->connected = false;
                    this->error = K_ECONNREFUSED;
                    return -K_ECONNREFUSED;
                }
                listener->loopbackAcceptQueue.push_back(accepted);
            }
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(listener->readingCond);
            BOXEDWINE_CONDITION_SIGNAL_ALL(listener->readingCond);
            this->error = 0;
            return 0;
        }
        if (this->nativeSocket < 0) return -K_EHOSTUNREACH;
    } else if (this->loopbackStream && this->nativeSocket < 0) {
        return -K_EAFNOSUPPORT;
    }

    char buffer[1024] = {};
    U32 result = 0;
    KMemory* memory = thread->memory;

    std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
#ifndef BOXEDWINE_MULTI_THREADED    
    if (this->connecting) {
        if (this->isWriteReady()) {
            this->error = 0;
            this->connecting = 0;
            this->connected = true;
            removeWaitingSocket(this->nativeSocket);
            return 0;
        }
        else {
            int error = 0;
            socklen_t errLen = 4;
            if (::getsockopt(this->nativeSocket, SOL_SOCKET, SO_ERROR, (char*)&error, &errLen) < 0) {
                return -K_EIO;
            }
            if (error) {
                result = translateNativeSocketError(t->nativeSocket, error);
                if (result != (U32)(-K_EWOULDBLOCK)) {
                    return result;
                }
            }
        }        
        addWaitingNativeSocket(t);
        BOXEDWINE_CONDITION_LOCK(this->writingCond);
        BOXEDWINE_CONDITION_WAIT(this->writingCond);
        BOXEDWINE_CONDITION_UNLOCK(this->writingCond);
    }
#endif
    if (len > sizeof(buffer)) {
        return -K_EINVAL;
	}
    memory->memcpy(buffer, address, len);

    U16 family = memory->readw(address);
    struct sockaddr* addr = nullptr;
    sockaddr_in addr_in = {0};
    U32 addrLen = len;
    if (family == K_AF_INET) {
        readSockAddrIn(&addr_in, memory, address);
        addr = (struct sockaddr*)&addr_in;
        addrLen = sizeof(struct sockaddr_in);
        if (memory->readb(address + 4) == 127 && memory->readb(address + 5) == 0 && memory->readb(address + 6) == 0 && memory->readb(address + 7) == 1) {
            U16 port = memory->readb(address + 3) | (((U32)memory->readb(address + 2)) << 8);
            if (port == 631) {
                // wine seems to try and connect to a print server, if we are running on Linux it hangs while trying to read.  That is probably a bug, for now just prevent the connection.
                return -K_ECONNREFUSED;
            }
        }
    } else {
        addr = (struct sockaddr*)buffer;
    }

    int r = ::connect(this->nativeSocket, addr, addrLen);
    LOG_SOCK("native socket: %x connect %s result=%x", nativeSocket, socketAddressName(thread->memory, address, len).c_str(), r);
    if (r == 0) {
#ifdef __EMSCRIPTEN__
        // SOCKFS returns zero while the WebSocket is still opening. Reporting
        // immediate success suppresses Wine's FD_CONNECT event. Let poll finish
        // the connection when the underlying WebSocket becomes writable.
        this->connected = false;
        this->connecting = true;
        if (!this->blocking) {
            this->error = K_EINPROGRESS;
            return -K_EINPROGRESS;
        }
        struct pollfd pending = {this->nativeSocket, POLLOUT, 0};
        if (::poll(&pending, 1, -1) < 0) {
            this->connecting = false;
            return handleNativeSocketError(t, true);
        }
        int connectError = 0;
        socklen_t connectErrorLen = sizeof(connectError);
        int optionResult = ::getsockopt(this->nativeSocket, SOL_SOCKET, SO_ERROR, (char*)&connectError, &connectErrorLen);
        this->connecting = false;
        if (optionResult < 0) return handleNativeSocketError(t, true);
        if (connectError) return translateNativeSocketError(this->nativeSocket, connectError);
        if (!(pending.revents & POLLOUT)) return -K_ECONNREFUSED;
        this->connected = true;
        this->error = 0;
        return 0;
#endif
        int error = 0;
        socklen_t optLen = 4;
        ::getsockopt(this->nativeSocket, SOL_SOCKET, SO_ERROR, (char*)&error, &optLen);

        if (error == 0) {
            this->connected = true;
            return 0;
        }
        this->connecting = false;
        return -K_ECONNREFUSED;
    }
    result = handleNativeSocketError(t, true);
    if ((S32)result == -K_EWOULDBLOCK) {
        result = -K_EINPROGRESS;
        t->error = K_EINPROGRESS;
    }
    if ((S32)result == -K_EINPROGRESS) {
        this->connected = true;
    }
#ifndef BOXEDWINE_MULTI_THREADED
    if (result == (U32)(-K_WAIT)) {            
        this->connecting = true;
    }
#endif
    return result;
}

U32 KNativeSocketObject::listen(KThread* thread, const KFileDescriptorPtr& fd, U32 backlog) {
    if (this->loopbackDatagram) return -K_EOPNOTSUPP;
    if (this->loopbackStream && (this->nativeSocket < 0 || this->loopbackPort)) {
        if (!this->loopbackPort) {
            U32 bound = this->bindLoopbackStream(0x7f000001u, 0);
            if ((S32)bound < 0) return bound;
        }
        this->loopbackStreamBacklog = std::max<U32>(1, std::min<U32>(backlog, 128));
        this->listening = true;
        this->error = 0;
        return 0;
    }

    S32 result = ::listen(this->nativeSocket, backlog);
    LOG_SOCK("native socket: %x list result=%x", nativeSocket, result);
    if (result>=0) {
        this->listening = true;
        this->error = 0;
        return result;
    }
    this->listening = false;
    std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
    return handleNativeSocketError(t, false);
}

U32 KNativeSocketObject::takeLoopbackStreamConnection(std::shared_ptr<KNativeSocketObject>& accepted, bool nonblocking) {
    if (!this->listening) return -K_EINVAL;
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->readingCond);
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(this->loopbackStreamMutex);
            if (!this->loopbackAcceptQueue.empty()) {
                accepted = this->loopbackAcceptQueue.front();
                this->loopbackAcceptQueue.pop_front();
                this->error = 0;
                return 0;
            }
        }
        if (!this->blocking || nonblocking) {
            this->error = K_EWOULDBLOCK;
            return -K_EWOULDBLOCK;
        }
        BOXEDWINE_CONDITION_WAIT(this->readingCond);
#ifdef BOXEDWINE_MULTI_THREADED
        KThread* current = KThread::currentThread();
        if (current && current->terminating) return -K_EINTR;
#endif
    }
}

U32 KNativeSocketObject::accept(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 len, U32 flags) {
    if (this->loopbackDatagram) return -K_EOPNOTSUPP;
    if (this->loopbackStream && (this->loopbackPort || this->nativeSocket < 0)) {
        std::shared_ptr<KNativeSocketObject> accepted;
        U32 dequeueResult = this->takeLoopbackStreamConnection(accepted, (flags & K_O_NONBLOCK) != 0);
        if ((S32)dequeueResult < 0) return dequeueResult;
        if (address && len && !thread->memory->canWrite(address, sizeof(struct sockaddr_in))) return -K_EFAULT;
        if (address) {
            struct sockaddr_in peer = {};
            peer.sin_family = AF_INET;
            peer.sin_port = htons(accepted->loopbackPeerPort);
            peer.sin_addr.s_addr = htonl(accepted->loopbackPeerAddress);
            writeSockAddrIn(&peer, thread->memory, address);
        }
        if (len) thread->memory->writed(len, sizeof(struct sockaddr_in));
        U32 accessFlags = K_O_RDWR;
        if (flags & K_O_NONBLOCK) accessFlags |= K_O_NONBLOCK;
        KFileDescriptorPtr resultFD = thread->process->allocFileDescriptor(accepted, accessFlags,
                                                                           (flags & FD_CLOEXEC) ? FD_CLOEXEC : 0,
                                                                           -1, 0);
        this->error = 0;
        return resultFD->handle;
    }

    struct sockaddr_in addr = {};
    socklen_t addrlen = sizeof(struct sockaddr);
    KMemory* memory = thread->memory;
    readSockAddrIn(&addr, memory, address);
    S32 result = (S32)::accept(this->nativeSocket, (struct sockaddr*)&addr, &addrlen);
    LOG_SOCK("native socket: %x listen result=%x", nativeSocket, result);

    if (result>=0) {
        std::shared_ptr<KNativeSocketObject> s = std::make_shared<KNativeSocketObject>(this->domain, this->type, this->protocol);
        KFileDescriptorPtr resultFD = KThread::currentThread()->process->allocFileDescriptor(s, K_O_RDWR, 0, -1, 0);

        if (flags & FD_CLOEXEC) {
            resultFD->descriptorFlags|=FD_CLOEXEC;
        }
        if (flags & K_O_NONBLOCK) {
            resultFD->kobject->setBlocking(false);
        }

        s->nativeSocket = result;
#ifndef BOXEDWINE_MULTI_THREADED
        setNativeBlocking(result, false);
#endif

        if (address) {
            writeSockAddrIn(&addr, memory, address);
        }
        if (len) {
            memory->writed(len, 16);
        }
        this->error = 0;
        return resultFD->handle;
    }
    std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
    return handleNativeSocketError(t, false);
}
    
U32 KNativeSocketObject::getsockname(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 plen) {
    KMemory* memory = thread->memory;
    if (this->loopbackDatagram) {
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(this->loopbackPort);
        addr.sin_addr.s_addr = htonl(this->loopbackPort ? this->loopbackAddress : 0);
        writeSockAddrIn(&addr, memory, address);
        memory->writed(plen, sizeof(addr));
        this->error = 0;
        return 0;
    }
    if (this->loopbackStream && (this->loopbackPort || this->loopbackStreamConnected || this->nativeSocket < 0)) {
        struct sockaddr_in local = {};
        local.sin_family = AF_INET;
        local.sin_port = htons(this->loopbackPort);
        local.sin_addr.s_addr = htonl(this->loopbackAddress);
        writeSockAddrIn(&local, memory, address);
        memory->writed(plen, sizeof(local));
        this->error = 0;
        return 0;
    }



    //socklen_t len = (socklen_t)memory->readd( plen);
    struct sockaddr_in addr = {0};
    socklen_t nativeLen = sizeof(struct sockaddr_in);
    U32 result = ::getsockname(this->nativeSocket, (struct sockaddr*)&addr, &nativeLen);
    if (result) {
        std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
        result = handleNativeSocketError(t, false);
    } else {
        writeSockAddrIn(&addr, memory, address);
        memory->writed(plen, 16);
        this->error = 0;
    }
    return result;
}

U32 KNativeSocketObject::getpeername(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 plen) {
    if (this->loopbackDatagram) {
        if (!this->connected) return -K_ENOTCONN;
        struct sockaddr_in peer = {};
        peer.sin_family = AF_INET;
        peer.sin_port = htons(this->loopbackPeerPort);
        peer.sin_addr.s_addr = htonl(this->loopbackPeerAddress);
        writeSockAddrIn(&peer, thread->memory, address);
        thread->memory->writed(plen, sizeof(peer));
        return 0;
    }
    if (this->loopbackStream && (this->loopbackStreamConnected || this->nativeSocket < 0)) {
        if (!this->loopbackStreamConnected) return -K_ENOTCONN;
        struct sockaddr_in peer = {};
        peer.sin_family = AF_INET;
        peer.sin_port = htons(this->loopbackPeerPort);
        peer.sin_addr.s_addr = htonl(this->loopbackPeerAddress);
        writeSockAddrIn(&peer, thread->memory, address);
        thread->memory->writed(plen, sizeof(peer));
        return 0;
    }

    KMemory* memory = thread->memory;
    //socklen_t len = (socklen_t)memory->readd( plen);
    struct sockaddr_in addr = {0};
    socklen_t nativeLen = sizeof(struct sockaddr_in);
    
    S32 result = ::getpeername(this->nativeSocket, (struct sockaddr*)&addr, &nativeLen);
    if (result==0) {
        writeSockAddrIn(&addr, memory, address);
        memory->writed(plen, 16);
        this->error = 0;
    } else {
        std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
        result = handleNativeSocketError(t, false);
    }
    return result;
}

U32 KNativeSocketObject::shutdown(KThread* thread, const KFileDescriptorPtr& fd, U32 how) {
    if (this->loopbackDatagram) {
        if (how != K_SHUT_RD && how != K_SHUT_WR && how != K_SHUT_RDWR) return -K_EINVAL;
        if (how == K_SHUT_RD || how == K_SHUT_RDWR) this->inClosed = true;
        if (how == K_SHUT_WR || how == K_SHUT_RDWR) this->outClosed = true;
        return 0;
    }
    if (this->loopbackStream && (this->loopbackStreamConnected || this->nativeSocket < 0)) {
        if (how != K_SHUT_RD && how != K_SHUT_WR && how != K_SHUT_RDWR) return -K_EINVAL;
        if (how == K_SHUT_RD || how == K_SHUT_RDWR) this->inClosed = true;
        if (how == K_SHUT_WR || how == K_SHUT_RDWR) this->closeLoopbackStream();
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->readingCond);
        BOXEDWINE_CONDITION_SIGNAL_ALL(this->readingCond);
        return 0;
    }

    LOG_SOCK("native socket: %x shutdown", nativeSocket);
    if (::shutdown(this->nativeSocket, how) == 0)
        return 0;
    return -1;
}

U32 KNativeSocketObject::setsockopt(KThread* thread, const KFileDescriptorPtr& fd, U32 level, U32 name, U32 value, U32 len) {
    if (this->loopbackDatagram) {
        if (level != K_SOL_SOCKET && level != K_IPPROTO_IP) return -K_ENOPROTOOPT;
        this->error = 0;
        return 0;
    }
    if (this->loopbackStream && level == K_SOL_SOCKET && name == K_SO_REUSEADDR && len == 4) {
        const U32 option = thread->memory->readd(value);
        this->loopbackReuseAddress = option != 0;
        if (this->nativeSocket >= 0) ::setsockopt(this->nativeSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&option, 4);
        this->error = 0;
        return 0;
    }
    if (this->loopbackStream && (this->loopbackPort || this->loopbackStreamConnected || this->nativeSocket < 0)) {
        if (level == K_SOL_SOCKET && len == 4 &&
            (name == K_SO_REUSEADDR || name == K_SO_RCVBUF || name == K_SO_SNDBUF)) {
            const U32 option = thread->memory->readd(value);
            if (name == K_SO_REUSEADDR) this->loopbackReuseAddress = option != 0;
            else if (name == K_SO_RCVBUF) this->recvLen = option;
            else this->sendLen = option;
            if (this->nativeSocket >= 0) {
                const int nativeName = name == K_SO_REUSEADDR ? SO_REUSEADDR :
                                       name == K_SO_RCVBUF ? SO_RCVBUF : SO_SNDBUF;
                ::setsockopt(this->nativeSocket, SOL_SOCKET, nativeName, (const char*)&option, 4);
            }
            this->error = 0;
            return 0;
        }
        if (this->nativeSocket < 0) return level != K_SOL_SOCKET ? -K_ENOPROTOOPT : -K_EINVAL;
    }

    KMemory* memory = thread->memory;

    LOG_SOCK("%x native socket: %x setsockopt level=%x name=%x value=%x", thread->id, nativeSocket, memory->readd(value));
    if (level == K_IPPROTO_IP) {
        U32 v = 0;
        switch (name) {
        case K_IP_MTU_DISCOVER:
            if (len != 4)
                kpanic("KNativeSocketObject::setsockopt K_IP_MULTICAST_TTL expecting len of 4");
            v = memory->readd(value);
#if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DO) && defined(IP_PMTUDISC_DONT)
            ::setsockopt(this->nativeSocket, IPPROTO_IP, IP_MTU_DISCOVER, (const char*)&v, 4);
#endif
            break;
        case K_IP_TOS: 
            if (len != 4)
                kpanic("KNativeSocketObject::setsockopt K_IP_TOS expecting len of 4");
            v = memory->readd(value);
            ::setsockopt(this->nativeSocket, IPPROTO_IP, IP_TOS, (const char*)&v, 4);
            break;
        case K_IP_RECVTOS:
            if (len != 4)
                kpanic("KNativeSocketObject::setsockopt K_IP_RECVTOS expecting len of 4");
            v = memory->readd(value);
            ::setsockopt(this->nativeSocket, IPPROTO_IP, IP_RECVTOS, (const char*)&v, 4);
            break;
        case K_IP_RECVERR:
            if (len != 4)
                kpanic("KNativeSocketObject::setsockopt K_IP_RECVERR expecting len of 4");
            v = memory->readd(value);
#ifdef IP_RECVERR
            ::setsockopt(this->nativeSocket, IPPROTO_IP, IP_RECVERR, (const char*)&v, 4);
#endif
            break;
        default:
            if (bw64RecoverEnabled()) bw64RecoverLogOnce("socket.native.setsockopt.ip", "unsupported IP socket option; returned EINVAL");
            else kwarn_fmt("KNativeSocketObject::setsockopt IPPROTO_IP name %d not implemented", name);
            return -K_EINVAL;
        }
    }
    else if (level == K_IPPROTO_TCP) {
        U32 v = 0;
        switch (name) {
        case K_TCP_NODELAY:
            if (len != 4) {
                kpanic("KNativeSocketObject::setsockopt TCP_NODELAY expecting len of 4");
            }
            v = memory->readd(value);
            ::setsockopt(this->nativeSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&v, 4);
            break;
        case K_TCP_KEEPIDLE:
            if (len != 4) {
                kpanic("KNativeSocketObject::setsockopt TCP_KEEPIDLE expecting len of 4");
            }
            v = memory->readd(value);
            ::setsockopt(this->nativeSocket, IPPROTO_TCP, TCP_KEEPIDLE, (const char*)&v, 4);
            break;
        case K_TCP_KEEPINTVL:
            if (len != 4) {
                kpanic("KNativeSocketObject::setsockopt TCP_KEEPINTVL expecting len of 4");
            }
            v = memory->readd(value);
            ::setsockopt(this->nativeSocket, IPPROTO_TCP, TCP_KEEPINTVL, (const char*)&v, 4);
            break;
        case K_TCP_SYNCNT:
#ifdef TCP_SYNCNT
            if (len != 4) {
                kpanic("KNativeSocketObject::setsockopt TCP_SYNCNT expecting len of 4");
            }
            v = memory->readd(value);
            ::setsockopt(this->nativeSocket, IPPROTO_TCP, TCP_SYNCNT, (const char*)&v, 4);
#endif
            break;
        default:
            if (bw64RecoverEnabled()) bw64RecoverLogOnce("socket.native.setsockopt.tcp", "unsupported TCP socket option; returned EINVAL");
            else kwarn_fmt("KNativeSocketObject::setsockopt IPPROTO_TCP name %d not implemented", name);
            return -K_EINVAL;
        }
    } else if (level == K_SOL_SOCKET) {
        U32 v = 0;
        switch (name) {
            case K_SO_RCVBUF:
                if (len!=4)
                    kpanic("KNativeSocketObject::setsockopt SO_RCVBUF expecting len of 4");
                this->recvLen = memory->readd(value);
                ::setsockopt(this->nativeSocket, SOL_SOCKET, SO_RCVBUF, (const char*)&this->recvLen, 4);
                break;
            case K_SO_SNDBUF:
                if (len != 4)
                    kpanic("KNativeSocketObject::setsockopt SO_SNDBUF expecting len of 4");
                this->sendLen = memory->readd(value);
                ::setsockopt(this->nativeSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&this->sendLen, 4);
                break;
            case K_SO_PRIORITY:
                if (len != 4)
                    kpanic("KNativeSocketObject::setsockopt SO_PRIORITY expecting len of 4");
                v = memory->readd(value);
                setNativeSocketPriority(this, v);
#ifdef SO_PRIORITY
                if (this->nativeSocket >= 0)
                    ::setsockopt(this->nativeSocket, SOL_SOCKET, SO_PRIORITY, (const char*)&v, 4);
#endif
                break;
            case K_SO_SNDTIMEO:            
                if (len != 8) {
                    kpanic("KNativeSocketObject::setsockopt SO_SNDTIMEO expecting len of 8");
                }
                {
#ifdef BOXEDWINE_MSVC
                    U32 sec = memory->readd(value);
                    U32 usec = memory->readd(value+4);
                    DWORD v = sec * 1000 + usec / 1000;
                    len = 4;
#else
                    struct timeval v;
                    v.tv_usec = memory->readd(value+4);
                    v.tv_sec = memory->readd(value);
                    len = sizeof(struct timeval);
#endif
                    ::setsockopt(this->nativeSocket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&v, len);
                }
                break;
            case K_SO_RCVTIMEO:
                if (len != 8) {
                    kpanic("KNativeSocketObject::setsockopt SO_RCVTIMEO expecting len of 8");
                }
                {
#ifdef BOXEDWINE_MSVC
                    U32 sec = memory->readd(value);
                    U32 usec = memory->readd(value + 4);
                    DWORD v = sec * 1000 + usec / 1000;
                    len = 4;
#else
                    struct timeval v;
                    v.tv_usec = memory->readd(value + 4);
                    v.tv_sec = memory->readd(value);
                    len = sizeof(struct timeval);
#endif
                    ::setsockopt(this->nativeSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&v, len);
                }
                break;
            case K_SO_TIMESTAMP:
                break;
            case K_SO_KEEPALIVE:
                if (len != 4)
                    kpanic("KNativeSocketObject::setsockopt SO_KEEPALIVE expecting len of 4");
                v = memory->readd(value);
                ::setsockopt(this->nativeSocket, SOL_SOCKET, SO_KEEPALIVE, (const char*)&v, 4);
                break;
            case K_SO_BROADCAST:
                if (len != 4)
                    kpanic("KNativeSocketObject::setsockopt SO_BROADCAST expecting len of 4");
                v = memory->readd(value);
                ::setsockopt(this->nativeSocket, SOL_SOCKET, SO_BROADCAST, (const char*)&v, 4);
                break;
            case K_SO_REUSEADDR:
                if (len != 4) {
                    kpanic("KNativeSocketObject::setsockopt SO_REUSEADDR expecting len of 4");
                }
                v = memory->readd(value);
                ::setsockopt(this->nativeSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, 4);
                break;
            case K_SO_LINGER: // steam installer triggered this
                if (len != 8) {
                    kpanic("KNativeSocketObject::setsockopt SO_LINGER expecting len of 8");
                }
                {
                    struct linger l;
                    l.l_onoff = memory->readd(value);
                    l.l_linger = memory->readd(value + 4);
                    ::setsockopt(this->nativeSocket, SOL_SOCKET, SO_LINGER, (const char*)&l, 8);
                }
                break;
            default:
                if (bw64RecoverEnabled()) bw64RecoverLogOnce("socket.native.setsockopt.sol", "unsupported SOL_SOCKET option; returned EINVAL");
                else kwarn_fmt("KNativeSocketObject::setsockopt SOL_SOCKET name %d not implemented", name);
                return -K_EINVAL;
        }
    } else {
        if (bw64RecoverEnabled()) bw64RecoverLogOnce("socket.native.setsockopt.level", "unsupported socket option level; returned EINVAL");
        else kwarn_fmt("KNativeSocketObject::setsockopt level %d not implemented", level);
        return -K_EINVAL;
    }
    this->error = 0;
    return 0;
}

U32 KNativeSocketObject::getsockopt(KThread* thread, const KFileDescriptorPtr& fd, U32 level, U32 name, U32 value, U32 len_address) {
    KMemory* memory = thread->memory;
    if (this->loopbackDatagram) {
        if (level != K_SOL_SOCKET) return -K_ENOPROTOOPT;
        U32 result = 0;
        switch (name) {
        case K_SO_ERROR:
            result = this->error;
            this->error = 0;
            break;
        case K_SO_TYPE:
            result = this->type;
            break;
        case K_SO_PROTOCOL:
            result = this->protocol;
            break;
        case K_SO_RCVBUF:
            result = this->recvLen;
            break;
        case K_SO_SNDBUF:
            result = this->sendLen;
            break;
        case K_SO_BROADCAST:
            result = 0;
            break;
        case K_SO_REUSEADDR:
            result = this->loopbackReuseAddress ? 1 : 0;
            break;
        case K_SO_ACCEPTCONN:
            result = this->listening ? 1 : 0;
            break;
        default:
            if (this->nativeSocket < 0) return -K_EINVAL;
            goto native_getsockopt;
        }
        memory->writed(value, result);
        memory->writed(len_address, 4);
        return 0;
    }
    if (this->loopbackStream && level == K_SOL_SOCKET && name == K_SO_REUSEADDR) {
        memory->writed(value, this->loopbackReuseAddress ? 1 : 0);
        memory->writed(len_address, 4);
        return 0;
    }
    if (this->loopbackStream &&
        (this->loopbackPort || this->loopbackStreamConnected || this->nativeSocket < 0)) {
        if (level == K_SOL_SOCKET && (name == K_SO_ERROR || name == K_SO_TYPE || name == K_SO_PROTOCOL ||
            name == K_SO_RCVBUF || name == K_SO_SNDBUF || name == K_SO_REUSEADDR ||
            name == K_SO_ACCEPTCONN || name == K_SO_BROADCAST)) {
            U32 result = 0;
            switch (name) {
            case K_SO_ERROR: result = this->error; this->error = 0; break;
            case K_SO_TYPE: result = this->type; break;
            case K_SO_PROTOCOL: result = this->protocol; break;
            case K_SO_RCVBUF: result = this->recvLen; break;
            case K_SO_SNDBUF: result = this->sendLen; break;
            case K_SO_REUSEADDR: result = this->loopbackReuseAddress ? 1 : 0; break;
            case K_SO_ACCEPTCONN: result = this->listening ? 1 : 0; break;
            default: result = 0; break;
            }
            memory->writed(value, result);
            memory->writed(len_address, 4);
            return 0;
        }
        if (this->nativeSocket < 0) return level != K_SOL_SOCKET ? -K_ENOPROTOOPT : -K_EINVAL;
    }

native_getsockopt:
    socklen_t len = (socklen_t)memory->readd(len_address);
    S32 result = 0;
    U32 retrievedValue = 0;

    if (level == K_SOL_SOCKET) {
        if (name == K_SO_RCVBUF) {
            if (len!=4)
                kpanic("KNativeSocketObject::getsockopt SO_RCVBUF expecting len of 4");
            result = ::getsockopt(this->nativeSocket, SOL_SOCKET, SO_RCVBUF, (char*)&retrievedValue, &len);
            if (!result) {
                memory->writed(value, retrievedValue);
            }
        } else if (name == K_SO_SNDBUF) {
            if (len != 4)
                kpanic("KNativeSocketObject::getsockopt SO_SNDBUF expecting len of 4");
            result = ::getsockopt(this->nativeSocket, SOL_SOCKET, SO_SNDBUF, (char*)&retrievedValue, &len);
            if (!result) {
                memory->writed(value, retrievedValue);
            }
        } else if (name == K_SO_ERROR) {
            if (len != 4)
                kpanic("KNativeSocketObject::getsockopt SO_ERROR expecting len of 4");
            int nativeError = 0;
            result = ::getsockopt(this->nativeSocket, SOL_SOCKET, SO_ERROR, (char*)&nativeError, &len);
            if (!result) {
                retrievedValue = nativeError ? -translateNativeSocketError(this->nativeSocket, nativeError) : 0;
                memory->writed(value, retrievedValue);
#ifdef __EMSCRIPTEN__
                if (this->connecting && (nativeError || this->isWriteReady())) {
                    this->connecting = false;
                    this->connected = nativeError == 0;
                }
#endif
            }
        } else if (name == K_SO_TYPE) { 
            if (len != 4)
                kpanic("KNativeSocketObject::getsockopt K_SO_TYPE expecting len of 4");
            memory->writed(value, this->type);
            retrievedValue = this->type;
        } else if (name == K_SO_PRIORITY) {
            if (len != 4)
                kpanic("KNativeSocketObject::getsockopt SO_PRIORITY expecting len of 4");
            retrievedValue = getNativeSocketPriority(this);
            memory->writed(value, retrievedValue);
            memory->writed(len_address, 4);
        } else if (name == K_SO_OOBINLINE) {
            if (len != 4)
                kpanic("KNativeSocketObject::getsockopt SO_OOBINLINE expecting len of 4");
            result = ::getsockopt(this->nativeSocket, SOL_SOCKET, SO_OOBINLINE, (char*)&retrievedValue, &len);
            if (!result) {
                memory->writed(value, retrievedValue);
            }
        } else if (name == K_SO_BROADCAST) {
            if (len != 4)
                kpanic("KNativeSocketObject::getsockopt SO_BROADCAST expecting len of 4");
            U32 result = 0;
            result = ::getsockopt(this->nativeSocket, SOL_SOCKET, SO_BROADCAST, (char*)&retrievedValue, &len);
            if (!result) {
                memory->writed(value, retrievedValue);
            }
        } else if (name == K_SO_RCVTIMEO) {
            if (len != 8)
                kpanic("KNativeSocketObject::getsockopt SO_RCVTIMEO expecting len of 8");            
#ifdef BOXEDWINE_MSVC            
            len = 4;
            DWORD v = 0;
            result = ::getsockopt(this->nativeSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&v, &len);

            U32 sec = v / 1000;
            U32 usec = (v % 1000) * 1000;
#else
            struct timeval v;
            len = sizeof(struct timeval);
            result = ::getsockopt(this->nativeSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&v, &len);

            U32 sec = (U32)v.tv_sec;
            U32 usec = (U32)v.tv_usec;
#endif
            if (!result) {
                retrievedValue = sec;
                memory->writed(value, sec);
                memory->writed(value + 4, usec);
            }
        } else if (name == K_SO_SNDTIMEO) {
            if (len != 8)
                kpanic("KNativeSocketObject::getsockopt SO_SNDTIMEO expecting len of 8");
#ifdef BOXEDWINE_MSVC            
            len = 4;
            DWORD v = 0;
            result = ::getsockopt(this->nativeSocket, SOL_SOCKET, SO_SNDTIMEO, (char*)&v, &len);

            U32 sec = v / 1000;
            U32 usec = (v % 1000) * 1000;
#else
            struct timeval v;
            len = sizeof(struct timeval);
            result = ::getsockopt(this->nativeSocket, SOL_SOCKET, SO_SNDTIMEO, (char*)&v, &len);

            U32 sec = (U32)v.tv_sec;
            U32 usec = (U32)v.tv_usec;
#endif
            if (!result) {
                retrievedValue = sec;
                memory->writed(value, sec);
                memory->writed(value + 4, usec);
            }
        } else if (name == K_SO_BROADCAST) {
            if (len != 4)
                kpanic("KNativeSocketObject::getsockopt SO_BROADCAST expecting len of 4");
            result = ::getsockopt(this->nativeSocket, SOL_SOCKET, SO_BROADCAST, (char*)&retrievedValue, &len);
            if (!result) {
                memory->writed(value, retrievedValue);
            }
        } else if (name == K_SO_ACCEPTCONN) {
            if (len != 4)
                kpanic("KNativeSocketObject::getsockopt SO_ACCEPTCONN expecting len of 4");
            result = ::getsockopt(this->nativeSocket, SOL_SOCKET, SO_ACCEPTCONN, (char*)&retrievedValue, &len);
            if (!result) {
                memory->writed(value, retrievedValue);
            }
        } else if (name == K_SO_PROTOCOL) {
            if (len != 4)
                kpanic("KNativeSocketObject::getsockopt SO_PROTOCOL expecting len of 4");
            if (!result) {
                memory->writed(value, protocol);
            }
        } else if (name == K_SO_TYPE) {
            if (len != 4)
                kpanic("KNativeSocketObject::getsockopt SO_TYPE expecting len of 4");
            if (!result) {
                memory->writed(value, type);
            }
        } else {
            if (bw64RecoverEnabled()) bw64RecoverLogOnce("socket.native.getsockopt.sol", "unsupported SOL_SOCKET getsockopt option; returned EINVAL");
            else kwarn_fmt("KNativeSocketObject::getsockopt name %d not implemented", name);
            return -K_EINVAL;
        }
    } else {
        if (bw64RecoverEnabled()) bw64RecoverLogOnce("socket.native.getsockopt.level", "unsupported getsockopt level; returned EINVAL");
        else kwarn_fmt("KNativeSocketObject::getsockopt level %d not implemented", level);
        return -K_EINVAL;
    }
    LOG_SOCK("%x native socket: %x getsockopt level=%x name=%x value=%x result=%x", thread->id, nativeSocket, level, name, retrievedValue, result);
    if (result != 0) {
        std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
        return handleNativeSocketError(t, true);
    }
    this->error = 0;
    return (U32)result;
}

U32 KNativeSocketObject::sendmsg(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 flags) {
    MsgHdr hdr = {};
    KMemory* memory = thread->memory;
    readMsgHdr(thread, address, &hdr);

    if (hdr.msg_control) {
        CMsgHdr cmsg;			

        readCMsgHdr(thread, hdr.msg_control, &cmsg);
        if (cmsg.cmsg_level != K_SOL_SOCKET) {
            kwarn_fmt("KNativeSocketObject::sendmsg control level %d not implemented", cmsg.cmsg_level);
        } else if (cmsg.cmsg_type != K_SCM_RIGHTS) {
            kwarn_fmt("KNativeSocketObject::sendmsg control type %d not implemented", cmsg.cmsg_level);
        } else if ((cmsg.cmsg_len & 3) != 0) {
            kwarn_fmt("KNativeSocketObject::sendmsg control len %d not implemented", cmsg.cmsg_len);
        }
        if (hdr.msg_controllen>0) {
            kwarn("KNativeSocketObject::sendmsg does not support sending file handles");
        }				
    }
    // Snapshot the guest descriptors once: a second length read can race a
    // guest writer, and unchecked 32-bit totals can wrap below the allocation.
    if (hdr.msg_iovlen > 1024) return -K_EMSGSIZE;
    if (hdr.msg_iovlen && hdr.msg_iov > 0xffffffffu - hdr.msg_iovlen * 8) return -K_EFAULT;
    struct GuestIov { U32 address; U32 length; };
    std::vector<GuestIov> vectors;
    vectors.reserve(hdr.msg_iovlen);
    U32 len = 0;
    for (U32 i = 0; i < hdr.msg_iovlen; i++) {
        GuestIov entry{memory->readd(hdr.msg_iov + 8 * i), memory->readd(hdr.msg_iov + 8 * i + 4)};
        if (entry.length > 0x7fffffffu - len) return -K_EMSGSIZE;
        if (entry.length && entry.address > 0xffffffffu - entry.length) return -K_EFAULT;
        vectors.push_back(entry);
        len += entry.length;
    }
    U8 stackBuf[4096];
    std::unique_ptr<U8[]> heapBuf;
    if (len > sizeof(stackBuf)) heapBuf.reset(new U8[len]);
    U8* buffer = heapBuf ? heapBuf.get() : stackBuf;
    U32 offset = 0;
    for (const auto& entry : vectors) {
        memory->memcpy(buffer + offset, entry.address, entry.length);
        offset += entry.length;
    }
    struct sockaddr_in dest = {0};
    U32 destLen = 0;
    if (hdr.msg_namelen) {
        destLen = (U32)sizeof(struct sockaddr_in);
        readSockAddrIn(&dest, memory, hdr.msg_name);
    }

    U32 nativeFlags = 0;
    if (flags & K_MSG_OOB)
        nativeFlags |= MSG_OOB;
    flags &= ~K_MSG_NOSIGNAL;
    if (flags & (~1)) {
        kwarn_fmt("KNativeSocketObject::sendmsg unsupported flags: %d", flags);
    }

    if (this->loopbackStream && (this->loopbackStreamConnected || this->nativeSocket < 0)) {
        return this->sendLoopbackStream(buffer, len, flags);
    }

    U32 result;
    if (hdr.msg_namelen) {
        result = (U32)::sendto(this->nativeSocket, (const char*)buffer, len, nativeFlags, destLen == 0 ? nullptr : (struct sockaddr*)&dest, destLen);
    } else {
        result = (U32)::send(this->nativeSocket, (const char*)buffer, len, nativeFlags);
    }
    LOG_SOCK("%x native socket: %x sendmsg msg_name=%x msg_namelen=%x result=%x", thread->id, nativeSocket, hdr.msg_name, hdr.msg_namelen, result);
    if ((S32)result >= 0) {
        this->error = 0;
        return result;
    }
    std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
    return handleNativeSocketError(t, true);
}

U32 KNativeSocketObject::recvmsg(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 flags) {
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(readingCond);
    KMemory* memory = thread->memory;
    const U32 loopbackFlags = flags;
    char tmp[K_PAGE_SIZE] = { 0 };
    MsgHdr hdr = { 0 };
    U32 result = 0;
    U32 nativeFlags = 0;
    if (flags) {
        if (flags & K_MSG_PEEK) {
            nativeFlags |= MSG_PEEK;
            flags &= ~K_MSG_PEEK;
        }
        if (flags) {
            kwarn_fmt("KNativeSocketObject::recvmsg unhandled flag %x", flags);
        }
    }
    readMsgHdr(thread, address, &hdr);    

    for (U32 i = 0; i < hdr.msg_iovlen; i++) {
        U32 p = memory->readd(hdr.msg_iov + 8 * i);
        U32 requested = memory->readd(hdr.msg_iov + 8 * i + 4);
        if (!requested) continue;
        U32 len = requested;

        struct sockaddr_in in = {0};
        socklen_t inLen = sizeof(struct sockaddr_in);

        if (len>sizeof(tmp))
            len = sizeof(tmp);
        S32 r;
        if (this->loopbackStream && (this->loopbackStreamConnected || this->nativeSocket < 0)) {
            r = (S32)this->recvLoopbackStream((U8*)tmp, len, loopbackFlags);
        } else {
            r = (S32)::recvfrom(this->nativeSocket, tmp, len, nativeFlags, hdr.msg_name?(struct sockaddr*)&in:nullptr, hdr.msg_name ? &inLen : nullptr);
        }
        LOG_SOCK("%x native socket: %x recvmsg flags=%x msg_name=%x msg_namelen=%x result=%x", thread->id, nativeSocket, flags, hdr.msg_name, hdr.msg_namelen, r);
        //if (r < 0) {
        //    Platform::nanoSleep(10000000);
        //    r = (S32)::recvfrom(this->nativeSocket, tmp, len, nativeFlags, hdr.msg_name ? (struct sockaddr*)&in : nullptr, hdr.msg_name ? &inLen : nullptr);
        //}
        if (r>=0) {
            memory->memcpy(p, tmp, r);
            // :TODO: maybe copied fields to the expected location rather than assume the structures are the same
            if (hdr.msg_name && hdr.msg_namelen >= 16) {
                writeSockAddrIn(&in, memory, hdr.msg_name);
            }
            memory->writed(address + 4, inLen);
            result+=r;
            this->error = 0;
            // A short vector ends a stream scatter operation. Advancing after
            // the page-sized cap would leave holes in the preceding vector;
            // repeating MSG_PEEK would duplicate the stream's first bytes.
            if ((U32)r < requested || (nativeFlags & MSG_PEEK)) break;
        }
        else if (result) {
            break;
        } else if (this->loopbackStream && (this->loopbackStreamConnected || this->nativeSocket < 0)) {
            result = (U32)r;
            break;
        } else {
            std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
            result = handleNativeSocketError(t, false);
        }
    }
    if (this->type==K_SOCK_STREAM)
        memory->writed(address + 4, 0); // msg_namelen, set to 0 for connected sockets
    memory->writed(address + 20, 0); // msg_controllen
    return result;
}

U32 KNativeSocketObject::sendto(KThread* thread, const KFileDescriptorPtr& fd, U32 message, U32 length, U32 flags, U32 dest_addr, U32 dest_len) {
    if (this->loopbackDatagram) return this->sendLoopbackTo(thread, message, length, flags, dest_addr, dest_len);
    if (this->loopbackStream && (this->loopbackStreamConnected || this->nativeSocket < 0)) {
        if (dest_addr) {
            if (dest_len < sizeof(struct sockaddr_in) || thread->memory->readw(dest_addr) != K_AF_INET) return -K_EINVAL;
            struct sockaddr_in destination = {};
            readSockAddrIn(&destination, thread->memory, dest_addr);
            if (ntohs(destination.sin_port) != this->loopbackPeerPort ||
                !isGuestLoopbackAddress(ntohl(destination.sin_addr.s_addr))) return -K_EISCONN;
        }
        return sendLoopbackStreamGuest(this, thread, message, length, flags);
    }

    KMemory* memory = thread->memory;
    U32 nativeFlags = 0;
    struct sockaddr_in dest = { 0 };
    int len=sizeof(struct sockaddr_in);
  

    if (flags & K_MSG_OOB) {
        nativeFlags |= MSG_OOB;
        flags &= ~K_MSG_OOB;
    }
    if (flags & K_MSG_NOSIGNAL) {
        flags &= ~K_MSG_NOSIGNAL;
    }
    if (flags) {
        kwarn_fmt("KNativeSocketObject::sendto unsupported flags: %d", flags);
    }
    if (dest_addr) {
        readSockAddrIn(&dest, memory, dest_addr);
    }
    S8* tmp = new S8[length];
    memory->memcpy(tmp, message, length);
    U32 result = (U32)::sendto(this->nativeSocket, (char*)tmp, length, nativeFlags, dest_addr ? (struct sockaddr*)&dest : nullptr, dest_addr ? len : 0);
    LOG_SOCK("%x native socket: %x sendto dest_addr=%x dest_len=%x result=%x", thread->id, nativeSocket, dest_addr, dest_len, result);
    delete[] tmp;
    if ((S32)result>=0) {
        this->error = 0;
        return result;
    }
    std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
    return handleNativeSocketError(t, true);
}

U32 KNativeSocketObject::recvfrom(KThread* thread, const KFileDescriptorPtr& fd, U32 buffer, U32 length, U32 flags, U32 address, U32 address_len) {
    if (this->loopbackDatagram) return this->recvLoopbackFrom(thread, buffer, length, flags, address, address_len);
    if (this->loopbackStream && (this->loopbackStreamConnected || this->nativeSocket < 0)) {
        if (address && !thread->memory->canWrite(address, sizeof(struct sockaddr_in))) return -K_EFAULT;
        if (address_len && !thread->memory->canWrite(address_len, 4)) return -K_EFAULT;
        U32 result = recvLoopbackStreamGuest(this, thread, buffer, length, flags);
        if ((S32)result >= 0 && address) {
            struct sockaddr_in peer = {};
            peer.sin_family = AF_INET;
            peer.sin_port = htons(this->loopbackPeerPort);
            peer.sin_addr.s_addr = htonl(this->loopbackPeerAddress);
            writeSockAddrIn(&peer, thread->memory, address);
        }
        if ((S32)result >= 0 && address_len) thread->memory->writed(address_len, address ? sizeof(struct sockaddr_in) : 0);
        return result;
    }

    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(readingCond);
    U32 nativeFlags = 0;
    KMemory* memory = thread->memory;

    if (length && !thread->memory->canWrite(buffer, length)) {
        return -K_EFAULT;
    }
    if (flags & K_MSG_OOB)
        nativeFlags|=MSG_OOB;
    if (flags & K_MSG_PEEK)
        nativeFlags|=MSG_PEEK;
    if (flags & (~3)) {
        kwarn_fmt("krecvfrom unsupported flags: %d", flags);
    }
    int inLen=0;
    socklen_t outLen=0;
    struct sockaddr_in addr = {0};

    if (address_len) {
        inLen = sizeof(struct sockaddr_in);
        readSockAddrIn(&addr, memory, address);
    }
    char* tmp = nullptr;
    
    if (length) {
        tmp = new char[length];
    }
    outLen = inLen;
    U32 result = (U32)::recvfrom(this->nativeSocket, tmp, length, nativeFlags, address_len?(struct sockaddr*)&addr:nullptr, address_len?&outLen: nullptr);
    LOG_SOCK("%x native socket: %x recvfrom buffer=%x length=%d address=%x address_len=%x flags=%x result=%x", thread->id, nativeSocket, buffer, length, address, address_len, flags, result);
    if ((S32)result>=0) {
        memory->memcpy(buffer, tmp, result);
        if (address) {
            writeSockAddrIn(&addr, memory, address);
        }
        if (address_len) {
            memory->writed(address_len, 16);
        }
        this->error = 0;
    } else {
        std::shared_ptr< KNativeSocketObject> t = std::dynamic_pointer_cast<KNativeSocketObject>(shared_from_this());
        result = handleNativeSocketError(t, false);
        if ((S32)result == -K_EMSGSIZE) {
            if (length && buffer) {
                memory->memcpy(buffer, tmp, length);
            }
            if (address) {
                writeSockAddrIn(&addr, memory, address);
            }
            if (address_len) {
                memory->writed(address_len, 16);
            }
        } 
    }
    if (tmp) {
        delete[] tmp;
    }
    return result;
}

FsOpenNode* openHosts(const std::shared_ptr<FsNode>& node, U32 flags, U32 data) {
#ifdef WIN32
    if (!winsock_intialized) {
        WSADATA wsaData;
        static_cast<void>(WSAStartup(0x0202, &wsaData));
        winsock_intialized = 1;
    }
#endif
    char name[256] = {};
    char buf[256] = {};
    name[0] = 0;
    if (gethostname(name, 256) != 0) {
        strcpy(name, "Boxedwine");
    }
    snprintf(buf, sizeof(buf), "127.0.0.1\tlocalhost\n127.0.1.1\t%s\n::1\tip6-localhost ip6-loopback", name);
    return new BufferAccess(node, flags, BString::copy(buf));
}

FsOpenNode* openHostname(const std::shared_ptr<FsNode>& node, U32 flags, U32 data) {
#ifdef WIN32
    if (!winsock_intialized) {
        WSADATA wsaData;
        static_cast<void>(WSAStartup(0x0202, &wsaData));
        winsock_intialized = 1;
    }
#endif
    char buf[256];
    buf[0] = 0;
    if (gethostname(buf, 256) != 0) {
        strcpy(buf, "Boxedwine");
    }
    return new BufferAccess(node, flags, BString::copy(buf));
}

U32 ipAddress() {
    char hostbuffer[256];
    struct hostent* host_entry;
    int hostname;
    struct in_addr* addr_list;

    // retrieve hostname
    hostname = gethostname(hostbuffer, sizeof(hostbuffer));
    if (hostname == -1) {
        return 0;
    }

    // Retrieve IP addresses
    host_entry = gethostbyname(hostbuffer);
    if (host_entry == NULL) {
        return 0;
    }
    addr_list = *(struct in_addr**)host_entry->h_addr_list;
#ifdef WIN32
    return addr_list->S_un.S_addr;
#else
    return addr_list->s_addr;
#endif
}
