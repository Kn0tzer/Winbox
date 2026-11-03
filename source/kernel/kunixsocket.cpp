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

#include "kunixsocket.h"
#include "ksocket.h"
#include "kstat.h"
#include "ksignal.h"
#include "../x11wire/xwireserver.h"
#include "../x11wire/xwireconnection.h"

#include <map>
#include <mutex>

namespace {
// FD-111: abstract unix names (sun_path[0] == 0) have no filesystem
// node to resolve through, so bind() registers them here, keyed by
// the name bytes after the leading NUL (length-exact, may contain
// further NULs). Entries are weak and are removed by the owner's
// destructor; lookups treat an expired entry as unbound.
std::mutex& abstractRegistryMutex() {
    static std::mutex m;
    return m;
}
std::map<std::string, std::weak_ptr<KUnixSocketObject>>& abstractRegistry() {
    static std::map<std::string, std::weak_ptr<KUnixSocketObject>> r;
    return r;
}
}

KUnixSocketObject::KUnixSocketObject(U32 domain, U32 type, U32 protocol) : KSocketObject(KTYPE_UNIX_SOCKET, domain, type, protocol), 
    lockCond(std::make_shared<BoxedWineCondition>(B("KUnixSocketObject::lockCond"))), recvBuffer(128)
{
}

KUnixSocketObject::~KUnixSocketObject() {
    if (this->abstractRegistered) {
        std::lock_guard<std::mutex> guard(abstractRegistryMutex());
        auto it = abstractRegistry().find(this->abstractName);
        if (it != abstractRegistry().end() && it->second.expired()) {
            abstractRegistry().erase(it);
        }
    }
    // FD-113: the filesystem node of a bound pathname socket is NOT
    // removed here. On the host, close() never unlinks a bound path:
    // the node remains, addressed sends and connects answer
    // ECONNREFUSED (resolveNamedPeer finds the node but its kobject
    // weak_ptr has expired), a bind over it answers EADDRINUSE, and
    // only an explicit guest unlink removes it (after which the
    // answer becomes ENOENT). This destructor used to remove the
    // node, so a stale path resolved as ENOENT instead.

    std::shared_ptr<KUnixSocketObject> con = this->connection.lock();
    // A one-way datagram link (FD-111: connect() on a datagram socket
    // points at the bound peer, which never points back) must not
    // poison the peer with pair teardown when the sender goes away:
    // the bound socket keeps serving other senders on the host. Every
    // other link in the tree (socketpair, accept, the X11 wire
    // server) is mutual, and for those this endpoint's death IS the
    // peer's EOF, as today.
    if (con && this->oneWayPeerLink) {
        con = nullptr;
    }
    if (con) {
        // BW64_SOCKDTOR: witness a connected stream socket being destroyed.
        if (std::getenv("BW64_SOCKDTOR") && this->type == K_SOCK_STREAM && this->connected) {
            KThread* dt = KThread::currentThread();
            klog_fmt("SOCKDTOR: destroying connected stream sock this=%p pid=%u "
                     "peer=%p peerPid=%u byThreadPid=%d byTid=%d",
                     (void*)this, (unsigned)this->pid, (void*)con.get(),
                     (unsigned)con->pid,
                     (int)(dt && dt->process ? dt->process->id : -1),
                     (int)(dt ? dt->id : -1));
        }
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(con->lockCond);
        con = this->connection.lock();
        if (con) {
            if (getenv("BW64_WSREAD")) {
                klog_fmt("WSDISC: peer EOF set — pid=%u recvUsed=%u msgs=%u (this pid=%u)",
                         (unsigned)con->pid, (unsigned)con->recvBuffer.size_used(),
                         (unsigned)con->msgs.size(), (unsigned)this->pid);
            }
            con->connection.reset();
            con->inClosed = true;
            con->outClosed = true;
            // FD-119: a datagram survivor's first send after this
            // close answers -ECONNREFUSED on the host and later
            // sends -ENOTCONN; arm the one-shot so the send paths
            // can tell a just-closed peer from never having
            // connected.
            if (con->type == K_SOCK_DGRAM) {
                con->peerCloseRefusalPending = true;
            }
            BOXEDWINE_CONDITION_SIGNAL_ALL(con->lockCond);
        }
    }

    std::shared_ptr<KUnixSocketObject> c = this->connecting.lock();
    if (c) {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(c->lockCond);
        auto it = c->pendingConnections.begin();
        while (it != c->pendingConnections.end()) {
            std::shared_ptr<KUnixSocketObject> p = (*it).lock();
            // During destruction this socket's last strong reference is gone.
            // shared_from_this() would throw; its pending weak entry is expired.
            if (!p || p.get() == this) {
                it = c->pendingConnections.erase(it);
            } else {
                it++;
            }
        }
        this->connecting.reset();
    }
    {
		std::vector< std::shared_ptr<KUnixSocketObject> > pendingConnections;

        {
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
            for (auto& weakSocket : this->pendingConnections) {
                std::shared_ptr<KUnixSocketObject> s = weakSocket.lock();
                if (s) {
                    s->connecting.reset();
                    pendingConnections.push_back(s);
                }
            }
        }
        for (auto& s : pendingConnections) {
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(s->lockCond);
            BOXEDWINE_CONDITION_SIGNAL_ALL(s->lockCond);
        }
        while (!msgs.empty()) {
            this->msgs.pop();
        }
        BOXEDWINE_CONDITION_SIGNAL_ALL(this->lockCond);
    }
}

void KUnixSocketObject::debugDumpReadWait(U32 waiterPid, U32 waiterTid, FD fd) {
    // Thread-dump path: never block. This runs on the browser main thread
    // (see bw64_dump_guest_threads); a socket peer thread can hold lockCond
    // across a call that needs that same main thread, so a blocking
    // acquisition can deadlock the session. Try-lock and label instead.
    std::shared_ptr<KUnixSocketObject> peer;
    size_t ownPending = 0;
#ifdef BOXEDWINE_MULTI_THREADED
    if (!this->lockCond->tryLock()) {
        klog_fmt("BW_THREAD_DUMP socket-unavailable pid=%u tid=%u fd=%d lock=socket-lockCond lockOwner=%u",
                 waiterPid, waiterTid, (int)fd, this->lockCond->lockOwner);
        return;
    }
    ownPending = this->recvBuffer.size_used();
    peer = this->connection.lock();
    this->lockCond->unlock();
#else
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
        ownPending = this->recvBuffer.size_used();
        peer = this->connection.lock();
    }
#endif

    size_t peerPending = 0;
    U32 peerPid = 0;
    U32 peerTid = 0;
    bool peerPendingKnown = true;
    if (peer) {
#ifdef BOXEDWINE_MULTI_THREADED
        if (peer->lockCond->tryLock()) {
            peerPending = peer->recvBuffer.size_used();
            peer->lockCond->unlock();
        } else {
            peerPendingKnown = false;
            klog_fmt("BW_THREAD_DUMP socket-peer-unavailable pid=%u tid=%u fd=%d lock=socket-lockCond lockOwner=%u",
                     waiterPid, waiterTid, (int)fd, peer->lockCond->lockOwner);
        }
#else
        {
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(peer->lockCond);
            peerPending = peer->recvBuffer.size_used();
        }
#endif
        peerPid = peer->lastOwnerPid.load(std::memory_order_relaxed);
        peerTid = peer->lastOwnerTid.load(std::memory_order_relaxed);
    }
    (void)peerPendingKnown;
    klog_fmt("BW_THREAD_DUMP socket_read pid=%u tid=%u fd=%d socket=%p peer=%p ownPending=%llu peerPending=%llu peerOwner=%u/%u ownQueue=%llu/%llu:w%u/%u:r%u/%u:m%llu/%llu peerQueue=%llu/%llu:w%u/%u:r%u/%u:m%llu/%llu inClosed=%d outClosed=%d",
             waiterPid, waiterTid, (int)fd, (void*)this,
             peer ? (void*)peer.get() : nullptr,
             (unsigned long long)ownPending, (unsigned long long)peerPending,
             peerPid, peerTid,
             (unsigned long long)this->debugQueueBytesIn(),
             (unsigned long long)this->debugQueueBytesOut(),
             this->debugQueueWriterPid(), this->debugQueueWriterTid(),
             this->debugQueueReaderPid(), this->debugQueueReaderTid(),
             (unsigned long long)this->debugQueueMessagesIn(),
             (unsigned long long)this->debugQueueMessagesOut(),
             (unsigned long long)(peer ? peer->debugQueueBytesIn() : 0),
             (unsigned long long)(peer ? peer->debugQueueBytesOut() : 0),
             peer ? peer->debugQueueWriterPid() : 0,
             peer ? peer->debugQueueWriterTid() : 0,
             peer ? peer->debugQueueReaderPid() : 0,
             peer ? peer->debugQueueReaderTid() : 0,
             (unsigned long long)(peer ? peer->debugQueueMessagesIn() : 0),
             (unsigned long long)(peer ? peer->debugQueueMessagesOut() : 0),
             (int)this->inClosed, (int)this->outClosed);
}

void KUnixSocketObject::setBlocking(bool blocking) {
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
    this->blocking = blocking;
}

bool KUnixSocketObject::isBlocking() {
    return this->blocking;
}

void KUnixSocketObject::setAsync(bool isAsync) {
    KObject::setAsync(isAsync);
}

bool KUnixSocketObject::isAsync() {
    return KObject::isAsync();
}

KFileLock*  KUnixSocketObject::getLock(KFileLock* lock) {
    // No locks are ever held on this object (supportsLocks()==false),
    // so F_GETLK semantics are: no conflicting lock exists.
    return nullptr;
}

U32  KUnixSocketObject::setLock(KFileLock* lock, bool wait) {
    // Non-lockable object: fcntl lock commands are gated by
    // supportsLocks()==false in KProcess::fcntl and never reach this,
    // but a direct caller must get a real errno, not a bare -1.
    kwarn_fmt("kobject: KUnixSocketObject::setLock on a non-lockable object; returned ENOTSUP");
    return -K_ENOTSUP;
}

bool KUnixSocketObject::isOpen() {
    if (this->listening) {
        return true;
    }
    return connected && (!outClosed || !inClosed);
}

bool KUnixSocketObject::isReadReady() {
    //BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
    return this->inClosed || this->recvBuffer.size_used() || this->pendingConnections.size() || this->msgs.size();
}

bool KUnixSocketObject::isWriteReady() {
    return !connection.expired();
}

void KUnixSocketObject::waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) {
    bool addedLock = false;

    if (events & K_POLLIN) {
        BOXEDWINE_CONDITION_ADD_PARENT(this->lockCond, parentCondition);
        addedLock = true;
    }
    if (events & K_POLLOUT) {
        std::shared_ptr<KUnixSocketObject> con = this->connection.lock();
        if (con) {
            BOXEDWINE_CONDITION_ADD_PARENT(con->lockCond, parentCondition);
        } else {
            if (!addedLock) {
                BOXEDWINE_CONDITION_ADD_PARENT(this->lockCond, parentCondition);
                addedLock = true;
            }
        }
    }
    if (events && ((events & ~(K_POLLIN | K_POLLOUT)) || this->listening)) {
        if (!addedLock) {
            BOXEDWINE_CONDITION_ADD_PARENT(this->lockCond, parentCondition);
        }
    }
    if (events == 0) {
        BOXEDWINE_CONDITION_REMOVE_PARENT(this->lockCond, parentCondition);
    }
}

