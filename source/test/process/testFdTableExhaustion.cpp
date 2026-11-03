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

#include "kerror.h"
#include "ksystem.h"
#include "kevent.h"
#include "../cpu/testCPU.h"
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"

#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

// FD-05: filling a process's file-descriptor table to the
// MAX_NUMBER_OF_FILES ceiling used to kpanic inside
// KProcess::getNextFileDescriptorHandle, killing the whole emulator.
// A real kernel answers EMFILE and the guest lives. This test drives
// one process to the ceiling through the ordinary dup path, then
// checks that every descriptor-creating path answers with its error
// (never a panic, never a null dereference), that the ceiling itself
// is exact (dup2 may target the top legal slot but not the ceiling
// value), and that the table works again the moment a slot frees.
//
// The test context is shared with the rest of the suite, so the test
// tracks every handle it creates and closes exactly those on every
// exit path (FdGuard), leaving the table as it found it.

namespace {

struct FdGuard {
    KProcessPtr process;
    std::vector<U32> handles;
    ~FdGuard() {
        for (U32 h : handles) {
            process->close((FD)h);
        }
    }
    void track(U32 h) { handles.push_back(h); }
    void untrack(U32 h) {
        auto it = std::find(handles.begin(), handles.end(), h);
        if (it != handles.end()) {
            handles.erase(it);
        }
    }
};

} // namespace

void testFdTableExhaustion() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    KThread* thread = context.thread;

    char dirTemplate[] = "/tmp/bw64fd05XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for fd-table exhaustion test");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    FdGuard guard;
    guard.process = process;

    // Base descriptor: a real opened file, so the open path itself is
    // one of the paths exercised at the ceiling below.
    KFileDescriptorPtr base;
    U32 result = process->openFile(B("/"), B("/f.txt"), K_O_RDONLY | K_O_CREAT, base);
    if ((S32)result < 0 || !base) {
        testFail("openFile(O_CREAT) for /f.txt failed: %d", (S32)result);
        return;
    }
    const U32 hb = base->handle;
    guard.track(hb);

    // Drive the table to the ceiling through dup(). Before the FD-05
    // fix this loop ended in a kpanic that killed the test process;
    // the loop can now only exit with the kernel's EMFILE answer.
    S32 fillError = 0;
    for (;;) {
        U32 h = process->dup(hb);
        if ((S32)h < 0) {
            fillError = (S32)h;
            break;
        }
        guard.track(h);
    }
    if (fillError != -K_EMFILE) {
        testFail("fd-table fill: dup failed with %d, expected -EMFILE (%d) at the ceiling",
                 fillError, -K_EMFILE);
        return;
    }
    if (guard.handles.size() < MAX_NUMBER_OF_FILES - 512) {
        testFail("fd-table fill: only %zu descriptors held, the table never reached the "
                 "ceiling region (%d)", guard.handles.size(), (int)MAX_NUMBER_OF_FILES);
        return;
    }

    // At the ceiling, every descriptor-creating path must answer with
    // its error instead of panicking or dereferencing a null result.
    if (process->allocFileDescriptor(std::make_shared<KEvent>(), K_O_RDWR, 0, -1, 0)) {
        testFail("fd-table full: allocFileDescriptor returned a descriptor past the ceiling");
        return;
    }
    if ((S32)process->epollcreate(0, 0) != -K_EMFILE) {
        testFail("fd-table full: epoll_create did not return -EMFILE");
        return;
    }
    if ((S32)process->timerfd_create(1, 0) != -K_EMFILE) {
        testFail("fd-table full: timerfd_create did not return -EMFILE");
        return;
    }
    if ((S32)process->inotify_init1(0) != -K_EMFILE) {
        testFail("fd-table full: inotify_init did not return -EMFILE");
        return;
    }
    if ((S32)process->fcntrl(thread, (FD)hb, K_F_DUPFD, 0) != -K_EMFILE) {
        testFail("fd-table full: fcntl(F_DUPFD, 0) did not return -EMFILE");
        return;
    }
    if ((S32)process->fcntrl(thread, (FD)hb, K_F_DUPFD, MAX_NUMBER_OF_FILES) != -K_EINVAL) {
        testFail("fcntl(F_DUPFD, MAX_NUMBER_OF_FILES) did not return -EINVAL");
        return;
    }
    {
        KFileDescriptorPtr reopened;
        U32 openResult = process->openFile(B("/"), B("/f.txt"), K_O_RDONLY, reopened);
        if ((S32)openResult != -K_EMFILE || reopened) {
            testFail("fd-table full: open did not return -EMFILE (got %d)", (S32)openResult);
            return;
        }
    }

    // The ceiling is exact: the ceiling value itself is not a legal
    // dup2 target (Linux answers EBADF), the top legal slot is.
    if ((S32)process->dup2((FD)hb, (FD)MAX_NUMBER_OF_FILES) != -K_EBADF) {
        testFail("dup2 to the ceiling value did not return -EBADF");
        return;
    }
    const U32 top = MAX_NUMBER_OF_FILES - 1;
    if (std::find(guard.handles.begin(), guard.handles.end(), top) != guard.handles.end()) {
        if ((S32)process->dup2((FD)hb, (FD)top) != (S32)top) {
            testFail("dup2 to the top legal slot %u failed", top);
            return;
        }
    }

    // Recovery: freeing one slot makes exactly one more descriptor
    // available, at exactly the freed handle, and then the table is
    // full again. The guest survives its own exhaustion.
    const U32 freed = guard.handles.back();
    process->close((FD)freed);
    guard.untrack(freed);
    U32 again = process->dup(hb);
    if (again != freed) {
        testFail("fd-table recovery: dup after one close returned %u, expected the freed "
                 "handle %u", again, freed);
        if ((S32)again >= 0) {
            guard.track(again);
        }
        return;
    }
    guard.track(again);
    if ((S32)process->dup(hb) != -K_EMFILE) {
        testFail("fd-table recovery: table did not report full again after one re-dup");
        return;
    }

    // Drain everything this test created; the table must be fully
    // usable afterwards.
    for (U32 h : guard.handles) {
        if (h != hb) {
            process->close((FD)h);
        }
    }
    guard.handles.clear();
    guard.track(hb);
    U32 postDrain = process->dup(hb);
    if ((S32)postDrain < 0) {
        testFail("fd-table drain: dup failed after all test descriptors were closed: %d",
                 (S32)postDrain);
        return;
    }
    process->close((FD)postDrain);

    BString hostFile = BString::copy(dir) + "/f.txt";
    ::remove(hostFile.c_str());
    ::rmdir(dir);
}

#endif
