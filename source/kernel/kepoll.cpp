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

#include "kepoll.h"
#include "kscheduler.h"
#include "kunixsocket.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string.h>
#include <unordered_map>
#include <vector>

S32 internal_epoll_poll(KThread* thread, KPollData* data, U32 count, U32 timeout,
                         std::atomic<bool>* epollRestart, bool* restartRequested);

namespace {
struct KEPollWaiter {
    explicit KEPollWaiter(KThread* thread) : thread(thread) {}

    KThread* thread;
    std::atomic<bool> restart{false};
};

std::mutex g_epollDataMutex;
std::mutex g_epollWaitersMutex;
std::unordered_map<const KEPoll*, std::vector<std::shared_ptr<KEPollWaiter>>> g_epollWaiters;
std::unordered_map<const KEPoll*, std::unordered_map<FD, bool>> g_epollEdgeReady;

std::shared_ptr<KEPollWaiter> registerEpollWaiter(const KEPoll* epoll, KThread* thread) {
    std::shared_ptr<KEPollWaiter> waiter = std::make_shared<KEPollWaiter>(thread);
    std::lock_guard<std::mutex> lock(g_epollWaitersMutex);
    g_epollWaiters[epoll].push_back(waiter);
    return waiter;
}

bool finishEpollWaiter(const KEPoll* epoll, const std::shared_ptr<KEPollWaiter>& waiter) {
    // Match the lock order in wakeEpollWaiters: waiter registry, then pollCond.
    std::lock_guard<std::mutex> lock(g_epollWaitersMutex);
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(waiter->thread->pollCond);
    if (waiter->restart.exchange(false, std::memory_order_acq_rel)) {
        return true;
    }

    auto epollIt = g_epollWaiters.find(epoll);
    if (epollIt != g_epollWaiters.end()) {
        auto& waiters = epollIt->second;
        waiters.erase(std::remove(waiters.begin(), waiters.end(), waiter), waiters.end());
        if (waiters.empty()) {
            g_epollWaiters.erase(epollIt);
        }
    }
    return false;
}

void wakeEpollWaiters(const KEPoll* epoll) {
    std::lock_guard<std::mutex> lock(g_epollWaitersMutex);
    auto epollIt = g_epollWaiters.find(epoll);
    if (epollIt == g_epollWaiters.end()) {
        return;
    }
    for (const std::shared_ptr<KEPollWaiter>& waiter : epollIt->second) {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(waiter->thread->pollCond);
        waiter->restart.store(true, std::memory_order_release);
        BOXEDWINE_CONDITION_SIGNAL_ALL(waiter->thread->pollCond);
    }
}
}

KEPoll::KEPoll() : KObject(KTYPE_EPOLL) {
}

KEPoll::~KEPoll() {
    std::lock_guard<std::mutex> lock(g_epollDataMutex);
    g_epollEdgeReady.erase(this);
     for( const auto& n : this->data ) {
         delete n.value;
    }
}

void KEPoll::setBlocking(bool blocking) {
    if (blocking)
        kpanic("KEPoll::setBlocking not implemented yet");
}

bool KEPoll::isBlocking() {
    return false;
}

void KEPoll::setAsync(bool isAsync) {
    if (isAsync)
        kpanic("KEPoll::setAsync not implemented yet");
}

bool KEPoll::isAsync() {
    return false;
}

KFileLock* KEPoll::getLock(KFileLock* lock) {
    kdebug("KEPoll::getLock not implemented yet");
    return nullptr;
}

U32 KEPoll::setLock(KFileLock* lock, bool wait) {
    kdebug("KEPoll::setLock not implemented yet");
    return -1;
}

bool KEPoll::isOpen() {
    kdebug("KEPoll::isOpen not implemented yet");
    return false;
}

void KEPoll::waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) {
    kpanic("KEPoll::waitForEvents not implemented yet");
}

bool KEPoll::isReadReady() {
    kpanic("KEPoll::isReadReady not implemented yet");
    return false;
}

bool KEPoll::isWriteReady() {
    kpanic("KEPoll::isWriteReady not implemented yet");
    return false;
}