U32 KUnixSocketObject::internal_write(KThread* thread, const std::shared_ptr<KUnixSocketObject>& con, BOXEDWINE_CONDITION& cond, U32 buffer, U32 len) {
    KMemory* memory = thread->memory;

    if (!memory->canRead(buffer, len)) {
        return -K_EFAULT;
    }
    if (this->isRecordType()) {
        if (this->type == K_SOCK_DGRAM && !strcmp(this->destAddress.data, "/dev/log")) {
            BString s = memory->readString(buffer);
            printf("%s\n", s.c_str());
            return len;
        }
        // FD-111: write() on a connected datagram socket delivers a
        // datagram on the host; the payload used to be discarded
        // here while the length was reported. Queue it like the
        // FD-108 sendto branch (the caller signals con->lockCond).
        // FD-118: a seqpacket write is one record on the host and
        // takes the same queueing path.
        std::vector<U8> packet(len);
        memory->memcpy(packet.data(), buffer, len);
        return sendDatagramToPeer(thread, con, packet.data(), (U32)packet.size());
    }
    if (this->outClosed || !con) {
        // 64-bit threads must not take the SIGPIPE/-K_CONTINUE path (mis-delivered
        // on the 32-bit cpu, sentinel leaks as a short write -> wine "partial
        // write"); return a clean -EPIPE like Linux. See writeNative for the full
        // rationale.
        if (thread->process->is64Bit) {
            return -K_EPIPE;
        }
        return writePipeClosed(thread, false);
    }
    //printf("internal_write: %0.8X size=%d capacity=%d writeLen=%d", (int)&this->connection->recvBuffer, (int)this->connection->recvBuffer.size(), (int)this->connection->recvBuffer.capacity(), len);

    std::vector<U8> packet(len);
    memory->memcpy(packet.data(), buffer, len);
    const U32 kept = con->applySocketFilter(packet.data(), len);
    if (kept) {
        // FD-114: if the peer's message queue holds sendmsg bytes,
        // these bytes belong behind them in the stream; queue them
        // as a payload-only message so the read paths serve one
        // call order across the two receive stores.
        if (!con->msgs.empty()) {
            this->queueStreamBytesLocked(thread, con.get(), packet.data(), kept);
        } else {
            con->recvBuffer.put(packet.data(), kept);
        }
        con->notifyAsyncReady(K_POLL_IN);
    }
    return len;
}

U32 KUnixSocketObject::writev(KThread* thread, U32 iov, S32 iovcnt) {
    S32 len=0;
    std::shared_ptr<KUnixSocketObject> con = this->connection.lock();
    KMemory* memory = thread->memory;
    bool wrote = false;

    {
        BOXEDWINE_CONDITION& cond = (con?con->lockCond:this->lockCond);
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(cond);

        if (this->isRecordType()) {
            // FD-115: the host queues ONE datagram for the whole
            // gather; the per-segment loop below queued one per
            // segment. Gather into a single packet first, checking
            // every segment before anything is queued -- an
            // unreadable segment answers -EFAULT with nothing
            // delivered, the host's atomic gather (the same rule
            // FD-101 measured for sendmsg). Zero-length segments are
            // skipped without touching their base; a gather
            // totalling zero queues nothing.
            std::vector<U8> packet;
            bool badSegment = false;
            for (S32 i=0;i<iovcnt;i++) {
                U32 buf = memory->readd(iov + i * 8);
                U32 toWrite = memory->readd(iov + i * 8 + 4);
                if (!toWrite) {
                    continue;
                }
                if (!memory->canRead(buf, toWrite)) {
                    badSegment = true;
                    break;
                }
                const size_t at = packet.size();
                packet.resize(at + toWrite);
                memory->memcpy(packet.data() + at, buf, toWrite);
            }
            if (badSegment) {
                len = -K_EFAULT;
            } else if (!packet.empty()) {
                if (this->type == K_SOCK_DGRAM && !strcmp(this->destAddress.data, "/dev/log")) {
                    size_t n = 0;
                    while (n < packet.size() && packet[n]) n++;
                    printf("%.*s\n", (int)n, (const char*)packet.data());
                    len = (S32)packet.size();
                } else {
                    len = (S32)this->sendDatagramToPeer(thread, con, packet.data(), (U32)packet.size());
                }
            }
        } else for (S32 i=0;i<iovcnt;i++) {
            U32 buf = memory->readd(iov + i * 8);
            U32 toWrite = memory->readd(iov + i * 8 + 4);
            S32 result;

            if (toWrite) {
                result = this->internal_write(thread, con, cond, buf, toWrite);
                if (result < 0) {
                    if (i == 0) len = result; // first segment failed: report errno
                    break;
                }
                len += result;
            }
        }
        if (con) {
            BOXEDWINE_CONDITION_SIGNAL_ALL(cond);
        }
        wrote = (len > 0);
    }
    // Notify a server peer (X11 wire server) outside the lock — see write().
    if (con && wrote) con->onPeerWrote();
    return (U32)len;
}

U32 KUnixSocketObject::write(KThread* thread, U32 buffer, U32 len) {
    this->pid = thread->process->id; // kind of a hack to do this here
    std::shared_ptr<KUnixSocketObject> con = this->connection.lock();
    U32 result;
    {
        BOXEDWINE_CONDITION& cond = (con?con->lockCond:this->lockCond);
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(cond);
        result = this->internal_write(thread, con, cond, buffer, len);
        if (con) {
            BOXEDWINE_CONDITION_SIGNAL_ALL(con->lockCond);
        }
    }
    // Notify a server peer (e.g. the X11 wire server) outside the lock so its
    // synchronous request parsing can re-read this same recvBuffer.
    if (con) con->onPeerWrote();
    return result;
}

U32 KUnixSocketObject::writeNative(U8* buffer, U32 len) {
    if (this->isRecordType()) {
        if (this->type == K_SOCK_DGRAM && !strcmp(this->destAddress.data, "/dev/log")) {
            klog_fmt("%s", buffer);
            return len;
        }
        // FD-111: the native write path is the 64-bit bridge's
        // write() (sys_write64 stages the bytes host-side and calls
        // writeNative), so a connected datagram write must queue a
        // datagram here too, not just in internal_write. An
        // unconnected datagram write answers ENOTCONN, as the
        // host's send path does.
        // FD-118: a seqpacket write queues one record here too.
        // FD-119: the no-peer answer is peerCloseSendAnswer's.
        std::shared_ptr<KUnixSocketObject> dest = this->connection.lock();
        if (!dest) {
            // A seqpacket socket whose write side is closed answers
            // -EPIPE on this path, as the outClosed case below does.
            if (this->type == K_SOCK_SEQPACKET && this->outClosed) {
                return -K_EPIPE;
            }
            return this->peerCloseSendAnswer(KThread::currentThread(), false);
        }
        if (this->outClosed) {
            return -K_EPIPE;
        }
        KThread* thread = KThread::currentThread();
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(dest->lockCond);
        U32 result = queueDatagramLocked(thread, dest, buffer, len);
        BOXEDWINE_CONDITION_SIGNAL_ALL(dest->lockCond);
        return result;
    }
    std::shared_ptr<KUnixSocketObject> con = this->connection.lock();
    if (this->outClosed || !con) {
		KThread* thread = KThread::currentThread();
        // BW64_WRITECLOSED: the wineserver request-socket write hits this branch
        // mid-boot and returns -K_CONTINUE (from writePipeClosed's SIGPIPE path),
        // which leaks to wine as a bogus byte count -> "partial write". Log WHY:
        // is the peer genuinely outClosed, or did the connection weak_ptr fail to
        // lock (a transient under MT, peer object still alive elsewhere)?
        if (std::getenv("BW64_WRITECLOSED")) {
            klog_fmt("WRITECLOSED: this=%p pid=%d tid=%d len=%u outClosed=%d con=%s inClosed=%d type=%d connected=%d",
                     (void*)this,
                     (int)(thread && thread->process ? thread->process->id : -1),
                     (int)(thread ? thread->id : -1),
                     (unsigned)len, (int)this->outClosed,
                     con ? "alive" : "NULL", (int)this->inClosed, (int)this->type,
                     (int)this->connected);
        }
        // A connected stream socket whose peer weak_ptr momentarily fails to lock
        // (peer object transiently unreferenced under fd-table churn at boot)
        // is NOT a broken pipe: returning the SIGPIPE/-K_CONTINUE sentinel leaks
        // out of sys_write64 as a bogus byte count and wine reports "partial
        // write" -> fatal client error. Treat "connected, not explicitly closed,
        // peer not lockable" as a transient -EWOULDBLOCK so a blocking writer
        // retries (the wineserver request socket is blocking) instead of dying.
        if (!this->outClosed && this->connected && !con) {
            return -K_EWOULDBLOCK;
        }
        // Broken pipe: the peer endpoint is gone. writePipeClosed() raises SIGPIPE
        // via the 32-bit KThread::runSignal, which (a) builds the signal frame on
        // the UNUSED 32-bit this->cpu for a 64-bit thread (mis-delivery) and (b)
        // returns -K_CONTINUE, a sentinel the 64-bit dispatcher leaves in RAX as a
        // garbage "bytes written" count -> wine reads a short write on its
        // wineserver request socket -> "partial write" -> fatal client error +
        // wineserver heap corruption as it reaps the broken client. Real Linux
        // write-to-broken-pipe with SIGPIPE ignored (wine always SIG_IGNs it)
        // simply returns -EPIPE. So for a 64-bit thread, return a clean -EPIPE and
        // never leak the -K_CONTINUE sentinel; the 32-bit path keeps its existing
        // SIGPIPE behaviour. (See [[project_boxedwine64_bug2_teardown]] for the
        // same 32-bit-runSignal-on-a-64-bit-thread mis-delivery class.)
        if (thread && thread->process->is64Bit) {
            return -K_EPIPE;
        }
        if (thread) {
            return writePipeClosed(thread, false);
        }
        return -K_EPIPE;
    }

    const U32 kept = con->applySocketFilter(buffer, len);
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(con->lockCond);
        if (kept) {
            // FD-114: keep call order against queued sendmsg bytes
            // (see internal_write).
            if (!con->msgs.empty()) {
                this->queueStreamBytesLocked(KThread::currentThread(), con.get(), buffer, kept);
            } else {
                con->recvBuffer.put(buffer, kept);
            }
        }
        BOXEDWINE_CONDITION_SIGNAL_ALL(con->lockCond);
    }
    if (kept) con->notifyAsyncReady(K_POLL_IN);
    con->onPeerWrote();
    return len;
}

void KUnixSocketObject::signalReadReady() {
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(lockCond);
    BOXEDWINE_CONDITION_SIGNAL_ALL(lockCond);
}

U32 KUnixSocketObject::unixsocket_write_native_nowait(const std::shared_ptr<KObject>& obj, U8* value, int len) {
    if (obj->type!=KTYPE_UNIX_SOCKET)
        return 0;
    std::shared_ptr<KUnixSocketObject> s = std::dynamic_pointer_cast<KUnixSocketObject>(obj);

    if (s->type == K_SOCK_DGRAM) {
        return 0;
    }
    std::shared_ptr<KUnixSocketObject> con = s->connection.lock();
    if (s->outClosed || !con)
        return -K_EPIPE;
    const U32 kept = con->applySocketFilter(value, (U32)len);

    {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(con->lockCond);
        //printf("SOCKET write len=%d bufferSize=%d pos=%d\n", len, s->connection->recvBufferLen, s->connection->recvBufferWritePos);
        if (kept) {
            // FD-114: keep call order against queued sendmsg bytes
            // (see internal_write).
            if (!con->msgs.empty()) {
                s->queueStreamBytesLocked(KThread::currentThread(), con.get(), value, kept);
            } else {
                con->recvBuffer.put(value, kept);
            }
        }
        BOXEDWINE_CONDITION_SIGNAL_ALL(con->lockCond);
    }
    if (kept) con->notifyAsyncReady(K_POLL_IN);
    con->onPeerWrote();
    return len;
}

