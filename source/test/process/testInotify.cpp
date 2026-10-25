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
#ifdef BOXEDWINE_GUEST_X64

#include "cpu64.h"
#include "kerror.h"
#include "kmemory64.h"
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"
#include "../cpu/testCPU.h"

#include <stdlib.h>
#include <unistd.h>

S32 ksyscall64(CPU64* cpu);

// End-to-end inotify: watch a directory through the x64 syscalls, mutate
// it through the ordinary process file APIs, and read the events back:
// IN_CREATE on create, IN_MODIFY + IN_CLOSE_WRITE on write/close,
// IN_MOVED_FROM/IN_MOVED_TO with one cookie on rename, IN_DELETE on
// unlink, IN_IGNORED when the watch is removed. Mask filtering and the
// nonblocking empty read are pinned too.

namespace {
constexpr U32 IN_MODIFY = 0x00000002;
constexpr U32 IN_ATTRIB = 0x00000004;
constexpr U32 IN_CLOSE_WRITE = 0x00000008;
constexpr U32 IN_MOVED_FROM = 0x00000040;
constexpr U32 IN_MOVED_TO = 0x00000080;
constexpr U32 IN_CREATE = 0x00000100;
constexpr U32 IN_DELETE = 0x00000200;
constexpr U32 IN_DELETE_SELF = 0x00000400;
constexpr U32 IN_MOVE_SELF = 0x00000800;
constexpr U32 IN_UNMOUNT = 0x00002000;
constexpr U32 IN_Q_OVERFLOW = 0x00004000;
constexpr U32 IN_IGNORED = 0x00008000;
constexpr U32 IN_ISDIR = 0x40000000;
constexpr U32 IN_ALL_EVENTS = 0x00000fff;
constexpr U32 IN_NONBLOCK = 0x00000800;
constexpr U32 IN_CLOEXEC = 0x00080000;

struct ParsedEvent {
    S32 wd;
    U32 mask;
    U32 cookie;
    BString name;
};
}