U32 KEPoll::writeNative(U8* buffer, U32 len) {
    kpanic("KEPoll::writeNative not implemented yet");
    return 0;
}

U32 KEPoll::readNative(U8* buffer, U32 len) {
    kpanic("KEPoll::readNative not implemented yet");
    return 0;
}

U32 KEPoll::stat(KProcess* process, U32 address, bool is64) {
    kpanic("KEPoll::stat not implemented yet");
    return 0;
}

U32 KEPoll::map(KThread* thread, U32 address, U32 len, S32 prot, S32 flags, U64 off) {
    return 0;
}

bool KEPoll::canMap() {
    return false;
}

BString KEPoll::selfFd() {
    return B("anon_inode:[eventpoll]");
}

S64 KEPoll::seek(S64 pos) {
    return -K_ESPIPE; // :TODO: is this right?
}

S64 KEPoll::getPos() {
    return 0;
}

U32 KEPoll::ioctl(KThread* thread, U32 request) {
    return -K_ENOTTY;
}

bool KEPoll::supportsLocks() {
    return false;
}

S64 KEPoll::length() {
    return -1;
}

void KEPoll::close() {
}


#define K_EPOLL_CTL_ADD 1
#define K_EPOLL_CTL_DEL 2
#define K_EPOLL_CTL_MOD 3

U32 KEPoll::ctl(KMemory* memory, U32 op, FD fd, U32 address) {
    KFileDescriptorPtr targetFD = KThread::currentThread()->process->getFileDescriptor(fd);

    if (!targetFD) {
        return -K_EBADF;
    }

    {
        std::lock_guard<std::mutex> lock(g_epollDataMutex);
        Data* existing = this->data[fd];
        U32 requestedEvents = 0;
        U64 requestedData = 0;
        if (op != K_EPOLL_CTL_DEL) {
            requestedEvents = memory->readd(address);
            requestedData = memory->readq(address + 4);
            constexpr U32 EPOLLIN = 0x001, EPOLLPRI = 0x002, EPOLLOUT = 0x004;
            constexpr U32 EPOLLRDHUP = 0x2000, EPOLLWAKEUP = 0x20000000;
            constexpr U32 EPOLLEXCLUSIVE = 0x10000000, EPOLLET = 0x80000000;
            constexpr U32 EPOLLONESHOT = 0x40000000;
            constexpr U32 allowed = EPOLLIN | EPOLLPRI | EPOLLOUT | EPOLLRDHUP |
                EPOLLWAKEUP | EPOLLEXCLUSIVE | EPOLLET | EPOLLONESHOT;
            if (requestedEvents & ~allowed) return -K_EINVAL;
            if ((requestedEvents & EPOLLEXCLUSIVE) &&
                (op != K_EPOLL_CTL_ADD || !(requestedEvents & (EPOLLIN | EPOLLOUT)) ||
                 (requestedEvents & (EPOLLONESHOT | EPOLLWAKEUP)))) return -K_EINVAL;
        }

        switch (op) {
            case K_EPOLL_CTL_ADD:
                if (existing) {
                    return -K_EEXIST;
                }
                existing = new Data();
                existing->fd = fd;
                existing->events = requestedEvents;
                existing->data = requestedData;
                this->data.set(fd, existing);
                if (requestedEvents & 0x80000000u) g_epollEdgeReady[this][fd] = false;
                break;
            case K_EPOLL_CTL_DEL:
                if (!existing)
                    return -K_ENOENT;
                this->data.remove(fd);
                delete existing;
                g_epollEdgeReady[this].erase(fd);
                break;
            case K_EPOLL_CTL_MOD:
                if (!existing)
                    return -K_ENOENT;
                existing->events = requestedEvents;
                existing->data = requestedData;
                existing->enabled = true; // EPOLL_CTL_MOD rearms EPOLLONESHOT
                g_epollEdgeReady[this][fd] = false;
                break;
            default:
                return -K_EINVAL;
        }
    }
    wakeEpollWaiters(this);
    return 0;
}