U32 KUnixSocketObject::readNative(U8* buffer, U32 len) {
    std::shared_ptr<KUnixSocketObject> con = this->connection.lock();
    // FD-110: same exemption as the guest read path -- a datagram
    // socket's input is its queue, and a queued message is input
    // for any socket; the 64-bit bridge's read() (sys_read64)
    // reaches the queue through here.
    if (!this->inClosed && !con && this->type != K_SOCK_DGRAM && this->msgs.empty())
        return -K_EPIPE;
    con = nullptr; // don't hold a strong reference to this, if we are blocking then it would prevent the con object from being destroyed when its process is closed
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
    while (this->recvBuffer.size_used()==0 && this->msgs.empty()) {
        if (this->inClosed) {
            return 0;
        }
        if (!this->blocking) {
            return -K_EWOULDBLOCK;
        }
        BOXEDWINE_CONDITION_WAIT_SIGNALABLE(this->lockCond);
#ifdef BOXEDWINE_MULTI_THREADED
        KThread* thread = KThread::currentThread();
        // audio thread will call this function without a thread
        if (thread) {
            if (thread->terminating) {
                return -K_EINTR;
            }
            if (thread->startSignal) {
                thread->startSignal = false;
                return -K_CONTINUE;
            }
#ifdef BOXEDWINE_GUEST_X64
            if (thread->interruptBlockedSyscall()) {
                return -K_EINTR;
            }
#endif
        }
#endif
    }
    if (!this->msgs.empty()) {
        // FD-110: serve the message queue with the same semantics
        // as the guest read path (shared copyFromQueue).
        std::shared_ptr<KSocketMsg> front;
        return copyFromQueue(len, true, buffer, &front);
    }
    //printf("readNative: %0.8X size=%d capacity=%d writeLen=%d", (int)&this->recvBuffer, (int)this->recvBuffer.size(), (int)this->recvBuffer.capacity(), len);
    if (len > this->recvBuffer.size_used()) {
        len = (U32)this->recvBuffer.size_used();
    }
    this->recvBuffer.get(buffer, len);
    BOXEDWINE_CONDITION_SIGNAL_ALL(this->lockCond);
    //printf("    readNative: %0.8X size=%d capacity=%d writeLen=%d", (int)&this->recvBuffer, (int)this->recvBuffer.size(), (int)this->recvBuffer.capacity(), len);
    return len;
}

U32 KUnixSocketObject::read(KThread* thread, U32 buffer, U32 len) {
    return read(thread, buffer, len, 0);
}

U32 KUnixSocketObject::read(KThread* thread, U32 buffer, U32 len, U32 flags) {
    return read(thread, buffer, len, flags, nullptr);
}

U32 KUnixSocketObject::read(KThread* thread, U32 buffer, U32 len, U32 flags, ReadSource* source) {
    bool peek = false;
    if (flags & K_MSG_PEEK) {
        peek = true;
        flags &= ~K_MSG_PEEK;
    }
    if (flags) {
        kwarn_fmt("KUnixSocketObject::recvfrom unhandled flags=%x", flags);
    }

    U32 count = 0;
    this->pid = thread->process->id; // kind of a hack to do this here
    std::shared_ptr<KUnixSocketObject> con = this->connection.lock();
    KMemory* memory = thread->memory;

    if (!memory->canWrite(buffer, len)) {
        return -K_EFAULT;
    }
    // FD-110: the EPIPE is the unconnected-stream answer. A datagram
    // socket's input is its message queue, connected or not, and a
    // queued message is readable input for any socket; neither may
    // take the EPIPE.
    if (!this->inClosed && !con && this->type != K_SOCK_DGRAM && this->msgs.empty())
        return -K_EPIPE;
    con = nullptr; // don't hold a strong reference to this, if we are blocking then it would prevent the con object from being destroyed when its process is closed
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
    while (this->recvBuffer.size_used()==0 && this->msgs.empty()) {
        if (this->inClosed) {
            return 0;
        }
        if (!this->blocking) {
            return -K_EWOULDBLOCK;
        }
        BOXEDWINE_CONDITION_WAIT_SIGNALABLE(this->lockCond);
#ifdef BOXEDWINE_MULTI_THREADED
		if (thread->terminating) {
			return -K_EINTR;
		}
        if (thread->startSignal) {
            thread->startSignal = false;
            return -K_CONTINUE;
        }
#ifdef BOXEDWINE_GUEST_X64
        if (thread->interruptBlockedSyscall()) {
            return -K_EINTR;
        }
#endif
#endif
    }
    if (!this->msgs.empty()) {
        // FD-110: data queued as a message is received by the plain
        // read paths too, with the host's semantics (measured,
        // test-results/fd109-112/hostref-read-fd110.c): a datagram
        // socket returns the front datagram whole -- segments
        // concatenated, an over-long datagram truncated with the
        // remainder discarded, exactly one datagram per call; a
        // stream socket reads across the queued messages as bytes.
        // The queue is consulted before the stream buffer, the same
        // order recvmsg already used; a program interleaving write()
        // and sendmsg() on one stream socket can observe the two
        // stores out of call order, the settled divergence recorded
        // with FD-110 (the stores carry no shared sequence).
        std::shared_ptr<KSocketMsg> front;
        // Stage through a bounded buffer: the copy never takes more
        // than the payload actually queued (for a datagram, the
        // front message's payload), however large the guest's
        // receive length is.
        U32 payload = 0;
        {
            std::queue<std::shared_ptr<KSocketMsg>> walk = this->msgs;
            while (!walk.empty()) {
                const std::vector<U8>& data = walk.front()->data;
                for (U32 pos = 0; pos + 4 <= (U32)data.size();) {
                    const U32 segLen = (U32)data[pos] | ((U32)data[pos + 1] << 8) |
                        ((U32)data[pos + 2] << 16) | ((U32)data[pos + 3] << 24);
                    pos += 4 + segLen;
                    payload += segLen;
                }
                walk.pop();
                if (this->isRecordType()) {
                    break;
                }
            }
        }
        std::vector<U8> bytes(std::min(len, payload));
        count = copyFromQueue((U32)bytes.size(), !peek, bytes.data(), &front);
        if (count) {
            memory->memcpy(buffer, bytes.data(), count);
        }
        if (source && front) {
            source->senderAddr = front->senderAddr;
        }
        return count;
    }
    count = std::min(len, (U32)this->recvBuffer.size_used());
    if (peek) {
        U32 availableOnPage = K_PAGE_SIZE - (buffer & K_PAGE_MASK);
        if (count <= availableOnPage) {
            memory->performOnMemory(buffer, count, false, [this](U8* ram, U32 len) {
                this->recvBuffer.peek(ram, len);
                return true;
                });
        } else {
            U8* tmp = new U8[count];
            this->recvBuffer.peek(tmp, count);
            U8* p = tmp;
            memory->performOnMemory(buffer, count, false, [&p](U8* ram, U32 len) {
                memcpy(ram, p, len);
                p += len;
                return true;
                });
            delete[] tmp;
        }
    }
    else {
        memory->performOnMemory(buffer, count, false, [this](U8* ram, U32 len) {
            this->recvBuffer.get(ram, len);
            return true;
            });
    }
    con = this->connection.lock();
    if (con) {
        BOXEDWINE_CONDITION_SIGNAL_ALL(this->lockCond);
    }
    if (source && con) {
        // Stream-buffer bytes come from the connected peer; its
        // bound address is the source recvfrom reports (FD-109).
        source->senderAddr = con->localAddr;
    }

    return count;
}

U32 KUnixSocketObject::stat(KProcess* process, U32 address, bool is64) {
    KSystem::writeStat(process, B(""), address, is64, true, (this->node?this->node->id:0), K_S_IFSOCK|K__S_IWRITE|K__S_IREAD, (this->node?this->node->rdev:0), 0, 4096, 0, this->lastModifiedTime, 1);
    return 0;
}

U32 KUnixSocketObject::map(KThread* thread, U32 address, U32 len, S32 prot, S32 flags, U64 off) {
    return 0;
}

bool KUnixSocketObject::canMap() {
    return false;
}

BString KUnixSocketObject::selfFd() {
    return B("anon_inode:[pipe]");
}

S64 KUnixSocketObject::seek(S64 pos) {
    return -K_ESPIPE;
}

S64 KUnixSocketObject::getPos() {
    return 0;
}

U32 KUnixSocketObject::ioctl(KThread* thread, U32 request) {
    return -K_ENOTTY;
}

bool KUnixSocketObject::supportsLocks() {
    return false;
}

S64 KUnixSocketObject::length() {
    return -1;
}

class UnixSocketNode : public FsNode {
public:
    UnixSocketNode(U32 id, U32 rdev, BString path, std::shared_ptr<FsNode> parent) : FsNode(Type::Socket, id, rdev, path, B(""), B(""), false, parent) {}
    U32 rename(BString path) override {return -K_EIO;}
    bool remove() override {if (!this->parent.lock()) return false; this->removeNodeFromParent(); return true;}
    U64 lastModified() override {return 0;}
    U64 length() override {return 0;}
    FsOpenNode* open(U32 flags) override {kwarn("unixsocket_open was called, this shouldn't happen.  syscall_open should detect we have a kobject already"); return nullptr;}
    U32 getType(bool checkForLink) override {return 12;} // DT_SOCK
    U32 getMode() override {return K__S_IREAD | K__S_IWRITE | K_S_IFSOCK;}
    U32 removeDir() override {
        // rmdir() on a socket file: Linux answers ENOTDIR, since the
        // path is not a directory. This used to kpanic.
        kwarn_fmt("BW64-FAIL unixsocket-removeDir outcome=returned-ENOTDIR");
        return -K_ENOTDIR;
    }
    U32 setTimes(U64 lastAccessTime, U32 lastAccessTimeNano, U64 lastModifiedTime, U32 lastModifiedTimeNano) override {klog("UnixSocket::setTimes not implemented"); return 0;}
};

U32 KUnixSocketObject::bind(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 len) {
    KMemory* memory = thread->memory;

    U32 family = memory->readw(address);
    if (family==K_AF_UNIX) {
        if (len > 2 && memory->readb(address + 2) == 0) {
            // FD-111: an abstract name (sun_path[0] == 0) has no
            // filesystem node -- socketAddressName reads it as the
            // empty string and the bind used to record nothing at
            // all. Register the name (the bytes after the leading
            // NUL, length-exact) so addressed sends and datagram
            // connects can resolve it, and record the socket's own
            // address for FD-109's sender report. Both the key and
            // the recorded image clamp to the sockaddr_un size, as
            // the kernel's copy does.
            std::string key;
            const U32 keyLen = std::min<U32>(len - 3, 107);
            if (len > 3) {
                key.resize(keyLen);
                memory->memcpy(&key[0], address + 3, keyLen);
            }
            std::shared_ptr<KUnixSocketObject> self = std::dynamic_pointer_cast<KUnixSocketObject>(fd->kobject);
            {
                std::lock_guard<std::mutex> guard(abstractRegistryMutex());
                auto it = abstractRegistry().find(key);
                if (it != abstractRegistry().end() && !it->second.expired()) {
                    return -K_EADDRINUSE;
                }
                abstractRegistry()[key] = self;
            }
            this->abstractName = key;
            this->abstractRegistered = true;
            const U32 imageLen = std::min<U32>(len, 110);
            this->localAddr.resize(imageLen);
            memory->memcpy(this->localAddr.data(), address, imageLen);
            return 0;
        }
        BString name = socketAddressName(memory, address, len);

        if (name.length() == 0) {
            return 0; // :TODO: why does XOrg need this
        }
        std::shared_ptr<FsNode> node = Fs::getNodeFromLocalPath(thread->process->fsContext->currentDirectory, name, true);
        if (node) {
            return -K_EADDRINUSE;
        }        
        BString fullpath = Fs::getFullPath(thread->process->fsContext->currentDirectory, name);
        this->destAddress.family = family;
        strncpy(this->destAddress.data, fullpath.c_str(), sizeof(this->destAddress.data));
		this->destAddress.data[sizeof(this->destAddress.data) - 1] = 0;
        std::shared_ptr<FsNode> parentNode = Fs::getNodeFromLocalPath(B(""), Fs::getParentPath(fullpath), true);
        std::shared_ptr<UnixSocketNode> socketNode = std::make_shared<UnixSocketNode>(0, 2, fullpath, parentNode);
        parentNode->addChild(socketNode);
        std::shared_ptr<KUnixSocketObject> s = std::dynamic_pointer_cast<KUnixSocketObject>(fd->kobject);
        socketNode->kobject = fd->kobject;
        s->node = socketNode;
        // FD-109: record the socket's own address as the guest bound
        // it (the literal sun_path bytes, NUL-terminated): receivers
        // report exactly this for the socket's queued messages,
        // with length 2 + strlen + 1 as measured on the host.
        const U32 pathLen = memory->strlen(address + 2);
        this->localAddr.resize(2 + pathLen + 1);
        this->localAddr[0] = (U8)(family & 0xFF);
        this->localAddr[1] = (U8)(family >> 8);
        memory->memcpy(this->localAddr.data() + 2, address + 2, pathLen + 1);
        return 0;
    }
    return -K_EAFNOSUPPORT;
}