void testInotifyEvents() {
    TestContext& context = testContext();
    if (!context.process->memory64) context.process->memory64 = new KMemory64(context.process.get());
    KMemory64* memory = context.process->memory64;
    constexpr U64 page = 0x77000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("inotify test could not map its fixture page");
        return;
    }
    constexpr U64 pathAt = page + 0x100;
    constexpr U64 eventsAt = page + 0x200;
    constexpr U64 lenAt = page + 0xf00;

    // A private VFS root over a host temp dir (the statx fixture pattern).
    char dirTemplate[] = "/tmp/k3binotifyXXXXXX";
    char* hostDir = mkdtemp(dirTemplate);
    if (!hostDir) {
        testFail("inotify test could not create its host temp dir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(hostDir), true, true, nullptr);
    Fs::clearPathCache();

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    auto writePath = [&](const char* path) {
        memory->memcpyToGuest(pathAt, path, strlen(path) + 1);
    };

    // __NR_inotify_init1 = 294
    S64 fd = invoke(294, IN_NONBLOCK | IN_CLOEXEC, 0, 0, 0);
    if (fd < 0) {
        testFail("inotify_init1 failed: %lld", (long long)fd);
        return;
    }
    // __NR_inotify_add_watch = 254, watching the fixture root.
    writePath("/");
    S64 wd = invoke(254, (U64)fd, pathAt, IN_ALL_EVENTS, 0);
    if (wd < 1) {
        testFail("inotify_add_watch(/) failed: %lld", (long long)wd);
        return;
    }
    // A second watch on the same path returns the same descriptor with the
    // merged mask.
    S64 wdAgain = invoke(254, (U64)fd, pathAt, IN_ATTRIB, 0);
    if (wdAgain != wd) {
        testFail("re-adding a watch returned wd %lld, expected %lld", (long long)wdAgain, (long long)wd);
    }

    auto readEvents = [&](std::vector<ParsedEvent>& out) -> S64 {
        // __NR_read = 0
        S64 result = invoke(0, (U64)fd, eventsAt, 4096, 0);
        out.clear();
        if (result <= 0) return result;
        U64 pos = 0;
        while (pos + 16 <= (U64)result) {
            ParsedEvent event;
            event.wd = (S32)memory->readd(eventsAt + pos);
            event.mask = memory->readd(eventsAt + pos + 4);
            event.cookie = memory->readd(eventsAt + pos + 8);
            U32 len = memory->readd(eventsAt + pos + 12);
            if (len) {
                char nameBuffer[256];
                memset(nameBuffer, 0, sizeof(nameBuffer));
                memory->memcpyFromGuest(nameBuffer, eventsAt + pos + 16, len < 255 ? len : 255);
                event.name = BString::copy(nameBuffer);
            } else {
                event.name = BString();
            }
            out.push_back(event);
            pos += 16 + len;
        }
        return result;
    };
    auto hasEvent = [&](const std::vector<ParsedEvent>& events, U32 mask, const char* name) {
        for (const ParsedEvent& event : events) {
            if ((event.mask & mask) && (!name || event.name == name)) return true;
        }
        return false;
    };

    // Empty nonblocking read: -EAGAIN, not 0 and not a hang.
    std::vector<ParsedEvent> events;
    if (readEvents(events) != -K_EAGAIN) {
        testFail("empty inotify read did not return -EAGAIN");
    }

    // Create + write + close a file through the process file APIs.
    KProcess* process = context.process.get();
    KFileDescriptorPtr createdDesc;
    U32 openResult = process->openFile(B("/"), B("/k3bfile.txt"),
                                       K_O_WRONLY | K_O_CREAT | K_O_TRUNC, createdDesc);
    if (openResult != 0 || !createdDesc) {
        testFail("inotify test could not create its file: %d", (S32)openResult);
        return;
    }
    S32 fileFd = createdDesc->handle;
    if (readEvents(events) <= 0 || !hasEvent(events, IN_CREATE, "k3bfile.txt")) {
        testFail("no IN_CREATE for the created file");
    }
    if (!hasEvent(events, IN_ISDIR, nullptr)) {
        // a plain file must not be flagged as a directory event
    } else {
        testFail("file creation event wrongly carried IN_ISDIR");
    }
    std::shared_ptr<KFileDescriptor> fileDesc = process->getFileDescriptor(fileFd);
    std::shared_ptr<KFile> file = std::dynamic_pointer_cast<KFile>(fileDesc->kobject);
    constexpr U32 textAt = TEST_HEAP_ADDRESS + 0xd00;
    context.memory->memcpy(textAt, (const U8*)"hello", 5);
    file->write(context.thread, textAt, 5);
    process->close(fileFd);
    if (readEvents(events) <= 0 || !hasEvent(events, IN_MODIFY, "k3bfile.txt") ||
        !hasEvent(events, IN_CLOSE_WRITE, "k3bfile.txt")) {
        testFail("write/close did not deliver IN_MODIFY and IN_CLOSE_WRITE");
    }

    // Rename: FROM and TO share one cookie.
    if (process->rename(B("/k3bfile.txt"), B("/k3brenamed.txt")) != 0) {
        testFail("inotify test rename failed");
    }
    readEvents(events);
    bool fromFound = false, toFound = false;
    U32 cookie = 0;
    for (const ParsedEvent& event : events) {
        if ((event.mask & IN_MOVED_FROM) && event.name == "k3bfile.txt") { fromFound = true; cookie = event.cookie; }
        if ((event.mask & IN_MOVED_TO) && event.name == "k3brenamed.txt" && event.cookie == cookie && cookie != 0) toFound = true;
    }
    if (!fromFound || !toFound) {
        testFail("rename did not deliver a cookie-matched IN_MOVED_FROM/IN_MOVED_TO pair");
    }

    // Unlink: IN_DELETE.
    if (process->unlinkFile(B("/k3brenamed.txt")) != 0) {
        testFail("inotify test unlink failed");
    }
    if (readEvents(events) <= 0 || !hasEvent(events, IN_DELETE, "k3brenamed.txt")) {
        testFail("no IN_DELETE for the unlinked file");
    }

    // Mask filtering: a watch that only asked for IN_ATTRIB must not see
    // creates. Use a second fd so the first watch's IN_ALL_EVENTS does not
    // mask the check.
    S64 fd2 = invoke(294, IN_NONBLOCK, 0, 0, 0);
    writePath("/");
    S64 wd2 = invoke(254, (U64)fd2, pathAt, IN_ATTRIB, 0);
    if (wd2 < 1) {
        testFail("second inotify_add_watch failed: %lld", (long long)wd2);
    } else {
        KFileDescriptorPtr quietDesc;
        if (process->openFile(B("/"), B("/k3bquiet.txt"),
                              K_O_WRONLY | K_O_CREAT | K_O_TRUNC, quietDesc) == 0 && quietDesc) {
            process->close(quietDesc->handle);
        }
        CPU64 cpu2(memory);
        cpu2.thread = context.thread;
        cpu2.reg[X64_RAX].setU64(0);
        cpu2.reg[X64_RDI].setU64((U64)fd2);
        cpu2.reg[X64_RSI].setU64(eventsAt);
        cpu2.reg[X64_RDX].setU64(4096);
        ksyscall64(&cpu2);
        S64 quiet = (S64)cpu2.reg[X64_RAX].u64;
        if (quiet != -K_EAGAIN) {
            testFail("IN_ATTRIB-only watch received events for a create (read=%lld)", (long long)quiet);
        }
        process->unlinkFile(B("/k3bquiet.txt"));
    }

    // rm_watch: __NR_inotify_rm_watch = 255; the watch dies with IN_IGNORED.
    if (invoke(255, (U64)fd, (U64)wd, 0, 0) != 0) {
        testFail("inotify_rm_watch failed");
    }
    if (readEvents(events) <= 0 || !hasEvent(events, IN_IGNORED, nullptr)) {
        testFail("no IN_IGNORED after rm_watch");
    }
    if (invoke(255, (U64)fd, (U64)wd, 0, 0) != -K_EINVAL) {
        testFail("second rm_watch of the same wd was not -EINVAL");
    }
    invoke(3, (U64)fd, 0, 0, 0);  // close
    invoke(3, (U64)fd2, 0, 0, 0); // close

    (void)IN_DELETE_SELF; (void)IN_MOVE_SELF; (void)IN_UNMOUNT; (void)IN_Q_OVERFLOW;
    rmdir(hostDir);
}

#endif
#endif