U32 KEPoll::wait(KThread* thread, U32 events, U32 maxevents, U32 timeout) {
    S32 result = 0;
    KMemory* memory = thread->memory;

    std::shared_ptr<KEPollWaiter> waiter = registerEpollWaiter(this, thread);
    while (true) {
        std::vector<KPollData> pollSnapshot;
        {
            std::lock_guard<std::mutex> lock(g_epollDataMutex);
            for (const auto& n : this->data) {
                Data* next = n.value;
                if (!next->enabled) continue;
                KPollData pollData = {};
                pollData.events = next->events;
                pollData.fd = next->fd;
                pollData.data = next->data;
                pollSnapshot.push_back(pollData);
            }
        }
        {
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(thread->pollCond);
            thread->pollData.swap(pollSnapshot);
        }

        bool restartRequested = false;
        result = internal_epoll_poll(thread, thread->pollData.data(),
                                     (U32)thread->pollData.size(), timeout,
                                     &waiter->restart, &restartRequested);
        if (restartRequested) {
            continue;
        }
        if (finishEpollWaiter(this, waiter)) {
            continue;
        }
        break;
    }
    if (result >= 0) {
        // EPOLLET reports only a not-ready -> ready transition. Keep this
        // state beside the registrations because Data's public layout is
        // shared by the 32-bit and 64-bit kernel APIs.
        {
            std::lock_guard<std::mutex> lock(g_epollDataMutex);
            for (KPollData& data : thread->pollData) {
                Data* registration = this->data[(U32)data.fd];
                if (!registration || !(registration->events & 0x80000000u)) continue;
                bool& wasReady = g_epollEdgeReady[this][(FD)data.fd];
                if (data.revents) {
                    if (wasReady) data.revents = 0;
                    else wasReady = true;
                } else {
                    wasReady = false;
                }
            }
        }
        result = 0;
        for (KPollData& data : thread->pollData) {
            if (data.revents!=0) {
                if (getenv("BW64_EPDUMP")) {
                    KFileDescriptorPtr fd = thread->process->getFileDescriptor((FD)data.fd);
                    const char* kind = "?";
                    int inC = -1, outC = -1; long used = -1;
                    if (fd && fd->kobject) {
                        switch (fd->kobject->type) {
                            case KTYPE_FILE: kind = "file"; break;
                            case KTYPE_UNIX_SOCKET: {
                                kind = "unixsock";
                                std::shared_ptr<KUnixSocketObject> us = std::dynamic_pointer_cast<KUnixSocketObject>(fd->kobject);
                                if (us) { inC = us->inClosed; outC = us->outClosed; used = (long)us->debugRecvUsed(); }
                                break;
                            }
                            case KTYPE_NATIVE_SOCKET: kind = "natsock"; break;
                            case KTYPE_EVENT: kind = "event"; break;
                            case KTYPE_TIMER: kind = "timer"; break;
                            case KTYPE_SIGNAL: kind = "signal"; break;
                            default: kind = "other"; break;
                        }
                    }
                    klog_fmt("EPDUMP wait: fd=%d data=0x%llx revents=0x%x kind=%s inClosed=%d outClosed=%d recvUsed=%ld",
                             (int)data.fd, (unsigned long long)data.data,
                             (unsigned)data.revents, kind, inC, outC, used);
                }
                memory->writed(events + result * 12, data.revents);
                memory->writeq(events + result * 12 + 4, data.data);
                {
                    std::lock_guard<std::mutex> lock(g_epollDataMutex);
                    Data* registration = this->data[data.fd];
                    if (registration && (registration->events & 0x40000000u))
                        registration->enabled = false;
                }
                result++;
                if (result>=(S32)maxevents) {
                    kwarn("possible starvation in epoll, more events are ready than can be received.");
                    break;
                }
            }        
        }
    }
    return result;
}