U32 KUnixSocketObject::connect(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 len) {
    KMemory* memory = thread->memory;

    if (len < 2) {
        return -K_EINVAL;
	}
    this->pid = thread->process->id;
    if (len>sizeof(this->destAddress.data)+2) {
        // Guest-supplied address longer than a sockaddr_un: Linux
        // answers ENAMETOOLONG for an over-long sun_path. This used
        // to kpanic on the guest's addrlen.
        if (bw64RecoverEnabled()) {
            bw64RecoverLogOnce("unixsocket.connect.nametoolong", "BW64-FAIL unixsocket-connect outcome=returned-ENAMETOOLONG");
        } else {
            kwarn_fmt("BW64-FAIL unixsocket-connect addrlen=%u outcome=returned-ENAMETOOLONG", len);
        }
        return -K_ENAMETOOLONG;
    }
    this->destAddress.family = memory->readw(address);
    memory->memcpy(this->destAddress.data, address + 2, len - 2);
    if (this->type==K_SOCK_DGRAM) {
        // FD-111: a datagram connect used to set the flag and answer
        // -EISCONN without linking any peer, so nothing the socket
        // sent could arrive anywhere. Measured on the host
        // (test-results/fd109-112/hostref-connect-fd111.c): connect
        // links the named peer and returns 0; reconnecting switches
        // the peer; an AF_UNSPEC address disconnects (and later
        // sends answer ENOTCONN); an unbound peer name answers
        // ENOENT for a path, ECONNREFUSED for an abstract name,
        // EPROTOTYPE when a stream socket is bound there. The
        // /dev/log convention is preserved exactly: no socket is
        // bound there in the guest, and the write paths print to it
        // via destAddress.
        if (this->destAddress.family == 0) {
            this->connection.reset();
            this->oneWayPeerLink = false;
            this->connected = false;
            this->peerCloseRefusalPending = false;
            this->oneWayRefusalSpent = false;
            return 0;
        }
        if (!strcmp(this->destAddress.data, "/dev/log")) {
            this->connected = true;
            return -K_EISCONN;
        }
        U32 err = 0;
        std::shared_ptr<KUnixSocketObject> dest = resolveNamedPeer(thread, address, len, err);
        if (!dest) {
            return -(S32)err;
        }
        this->connection = dest;
        this->oneWayPeerLink = true;
        this->connected = true;
        this->peerCloseRefusalPending = false;
        this->oneWayRefusalSpent = false;
        return 0;
    } else if (this->type==K_SOCK_STREAM || this->type==K_SOCK_SEQPACKET) {
        if (this->destAddress.data[0]==0) {
            // :TODO: why
            memory->memcpy(this->destAddress.data, address + 3, len - 3);
        }
        if (this->destAddress.data[0] == 0) {
            return -K_ENOENT;
        }
        if (this->domain==K_AF_UNIX) {
            if (getenv("BW64_SCDUMP")) {
                klog_fmt("KUnixSocket::connect AF_UNIX path='%s' isX=%d",
                         this->destAddress.data,
                         (int)XWireServer::isXDisplayPath(this->destAddress.data));
            }
            // In-process X11 wire server: a guest connect to /tmp/.X11-unix/X<n>
            // is wine's real libX11 reaching for an X server. There is no real
            // X server in the rootfs, so hand the connection to our SDL-backed
            // wire server instead of returning ECONNREFUSED (which makes libX11
            // fall back to a TCP :6000 probe and then run headless/spinning).
            if (this->type==K_SOCK_STREAM && XWireServer::isXDisplayPath(this->destAddress.data)) {
                std::shared_ptr<KUnixSocketObject> self = std::dynamic_pointer_cast<KUnixSocketObject>(shared_from_this());
                if (XWireServer::instance().acceptConnection(self)) {
                    this->connected = true;
                    return 0;
                }
                this->destAddress.family = 0;
                return -K_ECONNREFUSED;
            }
            std::shared_ptr<FsNode> node = Fs::getNodeFromLocalPath(thread->process->fsContext->currentDirectory, BString::copy(this->destAddress.data), true);
            std::shared_ptr<KObject> kobject;
            if (node) {
                kobject = node->kobject.lock();
            }
            if (!node || !kobject || kobject->type!=KTYPE_UNIX_SOCKET) {
                this->destAddress.family = 0;
                return -K_ECONNREFUSED;
            }     
            std::shared_ptr<KUnixSocketObject> con = this->connection.lock();
            if (con) {
                this->connected = true;
                return 0;
            }     
            con = nullptr; // don't hold a strong reference to this, if we are blocking then it would prevent the con object from being destroyed when its process is closed
            if (!this->connecting.expired()) {
                if (this->blocking) {
                    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
                    while (!this->connecting.expired()) {
                        BOXEDWINE_CONDITION_WAIT(this->lockCond);
#ifdef BOXEDWINE_MULTI_THREADED
						if (thread->terminating) {
							return -K_EINTR;
						}
                        if (thread->startSignal) {
                            thread->startSignal = false;
                            return -K_CONTINUE;
                        }
#endif
                    }
                    if (this->connection.expired()) {
                        return -K_ECONNREFUSED;
                    }
                    this->connected = true;
                    return 0;
                } else {
                    return -K_EINPROGRESS;
                }
            } else {
                std::shared_ptr<KUnixSocketObject> destination = std::dynamic_pointer_cast<KUnixSocketObject>(node->kobject.lock());

                // FD-118 (host-measured): a connect whose type
                // differs from the bound socket's answers
                // -EPROTOTYPE, for stream and seqpacket clients
                // alike; the connect used to queue as a pending
                // connection whatever object was bound there.
                if (destination && destination->type != this->type) {
                    this->destAddress.family = 0;
                    return -K_EPROTOTYPE;
                }
                this->connecting = destination;
                {
                    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(destination->lockCond);
                    std::shared_ptr< KUnixSocketObject> t = std::dynamic_pointer_cast<KUnixSocketObject>(shared_from_this());
                    destination->pendingConnections.push_back(t);
                    BOXEDWINE_CONDITION_SIGNAL_ALL(destination->lockCond);
                }

                if (!this->blocking) {
                    return -K_EINPROGRESS;
                }
                // :TODO: what about a time out
                
                BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
                while (!this->connecting.expired()) {
                    BOXEDWINE_CONDITION_WAIT(this->lockCond);
#ifdef BOXEDWINE_MULTI_THREADED
					if (thread->terminating) {
						return -K_EINTR;
					}
                    if (thread->startSignal) {
                        thread->startSignal = false;
                        return -K_CONTINUE;
                    }
#endif
                }
                if (this->connection.expired()) {
                    return -K_ECONNREFUSED;
                }
                this->connected = true;
                return 0;
            }
        } else {
            if (bw64RecoverEnabled()) {
                bw64RecoverLogOnce("unixsocket.connect.domain", "BW64-FAIL unixsocket-connect outcome=returned-EAFNOSUPPORT");
            } else {
                kwarn_fmt("BW64-FAIL unixsocket-connect domain=%d outcome=returned-EAFNOSUPPORT", this->domain);
            }
            return -K_EAFNOSUPPORT;
        }
    } else {
        kwarn_fmt("connect not implemented for type %d", this->type);
        return -K_ECONNREFUSED;
    }
    // should never get here
    return 0;
}

U32 KUnixSocketObject::listen(KThread* thread, const KFileDescriptorPtr& fd, U32 backlog) {
    if (!this->node) {
        return -K_EDESTADDRREQ;
    }
    if (!this->connection.expired() || !this->connecting.expired()) {
        return -K_EINVAL;
    }
    this->listening = true;
    return 0;
}

U32 KUnixSocketObject::accept(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 len, U32 flags) {
    std::shared_ptr<KUnixSocketObject> pendingConnection;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
        while (!this->pendingConnections.size()) {
            if (!this->blocking) {
                return -K_EWOULDBLOCK;
            }
            BOXEDWINE_CONDITION_WAIT(this->lockCond);
    #ifdef BOXEDWINE_MULTI_THREADED
            if (thread->terminating) {
                return -K_EINTR;
            }
            if (thread->startSignal) {
                thread->startSignal = false;
                return -K_CONTINUE;
            }
    #endif
        }

        pendingConnection = this->pendingConnections.front().lock();
        this->pendingConnections.pop_front();
    }
    if (pendingConnection == nullptr) {
        return -K_ECONNABORTED;
	}

    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(pendingConnection->lockCond);
    std::shared_ptr<KUnixSocketObject> resultSocket = std::make_shared<KUnixSocketObject>(domain, type, protocol);
    KFileDescriptorPtr result = thread->process->allocFileDescriptor(resultSocket, K_O_RDWR, 0, -1, 0);
    if (!result) {
        // FD-05: EMFILE leaves the pending connection queued, as on
        // Linux; the connector keeps waiting for a later accept.
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
        this->pendingConnections.push_front(pendingConnection);
        BOXEDWINE_CONDITION_SIGNAL_ALL(this->lockCond);
        return -K_EMFILE;
    }

    if (flags & FD_CLOEXEC) {
        result->descriptorFlags|=FD_CLOEXEC;
    }
    if (flags & K_O_NONBLOCK) {
        result->kobject->setBlocking(false);
    }

    pendingConnection->connection = resultSocket;
    pendingConnection->connecting.reset();
    //pendingConnection->connected = true; this will be handled when the connecting thread is unblocked    

    resultSocket->connected = true;
    resultSocket->connection = pendingConnection; // weak reference
    
    BOXEDWINE_CONDITION_SIGNAL_ALL(pendingConnection->lockCond);

    return result->handle;
}

U32 KUnixSocketObject::getsockname(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 plen) {
    KMemory* memory = thread->memory;

    U32 len = memory->readd( plen);
    if (this->domain == K_AF_UNIX) {
        memory->writew(address, this->destAddress.family);
        len-=2;
        if (len>sizeof(this->destAddress.data))
            len = sizeof(this->destAddress.data);
        memory->memcpy(address + 2, this->destAddress.data, len);
        memory->writed(plen, 2 + (U32)strlen(this->destAddress.data) + 1);
        return 0;
    }
    kwarn_fmt("KUnixSocketObject::getsockname not implemented for domain %d", this->domain);
    return 0;
}

U32 KUnixSocketObject::getpeername(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 plen) {
    KMemory* memory = thread->memory;

    if (this->connection.expired())
        return -K_ENOTCONN;
    U32 len = memory->readd( plen);
    memory->writew(address, this->destAddress.family);
    len-=2;
    if (len>sizeof(this->destAddress.data))
        len = sizeof(this->destAddress.data);
    memory->memcpy(address + 2, this->destAddress.data, len);
    memory->writed(plen, 2 + (U32)strlen(this->destAddress.data) + 1);
    return 0;
}

U32 KUnixSocketObject::shutdown(KThread* thread, const KFileDescriptorPtr& fd, U32 how) {
    if (this->type == K_SOCK_DGRAM) {
        kwarn("shutdown on SOCK_DGRAM not implemented");
        return -1;
    }
    std::shared_ptr<KUnixSocketObject> con = this->connection.lock();
    if (!con) {
        return -K_ENOTCONN;
    }
    if (how == K_SHUT_RD) {
        {
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
            this->inClosed = true;
            BOXEDWINE_CONDITION_SIGNAL_ALL(this->lockCond);
        }
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(con->lockCond);
        con->outClosed = true;
        BOXEDWINE_CONDITION_SIGNAL_ALL(con->lockCond);
    } else if (how == K_SHUT_WR) {
        {
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
            this->outClosed = true;
            BOXEDWINE_CONDITION_SIGNAL_ALL(this->lockCond);
        }
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(con->lockCond);
        con->inClosed = true;
        BOXEDWINE_CONDITION_SIGNAL_ALL(con->lockCond);
    } else if (how == K_SHUT_RDWR) {
        {
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
            this->outClosed = true;
            this->inClosed = true;
            BOXEDWINE_CONDITION_SIGNAL_ALL(this->lockCond);
        }
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(con->lockCond);
        con->outClosed = true;
        con->inClosed = true;
        BOXEDWINE_CONDITION_SIGNAL_ALL(con->lockCond);
    }
    return 0;
}

