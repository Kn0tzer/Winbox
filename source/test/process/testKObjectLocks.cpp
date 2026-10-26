/*
 *  Copyright (C) 2012-2026  The BoxedWine Team
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

#ifdef __TEST

#include <fstream>
#include <string>
#include <iterator>

#include "../cpu/testCPU.h"
#include "kepoll.h"
#include "kevent.h"
#include "ktimer.h"
#include "kfilelock.h"

// The non-file KObject family (epoll, eventfd, timerfd, signalfd, netlink,
// unix/native sockets) does not support POSIX file locks. KProcess::fcntl
// gates F_GETLK/F_SETLK on supportsLocks(), so guests get -EBADF and the
// virtuals below are unreachable from the syscall ABI today; they still
// must not hand back success-shaped garbage (a bare -1 is errno EPERM
// shaped, nullptr from getLock is the one correct "no conflict" answer),
// and isOpen() feeds poll()'s POLLHUP decision, so it must be truthful.
void testKObjectLockStubs() {
    KFileLock lock;
    KEPoll epoll;
    if (!epoll.isOpen()) {
        testFail("KEPoll::isOpen must be true for a live epoll object (poll uses it for POLLHUP)");
        return;
    }
    if (epoll.supportsLocks() || epoll.getLock(&lock) != nullptr ||
        epoll.setLock(&lock, false) != (U32)-K_ENOTSUP) {
        testFail("KEPoll lock stubs must report no-conflict / -ENOTSUP");
        return;
    }
    KEvent event;
    if (!event.isOpen() || event.supportsLocks() || event.getLock(&lock) != nullptr ||
        event.setLock(&lock, false) != (U32)-K_ENOTSUP) {
        testFail("KEvent lock stubs must report no-conflict / -ENOTSUP");
        return;
    }
    KTimer timer;
    if (!timer.isOpen() || timer.supportsLocks() || timer.getLock(&lock) != nullptr ||
        timer.setLock(&lock, false) != (U32)-K_ENOTSUP) {
        testFail("KTimer lock stubs must report no-conflict / -ENOTSUP");
        return;
    }
    // Source guard for the rest of the family: no setLock may return a
    // bare -1 and no lock stub may hide behind a debug-only log again.
    std::string testPath = __FILE__;
    const size_t markerAt = testPath.find("/source/test/");
    if (markerAt == std::string::npos) {
        testFail("lock stub guard cannot locate source tree from %s", testPath.c_str());
        return;
    }
    const std::string root = testPath.substr(0, markerAt);
    const char* family[] = {
        "/source/kernel/kepoll.cpp", "/source/kernel/kevent.cpp",
        "/source/kernel/ksignal.cpp", "/source/kernel/knetlink.cpp",
        "/source/kernel/kunixsocket.cpp", "/source/kernel/knativesocket.cpp",
    };
    for (const char* relative : family) {
        std::ifstream in(root + relative);
        if (!in) {
            testFail("lock stub guard cannot read %s", relative);
            return;
        }
        const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (source.find("Lock not implemented yet") != std::string::npos) {
            testFail("%s still carries a debug-only lock stub", relative);
            return;
        }
    }
}

#endif