void KEPoll::debugDumpRegistrations(KProcess* process, U32 pid, U32 tid) {
    struct Registration {
        U32 fd;
        U32 events;
    };
    std::vector<Registration> registrations;
    {
        std::lock_guard<std::mutex> lock(g_epollDataMutex);
        for (const auto& entry : this->data) {
            const Data* registration = entry.value;
            if (registration) {
                registrations.push_back({registration->fd, registration->events});
            }
        }
    }
    for (const Registration& registration : registrations) {
        KFileDescriptorPtr fd = process ? process->getFileDescriptor((FD)registration.fd) : nullptr;
        bool readable = fd && fd->kobject && fd->kobject->isReadReady();
        bool writable = fd && fd->kobject && fd->kobject->isWriteReady();
        std::shared_ptr<KUnixSocketObject> socket = fd && fd->kobject
            ? std::dynamic_pointer_cast<KUnixSocketObject>(fd->kobject) : nullptr;
        std::shared_ptr<KUnixSocketObject> peer = socket ? socket->connection.lock() : nullptr;
        U32 mask = registration.events;
        char flags[48] = {0};
        int offset = 0;
        auto appendFlag = [&](const char* name) {
            if (offset && offset < (int)sizeof(flags) - 1) flags[offset++] = '|';
            offset += snprintf(flags + offset, sizeof(flags) - offset, "%s", name);
        };
        if (mask & 0x001) appendFlag("IN");
        if (mask & 0x004) appendFlag("OUT");
        if (mask & 0x80000000u) appendFlag("ET");
        if (mask & 0x40000000u) appendFlag("ONESHOT");
        if (!offset) strcpy(flags, "-");
        klog_fmt("BW_THREAD_DUMP epoll_fd pid=%u tid=%u fd=%u events=0x%x flags=%s readable=%d writable=%d object=%p recv=%llu msgs=%llu queue=%llu/%llu:w%u/%u:r%u/%u:m%llu/%llu peer=%p",
                 pid, tid, registration.fd, mask, flags, (int)readable,
                 (int)writable, fd && fd->kobject ? (void*)fd->kobject.get() : nullptr,
                 (unsigned long long)(socket ? socket->debugRecvUsed() : 0),
                 (unsigned long long)(socket ? socket->debugMsgsSize() : 0),
                 (unsigned long long)(socket ? socket->debugQueueBytesIn() : 0),
                 (unsigned long long)(socket ? socket->debugQueueBytesOut() : 0),
                 socket ? socket->debugQueueWriterPid() : 0,
                 socket ? socket->debugQueueWriterTid() : 0,
                 socket ? socket->debugQueueReaderPid() : 0,
                 socket ? socket->debugQueueReaderTid() : 0,
                 (unsigned long long)(socket ? socket->debugQueueMessagesIn() : 0),
                 (unsigned long long)(socket ? socket->debugQueueMessagesOut() : 0),
                 peer ? (void*)peer.get() : nullptr);
    }

    std::vector<KPollData> activeSnapshot;
    {
        std::lock_guard<std::mutex> lock(g_epollWaitersMutex);
        auto it = g_epollWaiters.find(this);
        if (it != g_epollWaiters.end()) {
            for (const std::shared_ptr<KEPollWaiter>& waiter : it->second) {
                KThread* thread = waiter->thread;
                if (!thread || !thread->process || thread->process->id != pid || thread->id != tid) {
                    continue;
                }
                BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(thread->pollCond);
                activeSnapshot = thread->pollData;
                break;
            }
        }
    }
    for (const KPollData& entry : activeSnapshot) {
        U32 mask = entry.events;
        char flags[48] = {0};
        int offset = 0;
        auto appendFlag = [&](const char* name) {
            if (offset && offset < (int)sizeof(flags) - 1) flags[offset++] = '|';
            offset += snprintf(flags + offset, sizeof(flags) - offset, "%s", name);
        };
        if (mask & 0x001) appendFlag("IN");
        if (mask & 0x004) appendFlag("OUT");
        if (mask & 0x80000000u) appendFlag("ET");
        if (mask & 0x40000000u) appendFlag("ONESHOT");
        if (!offset) strcpy(flags, "-");
        klog_fmt("BW_THREAD_DUMP epoll_snapshot pid=%u tid=%u fd=%d events=0x%x flags=%s revents=0x%x",
                 pid, tid, (int)entry.fd, mask, flags, entry.revents);
    }
}