U32 KUnixSocketObject::setsockopt(KThread* thread, const KFileDescriptorPtr& fd, U32 level, U32 name, U32 value, U32 len) {
    KMemory* memory = thread->memory;

    if (level == K_SOL_SOCKET) {
        if (name == K_SO_DETACH_FILTER) {
            if (this->socketFilter.empty()) return -K_EINVAL;
            this->socketFilter.clear();
            return 0;
        }
        if (name == K_SO_ATTACH_FILTER) {
            if (len < 8) return -K_EINVAL;
            if (!memory->canRead(value, 8)) return -K_EFAULT;
            const U32 instructionCount = memory->readw(value);
            const U32 instructionsAddress = memory->readd(value + 4);
            if (!instructionCount || instructionCount > 4096 ||
                instructionCount > (UINT32_MAX / sizeof(KClassicBpfInsn))) return -K_EINVAL;
            const U32 bytes = instructionCount * sizeof(KClassicBpfInsn);
            if (!memory->canRead(instructionsAddress, bytes)) return -K_EFAULT;
            std::vector<KClassicBpfInsn> program(instructionCount);
            for (U32 i = 0; i < instructionCount; ++i) {
                const U32 at = instructionsAddress + i * sizeof(KClassicBpfInsn);
                program[i].code = memory->readw(at);
                program[i].jt = memory->readb(at + 2);
                program[i].jf = memory->readb(at + 3);
                program[i].k = memory->readd(at + 4);
                const U16 code = program[i].code;
                const U16 cls = code & 0x07;
                const U16 mode = code & 0xe0;
                const U16 size = code & 0x18;
                bool valid = false;
                if (cls == 0x00) valid =
                    (mode == 0x00 && size == 0x00) ||
                    ((mode == 0x20 || mode == 0x40) && (size == 0x00 || size == 0x08 || size == 0x10)) ||
                    (mode == 0x60 && size == 0x00) || (mode == 0x80 && size == 0x00);
                else if (cls == 0x01) valid =
                    (mode == 0x00 && size == 0x00) || (mode == 0x60 && size == 0x00) ||
                    (mode == 0x80 && size == 0x00) || (mode == 0xa0 && size == 0x10);
                else if (cls == 0x02 || cls == 0x03) valid = code == cls && program[i].k < 16;
                else if (cls == 0x04) {
                    const U16 op = code & 0xf0;
                    valid = op == 0x00 || op == 0x10 || op == 0x20 || op == 0x30 || op == 0x40 ||
                            op == 0x50 || op == 0x60 || op == 0x70 || op == 0x80 || op == 0x90 || op == 0xa0;
                    if (op == 0x80 && (code & 0x08)) valid = false;
                    if ((op == 0x30 || op == 0x90) && !(code & 0x08) && !program[i].k) valid = false;
                } else if (cls == 0x05) {
                    const U16 op = code & 0xf0;
                    if (op == 0x00) valid = code == 0x05 && !program[i].jt && !program[i].jf &&
                                             i + 1 + program[i].k < instructionCount;
                    else if (op == 0x10 || op == 0x20 || op == 0x30 || op == 0x40)
                        valid = i + 1 + program[i].jt < instructionCount && i + 1 + program[i].jf < instructionCount;
                } else if (cls == 0x06) valid = code == 0x06 || code == 0x16;
                else if (cls == 0x07) valid = code == 0x07 || code == 0x87;
                if (!valid) return -K_EINVAL;
            }
            if ((program.back().code & 0x07) != 0x06) return -K_EINVAL;
            this->socketFilter.swap(program);
            return 0;
        }
        if (name == K_SO_LINGER) {
            if (len < 8) return -K_EINVAL;
            const S32 enabled = (S32)memory->readd(value);
            const S32 seconds = (S32)memory->readd(value + 4);
            if ((enabled != 0 && enabled != 1) || seconds < 0) return -K_EINVAL;
            this->lingerEnabled = (U32)enabled;
            this->lingerSeconds = (U32)seconds;
            return 0;
        }
        if (name == K_SO_RCVTIMEO || name == K_SO_SNDTIMEO) {
            if (len < 8) return -K_EINVAL;
            const U32 seconds = memory->readd(value);
            const U32 microseconds = memory->readd(value + 4);
            if (microseconds >= 1000000) return -K_EINVAL;
            if (name == K_SO_RCVTIMEO) {
                this->recvTimeoutSeconds = seconds;
                this->recvTimeoutMicroseconds = microseconds;
            } else {
                this->sendTimeoutSeconds = seconds;
                this->sendTimeoutMicroseconds = microseconds;
            }
            return 0;
        }
        if (len < 4) return -K_EINVAL;
        const S32 optionValue = (S32)memory->readd(value);
        switch (name) {
            case K_SO_RCVBUFFORCE:
            case K_SO_RCVBUF:
                if (optionValue <= 0) return -K_EINVAL;
                if (name == K_SO_RCVBUFFORCE && thread->process->userId != 0) return -K_EPERM;
                this->recvLen = (U32)std::min<S64>((S64)optionValue * 2, 0x7fffffff);
                break;
            case K_SO_SNDBUFFORCE:
            case K_SO_SNDBUF:
                if (optionValue <= 0) return -K_EINVAL;
                if (name == K_SO_SNDBUFFORCE && thread->process->userId != 0) return -K_EPERM;
                this->sendLen = (U32)std::min<S64>((S64)optionValue * 2, 0x7fffffff);
                break;
            case K_SO_PRIORITY:
                if (optionValue < 0 || optionValue > 6) return -K_EPERM;
                this->priority = (U32)optionValue;
                break;
            case K_SO_RCVLOWAT:
                this->recvLowWater = (U32)std::max<S32>(1, optionValue);
                break;
            case K_SO_SNDLOWAT:
                kwarn_fmt("ksocket: Unix SOL_SOCKET option %u is unsupported; returned ENOPROTOOPT", name);
                return -K_ENOPROTOOPT;
            case K_SO_DEBUG:
            case K_SO_REUSEADDR:
            case K_SO_DONTROUTE:
            case K_SO_BROADCAST:
            case K_SO_KEEPALIVE:
            case K_SO_OOBINLINE:
            case K_SO_NO_CHECK:
            case K_SO_BSDCOMPAT:
            case K_SO_REUSEPORT:
            case K_SO_PASSCRED:
            case K_SO_TIMESTAMP:
                if (optionValue) this->socketOptionFlags |= 1ULL << name;
                else this->socketOptionFlags &= ~(1ULL << name);
                break;
            default:
                kwarn_fmt("ksocket: Unix SOL_SOCKET option %u is unsupported; returned ENOPROTOOPT", name);
                return -K_ENOPROTOOPT;
        }
    } else {
        kwarn_fmt("ksocket: Unix socket option level %u is unsupported; returned ENOPROTOOPT", level);
        return -K_ENOPROTOOPT;
    }
    return 0;
}

U32 KUnixSocketObject::getsockopt(KThread* thread, const KFileDescriptorPtr& fd, U32 level, U32 name, U32 value, U32 len_address) {
    KMemory* memory = thread->memory;

    if (!len_address || !value) return -K_EFAULT;
    U32 len = memory->readd(len_address);
    auto writeInt = [&](U32 v) -> U32 {
        const U32 copied = std::min<U32>(len, 4);
        for (U32 i = 0; i < copied; ++i) memory->writeb(value + i, (U8)(v >> (i * 8)));
        memory->writed(len_address, 4);
        return 0;
    };
    if (level == K_SOL_SOCKET) {
        auto writePair = [&](U32 first, U32 second) -> U32 {
            const U32 copied = std::min<U32>(len, 8);
            if (copied) memory->writed(value, first);
            if (copied > 4) memory->writed(value + 4, second);
            memory->writed(len_address, 8);
            return 0;
        };
        if (name == K_SO_LINGER) return writePair(this->lingerEnabled, this->lingerSeconds);
        if (name == K_SO_RCVTIMEO) return writePair(this->recvTimeoutSeconds, this->recvTimeoutMicroseconds);
        if (name == K_SO_SNDTIMEO) return writePair(this->sendTimeoutSeconds, this->sendTimeoutMicroseconds);
        if (name == K_SO_RCVBUF) {
            return writeInt(this->recvLen);
        } else if (name == K_SO_SNDBUF) {
            return writeInt(this->sendLen);
        } else if (name == K_SO_ERROR) {
            U32 result = writeInt(this->error);
            this->error = 0;
            return result;
        } else if (name == K_SO_TYPE) { 
            return writeInt(this->type);
        } else if (name == K_SO_PRIORITY) {
            return writeInt(this->priority);
        } else if (name == K_SO_RCVLOWAT) {
            return writeInt(this->recvLowWater);
        } else if (name == K_SO_DOMAIN) {
            return writeInt(this->domain);
        } else if (name == K_SO_PROTOCOL) {
            return writeInt(this->protocol);
        } else if (name == K_SO_ACCEPTCONN) {
            return writeInt(this->listening ? 1 : 0);
        } else if (name == K_SO_DEBUG || name == K_SO_REUSEADDR || name == K_SO_DONTROUTE ||
                   name == K_SO_BROADCAST || name == K_SO_KEEPALIVE || name == K_SO_OOBINLINE ||
                   name == K_SO_NO_CHECK || name == K_SO_BSDCOMPAT || name == K_SO_REUSEPORT ||
                   name == K_SO_PASSCRED || name == K_SO_TIMESTAMP) {
            return writeInt((this->socketOptionFlags & (1ULL << name)) ? 1 : 0);
        } else if (name == K_SO_PEERCRED) {
            if (this->domain!=K_AF_UNIX) {
                return -K_EINVAL; // :TODO: is this right
            }
            std::shared_ptr<KUnixSocketObject> con = this->connection.lock();
            if (!con) {
                return -K_EINVAL; // :TODO: is this right
            }
            if (len < 12) return -K_EINVAL;
            memory->writed(value, con->pid);
            memory->writed(value + 4, thread->process->userId);
            memory->writed(value + 8, thread->process->groupId);
            memory->writed(len_address, 12);
            return 0;
        } else {
            kwarn_fmt("ksocket: Unix SOL_SOCKET getsockopt %u is unsupported; returned ENOPROTOOPT", name);
            return -K_ENOPROTOOPT;
        }
    } else {
        kwarn_fmt("ksocket: Unix getsockopt level %u is unsupported; returned ENOPROTOOPT", level);
        return -K_ENOPROTOOPT;
    }
    return 0;
}

std::shared_ptr<KUnixSocketObject> KUnixSocketObject::resolveNamedPeer(KThread* thread, U32 address, U32 len, U32& err) {
    KMemory* memory = thread->memory;
    err = 0;
    // Measured on the host (test-results/fd109-112/hostref-connect-fd111.c,
    // hostref-names-fd109.c): a destination that is not an AF_UNIX name,
    // or carries no name bytes, answers EINVAL.
    if (len <= 2 || memory->readw(address) != K_AF_UNIX) {
        err = K_EINVAL;
        return nullptr;
    }
    if (memory->readb(address + 2) == 0) {
        // Abstract name: the bytes after the leading NUL marker,
        // length-exact, clamped to the sockaddr_un size exactly as
        // bind() keys them. An unbound abstract name answers
        // ECONNREFUSED.
        std::string key;
        if (len > 3) {
            key.resize(std::min<U32>(len - 3, 107));
            memory->memcpy(&key[0], address + 3, (U32)key.size());
        }
        std::lock_guard<std::mutex> guard(abstractRegistryMutex());
        auto it = abstractRegistry().find(key);
        if (it != abstractRegistry().end()) {
            std::shared_ptr<KUnixSocketObject> dest = it->second.lock();
            if (dest) {
                if (dest->type != this->type) {
                    err = K_EPROTOTYPE;
                    return nullptr;
                }
                return dest;
            }
            abstractRegistry().erase(it);
        }
        err = K_ECONNREFUSED;
        return nullptr;
    }
    BString name = socketAddressName(memory, address, len);
    if (name.length() == 0) {
        err = K_EINVAL;
        return nullptr;
    }
    std::shared_ptr<FsNode> node = Fs::getNodeFromLocalPath(thread->process->fsContext->currentDirectory, name, true);
    if (!node) {
        // No filesystem entry at all (measured ENOENT).
        err = K_ENOENT;
        return nullptr;
    }
    std::shared_ptr<KObject> kobject = node->kobject.lock();
    if (!kobject || kobject->type != KTYPE_UNIX_SOCKET) {
        // The path exists but no live socket is bound to it (a
        // regular file, measured ECONNREFUSED).
        err = K_ECONNREFUSED;
        return nullptr;
    }
    std::shared_ptr<KUnixSocketObject> dest = std::dynamic_pointer_cast<KUnixSocketObject>(kobject);
    if (!dest) {
        err = K_ECONNREFUSED;
        return nullptr;
    }
    if (dest->type != this->type) {
        err = K_EPROTOTYPE;
        return nullptr;
    }
    return dest;
}

void KUnixSocketObject::queueStreamBytesLocked(KThread* thread, KUnixSocketObject* dest, const U8* bytes, U32 len) {
    std::shared_ptr<KSocketMsg> msg = std::make_shared<KSocketMsg>();
    // thread may be null when the queueing caller is a native write
    // path off a non-guest thread; the credential fields are
    // informational then and stay 0 (as in queueDatagramLocked).
    if (thread) {
        msg->senderPid = thread->process->id;
        msg->senderUid = thread->process->userId;
        msg->senderGid = thread->process->groupId;
    }
    msg->senderAddr = this->localAddr;
    msg->data.push_back((U8)len);
    msg->data.push_back((U8)(len >> 8));
    msg->data.push_back((U8)(len >> 16));
    msg->data.push_back((U8)(len >> 24));
    msg->data.insert(msg->data.end(), bytes, bytes + len);
    dest->msgs.push(msg);
}

void KUnixSocketObject::flushRecvBufferToQueueLocked(KThread* thread, KUnixSocketObject* sender) {
    const U32 buffered = (U32)this->recvBuffer.size_used();
    if (!buffered) {
        return;
    }
    std::vector<U8> bytes(buffered);
    this->recvBuffer.get(bytes.data(), buffered);
    sender->queueStreamBytesLocked(thread, this, bytes.data(), buffered);
}

bool KUnixSocketObject::isRecordType() const {
    return this->type == K_SOCK_DGRAM || this->type == K_SOCK_SEQPACKET;
}

U32 KUnixSocketObject::peerCloseSendAnswer(KThread* thread, bool noSignal) {
    // FD-119 (host-measured, test-results/fd118-119/): on a datagram
    // pair, closing the peer makes the survivor's FIRST connected
    // send answer -ECONNREFUSED and later sends -ENOTCONN, on every
    // send path, whichever path sends first; receiving queued data
    // first does not spend the refusal, and neither does an
    // addressed send to another peer. The pair destructor arms
    // peerCloseRefusalPending on the survivor. A one-way datagram
    // link (FD-111) has no back-pointer for a destructor to mark,
    // so its refusal is spent lazily here: a socket that was
    // connected one-way and now has no peer object gets the same
    // one shot.
    if (this->type == K_SOCK_DGRAM) {
        if (this->peerCloseRefusalPending) {
            this->peerCloseRefusalPending = false;
            return -K_ECONNREFUSED;
        }
        if (this->oneWayPeerLink && this->connected && !this->oneWayRefusalSpent) {
            this->oneWayRefusalSpent = true;
            return -K_ECONNREFUSED;
        }
        return -K_ENOTCONN;
    }
    // FD-118: a seqpacket socket whose peer closed (or whose write
    // side shut down) answers -EPIPE, with the same thread split as
    // the connected broken-pipe case in sendDatagramToPeer; with
    // no peer and an open write side it was never connected and
    // answers -ENOTCONN.
    if (this->type == K_SOCK_SEQPACKET && this->outClosed) {
        if (!thread || thread->process->is64Bit) {
            return -K_EPIPE;
        }
        return writePipeClosed(thread, noSignal);
    }
    return -K_ENOTCONN;
}

U32 KUnixSocketObject::sendDatagramToPeer(KThread* thread, const std::shared_ptr<KUnixSocketObject>& con, const U8* packet, U32 len) {
    // FD-111: an unconnected datagram send answers ENOTCONN, as the
    // host's send path does; FD-119's one-shot refusal after a peer
    // close and FD-118's seqpacket answers live in
    // peerCloseSendAnswer.
    if (!con) {
        return this->peerCloseSendAnswer(thread, false);
    }
    if (this->outClosed) {
        // Same split as the stream path: a clean -EPIPE for 64-bit
        // threads, the SIGPIPE path for 32-bit ones.
        if (thread->process->is64Bit) {
            return -K_EPIPE;
        }
        return writePipeClosed(thread, false);
    }
    return queueDatagramLocked(thread, con, packet, len);
}

U32 KUnixSocketObject::queueDatagramLocked(KThread* thread, const std::shared_ptr<KUnixSocketObject>& dest, const U8* packet, U32 len) {
    std::shared_ptr<KSocketMsg> msg = std::make_shared<KSocketMsg>();
    // thread may be null when the queueing caller is the native
    // write path off a non-guest thread; the credential fields are
    // informational then and stay 0.
    if (thread) {
        msg->senderPid = thread->process->id;
        msg->senderUid = thread->process->userId;
        msg->senderGid = thread->process->groupId;
    }
    msg->senderAddr = this->localAddr;
    const U32 kept = dest->applySocketFilter(packet, len);
    if (len && !kept) {
        // Linux reports the submitted length for filtered packets.
        return len;
    }
    msg->data.push_back((U8)kept);
    msg->data.push_back((U8)(kept >> 8));
    msg->data.push_back((U8)(kept >> 16));
    msg->data.push_back((U8)(kept >> 24));
    msg->data.insert(msg->data.end(), packet, packet + kept);
    dest->msgs.push(msg);
    dest->notifyAsyncReady(K_POLL_IN);
    return len;
}

U32 KUnixSocketObject::copyFromQueue(U32 len, bool consume, U8* out, std::shared_ptr<KSocketMsg>* front) {
    if (front) {
        *front = this->msgs.empty() ? nullptr : this->msgs.front();
    }
    if (this->msgs.empty()) {
        return 0;
    }
    // A zero len copies nothing, but the consume rule below must
    // still run: it is what lets a receive retire an empty datagram.
    // std::queue exposes no iteration; snapshot the shared_ptrs so
    // the walk sees a stable sequence. For consume, the real queue
    // is trimmed by the copied byte count afterwards.
    std::vector<std::shared_ptr<KSocketMsg>> snapshot;
    {
        std::queue<std::shared_ptr<KSocketMsg>> copy = this->msgs;
        while (!copy.empty()) {
            snapshot.push_back(copy.front());
            copy.pop();
        }
    }
    // FD-118: the front-message-only rule is the record rule; it
    // covers seqpacket exactly as it covers datagram.
    const bool datagram = this->isRecordType();
    U32 copied = 0;
    for (size_t mi = 0; mi < snapshot.size() && copied < len; mi++) {
        const std::vector<U8>& data = snapshot[mi]->data;
        for (U32 pos = 0; pos + 4 <= (U32)data.size() && copied < len;) {
            const U32 segLen = (U32)data[pos] | ((U32)data[pos + 1] << 8) |
                ((U32)data[pos + 2] << 16) | ((U32)data[pos + 3] << 24);
            pos += 4;
            const U32 avail = std::min(segLen, (U32)data.size() - pos);
            const U32 take = std::min(avail, len - copied);
            if (take) {
                memcpy(out + copied, data.data() + pos, take);
            }
            copied += take;
            pos += avail;
            if (avail < segLen) {
                break; // malformed segment tail; stop this message
            }
        }
        if (datagram) {
            break; // one message per receive, as on the host
        }
    }
    if (consume) {
        if (datagram) {
            // A datagram is consumed whole even when the buffer
            // truncated it; an empty datagram is consumed by the
            // receive that reports it.
            U32 payload = 0;
            const std::vector<U8>& data = snapshot[0]->data;
            for (U32 pos = 0; pos + 4 <= (U32)data.size();) {
                const U32 segLen = (U32)data[pos] | ((U32)data[pos + 1] << 8) |
                    ((U32)data[pos + 2] << 16) | ((U32)data[pos + 3] << 24);
                pos += 4 + segLen;
                payload += segLen;
            }
            if (copied || !payload) {
                this->msgs.pop();
            }
        } else {
            U32 remaining = copied;
            while (remaining && !this->msgs.empty()) {
                std::shared_ptr<KSocketMsg> msg = this->msgs.front();
                U32 payload = 0;
                for (U32 pos = 0; pos + 4 <= (U32)msg->data.size();) {
                    const U32 segLen = (U32)msg->data[pos] | ((U32)msg->data[pos + 1] << 8) |
                        ((U32)msg->data[pos + 2] << 16) | ((U32)msg->data[pos + 3] << 24);
                    pos += 4 + segLen;
                    payload += segLen;
                }
                if (remaining < payload) {
                    U32 toTrim = remaining;
                    while (toTrim) {
                        const U32 segLen = (U32)msg->data[0] | ((U32)msg->data[1] << 8) |
                            ((U32)msg->data[2] << 16) | ((U32)msg->data[3] << 24);
                        if (toTrim >= segLen) {
                            msg->data.erase(msg->data.begin(), msg->data.begin() + 4 + segLen);
                            toTrim -= segLen;
                        } else {
                            const U32 rest = segLen - toTrim;
                            msg->data[0] = (U8)rest;
                            msg->data[1] = (U8)(rest >> 8);
                            msg->data[2] = (U8)(rest >> 16);
                            msg->data[3] = (U8)(rest >> 24);
                            msg->data.erase(msg->data.begin() + 4, msg->data.begin() + 4 + toTrim);
                            toTrim = 0;
                        }
                    }
                    remaining = 0;
                } else {
                    remaining -= payload;
                    this->msgs.pop();
                }
            }
        }
    }
    return copied;
}

U32 KUnixSocketObject::writePipeClosed(KThread* thread, bool noSignal) {
    if (noSignal || thread->process->sigActions[K_SIGPIPE].handlerAndSigAction == K_SIG_IGN || !thread->readyForSignal(K_SIGPIPE)) {
        return -K_EPIPE;
    }
    thread->runSignal(K_SIGPIPE, 0, 0);
    return -K_CONTINUE;
}

U32 KUnixSocketObject::sendmsg(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 flags) {
    MsgHdr hdr = {};
    KMemory* memory = thread->memory;
    U32 result = 0;
    std::shared_ptr<KUnixSocketObject> con = this->connection.lock();

    if (this->type == K_SOCK_DGRAM) {
        // FD-111: a destination in the message header is an
        // addressed send, and on the host it wins over a connected
        // peer for this call (measured,
        // test-results/fd109-112/hostref-connect-fd111.c). Resolve
        // it before the connection is consulted; the rest of this
        // function then runs unchanged against the destination.
        MsgHdr destHdr = {};
        readMsgHdr(thread, address, &destHdr);
        if (destHdr.msg_name) {
            U32 err = 0;
            std::shared_ptr<KUnixSocketObject> dest = resolveNamedPeer(thread, destHdr.msg_name, destHdr.msg_namelen, err);
            if (!dest) {
                return -(S32)err;
            }
            con = dest;
        }
    }

    if (!con) {
        if (this->type == K_SOCK_DGRAM) {
            // FD-119: the no-peer answer is the peer-close
            // accounting (the one-shot -ECONNREFUSED after a peer
            // close, else -ENOTCONN), not a plain ENOTCONN.
            return this->peerCloseSendAnswer(thread, (flags & K_MSG_NOSIGNAL) != 0);
        }
        if (this->type == K_SOCK_SEQPACKET) {
            // FD-118: a peer that closed with the write side shut
            // answers -EPIPE; never connected answers -ENOTCONN.
            if (this->outClosed) {
                return -K_EPIPE;
            }
            return -K_ENOTCONN;
        }
        if (this->type == K_SOCK_STREAM) {
            return -K_ENOTCONN;
        }
        // A socket of any other type with neither a connection nor
        // a header destination: the host answers ENOTCONN (measured;
        // the EDESTADDRREQ this used to return predates the
        // measurement).
        if (bw64RecoverEnabled()) {
            bw64RecoverLogOnce("unixsocket.sendmsg.nodest", "BW64-FAIL unixsocket-sendmsg outcome=returned-ENOTCONN");
        } else {
            kwarn_fmt("BW64-FAIL unixsocket-sendmsg type=%d outcome=returned-ENOTCONN", this->type);
        }
        return -K_ENOTCONN;
    }
    bool noSignal = (flags & K_MSG_NOSIGNAL) != 0;
    flags &= ~K_MSG_NOSIGNAL;
    if (flags) {
        kwarn_fmt("KUnixSocketObject::sendmsg unhandled flag=%x", flags);
    }
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(con->lockCond);
    // FD-119: a datagram socket's outClosed means only that its
    // pair peer died (its own shutdown is unimplemented); neither
    // a connected nor a name-resolved datagram sendmsg answers
    // for it here -- the no-peer tail holds the connected
    // accounting, and an addressed send delivers.
    if (this->outClosed && this->type != K_SOCK_DGRAM) {
        return writePipeClosed(thread, noSignal);
    }
    readMsgHdr(thread, address, &hdr);

    std::shared_ptr<KSocketMsg> msg = std::make_shared<KSocketMsg>();
    msg->senderPid = thread->process->id;
    msg->senderAddr = this->localAddr;
    msg->senderUid = thread->process->userId;
    msg->senderGid = thread->process->groupId;

    if (hdr.msg_control) {
        CMsgHdr cmsg;			

        readCMsgHdr(thread, hdr.msg_control, &cmsg);
        if (cmsg.cmsg_level != K_SOL_SOCKET) {
            if (bw64RecoverEnabled()) {
                bw64RecoverLogOnce("unixsocket.sendmsg.cmsglevel", "BW64-FAIL unixsocket-sendmsg outcome=returned-EINVAL");
            } else {
                kwarn_fmt("BW64-FAIL unixsocket-sendmsg cmsg-level=%d outcome=returned-EINVAL", cmsg.cmsg_level);
            }
            return -K_EINVAL;
        } else if (cmsg.cmsg_type != K_SCM_RIGHTS) {
            if (bw64RecoverEnabled()) {
                bw64RecoverLogOnce("unixsocket.sendmsg.cmsgtype", "BW64-FAIL unixsocket-sendmsg outcome=returned-EINVAL");
            } else {
                kwarn_fmt("BW64-FAIL unixsocket-sendmsg cmsg-type=%d outcome=returned-EINVAL", cmsg.cmsg_type);
            }
            return -K_EINVAL;
        } else if ((cmsg.cmsg_len & 3) != 0) {
            if (bw64RecoverEnabled()) {
                bw64RecoverLogOnce("unixsocket.sendmsg.cmsglen", "BW64-FAIL unixsocket-sendmsg outcome=returned-EINVAL");
            } else {
                kwarn_fmt("BW64-FAIL unixsocket-sendmsg cmsg-len=%d outcome=returned-EINVAL", cmsg.cmsg_len);
            }
            return -K_EINVAL;
        }

        for (U32 i=0;i<hdr.msg_controllen/16;i++) {
            KFileDescriptorPtr f = thread->process->getFileDescriptor(memory->readd(hdr.msg_control + 16 * i + 12));
            if (!f) {
                return -K_EBADF;
            } else {
                KSocketMsgObject d;
                d.object = f->kobject;
                d.accessFlags = f->accessFlags;
                // BW64_SOCKDTOR: witness the SENDER passing a fd via SCM_RIGHTS —
                // which pid sends which kobject. Pairs with the recvmsg install +
                // SOCKCLOSE pointer trace to see how a socket ends up shared across
                // processes (the dup-vs-share question).
                if (std::getenv("BW64_SOCKDTOR")) {
                    klog_fmt("SCMSEND: pid=%d tid=%d sends kobj=%p type=%d (use_count=%ld)",
                             (int)thread->process->id, (int)thread->id,
                             (void*)f->kobject.get(), (int)f->kobject->type,
                             f->kobject.use_count());
                }
                msg->objects.push_back(d);
            }
        }				
    }
    // The X11 wire server is a raw byte-stream endpoint, not a datagram/SCM
    // peer: winex11's libxcb writes core X requests through sendmsg(), but the
    // wire parser (XWireServerSocket::readNativeNonBlocking) reads from
    // recvBuffer and is driven by onPeerWrote() — exactly like write()/writev().
    // Routing these bytes into the length-prefixed msgs queue (which never
    // calls onPeerWrote) left the request bytes unparsed, so notepad blocked
    // forever after connecting. Feed XWire peers as a contiguous stream and
    // trigger the parser, the same wakeup path write() uses.
    bool isXWirePeer = std::dynamic_pointer_cast<XWireServerSocket>(con) != nullptr;
    if (isXWirePeer) {
        for (U32 i = 0; i < hdr.msg_iovlen; i++) {
            U32 p = memory->readd(hdr.msg_iov + 8 * i);
            U32 len = memory->readd(hdr.msg_iov + 8 * i + 4);
            while (len) {
                U8 byte = memory->readb(p++);
                con->recvBuffer.put(&byte, 1);
                len--;
                result++;
            }
        }
        BOXEDWINE_CONDITION_SIGNAL_ALL(con->lockCond);
        if (getenv("BW64_XWIRE")) {
            klog_fmt("KUnixSocket::sendmsg datalen=%d to XWire peer via recvBuffer", (int)result);
        }
        // onPeerWrote() runs the parser, which reacquires con->lockCond inside
        // readNativeNonBlocking. con->lockCond->m is a plain std::mutex, so we
        // must drop our lock first or self-deadlock. boxedWineCriticalSection is
        // the unique_lock created by BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION —
        // which only exists in the multi-threaded build; single-threaded has no
        // lock to drop (and no reentrancy to deadlock against), so skip it.
#ifdef BOXEDWINE_MULTI_THREADED
        boxedWineCriticalSection.unlock();
#endif
        con->onPeerWrote();
        return result;
    }
    for (U32 i=0;i<hdr.msg_iovlen;i++) {
        U32 p = memory->readd(hdr.msg_iov + 8 * i);
        U32 len = memory->readd(hdr.msg_iov + 8 * i + 4);

        msg->data.push_back((U8)len);
        msg->data.push_back((U8)(len >> 8));
        msg->data.push_back((U8)(len >> 16));
        msg->data.push_back((U8)(len >> 24));
        while (len) {
            msg->data.push_back(memory->readb(p++));
            len--;
            result++;
        }
    }
    std::vector<U8> packet;
    packet.reserve(result);
    for (U32 pos = 0; pos + 4 <= msg->data.size();) {
        const U32 segment = (U32)msg->data[pos] | ((U32)msg->data[pos + 1] << 8) |
            ((U32)msg->data[pos + 2] << 16) | ((U32)msg->data[pos + 3] << 24);
        pos += 4;
        if (segment > msg->data.size() - pos) return -K_EINVAL;
        packet.insert(packet.end(), msg->data.begin() + pos, msg->data.begin() + pos + segment);
        pos += segment;
    }
    const U32 kept = con->applySocketFilter(packet.data(), (U32)packet.size());
    if (!kept && !packet.empty()) return result; // Linux reports the submitted length for filtered packets.
    // FD-118: a zero-length seqpacket sendmsg is an empty record
    // on the host too, so the queueing below covers both record
    // types (isRecordType), not datagram alone.
    // FD-112: a zero-length datagram is a real message on the host
    // (it wakes the receiver and recvmsg returns 0 for it), but the
    // filter answers the packet length -- 0 -- when no filter is
    // installed, so the empty packet took the filtered path above
    // and vanished; FD-108's sendto branch already distinguishes
    // the two cases. A zero-length STREAM sendmsg queues nothing on
    // the host (nothing becomes readable, recvmsg stays at
    // EWOULDBLOCK), so the empty queueing is record-only (datagram then, seqpacket since FD-118); any
    // descriptors passed with an empty stream message go nowhere
    // with it -- the settled boundary recorded with FD-112.
    if (packet.empty() && !this->isRecordType()) return result;
    msg->data.clear();
    msg->data.push_back((U8)kept);
    msg->data.push_back((U8)(kept >> 8));
    msg->data.push_back((U8)(kept >> 16));
    msg->data.push_back((U8)(kept >> 24));
    msg->data.insert(msg->data.end(), packet.begin(), packet.begin() + kept);
    // FD-114: bytes the plain write paths left in the peer's stream
    // buffer were written before this message; move them into the
    // queue ahead of it so the read paths serve call order.
    if (this->type == K_SOCK_STREAM) {
        con->flushRecvBufferToQueueLocked(thread, this);
    }
    con->msgs.push(msg);
    BOXEDWINE_CONDITION_SIGNAL_ALL(con->lockCond);

    if (getenv("BW64_XWIRE")) {
        klog_fmt("KUnixSocket::sendmsg datalen=%d to peer (isXWire=%d) via msgs queue",
                 (int)result, (int)(std::dynamic_pointer_cast<XWireServerSocket>(con) != nullptr));
    }
    con->notifyAsyncReady(K_POLL_IN);
    return result;
}

U32 KUnixSocketObject::recvmsg(KThread* thread, const KFileDescriptorPtr& fd, U32 address, U32 flags) {
    MsgHdr hdr = {};
    U32 result = 0;
    KMemory* memory = thread->memory;

    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->lockCond);
    while (!this->msgs.size()) {
        if (this->recvBuffer.size_used()) {
            readMsgHdr(thread, address, &hdr);        
            for (U32 i = 0; i < hdr.msg_iovlen; i++) {
                U32 p = memory->readd(hdr.msg_iov + 8 * i);
                U32 len = memory->readd(hdr.msg_iov + 8 * i + 4);

                U32 count = std::min(len, (U32)this->recvBuffer.size_used());
                memory->performOnMemory(p, count, false, [this](U8* ram, U32 len) {
                    this->recvBuffer.get(ram, len);
                    return true;
                    });
                result += count;
            }
            // FD-109: bytes from the stream buffer come from the
            // connected peer; report its bound address as the
            // source. An unnamed peer (the ordinary socketpair and
            // wineserver case) has an empty localAddr, which reports
            // length 0 -- the value this line used to hardcode for
            // stream sockets.
            {
                std::shared_ptr<KUnixSocketObject> peer = this->connection.lock();
                const U32 peerLen = peer ? (U32)peer->localAddr.size() : 0;
                if (hdr.msg_name && peer && peerLen) {
                    memory->memcpy(hdr.msg_name, peer->localAddr.data(), std::min(hdr.msg_namelen, peerLen));
                }
                memory->writed(address + 4, peerLen); // msg_namelen
            }
            memory->writed(address + 20, 0); // msg_controllen
            return result;
        }
        if (this->inClosed) {
            return 0;
        }
        if (!this->blocking) {
            return -K_EWOULDBLOCK;
        }
        // :TODO: what about a time out
        BOXEDWINE_CONDITION_WAIT_SIGNALABLE(this->lockCond);
#ifdef BOXEDWINE_MULTI_THREADED
		if (thread->terminating) {
			return -K_EINTR;
		}
        if (thread->startSignal) {
            thread->startSignal = false;
            return -K_CONTINUE;
        }
#ifdef BOXEDWINE_GUEST_X64
        if (thread->interruptBlockedSyscall()) {
            return -K_EINTR;
        }
#endif
#endif
    }

    readMsgHdr(thread, address, &hdr);
    std::shared_ptr<KSocketMsg> msg = this->msgs.front();
    this->msgs.pop();

    // FD-109: report the sender recorded with the queued message.
    // This path never wrote msg_name/msg_namelen at all, so for a
    // 64-bit guest the bridge's initialised length (the offered
    // capacity) survived and the guest was handed that many bytes
    // of bounce-buffer content as the "address". The host reports
    // the sender's real bound name (length 2 + strlen + 1 for a
    // pathname, 2 + 1 + namelen for an abstract name, both
    // measured) or 0 for an unnamed sender; a short guest buffer
    // truncates the copy but not the reported length.
    if (hdr.msg_name && !msg->senderAddr.empty()) {
        memory->memcpy(hdr.msg_name, msg->senderAddr.data(),
                       std::min(hdr.msg_namelen, (U32)msg->senderAddr.size()));
    }
    memory->writed(address + 4, (U32)msg->senderAddr.size()); // msg_namelen

    if (hdr.msg_control) {
        U32 used = 0;
        U32 msgFlags = 0;
        if (this->socketOptionFlags & (1ULL << K_SO_PASSCRED)) {
            if (hdr.msg_controllen >= 24) {
                writeCMsgHdr(thread, hdr.msg_control, 24, K_SOL_SOCKET, K_SCM_CREDENTIALS);
                memory->writed(hdr.msg_control + 12, msg->senderPid);
                memory->writed(hdr.msg_control + 16, msg->senderUid);
                memory->writed(hdr.msg_control + 20, msg->senderGid);
                used = 24;
            } else {
                msgFlags |= K_MSG_CTRUNC;
            }
        }
        U32 i=0;
        for (;used + 16 <= hdr.msg_controllen && i<msg->objects.size();i++) {
            KFileDescriptorPtr recvFd = thread->process->allocFileDescriptor(msg->objects[i].object, msg->objects[i].accessFlags, 0, -1, 0);
            if (!recvFd) {
                // FD-05: the receiver's table is full. The descriptors
                // that fit are delivered; the rest are marked
                // MSG_CTRUNC by the count check below and dropped with
                // the message -- the close Linux performs for them.
                break;
            }
            // BW64_SOCKDTOR: witness the RECEIVER installing a passed fd. The
            // object is SHARED (same shared_ptr) with the sender's fd — this is
            // the share-vs-dup site. Log the kobj + the receiver's new fd + the
            // post-install use_count (how many fd tables now reference it).
            if (std::getenv("BW64_SOCKDTOR")) {
                klog_fmt("SCMRECV: pid=%d tid=%d installs kobj=%p as fd=%d type=%d (use_count=%ld)",
                         (int)thread->process->id, (int)thread->id,
                         (void*)msg->objects[i].object.get(), (int)recvFd->handle,
                         (int)msg->objects[i].object->type,
                         msg->objects[i].object.use_count());
            }
            writeCMsgHdr(thread, hdr.msg_control + used, 16, K_SOL_SOCKET, K_SCM_RIGHTS);
            memory->writed(hdr.msg_control + used + 12, recvFd->handle);
            used += 16;
        }
		if (i < msg->objects.size()) msgFlags |= K_MSG_CTRUNC;
		memory->writed(address + 20, used); // msg_controllen
		memory->writed(address + 24, msgFlags);
    } else if (this->socketOptionFlags & (1ULL << K_SO_PASSCRED)) {
        memory->writed(address + 20, 0);
        memory->writed(address + 24, K_MSG_CTRUNC);
    }
    U32 pos = 0;
    for (U32 i=0;i<hdr.msg_iovlen;i++) {
        U32 p = memory->readd(hdr.msg_iov + 8 * i);
        U32 len = memory->readd(hdr.msg_iov + 8 * i + 4);
        U32 dataLen = msg->data[pos] | (((U32)msg->data[pos + 1]) << 8) | (((U32)msg->data[pos + 2]) << 16) | (((U32)msg->data[pos + 3]) << 24);
        pos+=4;
        if (len<dataLen) {
            // The receive iov is smaller than the stored segment: copy
            // what fits and drop the rest of the segment (datagram-style
            // truncation) instead of the kpanic this used to be. The
            // stored stream is length-prefixed per segment, so skipping
            // the remainder keeps the walk in sync.
            if (bw64RecoverEnabled()) {
                bw64RecoverLogOnce("unixsocket.recvmsg.truncate", "BW64-FAIL unixsocket-recvmsg outcome=segment-truncated");
            } else {
                kwarn_fmt("BW64-FAIL unixsocket-recvmsg iovlen=%u segmentlen=%u outcome=segment-truncated", len, dataLen);
            }
            memory->memcpy(p, msg->data.data() + pos, len);
            pos+=dataLen;
            result+=len;
            continue;
        }
        memory->memcpy(p, msg->data.data() + pos, dataLen);
        pos+=dataLen;
        result+=dataLen;
    }  
    if (!this->connection.expired()) {
        BOXEDWINE_CONDITION_SIGNAL_ALL(this->lockCond);
    }
    return result;
}

U32 KUnixSocketObject::sendto(KThread* thread, const KFileDescriptorPtr& fd, U32 message, U32 length, U32 flags, U32 dest_addr, U32 dest_len) {
    if (flags & ~(K_MSG_NOSIGNAL | K_MSG_DONTWAIT)) {
        if (bw64RecoverEnabled()) {
            bw64RecoverLogOnce("unixsocket.sendto.flags", "BW64-FAIL unixsocket-sendto outcome=returned-EINVAL");
        } else {
            kwarn_fmt("BW64-FAIL unixsocket-sendto flags=0x%x outcome=returned-EINVAL", flags);
        }
        return -K_EINVAL;
    }
    if (dest_addr) {
        // FD-111: an addressed datagram is delivered to the socket
        // bound at the destination name. The destination used to
        // answer -EDESTADDRREQ for every unix socket (and, before
        // that, reported success while sending nothing, which also
        // made send() on a unix socket a silent no-op, since ksend
        // routes through here). The flag validation above now runs
        // for addressed sends too; it previously sat below this
        // branch, behind the EDESTADDRREQ.
        if (this->type == K_SOCK_SEQPACKET) {
            // FD-118 (host-measured): an addressed send on a
            // CONNECTED seqpacket socket ignores the destination
            // and delivers the record to the connected peer; on an
            // unconnected one it answers the no-peer accounting
            // (-ENOTCONN, or -EPIPE once the write side is closed).
            // The name is never resolved for this type -- unlike
            // datagram, where the address wins over the connection.
            std::shared_ptr<KUnixSocketObject> con = this->connection.lock();
            if (!con) {
                return this->peerCloseSendAnswer(thread, (flags & K_MSG_NOSIGNAL) != 0);
            }
            const bool noSignal = (flags & K_MSG_NOSIGNAL) != 0;
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(con->lockCond);
            if (this->outClosed) {
                return writePipeClosed(thread, noSignal);
            }
            KMemory* memory = thread->memory;
            std::vector<U8> packet;
            packet.reserve(length);
            for (U32 i = 0; i < length; i++) packet.push_back(memory->readb(message + i));
            U32 result = queueDatagramLocked(thread, con, packet.data(), length);
            BOXEDWINE_CONDITION_SIGNAL_ALL(con->lockCond);
            return result;
        }
        if (this->type != K_SOCK_DGRAM) {
            // Measured: a stream socket's sendto with a destination
            // answers EOPNOTSUPP.
            if (bw64RecoverEnabled()) {
                bw64RecoverLogOnce("unixsocket.sendto.dest", "BW64-FAIL unixsocket-sendto outcome=returned-EOPNOTSUPP");
            } else {
                kwarn_fmt("BW64-FAIL unixsocket-sendto destlen=%u outcome=returned-EOPNOTSUPP", dest_len);
            }
            return -K_EOPNOTSUPP;
        }
        KMemory* memory = thread->memory;
        if (dest_len > 2 && memory->readw(dest_addr) == K_AF_UNIX && memory->readb(dest_addr + 2) != 0 &&
            socketAddressName(memory, dest_addr, dest_len) == "/dev/log") {
            // The /dev/log print convention, as in internal_write:
            // no socket is ever bound there.
            BString s = memory->readString(message);
            printf("%s\n", s.c_str());
            return length;
        }
        U32 err = 0;
        std::shared_ptr<KUnixSocketObject> dest = resolveNamedPeer(thread, dest_addr, dest_len, err);
        if (!dest) {
            return -(S32)err;
        }
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(dest->lockCond);
        // FD-119: no outClosed check on an addressed send. A
        // datagram socket's outClosed is only ever set by its pair
        // peer's death (its own shutdown is unimplemented), and the
        // host delivers an addressed datagram regardless of the
        // pair link's state; the check answered a live addressed
        // send with the broken-pipe path after a pair peer closed.
        std::vector<U8> packet;
        packet.reserve(length);
        for (U32 i = 0; i < length; i++) packet.push_back(memory->readb(message + i));
        U32 result = queueDatagramLocked(thread, dest, packet.data(), length);
        BOXEDWINE_CONDITION_SIGNAL_ALL(dest->lockCond);
        return result;
    }
    // FD-108: a datagram send on a connected pair queues a real
    // datagram on the peer. This previously routed every send to the
    // stream write path, whose datagram branch discarded the payload
    // and returned the length, so the send was reported but never
    // delivered. The queueing tail is queueDatagramLocked, shared
    // with the addressed branch above.
    if (this->type == K_SOCK_DGRAM) {
        std::shared_ptr<KUnixSocketObject> con = this->connection.lock();
        if (con) {
            const bool noSignal = (flags & K_MSG_NOSIGNAL) != 0;
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(con->lockCond);
            if (this->outClosed) {
                return writePipeClosed(thread, noSignal);
            }
            KMemory* memory = thread->memory;
            std::vector<U8> packet;
            packet.reserve(length);
            for (U32 i = 0; i < length; i++) packet.push_back(memory->readb(message + i));
            U32 result = queueDatagramLocked(thread, con, packet.data(), length);
            BOXEDWINE_CONDITION_SIGNAL_ALL(con->lockCond);
            return result;
        }
        if (!strcmp(this->destAddress.data, "/dev/log")) {
            // Connected to /dev/log (which links no peer): the write
            // path prints the payload.
            return this->write(thread, message, length);
        }
        // Measured: an unconnected datagram send answers ENOTCONN.
        // It used to fall through to the write path, which discarded
        // the payload and reported the length as sent. FD-119: after
        // a peer close the first send answers -ECONNREFUSED instead;
        // peerCloseSendAnswer holds that accounting.
        return this->peerCloseSendAnswer(thread, false);
    }
    // Connected send: the write path is the data path. MSG_NOSIGNAL is
    // accepted but not honored (the write path applies write()'s
    // SIGPIPE behavior), and MSG_DONTWAIT is accepted with the socket's
    // own blocking flag governing, as with write().
    return this->write(thread, message, length);
}

U32 KUnixSocketObject::recvfrom(KThread* thread, const KFileDescriptorPtr& fd, U32 buffer, U32 length, U32 flags, U32 address, U32 address_len) {
    if (address == 0) {
        return read(thread, buffer, length, flags);
    }
    // FD-109/FD-110: the data path is the same plain read, but the
    // source report is the address the read actually consumed from
    // -- the queued message's recorded sender, or the connected
    // peer's bound address for stream-buffer bytes. Measured on the
    // host (hostref-names-fd109.c, hostref-tail-fd111.c,
    // hostref-stream-addr.c in test-results/fd109-112/): an unnamed
    // source reports length 0 and copies nothing -- EXCEPT the
    // single-call recv on a connected stream socket, which answers
    // family with length 2 for its unnamed peer (the datagram
    // entries report 0). This entry point used to report family +
    // length 2 for everything, which happened to match only that
    // one case.
    ReadSource source;
    U32 result = read(thread, buffer, length, flags, &source);
    if ((S32)result >= 0 && address_len) {
        KMemory* memory = thread->memory;
        U32 full = (U32)source.senderAddr.size();
        if (full == 0 && this->type == K_SOCK_STREAM && this->connection.lock()) {
            memory->writew(address, K_AF_UNIX);
            memory->writed(address_len, 2);
            return result;
        }
        const U32 copy = std::min(memory->readd(address_len), full);
        if (copy) {
            memory->memcpy(address, source.senderAddr.data(), copy);
        }
        memory->writed(address_len, full);
    }
    return result;
}
