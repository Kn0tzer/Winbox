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
#include "kinotify.h"
#include "kevent.h"
#include "../../io/fs.h"
#include "../../io/fsfilenode.h"
#include "../cpu/testCPU.h"

#include <stdlib.h>
#include <dirent.h>
#include <atomic>
#include <thread>
#include <unistd.h>
#include <sys/stat.h>

#ifdef BOXEDWINE_GUEST_X64

#include "cpu64.h"
#include "kmemory64.h"

S32 ksyscall64(CPU64* cpu);

// End-to-end inotify: watch a directory through the x64 syscalls, mutate
// it through the ordinary process file APIs, and read the events back:
// IN_CREATE on create, IN_MODIFY + IN_CLOSE_WRITE on write/close,
// IN_MOVED_FROM/IN_MOVED_TO with one cookie on rename, IN_DELETE on
// unlink, IN_IGNORED when the watch is removed. Mask filtering and the
// nonblocking empty read are pinned too.

namespace {
constexpr U32 IN_ACCESS = 0x00000001;
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
constexpr U32 IN_MASK_ADD = 0x20000000;

struct ParsedEvent {
    S32 wd;
    U32 mask;
    U32 cookie;
    U32 len;
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

    // Occupy the lowest free descriptor first: in this harness the
    // process has no stdio fds, and sys_read64 treats fd 0 as stdin
    // (returns EOF), which would shadow the inotify fd if it landed on 0.
    // __NR_timerfd_create = 283; the timerfd stays open for the whole test.
    S64 fillerFd = invoke(283, 1, 0, 0, 0);
    if (fillerFd < 0) {
        testFail("timerfd_create filler failed: %lld", (long long)fillerFd);
        return;
    }
    // __NR_inotify_init1 = 294. Guest-visible contract with the FD-38
    // gate open: invalid flags are refused with EINVAL, valid flags
    // hand back a working descriptor.
    if (invoke(294, 0x40000000, 0, 0, 0) != -K_EINVAL) {
        testFail("inotify_init1 did not refuse invalid flags with EINVAL");
        return;
    }
    S64 initFd = invoke(294, IN_NONBLOCK | IN_CLOEXEC, 0, 0, 0);
    if (initFd < 0) {
        testFail("inotify_init1 did not return a descriptor: %lld", (long long)initFd);
        return;
    }
    if (invoke(3, (U64)initFd, 0, 0, 0) != 0) {
        testFail("close of the inotify_init1 descriptor failed");
        return;
    }
    // The delivery machinery itself is exercised in full below: each
    // instance is built the way inotify_init1 would build it and
    // registered, and every watch/read then goes through the real
    // syscall path (add_watch = 254, rm_watch = 255, read = 0).
    auto makeInotifyFd = [&]() -> S64 {
        std::shared_ptr<KInotify> inst = std::make_shared<KInotify>(false);
        KInotify::registerInstance(inst);
        KFileDescriptorPtr desc = context.process->allocFileDescriptor(inst, K_O_RDONLY, 0, -1, 0);
        return desc ? (S64)desc->handle : -1;
    };
    S64 fd = makeInotifyFd();
    if (fd < 0) {
        testFail("inotify fd creation failed: %lld", (long long)fd);
        return;
    }
    // __NR_inotify_add_watch = 254, watching the fixture root.
    writePath("/");
    S64 wd = invoke(254, (U64)fd, pathAt, IN_ALL_EVENTS, 0);
    if (wd < 1) {
        testFail("inotify_add_watch(/) failed: %lld", (long long)wd);
        return;
    }
    // A second watch on the same path returns the same descriptor; with
    // IN_MASK_ADD the masks merge.
    S64 wdAgain = invoke(254, (U64)fd, pathAt, IN_ATTRIB | IN_MASK_ADD, 0);
    if (wdAgain != wd) {
        testFail("re-adding a watch returned wd %lld, expected %lld", (long long)wdAgain, (long long)wd);
    }

    auto readEventsOn = [&](S64 whichFd, std::vector<ParsedEvent>& out) -> S64 {
        // __NR_read = 0
        S64 result = invoke(0, (U64)whichFd, eventsAt, 4096, 0);
        out.clear();
        if (result <= 0) return result;
        U64 pos = 0;
        while (pos + 16 <= (U64)result) {
            ParsedEvent event;
            event.wd = (S32)memory->readd(eventsAt + pos);
            event.mask = memory->readd(eventsAt + pos + 4);
            event.cookie = memory->readd(eventsAt + pos + 8);
            U32 len = memory->readd(eventsAt + pos + 12);
            event.len = len;
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
    auto readEvents = [&](std::vector<ParsedEvent>& out) -> S64 {
        return readEventsOn(fd, out);
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
    for (const ParsedEvent& event : events) {
        if ((event.mask & IN_CREATE) && event.name == "k3bfile.txt" && event.len != 16) {
            testFail("IN_CREATE name field is %u bytes; Linux pads it to a multiple of 16 (expected 16)", event.len);
        }
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
    // The harness holds its own references to the descriptor/object; the
    // open node's close (and thus IN_CLOSE_WRITE) runs when the last
    // reference goes away, so drop ours before draining.
    file.reset();
    fileDesc.reset();
    createdDesc.reset();
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
    // A rename is a move, not a delete plus a create: the structural
    // detach/attach inside FsFileNode::rename must stay silent, or
    // watchers see a phantom IN_DELETE and IN_CREATE around the pair.
    if (hasEvent(events, IN_DELETE, "k3bfile.txt") || hasEvent(events, IN_CREATE, "k3brenamed.txt")) {
        testFail("rename produced a phantom IN_DELETE/IN_CREATE alongside the move pair");
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
    S64 fd2 = makeInotifyFd();
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

    // A plain re-add replaces the mask (Linux semantics): after the
    // watch is narrowed to IN_ATTRIB alone, creates and deletes in the
    // directory deliver nothing, while a timestamp change still does.
    {
        S64 fd3 = makeInotifyFd();
        if (fd3 < 0) {
            testFail("inotify_init1 for the mask-replace section failed: %lld", (long long)fd3);
        } else {
            writePath("/");
            S64 wdR = invoke(254, (U64)fd3, pathAt, IN_CREATE | IN_DELETE | IN_ATTRIB, 0);
            if (wdR < 1) {
                testFail("mask-replace section add_watch failed: %lld", (long long)wdR);
            } else {
                KFileDescriptorPtr probeDesc;
                if (process->openFile(B("/"), B("/probe1.txt"), K_O_WRONLY | K_O_CREAT | K_O_TRUNC, probeDesc) == 0 && probeDesc) {
                    process->close(probeDesc->handle);
                    probeDesc.reset();
                }
                if (readEventsOn(fd3, events) <= 0 || !hasEvent(events, IN_CREATE, "probe1.txt")) {
                    testFail("mask-replace section: sanity IN_CREATE missing before the re-add");
                }
                writePath("/");
                if (invoke(254, (U64)fd3, pathAt, IN_ATTRIB, 0) != wdR) {
                    testFail("mask-replace section: re-add did not return the same wd");
                }
                if (process->openFile(B("/"), B("/probe2.txt"), K_O_WRONLY | K_O_CREAT | K_O_TRUNC, probeDesc) == 0 && probeDesc) {
                    process->close(probeDesc->handle);
                    probeDesc.reset();
                }
                process->unlinkFile(B("/probe1.txt"));
                if (readEventsOn(fd3, events) != -K_EAGAIN) {
                    testFail("mask-replace section: events arrived for kinds the replaced mask no longer names");
                }
                std::shared_ptr<FsNode> probe2 = Fs::getNodeFromLocalPath(B(""), B("/probe2.txt"), true);
                if (!probe2 || probe2->setTimes(1700000000, 0, 1700000000, 0) != 0) {
                    testFail("mask-replace section: setTimes on probe2 failed");
                } else if (readEventsOn(fd3, events) <= 0 || !hasEvent(events, IN_ATTRIB, "probe2.txt")) {
                    testFail("setTimes produced no IN_ATTRIB for the watched directory");
                }
                process->unlinkFile(B("/probe2.txt"));
            }
            invoke(3, (U64)fd3, 0, 0, 0);
        }
    }

    // A watch on a file itself: renaming it reports IN_MOVE_SELF and the
    // watch follows the file; unlinking it reports IN_ATTRIB (the link
    // count drops), then IN_DELETE_SELF, then IN_IGNORED.
    {
        KFileDescriptorPtr victimDesc;
        if (process->openFile(B("/"), B("/victim.txt"), K_O_WRONLY | K_O_CREAT | K_O_TRUNC, victimDesc) == 0 && victimDesc) {
            process->close(victimDesc->handle);
            victimDesc.reset();
        }
        S64 fd4 = makeInotifyFd();
        writePath("/victim.txt");
        S64 wdV = fd4 < 0 ? -1 : invoke(254, (U64)fd4, pathAt, IN_ALL_EVENTS, 0);
        if (wdV < 1) {
            testFail("file-watch section: add_watch on a file failed");
        } else {
            if (process->rename(B("/victim.txt"), B("/victim2.txt")) != 0) {
                testFail("file-watch section: rename failed");
            }
            if (readEventsOn(fd4, events) <= 0 || events.size() != 1 ||
                !(events[0].mask & IN_MOVE_SELF) || events[0].wd != wdV || events[0].len != 0) {
                testFail("file-watch section: rename of a watched file did not deliver exactly one IN_MOVE_SELF");
            }
            KFileDescriptorPtr writeDesc;
            if (process->openFile(B("/"), B("/victim2.txt"), K_O_WRONLY, writeDesc) == 0 && writeDesc) {
                std::shared_ptr<KFile> wfile = std::dynamic_pointer_cast<KFile>(writeDesc->kobject);
                constexpr U32 textAt2 = TEST_HEAP_ADDRESS + 0xd00;
                context.memory->memcpy(textAt2, (const U8*)"x", 1);
                wfile->write(context.thread, textAt2, 1);
                process->close(writeDesc->handle);
                wfile.reset();
                writeDesc.reset();
            }
            if (readEventsOn(fd4, events) <= 0 || !hasEvent(events, IN_MODIFY, nullptr) ||
                !hasEvent(events, IN_CLOSE_WRITE, nullptr)) {
                testFail("file-watch section: the watch did not follow the renamed file");
            }
            for (const ParsedEvent& event : events) {
                if (event.wd != wdV) {
                    testFail("file-watch section: event on unexpected wd %d", event.wd);
                }
            }
            if (process->unlinkFile(B("/victim2.txt")) != 0) {
                testFail("file-watch section: unlink failed");
            }
            readEventsOn(fd4, events);
            if (events.size() != 3 ||
                !(events[0].mask & IN_ATTRIB) || events[0].len != 0 ||
                !(events[1].mask & IN_DELETE_SELF) ||
                !(events[2].mask & IN_IGNORED)) {
                testFail("file-watch section: unlink of a watched file did not deliver IN_ATTRIB, IN_DELETE_SELF, IN_IGNORED in order");
            }
        }
        if (fd4 >= 0) invoke(3, (U64)fd4, 0, 0, 0);
    }

    // A watch on a directory itself: a metadata change reports
    // IN_ATTRIB with IN_ISDIR set, but a rename reports IN_MOVE_SELF
    // without IN_ISDIR (both measured on the host kernel).
    {
        writePath("/watchdir");
        // __NR_mkdir = 83: create the directory through the guest so it
        // exists in the VFS (a host-side mkdir would be invisible once
        // the root's children are loaded).
        if (invoke(83, pathAt, 0755, 0, 0) != 0) {
            testFail("dir-self section: guest mkdir failed");
        } else {
            S64 fd5 = makeInotifyFd();
            writePath("/watchdir");
            S64 wdD = fd5 < 0 ? -1 : invoke(254, (U64)fd5, pathAt, IN_ALL_EVENTS, 0);
            if (wdD < 1) {
                testFail("dir-self section: add_watch on a directory failed");
            } else {
                std::vector<ParsedEvent> dEvents;
                std::shared_ptr<FsNode> dirNode = Fs::getNodeFromLocalPath(B(""), B("/watchdir"), true);
                if (!dirNode || dirNode->setTimes(1700000000, 0, 1700000000, 0) != 0) {
                    testFail("dir-self section: setTimes on the watched directory failed");
                } else {
                    readEventsOn(fd5, dEvents);
                    if (dEvents.size() != 1 || !(dEvents[0].mask & IN_ATTRIB) || !(dEvents[0].mask & IN_ISDIR) || dEvents[0].len != 0) {
                        testFail("dir-self section: setTimes on a watched directory did not deliver IN_ATTRIB with IN_ISDIR");
                    }
                }
                if (process->rename(B("/watchdir"), B("/watchdir2")) != 0) {
                    testFail("dir-self section: rename of the watched directory failed");
                } else {
                    readEventsOn(fd5, dEvents);
                    if (dEvents.size() != 1 || !(dEvents[0].mask & IN_MOVE_SELF) || (dEvents[0].mask & IN_ISDIR) || dEvents[0].len != 0) {
                        testFail("dir-self section: rename of a watched directory did not deliver IN_MOVE_SELF without IN_ISDIR");
                    }
                }
            }
            if (fd5 >= 0) invoke(3, (U64)fd5, 0, 0, 0);
        }
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
    invoke(3, (U64)fillerFd, 0, 0, 0);

    (void)IN_DELETE_SELF; (void)IN_MOVE_SELF; (void)IN_UNMOUNT; (void)IN_Q_OVERFLOW;
    rmdir(hostDir);
}

// FD-80: x64 preadv (295) / preadv2 (327). The name table knew both
// numbers but the dispatch switch had no case for them, so every call
// answered ENOSYS; pread64 itself was already implemented. Host-kernel
// reference behaviour (measured on the box's 6.17 kernel with
// dedicated reference programs):
//  - segments are filled in order at successive offsets; the file
//    position does not move; a short read at EOF ends the walk; a
//    zero-length segment is skipped.
//  - IN_ACCESS is generated exactly once per successful call --
//    including calls that return 0 (at EOF, or with iovcnt 0); the
//    vectored path differs from pread/read there, which stay silent
//    when no bytes move.
//  - any negative offset is EINVAL, except preadv2's -1, which is
//    the file-position mode (FD-99); a negative iovcnt, or iovcnt >
//    IOV_MAX, is EINVAL; an unknown preadv2 flag bit is EOPNOTSUPP,
//    checked last (FD-100); every defined RWF flag (mask 0x3f) is
//    accepted on a read; a bad descriptor is EBADF.
// FD-83 / FD-84 extend this test below (sections 9-10): the same
// host reference programme family measured readv's event contract
// (exactly one IN_ACCESS per successful call, 0-byte successes
// included) and the wrong-mode errno matrix for the whole
// positioned/plain read family (EBADF, with the validation order
// pinned case by case). Reference: test-results/fd83-84/.
void testPreadv64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x7A000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("preadv test could not map its fixture page");
        return;
    }
    char dirTemplate[] = "/tmp/k3bpreadvXXXXXX";
    char* hostDir = mkdtemp(dirTemplate);
    if (!hostDir) {
        testFail("preadv test could not create its host temp dir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(hostDir), true, true, nullptr);
    Fs::clearPathCache();

    KFileDescriptorPtr fileDesc;
    {
        U32 result = process->openFile(B("/"), B("/data.bin"), K_O_RDWR | K_O_CREAT, fileDesc);
        if ((S32)result < 0 || !fileDesc) {
            testFail("preadv test could not create /data.bin");
            return;
        }
        const char* text = "0123456789ABCDEFGHIJ";
        fileDesc->kobject->writeNative((U8*)text, 20);
        fileDesc->kobject->seek(0);
    }
    S32 fd = (S32)fileDesc->handle;

    std::shared_ptr<KInotify> inotify = std::make_shared<KInotify>(false);
    KInotify::registerInstance(inotify);
    KFileDescriptorPtr inotifyDesc = process->allocFileDescriptor(inotify, K_O_RDONLY, 0, -1, 0);
    if (!inotifyDesc || !inotifyDesc->kobject) {
        testFail("preadv test could not create its inotify instance");
        return;
    }
    S32 wd = (S32)process->inotify_add_watch((S32)inotifyDesc->handle, B("/data.bin"), 0xfff);
    if (wd < 1) {
        testFail("preadv test could not watch /data.bin");
        return;
    }
    auto drainMasks = [&]() {
        std::vector<U32> masks;
        U8 buffer[8192];
        S32 total = (S32)inotifyDesc->kobject->readNative(buffer, sizeof(buffer));
        S32 offset = 0;
        while (total > 0 && offset + 16 <= total) {
            U32 mask, nameLen;
            memcpy(&mask, buffer + offset + 4, 4);
            memcpy(&nameLen, buffer + offset + 12, 4);
            masks.push_back(mask);
            offset += 16 + (S32)nameLen;
        }
        return masks;
    };
    auto accessCount = [&](const std::vector<U32>& masks) {
        int n = 0;
        for (U32 m : masks) if (m & IN_ACCESS) n++;
        return n;
    };
    drainMasks(); // setup noise

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    constexpr U64 iovAt = page + 0x100;
    constexpr U64 seg0 = page + 0x400;
    constexpr U64 seg1 = page + 0x500;
    constexpr U64 seg2 = page + 0x600;
    auto setIov = [&](U32 idx, U64 base, U64 len) {
        memory->writeq(iovAt + idx * 16, base);
        memory->writeq(iovAt + idx * 16 + 8, len);
    };
    auto clearSegs = [&]() {
        memory->writeq(seg0, 0); memory->writeq(seg1, 0); memory->writeq(seg2, 0);
    };
    auto segText = [&](U64 at, U32 len) {
        std::string s;
        for (U32 i = 0; i < len; i++) s += (char)memory->readb(at + i);
        return s;
    };
    auto filePos = [&]() -> S64 { return invoke6(8, (U64)(U32)fd, 0, 1, 0, 0, 0); }; // lseek(fd, 0, SEEK_CUR)

    // 1. Three segments at offset 2; position untouched; one IN_ACCESS.
    setIov(0, seg0, 4); setIov(1, seg1, 4); setIov(2, seg2, 4);
    clearSegs();
    S64 r = invoke6(295, (U64)(U32)fd, iovAt, 3, 2, 0, 0);
    if (r != 12 || segText(seg0, 4) != "2345" || segText(seg1, 4) != "6789" || segText(seg2, 4) != "ABCD") {
        testFail("preadv 3 segments at offset 2 returned %lld with segments '%s' '%s' '%s'",
                 (long long)r, segText(seg0, 4).c_str(), segText(seg1, 4).c_str(), segText(seg2, 4).c_str());
    }
    if (filePos() != 0) {
        testFail("preadv moved the file position to %lld", (long long)filePos());
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("preadv must generate exactly one IN_ACCESS");
    }

    // 2. Short read across the EOF boundary ends the walk.
    setIov(0, seg0, 4); setIov(1, seg1, 4);
    clearSegs();
    r = invoke6(295, (U64)(U32)fd, iovAt, 2, 18, 0, 0);
    if (r != 2 || segText(seg0, 2) != "IJ" || segText(seg1, 4) != std::string(4, '\0')) {
        testFail("preadv across EOF returned %lld (segment0 '%s')", (long long)r, segText(seg0, 4).c_str());
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("short preadv must generate exactly one IN_ACCESS");
    }

    // 3. At EOF the call returns 0 and still generates IN_ACCESS once
    // (the vectored path's measured host behaviour).
    r = invoke6(295, (U64)(U32)fd, iovAt, 1, 1000, 0, 0);
    if (r != 0) {
        testFail("preadv at EOF returned %lld", (long long)r);
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("preadv at EOF must still generate one IN_ACCESS");
    }

    // 4. iovcnt 0 returns 0 and generates IN_ACCESS once.
    r = invoke6(295, (U64)(U32)fd, iovAt, 0, 0, 0, 0);
    if (r != 0) {
        testFail("preadv with iovcnt 0 returned %lld", (long long)r);
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("preadv with iovcnt 0 must generate one IN_ACCESS");
    }

    // 5. Validation, and none of it generates IN_ACCESS.
    if (invoke6(295, (U64)(U32)fd, iovAt, (U64)-1, 0, 0, 0) != -K_EINVAL) {
        testFail("preadv with iovcnt -1 was not -EINVAL");
    }
    if (invoke6(295, (U64)(U32)fd, iovAt, 1025, 0, 0, 0) != -K_EINVAL) {
        testFail("preadv with iovcnt IOV_MAX+1 was not -EINVAL");
    }
    if (invoke6(295, (U64)(U32)fd, iovAt, 1, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0) != -K_EINVAL) {
        testFail("preadv with offset -1 was not -EINVAL");
    }
    if (invoke6(295, (U64)(U32)fd, iovAt, 1, 0xFFFFFFFEULL, 0xFFFFFFFFULL, 0) != -K_EINVAL) {
        testFail("preadv with offset -2 was not -EINVAL");
    }
    if (invoke6(295, 9999, iovAt, 1, 0, 0, 0) != -9) {
        testFail("preadv with a bad descriptor was not -EBADF");
    }
    if (accessCount(drainMasks()) != 0) {
        testFail("failed preadv calls generated IN_ACCESS");
    }

    // 6. pos_h is honored: offset 2^32 + 2 is past EOF (a low-word-only
    // implementation would read at offset 2 and return data).
    setIov(0, seg0, 4);
    clearSegs();
    r = invoke6(295, (U64)(U32)fd, iovAt, 1, 2, 1, 0);
    if (r != 0) {
        testFail("preadv at offset 2^32+2 returned %lld (pos_h ignored?)", (long long)r);
    }
    drainMasks();

    // 7. A zero-length segment in the middle is skipped.
    setIov(0, seg0, 4); setIov(1, seg1, 0); setIov(2, seg2, 4);
    clearSegs();
    r = invoke6(295, (U64)(U32)fd, iovAt, 3, 0, 0, 0);
    if (r != 8 || segText(seg0, 4) != "0123" || segText(seg2, 4) != "4567") {
        testFail("preadv with a zero-length segment returned %lld ('%s' '%s')",
                 (long long)r, segText(seg0, 4).c_str(), segText(seg2, 4).c_str());
    }
    drainMasks();

    // 8. preadv2: flags 0, RWF_NOWAIT and RWF_APPEND are accepted on a
    // read; an unknown flag bit is EOPNOTSUPP.
    setIov(0, seg0, 4);
    clearSegs();
    r = invoke6(327, (U64)(U32)fd, iovAt, 1, 4, 0, 0);
    if (r != 4 || segText(seg0, 4) != "4567") {
        testFail("preadv2 with flags 0 returned %lld ('%s')", (long long)r, segText(seg0, 4).c_str());
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("preadv2 must generate exactly one IN_ACCESS");
    }
    if (invoke6(327, (U64)(U32)fd, iovAt, 1, 4, 0, 0x8) != 4) { // RWF_NOWAIT
        testFail("preadv2 with RWF_NOWAIT failed");
    }
    drainMasks();
    if (invoke6(327, (U64)(U32)fd, iovAt, 1, 4, 0, 0x10) != 4) { // RWF_APPEND
        testFail("preadv2 with RWF_APPEND failed");
    }
    drainMasks();
    if (invoke6(327, (U64)(U32)fd, iovAt, 1, 4, 0, 0x40) != -K_EOPNOTSUPP) {
        testFail("preadv2 with an unknown flag bit was not -EOPNOTSUPP");
    }
    if (accessCount(drainMasks()) != 0) {
        testFail("rejected preadv2 generated IN_ACCESS");
    }

    // 9. FD-84: the positioned/plain read family answers -EBADF on
    // a descriptor that cannot be read (host-measured on the 6.17
    // kernel; reference in test-results/fd83-84/). The validation
    // order is part of the measured contract: a negative offset
    // (EINVAL) precedes the mode answer; the mode answer precedes
    // preadv2's flag check (EOPNOTSUPP), preadv's iovcnt check
    // (EINVAL), and the zero-count returns. Failures generate no
    // IN_ACCESS.
    {
        KFileDescriptorPtr wdesc;
        U32 result = process->openFile(B("/"), B("/data.bin"), K_O_WRONLY, wdesc);
        if ((S32)result < 0 || !wdesc) {
            testFail("preadv test could not open /data.bin write-only");
            return;
        }
        U64 wfd = (U64)(U32)wdesc->handle;
        setIov(0, seg0, 4);
        if (invoke6(295, wfd, iovAt, 1, 0, 0, 0) != -K_EBADF) {
            testFail("preadv on a write-only descriptor was not -EBADF");
        }
        if (invoke6(327, wfd, iovAt, 1, 0, 0, 0x40) != -K_EBADF) {
            testFail("preadv2 with an unknown flag on a write-only descriptor was not -EBADF (mode precedes the flag check)");
        }
        if (invoke6(295, wfd, iovAt, (U64)-1, 0, 0, 0) != -K_EBADF) {
            testFail("preadv with iovcnt -1 on a write-only descriptor was not -EBADF (mode precedes the iovcnt check)");
        }
        if (invoke6(295, wfd, iovAt, 1, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0) != -K_EINVAL) {
            testFail("preadv with offset -1 on a write-only descriptor was not -EINVAL (offset precedes the mode check)");
        }
        if (invoke6(295, wfd, iovAt, 0, 0, 0, 0) != -K_EBADF) {
            testFail("preadv with iovcnt 0 on a write-only descriptor was not -EBADF");
        }
        if (invoke6(17, wfd, seg0, 4, 0, 0, 0) != -K_EBADF) {
            testFail("pread64 on a write-only descriptor was not -EBADF");
        }
        if (invoke6(17, wfd, seg0, 0, 0, 0, 0) != -K_EBADF) {
            testFail("pread64 with count 0 on a write-only descriptor was not -EBADF");
        }
        if (invoke6(17, wfd, seg0, 4, 0xFFFFFFFFFFFFFFFFULL, 0, 0) != -K_EINVAL) {
            testFail("pread64 with offset -1 on a write-only descriptor was not -EINVAL (offset precedes the mode check)");
        }
        if (invoke6(0, wfd, seg0, 4, 0, 0, 0) != -K_EBADF) {
            testFail("read on a write-only descriptor was not -EBADF");
        }
        if (invoke6(0, wfd, seg0, 0, 0, 0, 0) != -K_EBADF) {
            testFail("read with count 0 on a write-only descriptor was not -EBADF");
        }
        if (accessCount(drainMasks()) != 0) {
            testFail("wrong-mode reads generated IN_ACCESS");
        }
        process->close(wdesc->handle);
    }

    // 10. FD-83: readv (19) generates exactly one IN_ACCESS per
    // successful call -- a multi-segment walk included -- and the
    // 0-byte successes (at EOF, iovcnt 0, all-zero-length segments)
    // generate one too, where plain read/pread stay silent (host
    // behaviour measured on the 6.17 kernel; reference in
    // test-results/fd83-84/). Failures generate none. Before the
    // fix, readv notified per segment through the plain read path
    // (the inotify queue's coalescing of consecutive identical
    // events masks that in the simple count below) and stayed
    // silent on every 0-byte success -- the cases that provably
    // diverge, and the ones that fail on the unfixed code.
    {
        // A second descriptor for the same file: the plain-read
        // path special-cases descriptor 0 (stdin), and the first
        // descriptor may hold it in this harness.
        KFileDescriptorPtr rdesc;
        U32 result = process->openFile(B("/"), B("/data.bin"), K_O_RDONLY, rdesc);
        if ((S32)result < 0 || !rdesc) {
            testFail("readv test could not reopen /data.bin read-only");
            return;
        }
        U64 rfd = (U64)(U32)rdesc->handle;
        auto seekTo = [&](U64 off) { invoke6(8, rfd, off, 0, 0, 0, 0); };

        // Multi-segment walk: the data lands in order, the position
        // advances by the total, exactly one IN_ACCESS.
        setIov(0, seg0, 4); setIov(1, seg1, 4); setIov(2, seg2, 4);
        clearSegs();
        seekTo(0);
        r = invoke6(19, rfd, iovAt, 3, 0, 0, 0);
        if (r != 12 || segText(seg0, 4) != "0123" || segText(seg1, 4) != "4567" || segText(seg2, 4) != "89AB") {
            testFail("readv 3 segments returned %lld ('%s' '%s' '%s')",
                     (long long)r, segText(seg0, 4).c_str(), segText(seg1, 4).c_str(), segText(seg2, 4).c_str());
        }
        if (invoke6(8, rfd, 0, 1, 0, 0, 0) != 12) {
            testFail("readv did not advance the file position to 12");
        }
        if (accessCount(drainMasks()) != 1) {
            testFail("readv across 3 segments must generate exactly one IN_ACCESS");
        }

        // A short final segment at the EOF boundary ends the walk;
        // the partial success still generates exactly one event.
        setIov(0, seg0, 4); setIov(1, seg1, 4);
        clearSegs();
        seekTo(18);
        r = invoke6(19, rfd, iovAt, 2, 0, 0, 0);
        if (r != 2 || segText(seg0, 2) != "IJ") {
            testFail("readv across EOF returned %lld ('%s')", (long long)r, segText(seg0, 4).c_str());
        }
        if (accessCount(drainMasks()) != 1) {
            testFail("short readv must generate exactly one IN_ACCESS");
        }

        // At EOF the call returns 0 and still generates one
        // IN_ACCESS (the vectored path's measured host behaviour).
        setIov(0, seg0, 4);
        seekTo(20);
        r = invoke6(19, rfd, iovAt, 1, 0, 0, 0);
        if (r != 0) {
            testFail("readv at EOF returned %lld", (long long)r);
        }
        if (accessCount(drainMasks()) != 1) {
            testFail("readv at EOF must still generate one IN_ACCESS");
        }

        // iovcnt 0 returns 0 and generates one IN_ACCESS.
        r = invoke6(19, rfd, iovAt, 0, 0, 0, 0);
        if (r != 0) {
            testFail("readv with iovcnt 0 returned %lld", (long long)r);
        }
        if (accessCount(drainMasks()) != 1) {
            testFail("readv with iovcnt 0 must generate one IN_ACCESS");
        }

        // All-zero-length segments: a 0-byte success, one IN_ACCESS.
        setIov(0, seg0, 0); setIov(1, seg1, 0);
        r = invoke6(19, rfd, iovAt, 2, 0, 0, 0);
        if (r != 0) {
            testFail("readv of zero-length segments returned %lld", (long long)r);
        }
        if (accessCount(drainMasks()) != 1) {
            testFail("readv of zero-length segments must generate one IN_ACCESS");
        }

        // Failures generate no IN_ACCESS: bad descriptor, iovcnt
        // out of range, wrong mode (FD-84 -- iovcnt 0 included: the
        // host mode-checks before the call can succeed). The walk
        // is parked at EOF so that on unfixed code the out-of-range
        // iovcnt calls short-read and return instead of walking
        // the iovec array without bound.
        setIov(0, seg0, 4);
        seekTo(20);
        if (invoke6(19, 9999, iovAt, 1, 0, 0, 0) != -K_EBADF) {
            testFail("readv with a bad descriptor was not -EBADF");
        }
        if (invoke6(19, rfd, iovAt, 1025, 0, 0, 0) != -K_EINVAL) {
            testFail("readv with iovcnt IOV_MAX+1 was not -EINVAL");
        }
        if (invoke6(19, rfd, iovAt, (U64)-1, 0, 0, 0) != -K_EINVAL) {
            testFail("readv with iovcnt -1 was not -EINVAL");
        }
        {
            KFileDescriptorPtr wdesc;
            U32 wresult = process->openFile(B("/"), B("/data.bin"), K_O_WRONLY, wdesc);
            if ((S32)wresult < 0 || !wdesc) {
                testFail("readv test could not open /data.bin write-only");
                return;
            }
            U64 wfd = (U64)(U32)wdesc->handle;
            if (invoke6(19, wfd, iovAt, 1, 0, 0, 0) != -K_EBADF) {
                testFail("readv on a write-only descriptor was not -EBADF");
            }
            if (invoke6(19, wfd, iovAt, 0, 0, 0, 0) != -K_EBADF) {
                testFail("readv with iovcnt 0 on a write-only descriptor was not -EBADF");
            }
            process->close(wdesc->handle);
        }
        if (accessCount(drainMasks()) != 0) {
            testFail("failed readv calls generated IN_ACCESS");
        }

        // Control: a plain read at EOF stays silent (the FD-59
        // behaviour the vectored fix must not disturb).
        seekTo(20);
        if (invoke6(0, rfd, seg0, 4, 0, 0, 0) != 0) {
            testFail("plain read at EOF did not return 0");
        }
        if (accessCount(drainMasks()) != 0) {
            testFail("plain read at EOF generated IN_ACCESS");
        }
        process->close(rdesc->handle);
    }
    // 10. FD-99: preadv2's offset -1 is the file-position mode (read
    // at the current position and advance it, as pwritev2's -1 does);
    // preadv's own -1 stays EINVAL. RWF_NOAPPEND (0x20) is a defined
    // read flag on the host kernel -- the defined mask is 0x3f, not
    // 0x1f. The offset validity check precedes the descriptor check
    // (the write family's measured order, and KProcess::preadv's):
    // a bad descriptor with an offset that is invalid for the call
    // answers EINVAL; preadv2's -1 is valid, so there the descriptor
    // answers EBADF.
    if (invoke6(8, (U64)(U32)fd, 5, 0, 0, 0, 0) != 5) { // lseek SEEK_SET
        testFail("preadv test could not seek its fixture file");
        return;
    }
    setIov(0, seg0, 4); setIov(1, seg1, 4);
    clearSegs();
    r = invoke6(327, (U64)(U32)fd, iovAt, 2, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0);
    if (r != 8 || segText(seg0, 4) != "5678" || segText(seg1, 4) != "9ABC") {
        testFail("preadv2 offset -1 returned %lld with segments '%s' '%s'",
                 (long long)r, segText(seg0, 4).c_str(), segText(seg1, 4).c_str());
    }
    if (filePos() != 13) {
        testFail("preadv2 offset -1 left the position at %lld, expected 13", (long long)filePos());
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("preadv2 offset -1 must generate exactly one IN_ACCESS");
    }
    if (invoke6(8, (U64)(U32)fd, 0, 0, 0, 0, 0) != 0) { // rewind
        testFail("preadv test could not rewind its fixture file");
        return;
    }
    setIov(0, seg0, 4);
    clearSegs();
    if (invoke6(327, (U64)(U32)fd, iovAt, 1, 4, 0, 0x20) != 4 || segText(seg0, 4) != "4567") {
        testFail("preadv2 with RWF_NOAPPEND (0x20) did not read 4 bytes at offset 4");
    }
    drainMasks();
    if (invoke6(327, (U64)(U32)fd, iovAt, 1, 0xFFFFFFFEULL, 0xFFFFFFFFULL, 0) != -K_EINVAL) {
        testFail("preadv2 with offset -2 was not -EINVAL");
    }
    if (invoke6(295, 9999, iovAt, 1, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0) != -K_EINVAL) {
        testFail("preadv with a bad descriptor and offset -1 was not -EINVAL");
    }
    if (invoke6(327, 9999, iovAt, 1, 0xFFFFFFFEULL, 0xFFFFFFFFULL, 0) != -K_EINVAL) {
        testFail("preadv2 with a bad descriptor and offset -2 was not -EINVAL");
    }
    if (invoke6(327, 9999, iovAt, 1, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0) != -9) {
        testFail("preadv2 with a bad descriptor and offset -1 was not -EBADF");
    }
    if (accessCount(drainMasks()) != 0) {
        testFail("rejected preadv2 position-mode calls generated IN_ACCESS");
    }

    // 11. FD-100: the preadv2 flags check runs LAST -- behind the
    // iovcnt range check and the iovec array check. An undefined flag
    // bit combined with an out-of-range iovcnt answers EINVAL, and
    // combined with a null or unmapped iovec array answers EFAULT;
    // only a call that passes those checks reaches EOPNOTSUPP (the
    // section-8 case). The array check covers the array's full range,
    // as sys_pwritev64's already did.
    if (invoke6(327, (U64)(U32)fd, iovAt, (U64)-1, 0, 0, 0x40) != -K_EINVAL) {
        testFail("preadv2 with iovcnt -1 and an undefined flag was not -EINVAL");
    }
    if (invoke6(327, (U64)(U32)fd, iovAt, 1025, 0, 0, 0x40) != -K_EINVAL) {
        testFail("preadv2 with iovcnt IOV_MAX+1 and an undefined flag was not -EINVAL");
    }
    if (invoke6(327, (U64)(U32)fd, 0, 1, 0, 0, 0x40) != -K_EFAULT) {
        testFail("preadv2 with a null iovec and an undefined flag was not -EFAULT");
    }
    if (invoke6(327, (U64)(U32)fd, 0x1000, 1, 0, 0, 0x40) != -K_EFAULT) {
        testFail("preadv2 with an unmapped iovec and an undefined flag was not -EFAULT");
    }
    if (invoke6(327, (U64)(U32)fd, 0, 1, 0, 0, 0) != -K_EFAULT) {
        testFail("preadv2 with a null iovec was not -EFAULT");
    }
    if (invoke6(327, (U64)(U32)fd, 0x1000, 1, 0, 0, 0) != -K_EFAULT) {
        testFail("preadv2 with an unmapped iovec was not -EFAULT");
    }
    if (accessCount(drainMasks()) != 0) {
        testFail("rejected preadv2 flag-order calls generated IN_ACCESS");
    }

    process->close((S32)inotifyDesc->handle);
    process->close(fd);
    rmdir(hostDir);
}

// FD-82: the 64-bit guest file-mmap path (sys_mmap64_file)
// materializes file mappings as copied/shared pages in KMemory64,
// which carried no open-description accounting, so FD-79's
// close-event deferral did not apply there: a 64-bit guest saw
// IN_CLOSE_WRITE / IN_CLOSE_NOWRITE at fd close while such a mapping
// was still live, where the host defers the event to the last unmap.
// Same host contract as testCloseWriteDeferral (measured on the box's
// 6.17 kernel), driven here through the real 64-bit syscall dispatch
// (mmap 9 / munmap 11 / close 3), so the materialized path itself is
// the thing under test.
void testCloseWriteDeferral64() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    char dirTemplate[] = "/tmp/bw64kclose64XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for close-write deferral 64 test");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    // Create /w64.txt with known content before any watch exists.
    {
        KFileDescriptorPtr created;
        U32 result = process->openFile(B("/"), B("/w64.txt"), K_O_RDWR | K_O_CREAT, created);
        if ((S32)result < 0 || !created) {
            testFail("openFile(O_CREAT) for /w64.txt failed: %d", (S32)result);
            return;
        }
        const char* text = "0123456789abcdef";
        created->kobject->writeNative((U8*)text, 16);
        process->close(created->handle);
    }

    std::shared_ptr<KInotify> inotify = std::make_shared<KInotify>(false);
    KInotify::registerInstance(inotify);
    KFileDescriptorPtr inotifyDesc = process->allocFileDescriptor(inotify, K_O_RDONLY, 0, -1, 0);
    if (!inotifyDesc || !inotifyDesc->kobject) {
        testFail("allocFileDescriptor for the inotify instance failed");
        return;
    }
    S32 wd = (S32)process->inotify_add_watch((S32)inotifyDesc->handle, B("/w64.txt"), 0xfff);
    if (wd < 1) {
        testFail("inotify_add_watch on /w64.txt failed: %d", wd);
        return;
    }

    struct Ev { S32 wd; U32 mask; };
    auto drainEvents = [&]() {
        std::vector<Ev> events;
        U8 buffer[8192];
        S32 total = (S32)inotifyDesc->kobject->readNative(buffer, sizeof(buffer));
        S32 offset = 0;
        while (total > 0 && offset + 16 <= total) {
            Ev e;
            U32 nameLen;
            memcpy(&e.wd, buffer + offset, 4);
            memcpy(&e.mask, buffer + offset + 4, 4);
            memcpy(&nameLen, buffer + offset + 12, 4);
            events.push_back(e);
            offset += 16 + (S32)nameLen;
        }
        return events;
    };
    auto hasClose = [](const std::vector<Ev>& events, S32 wd_, U32 bit) {
        for (auto& e : events) {
            if (e.wd == wd_ && (e.mask & bit)) return true;
        }
        return false;
    };
    constexpr U32 IN_CLOSE_WRITE_BIT = 0x8;
    constexpr U32 IN_CLOSE_NOWRITE_BIT = 0x10;

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    auto sysMmap = [&](U64 addr, U64 len, U64 prot, U64 flags, S32 fd) -> S64 {
        return invoke6(9, addr, len, prot, flags, (U64)(U32)fd, 0);
    };
    auto sysMunmap = [&](U64 addr, U64 len) -> S64 {
        return invoke6(11, addr, len, 0, 0, 0, 0);
    };
    auto sysClose = [&](S32 fd) -> S64 {
        return invoke6(3, (U64)(U32)fd, 0, 0, 0, 0, 0);
    };
    auto openWatched = [&](U32 flags, S32& handleOut) -> bool {
        KFileDescriptorPtr d;
        U32 result = process->openFile(B("/"), B("/w64.txt"), flags, d);
        if ((S32)result < 0 || !d) return false;
        handleOut = (S32)d->handle;
        return true; // the local reference drops here; the fd table holds the description
    };
    constexpr U64 PROT_R = 1;
    constexpr U64 PROT_RW = 3;
    constexpr U64 MAP_SHARED_ = 0x01;
    constexpr U64 MAP_PRIVATE_ = 0x02;
    constexpr U64 MAP_FIXED_ = 0x10;
    constexpr U64 MAP_ANON_ = 0x20;

    // Case A64: a MAP_SHARED writable mapping made through the 64-bit
    // mmap syscall stays live across close -- IN_CLOSE_WRITE must
    // wait for the munmap.
    {
        S32 h;
        if (!openWatched(K_O_RDWR, h)) { testFail("case A64 open failed"); return; }
        drainEvents(); // IN_OPEN
        S64 addr = sysMmap(0, K64_PAGE_SIZE, PROT_RW, MAP_SHARED_, h);
        if (addr < 0) { testFail("case A64 mmap failed: %lld", (long long)addr); return; }
        if (memory->readb((U64)addr) != '0') {
            testFail("case A64: the shared mapping does not show the file content");
        }
        memory->writeb((U64)addr, 'X');
        sysClose(h);
        if (hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case A64: IN_CLOSE_WRITE fired at fd close while a 64-bit shared mapping is live");
        }
        if (sysMunmap((U64)addr, K64_PAGE_SIZE) != 0) { testFail("case A64 munmap failed"); return; }
        if (!hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case A64: IN_CLOSE_WRITE did not arrive at munmap");
        }
    }

    // Case B64: a read-only description with a MAP_PRIVATE read
    // mapping defers IN_CLOSE_NOWRITE the same way.
    {
        S32 h;
        if (!openWatched(K_O_RDONLY, h)) { testFail("case B64 open failed"); return; }
        drainEvents();
        S64 addr = sysMmap(0, K64_PAGE_SIZE, PROT_R, MAP_PRIVATE_, h);
        if (addr < 0) { testFail("case B64 mmap failed: %lld", (long long)addr); return; }
        if (memory->readb((U64)addr) != '0') {
            testFail("case B64: the private mapping does not show the file content");
        }
        sysClose(h);
        std::vector<Ev> atClose = drainEvents();
        if (hasClose(atClose, wd, IN_CLOSE_NOWRITE_BIT) || hasClose(atClose, wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case B64: a close event fired at fd close while a 64-bit private mapping is live");
        }
        if (sysMunmap((U64)addr, K64_PAGE_SIZE) != 0) { testFail("case B64 munmap failed"); return; }
        if (!hasClose(drainEvents(), wd, IN_CLOSE_NOWRITE_BIT)) {
            testFail("case B64: IN_CLOSE_NOWRITE did not arrive at munmap");
        }
    }

    // Case C64: two mappings of one description -- the event waits
    // for the second munmap.
    {
        S32 h;
        if (!openWatched(K_O_RDWR, h)) { testFail("case C64 open failed"); return; }
        drainEvents();
        S64 addr1 = sysMmap(0, K64_PAGE_SIZE, PROT_RW, MAP_SHARED_, h);
        S64 addr2 = sysMmap(0, K64_PAGE_SIZE, PROT_RW, MAP_SHARED_, h);
        if (addr1 < 0 || addr2 < 0 || addr1 == addr2) { testFail("case C64 mmaps failed: %lld %lld", (long long)addr1, (long long)addr2); return; }
        sysClose(h);
        if (hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case C64: IN_CLOSE_WRITE fired at fd close with two 64-bit mappings live");
        }
        if (sysMunmap((U64)addr1, K64_PAGE_SIZE) != 0) { testFail("case C64 first munmap failed"); return; }
        if (hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case C64: IN_CLOSE_WRITE fired at the first of two munmaps");
        }
        if (sysMunmap((U64)addr2, K64_PAGE_SIZE) != 0) { testFail("case C64 second munmap failed"); return; }
        if (!hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case C64: IN_CLOSE_WRITE did not arrive at the last munmap");
        }
    }

    // Case D64: mapping unmapped before close -- nothing at munmap,
    // the event fires at close.
    {
        S32 h;
        if (!openWatched(K_O_RDWR, h)) { testFail("case D64 open failed"); return; }
        drainEvents();
        S64 addr = sysMmap(0, K64_PAGE_SIZE, PROT_RW, MAP_SHARED_, h);
        if (addr < 0) { testFail("case D64 mmap failed: %lld", (long long)addr); return; }
        if (sysMunmap((U64)addr, K64_PAGE_SIZE) != 0) { testFail("case D64 munmap failed"); return; }
        if (hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case D64: IN_CLOSE_WRITE fired at munmap while the fd is still open");
        }
        sysClose(h);
        if (!hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case D64: IN_CLOSE_WRITE did not fire at close after the mapping was gone");
        }
    }

    // Case E64: no mapping at all -- the event still fires at close.
    {
        S32 h;
        if (!openWatched(K_O_RDWR, h)) { testFail("case E64 open failed"); return; }
        drainEvents();
        sysClose(h);
        if (!hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case E64: plain close produced no IN_CLOSE_WRITE");
        }
    }

    // Case F64: an anonymous MAP_FIXED mapping over the file mapping
    // kills it by replacement -- the deferred event arrives at the
    // overwrite, not before.
    {
        S32 h;
        if (!openWatched(K_O_RDWR, h)) { testFail("case F64 open failed"); return; }
        drainEvents();
        S64 addr = sysMmap(0, K64_PAGE_SIZE, PROT_RW, MAP_SHARED_, h);
        if (addr < 0) { testFail("case F64 mmap failed: %lld", (long long)addr); return; }
        sysClose(h);
        if (hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case F64: IN_CLOSE_WRITE fired at fd close while a 64-bit shared mapping is live");
        }
        S64 r = sysMmap((U64)addr, K64_PAGE_SIZE, PROT_RW, MAP_FIXED_ | MAP_ANON_, -1);
        if (r != addr) { testFail("case F64 overwrite mmap failed: %lld", (long long)r); return; }
        if (!hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case F64: IN_CLOSE_WRITE did not arrive when the mapping was overwritten");
        }
        sysMunmap((U64)addr, K64_PAGE_SIZE);
    }

    // Case G64: a partial unmap of a two-page mapping does not fire
    // the event; unmapping the last page does.
    {
        S32 h;
        if (!openWatched(K_O_RDWR, h)) { testFail("case G64 open failed"); return; }
        drainEvents();
        S64 addr = sysMmap(0, 2 * K64_PAGE_SIZE, PROT_RW, MAP_SHARED_, h);
        if (addr < 0) { testFail("case G64 mmap failed: %lld", (long long)addr); return; }
        sysClose(h);
        if (hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case G64: IN_CLOSE_WRITE fired at fd close while a two-page mapping is live");
        }
        if (sysMunmap((U64)addr, K64_PAGE_SIZE) != 0) { testFail("case G64 partial munmap failed"); return; }
        if (hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case G64: IN_CLOSE_WRITE fired at a partial munmap");
        }
        if (sysMunmap((U64)addr + K64_PAGE_SIZE, K64_PAGE_SIZE) != 0) { testFail("case G64 final munmap failed"); return; }
        if (!hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case G64: IN_CLOSE_WRITE did not arrive at the last page's munmap");
        }
    }

    process->close((S32)inotifyDesc->handle);
    rmdir(dir);
}


// FD-101: the 64-bit sendmsg path (and sendmmsg, which loops it) must
// not fabricate zero bytes for guest memory it cannot read. Host-kernel
// reference behaviour (measured on the box's 6.17 kernel with dedicated
// reference programs, test-results/fd101/hostref-sendmsg-fd101 and
// hostref-sendto-fd101): for AF_UNIX the send is all-or-nothing for
// EVERY socket type -- if any byte of any nonzero segment is unreadable,
// the call answers -EFAULT and nothing arrives; this holds for a fully
// unreadable segment, a segment whose readable prefix runs into an
// unreadable page, and a bad segment anywhere in the array. Zero-length
// segments are skipped even when their base is unreadable; a
// PROT_WRITE-only segment is readable to the copy (x86
// write-implies-read) and lands its real bytes. The msghdr, the iov
// array, msg_name and msg_control are imported before the data: an
// unreadable one answers -EFAULT, an iovlen over UIO_MAXIOV answers
// -EMSGSIZE, and the descriptor answers (-EBADF / -ENOTSOCK) precede
// every copy fault. sendmmsg delivers the messages before the bad one
// and reports their count. On the base the calls below land zero-filled
// bytes and report success.
// The peer is drained through recvmsg (the message-queue receive that
// pairs with sendmsg in this socket layer), gated by a zero-timeout
// poll so an empty queue can never block the test; each case uses a
// fresh socketpair so no case can see another case's bytes.
void testSendmsgBadBuffer64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 dataPage = 0x71000000ULL;
    constexpr U64 crossPage = 0x72000000ULL;
    constexpr U64 nonePage = 0x73000000ULL;
    constexpr U64 wonlyPage = 0x74000000ULL;
    constexpr U64 bigArray = 0x75000000ULL;
    constexpr U64 unmapped = 0x6200000000ULL;
    auto mapFixture = [&](U64 addr, U64 size, U32 prot) {
        if (!memory->getPageFlags(addr >> K64_PAGE_SHIFT) &&
            memory->mmapAnonymousFixed(addr, size, prot) != addr) {
            testFail("sendmsg-bad-buffer test could not map its fixture page at 0x%llx", (unsigned long long)addr);
            return false;
        }
        return true;
    };
    if (!mapFixture(dataPage, K64_PAGE_SIZE, 3) || !mapFixture(crossPage, K64_PAGE_SIZE, 3) ||
        !mapFixture(nonePage, K64_PAGE_SIZE, 0) || !mapFixture(wonlyPage, K64_PAGE_SIZE, 2) ||
        !mapFixture(bigArray, 5 * K64_PAGE_SIZE, 3)) return;
    if (memory->getPageFlags(unmapped >> K64_PAGE_SHIFT) != 0 ||
        memory->getPageFlags((crossPage + K64_PAGE_SIZE) >> K64_PAGE_SHIFT) != 0) {
        testFail("sendmsg-bad-buffer test fixture addresses are unexpectedly mapped");
        return;
    }
    if (!(memory->getPageFlags(nonePage >> K64_PAGE_SHIFT) & K64_PAGE_MAPPED) ||
        (memory->getPageFlags(nonePage >> K64_PAGE_SHIFT) & K64_PAGE_READ)) {
        testFail("sendmsg-bad-buffer test PROT_NONE fixture has the wrong flags");
        return;
    }
    if (!(memory->getPageFlags(wonlyPage >> K64_PAGE_SHIFT) & K64_PAGE_READ)) {
        testFail("sendmsg-bad-buffer test write-only fixture lacks the x86 write-implies-read flag");
        return;
    }

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

    // Fixture layout on the data page.
    constexpr U64 textAt = dataPage + 0x000;      // 16 good bytes
    constexpr U64 svAt = dataPage + 0x100;        // socketpair out fds
    constexpr U64 recvAt = dataPage + 0x200;      // drain buffer
    constexpr U64 pollAt = dataPage + 0x300;      // one pollfd
    constexpr U64 hdrAt = dataPage + 0x1000;      // send msghdr (slot 0)
    constexpr U64 iovAt = dataPage + 0x1800;      // send iovec arena
    constexpr U64 riovAt = dataPage + 0x1C00;     // recv iovec
    constexpr U64 rhdrAt = dataPage + 0x1C80;     // recv msghdr
    const char* text = "0123456789ABCDEF";
    memory->memcpyToGuest(textAt, text, 16);
    std::vector<U8> pattern(K64_PAGE_SIZE);
    for (U32 i = 0; i < K64_PAGE_SIZE; i++) pattern[i] = (U8)('a' + (i % 26));
    memory->memcpyToGuest(crossPage, pattern.data(), K64_PAGE_SIZE);
    const char* wonlyText = "FEDCBA9876543210";
    for (U32 i = 0; i < 16; i++) memory->writeb(wonlyPage + i, (U8)wonlyText[i]);
    memory->writeq(riovAt, recvAt);
    memory->writeq(riovAt + 8, 512);
    for (U32 i = 0; i < 56; i += 8) memory->writeq(rhdrAt + i, 0);
    memory->writeq(rhdrAt + 16, riovAt);
    memory->writeq(rhdrAt + 24, 1);

    struct Pair {
        S32 tx = 0, rx = 0;
    };
    auto makePair = [&](U32 type, Pair& p) {
        if (invoke(53, 1, type, 0, svAt) != 0) {
            testFail("sendmsg-bad-buffer test could not create a socketpair (type %u)", type);
            return false;
        }
        p.tx = (S32)memory->readd(svAt);
        p.rx = (S32)memory->readd(svAt + 4);
        return true;
    };
    auto closePair = [&](Pair& p) {
        process->close((S32)p.tx);
        process->close((S32)p.rx);
    };
    // Build the send msghdr (slot 0) over the iov arena.
    auto buildHdr = [&](U64 iovAddr, U64 iovlen) -> U64 {
        for (U32 i = 0; i < 56; i += 8) memory->writeq(hdrAt + i, 0);
        memory->writeq(hdrAt + 16, iovAddr);
        memory->writeq(hdrAt + 24, iovlen);
        return hdrAt;
    };
    auto setIov = [&](U32 idx, U64 base, U64 len) {
        memory->writeq(iovAt + idx * 16, base);
        memory->writeq(iovAt + idx * 16 + 8, len);
    };
    // Drain whatever is queued on rx via recvmsg; returns the byte
    // count and fills `got`. A zero-timeout poll gates every recvmsg
    // so an empty queue never blocks.
    auto drain = [&](S32 rx, std::string& got) -> S64 {
        got.clear();
        S64 total = 0;
        for (int iter = 0; iter < 8; iter++) {
            memory->writed(pollAt, (U32)rx);
            memory->writew(pollAt + 4, 1); // POLLIN
            memory->writew(pollAt + 6, 0);
            if (invoke(7, pollAt, 1, 0, 0) <= 0) break;
            if (!(memory->readw(pollAt + 6) & 1)) break;
            S64 rc = invoke(47, (U64)(U32)rx, rhdrAt, 0, 0);
            if (rc <= 0) break;
            std::vector<U8> buf((size_t)rc);
            memory->memcpyFromGuest(buf.data(), recvAt, (U64)rc);
            got.append((const char*)buf.data(), (size_t)rc);
            total += rc;
        }
        return total;
    };
    // One sendmsg case on a fresh stream pair: the call must return
    // wantRet and the peer must hold exactly wantBytes.
    auto sendCase = [&](const char* what, S64 wantRet, const std::string& wantBytes) {
        Pair p;
        if (!makePair(1 /*SOCK_STREAM*/, p)) return;
        S64 r = invoke(46, (U64)(U32)p.tx, hdrAt, 0, 0);
        std::string got;
        S64 held = drain(p.rx, got);
        if (r != wantRet || held != (S64)wantBytes.size() || got != wantBytes) {
            testFail("%s returned %lld (want %lld), peer holds %lld bytes",
                     what, (long long)r, (long long)wantRet, (long long)held);
        }
        closePair(p);
    };

    // 1. Fully unmapped segment: -EFAULT, nothing arrives.
    setIov(0, unmapped, 16);
    buildHdr(iovAt, 1);
    sendCase("sendmsg from an unmapped segment", -K_EFAULT, "");
    // 2. PROT_NONE segment: same.
    setIov(0, nonePage, 16);
    buildHdr(iovAt, 1);
    sendCase("sendmsg from a PROT_NONE segment", -K_EFAULT, "");
    // 3. Control: a readable segment lands its real bytes.
    setIov(0, textAt, 16);
    buildHdr(iovAt, 1);
    sendCase("control sendmsg", 16, std::string(text, 16));
    // 4. A readable prefix running into an unmapped page: the host
    // faults the whole send (-EFAULT, nothing arrives) -- there is no
    // short-count prefix shape on the message paths.
    setIov(0, crossPage + 4000, 200);
    buildHdr(iovAt, 1);
    sendCase("crossing sendmsg", -K_EFAULT, "");
    // 5. Good segment then a bad one: -EFAULT, nothing arrives.
    setIov(0, textAt, 8);
    setIov(1, unmapped, 16);
    buildHdr(iovAt, 2);
    sendCase("sendmsg good+bad", -K_EFAULT, "");
    // 6. Bad segment first: same.
    setIov(0, unmapped, 16);
    setIov(1, textAt, 8);
    buildHdr(iovAt, 2);
    sendCase("sendmsg bad+good", -K_EFAULT, "");
    // 7. A zero-length segment is skipped even with an unreadable base.
    setIov(0, unmapped, 0);
    setIov(1, textAt, 8);
    buildHdr(iovAt, 2);
    sendCase("sendmsg zerolen-bad+good", 8, std::string(text, 8));
    // 8. A PROT_WRITE-only segment is readable to the copy and lands
    // its real bytes.
    setIov(0, wonlyPage, 16);
    buildHdr(iovAt, 1);
    sendCase("sendmsg from a write-only page", 16, std::string(wonlyText, 16));
    // 9. The iov array itself unreadable: -EFAULT.
    buildHdr(unmapped, 1);
    sendCase("sendmsg with an unmapped iov array", -K_EFAULT, "");
    // 10. The msghdr itself unreadable: -EFAULT.
    {
        Pair p;
        if (makePair(1, p)) {
            std::string got;
            S64 r = invoke(46, (U64)(U32)p.tx, unmapped, 0, 0);
            if (r != -K_EFAULT || drain(p.rx, got) != 0) {
                testFail("sendmsg with an unmapped msghdr returned %lld", (long long)r);
            }
            closePair(p);
        }
    }
    // 11. iovlen over UIO_MAXIOV answers -EMSGSIZE even when every
    // segment is zero-length (host-measured).
    for (U32 i = 0; i < 1025; i++) {
        memory->writeq(bigArray + i * 16, textAt);
        memory->writeq(bigArray + i * 16 + 8, 0);
    }
    buildHdr(bigArray, 1025);
    sendCase("sendmsg with iovlen 1025", -K_EMSGSIZE, "");
    // 12. msg_name unreadable (connected pair: the host imports the
    // name before the data and faults): -EFAULT, nothing arrives.
    setIov(0, textAt, 8);
    buildHdr(iovAt, 1);
    memory->writeq(hdrAt + 0, unmapped); // msg_name
    memory->writed(hdrAt + 8, 16);       // msg_namelen
    sendCase("sendmsg with an unmapped msg_name", -K_EFAULT, "");
    // 13. msg_control unreadable: -EFAULT, nothing arrives.
    setIov(0, textAt, 8);
    buildHdr(iovAt, 1);
    memory->writeq(hdrAt + 32, unmapped); // msg_control
    memory->writeq(hdrAt + 40, 16);       // msg_controllen
    sendCase("sendmsg with an unmapped msg_control", -K_EFAULT, "");
    // 14. SCM_RIGHTS whose fd array runs into an unmapped page: the
    // header sits at the tail of the crossing page (fds at +16: the
    // first is the last readable word, the second is unmapped). The
    // fd value is read back after the pair exists, so this case
    // builds its header inline.
    {
        Pair p;
        if (makePair(1, p)) {
            U64 ctl = crossPage + 4076;
            memory->writeq(ctl + 0, 24);     // cmsg_len (header + 2 fds)
            memory->writed(ctl + 8, 1);      // SOL_SOCKET
            memory->writed(ctl + 12, 1);     // SCM_RIGHTS
            memory->writed(ctl + 16, (U32)p.tx);
            setIov(0, textAt, 8);
            buildHdr(iovAt, 1);
            memory->writeq(hdrAt + 32, ctl);
            memory->writeq(hdrAt + 40, 24);
            std::string got;
            S64 r = invoke(46, (U64)(U32)p.tx, hdrAt, 0, 0);
            if (r != -K_EFAULT || drain(p.rx, got) != 0) {
                testFail("sendmsg with a crossing SCM_RIGHTS array returned %lld, peer holds %u bytes",
                         (long long)r, (unsigned)got.size());
            }
            closePair(p);
        }
    }
    // 15. Ordering guard (already holds on the base and must not
    // move): a bad fd answers -EBADF ahead of any copy fault.
    setIov(0, unmapped, 16);
    buildHdr(iovAt, 1);
    if (invoke(46, 9999, hdrAt, 0, 0) != -K_EBADF) {
        testFail("sendmsg with a bad fd and a bad segment was not -EBADF");
    }
    // 16. sendmmsg: a good message before the bad one is delivered and
    // counted, with its msg_len written back; the bad one sends
    // nothing. Both messages share one pair.
    {
        Pair p;
        if (makePair(1, p)) {
            constexpr U64 vecAt = dataPage + 0x2400;
            for (U32 i = 0; i < 128; i += 8) memory->writeq(vecAt + i, 0);
            setIov(0, textAt, 8);
            setIov(1, unmapped, 16);
            memory->writeq(vecAt + 16, iovAt);      // msg0 iov
            memory->writeq(vecAt + 24, 1);
            memory->writeq(vecAt + 64 + 16, iovAt + 16); // msg1 iov
            memory->writeq(vecAt + 64 + 24, 1);
            std::string got;
            S64 r = invoke(307, (U64)(U32)p.tx, vecAt, 2, 0);
            if (r != 1 || memory->readd(vecAt + 56) != 8 ||
                drain(p.rx, got) != 8 || got != std::string(text, 8)) {
                testFail("sendmmsg good,bad returned %lld (msg_len %u, peer holds %u bytes)",
                         (long long)r, (unsigned)memory->readd(vecAt + 56), (unsigned)got.size());
            }
            // 17. sendmmsg with the bad message first: -EFAULT,
            // nothing.
            memory->writeq(vecAt + 16, iovAt + 16);
            memory->writeq(vecAt + 64 + 16, iovAt);
            r = invoke(307, (U64)(U32)p.tx, vecAt, 2, 0);
            if (r != -K_EFAULT || drain(p.rx, got) != 0) {
                testFail("sendmmsg bad,good returned %lld, peer holds %u bytes",
                         (long long)r, (unsigned)got.size());
            }
            closePair(p);
        }
    }
    // 18. Datagram pair: the same all-or-nothing shape, and a good
    // datagram arrives whole.
    {
        Pair p;
        if (makePair(2 /*SOCK_DGRAM*/, p)) {
            setIov(0, textAt, 8);
            setIov(1, unmapped, 16);
            buildHdr(iovAt, 2);
            std::string got;
            S64 r = invoke(46, (U64)(U32)p.tx, hdrAt, 0, 0);
            if (r != -K_EFAULT || drain(p.rx, got) != 0) {
                testFail("dgram sendmsg good+bad returned %lld, peer holds %u bytes",
                         (long long)r, (unsigned)got.size());
            }
            closePair(p);
        }
        Pair q;
        if (makePair(2, q)) {
            setIov(0, textAt, 16);
            buildHdr(iovAt, 1);
            std::string got;
            S64 r = invoke(46, (U64)(U32)q.tx, hdrAt, 0, 0);
            if (r != 16 || drain(q.rx, got) != 16 || got != std::string(text, 16)) {
                testFail("control dgram sendmsg returned %lld, peer holds %u bytes",
                         (long long)r, (unsigned)got.size());
            }
            closePair(q);
        }
    }
    // 19. A non-socket descriptor answers -ENOTSOCK with a bad segment
    // (guard: holds on the base, must not move to -EFAULT).
    {
        char dirTemplate[] = "/tmp/k3bwmsgXXXXXX";
        char* hostDir = mkdtemp(dirTemplate);
        if (!hostDir) {
            testFail("sendmsg-bad-buffer test could not create its host temp dir");
            return;
        }
        std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
        struct RootRestore {
            std::shared_ptr<FsFileNode> previous;
            ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
        } rootRestore{previousRoot};
        Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(hostDir), true, true, nullptr);
        Fs::clearPathCache();
        KFileDescriptorPtr fileDesc;
        U32 result = process->openFile(B("/"), B("/data.bin"), K_O_RDWR | K_O_CREAT, fileDesc);
        if ((S32)result < 0 || !fileDesc) {
            testFail("sendmsg-bad-buffer test could not create /data.bin");
            return;
        }
        setIov(0, unmapped, 16);
        buildHdr(iovAt, 1);
        if (invoke(46, (U64)(U32)fileDesc->handle, hdrAt, 0, 0) != -K_ENOTSOCK) {
            testFail("sendmsg on a file descriptor with a bad segment was not -ENOTSOCK");
        }
        process->close((S32)fileDesc->handle);
        rmdir(hostDir);
    }
}


// FD-97: the 64-bit single-call write paths (write/pwrite64, and writev
// through them) must not fabricate zero bytes for an unreadable guest
// buffer. Host-kernel reference behaviour (measured on the box's 6.17
// kernel with a dedicated reference program,
// test-results/fd97/hostref-fd97): a fully unreadable buffer answers
// -EFAULT and writes nothing; a buffer whose readable prefix runs into
// an unreadable page writes the prefix as a short count; count 0 with a
// bad buffer returns 0; a bad fd with a bad buffer answers -EBADF (the
// descriptor answer precedes the copy fault); a PROT_WRITE-only buffer
// is readable to the copy (x86 write-implies-read) and lands its real
// bytes. On the base the calls below land zero-filled bytes and report
// success.
void testWriteBadBuffer64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 dataPage = 0x7B000000ULL;
    constexpr U64 crossPage = 0x7C000000ULL;
    constexpr U64 nonePage = 0x7D000000ULL;
    constexpr U64 wonlyPage = 0x7E000000ULL;
    constexpr U64 unmapped = 0x6000000000ULL;
    auto mapFixture = [&](U64 addr, U32 prot) {
        if (!memory->getPageFlags(addr >> K64_PAGE_SHIFT) &&
            memory->mmapAnonymousFixed(addr, K64_PAGE_SIZE, prot) != addr) {
            testFail("write-bad-buffer test could not map its fixture page at 0x%llx", (unsigned long long)addr);
            return false;
        }
        return true;
    };
    if (!mapFixture(dataPage, 3) || !mapFixture(crossPage, 3) ||
        !mapFixture(nonePage, 0) || !mapFixture(wonlyPage, 2)) return;
    if (memory->getPageFlags(unmapped >> K64_PAGE_SHIFT) != 0 ||
        memory->getPageFlags((crossPage + K64_PAGE_SIZE) >> K64_PAGE_SHIFT) != 0) {
        testFail("write-bad-buffer test fixture addresses are unexpectedly mapped");
        return;
    }
    if (!(memory->getPageFlags(nonePage >> K64_PAGE_SHIFT) & K64_PAGE_MAPPED) ||
        (memory->getPageFlags(nonePage >> K64_PAGE_SHIFT) & K64_PAGE_READ)) {
        testFail("write-bad-buffer test PROT_NONE fixture has the wrong flags");
        return;
    }
    if (!(memory->getPageFlags(wonlyPage >> K64_PAGE_SHIFT) & K64_PAGE_READ)) {
        testFail("write-bad-buffer test write-only fixture lacks the x86 write-implies-read flag");
        return;
    }

    char dirTemplate[] = "/tmp/k3bwbadbufXXXXXX";
    char* hostDir = mkdtemp(dirTemplate);
    if (!hostDir) {
        testFail("write-bad-buffer test could not create its host temp dir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(hostDir), true, true, nullptr);
    Fs::clearPathCache();

    KFileDescriptorPtr fileDesc;
    {
        U32 result = process->openFile(B("/"), B("/data.bin"), K_O_RDWR | K_O_CREAT, fileDesc);
        if ((S32)result < 0 || !fileDesc) {
            testFail("write-bad-buffer test could not create /data.bin");
            return;
        }
    }
    const S32 fd = (S32)fileDesc->handle;

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
    auto fileBytes = [&]() {
        std::string s;
        U8 buf[8192];
        fileDesc->kobject->seek(0);
        S32 n = (S32)fileDesc->kobject->readNative(buf, sizeof(buf));
        if (n > 0) s.assign((const char*)buf, (size_t)n);
        return s;
    };
    auto seekSet = [&](S64 pos) { invoke(8, (U64)(U32)fd, (U64)pos, 0, 0); };
    auto filePos = [&]() -> S64 { return invoke(8, (U64)(U32)fd, 0, 1, 0); };

    // Guest data: a 16-byte string on the data page, a full pattern page
    // for the crossing cases, and the write-only page's real bytes.
    const char* text = "0123456789ABCDEF";
    memory->memcpyToGuest(dataPage, text, 16);
    std::vector<U8> pattern(K64_PAGE_SIZE);
    for (U32 i = 0; i < K64_PAGE_SIZE; i++) pattern[i] = (U8)('a' + (i % 26));
    memory->memcpyToGuest(crossPage, pattern.data(), K64_PAGE_SIZE);
    const char* wonlyText = "FEDCBA9876543210";
    for (U32 i = 0; i < 16; i++) memory->writeb(wonlyPage + i, (U8)wonlyText[i]);
    const std::string crossTail((const char*)pattern.data() + 4000, 96);

    // 1. write from a fully unmapped buffer: -EFAULT, nothing lands.
    S64 r = invoke(1, (U64)(U32)fd, unmapped, 16, 0);
    if (r != -K_EFAULT || !fileBytes().empty()) {
        testFail("write from an unmapped buffer returned %lld, file holds %u bytes",
                 (long long)r, (unsigned)fileBytes().size());
    }
    // 2. write from a mapped but unreadable (PROT_NONE) page: same.
    r = invoke(1, (U64)(U32)fd, nonePage, 16, 0);
    if (r != -K_EFAULT || !fileBytes().empty()) {
        testFail("write from a PROT_NONE page returned %lld, file holds %u bytes",
                 (long long)r, (unsigned)fileBytes().size());
    }
    // 3. Control: a readable buffer lands its real bytes.
    r = invoke(1, (U64)(U32)fd, dataPage, 16, 0);
    if (r != 16 || fileBytes() != std::string(text, 16)) {
        testFail("control write returned %lld, file size %u", (long long)r, (unsigned)fileBytes().size());
    }
    // 4. A readable prefix running into an unmapped page writes the
    // prefix as a short count (96 of 200), not zeros or an error.
    seekSet(16);
    r = invoke(1, (U64)(U32)fd, crossPage + 4000, 200, 0);
    if (r != 96 || fileBytes() != std::string(text, 16) + crossTail) {
        testFail("crossing write returned %lld (file size %u)", (long long)r, (unsigned)fileBytes().size());
    }
    // 5. One readable byte before the boundary: count 1.
    r = invoke(1, (U64)(U32)fd, crossPage + 4095, 2, 0);
    if (r != 1 || fileBytes().size() != 113 || fileBytes()[112] != (char)pattern[4095]) {
        testFail("last-byte crossing write returned %lld (file size %u)", (long long)r, (unsigned)fileBytes().size());
    }
    // 6. pwrite from a fully unmapped buffer: -EFAULT, file and position
    // untouched.
    seekSet(113);
    r = invoke(18, (U64)(U32)fd, unmapped, 16, 0);
    if (r != -K_EFAULT || fileBytes().size() != 113 || fileBytes().substr(0, 16) != std::string(text, 16) || filePos() != 113) {
        testFail("pwrite from an unmapped buffer returned %lld (file size %u, pos %lld)",
                 (long long)r, (unsigned)fileBytes().size(), (long long)filePos());
    }
    // 7. pwrite with a readable prefix at offset 8: short count 96, the
    // prefix lands at the offset, the position does not move.
    r = invoke(18, (U64)(U32)fd, crossPage + 4000, 200, 8);
    if (r != 96 || fileBytes().substr(8, 96) != crossTail || fileBytes().size() != 113 || filePos() != 113) {
        testFail("crossing pwrite returned %lld (file size %u, pos %lld)",
                 (long long)r, (unsigned)fileBytes().size(), (long long)filePos());
    }
    // 8. Count 0 with a bad buffer is 0, not a fault (host-measured).
    if (invoke(1, (U64)(U32)fd, unmapped, 0, 0) != 0 || invoke(18, (U64)(U32)fd, unmapped, 0, 0) != 0) {
        testFail("zero-count write/pwrite from an unmapped buffer was not 0");
    }
    // 9. A bad fd with a bad buffer answers -EBADF: the descriptor
    // answer precedes the copy fault, as on the host.
    if (invoke(1, 9999, unmapped, 16, 0) != -9) {
        testFail("write with a bad fd and a bad buffer was not -EBADF");
    }
    // 10. writev inherits the shape through sys_write64: a bad first
    // segment answers -EFAULT and writes nothing.
    constexpr U64 iovAt = dataPage + 0x800;
    memory->writeq(iovAt, unmapped);
    memory->writeq(iovAt + 8, 16);
    seekSet(113);
    r = invoke(20, (U64)(U32)fd, iovAt, 1, 0);
    if (r != -K_EFAULT || fileBytes().size() != 113) {
        testFail("writev with an unmapped first segment returned %lld (file size %u)",
                 (long long)r, (unsigned)fileBytes().size());
    }
    // 11. writev with a bad later segment returns the bytes already
    // written.
    memory->writeq(iovAt, dataPage);
    memory->writeq(iovAt + 8, 8);
    memory->writeq(iovAt + 16, unmapped);
    memory->writeq(iovAt + 24, 16);
    r = invoke(20, (U64)(U32)fd, iovAt, 2, 0);
    if (r != 8 || fileBytes().size() != 121 || fileBytes().substr(113, 8) != "01234567") {
        testFail("writev with an unmapped second segment returned %lld (file size %u)",
                 (long long)r, (unsigned)fileBytes().size());
    }
    // 12. A PROT_WRITE-only buffer is readable to the copy under the
    // x86 write-implies-read rule and lands its real bytes.
    seekSet(121);
    r = invoke(1, (U64)(U32)fd, wonlyPage, 16, 0);
    if (r != 16 || fileBytes().substr(121, 16) != std::string(wonlyText, 16)) {
        testFail("write from a write-only page returned %lld", (long long)r);
    }

    process->close(fd);
    rmdir(hostDir);
}


// FD-93: the 64-bit write family's wrong-mode answers. Host-kernel
// reference behaviour (measured on the box's 6.17 kernel with
// dedicated reference programs, preserved under
// test-results/fd92-93/; extends lane-fd83-84's hostref-write.c
// matrix with the validation orderings):
//  - write, pwrite64 and writev on a descriptor opened read-only
//    answer EBADF -- for count 0 and iovcnt 0 as well; the
//    descriptor check also precedes write's zero-count return.
//  - pwrite64's negative offset answers EINVAL ahead of both the
//    descriptor and mode answers (measured on a bad descriptor
//    too); the mode answer precedes writev's iovcnt range check
//    (EINVAL) and its null-iovec check (EFAULT).
//  - a successful pwrite64 does not move the file position.
void testWriteFamily64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x7B000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE * 8, 3) != page) {
        testFail("write-family test could not map its fixture pages");
        return;
    }
    char dirTemplate[] = "/tmp/k3bwritefamXXXXXX";
    char* hostDir = mkdtemp(dirTemplate);
    if (!hostDir) {
        testFail("write-family test could not create its host temp dir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(hostDir), true, true, nullptr);
    Fs::clearPathCache();

    KFileDescriptorPtr rdwrDesc;
    {
        U32 result = process->openFile(B("/"), B("/data.bin"), K_O_RDWR | K_O_CREAT, rdwrDesc);
        if ((S32)result < 0 || !rdwrDesc) {
            testFail("write-family test could not create /data.bin");
            return;
        }
        const char* text = "0123456789ABCDEFGHIJ";
        rdwrDesc->kobject->writeNative((U8*)text, 20);
        rdwrDesc->kobject->seek(0);
    }
    KFileDescriptorPtr rdonlyDesc;
    {
        U32 result = process->openFile(B("/"), B("/data.bin"), K_O_RDONLY, rdonlyDesc);
        if ((S32)result < 0 || !rdonlyDesc) {
            testFail("write-family test could not open /data.bin read-only");
            return;
        }
    }
    const S32 wfd = (S32)rdwrDesc->handle;
    const S32 rfd = (S32)rdonlyDesc->handle;

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
    constexpr U64 bufAt = page + 0x100;
    constexpr U64 iovAt = page + 0x200;
    constexpr U64 bigIovAt = page + 0x1000;
    memory->memcpyToGuest(bufAt, "xyz", 3);
    auto setIov = [&](U64 at, U32 idx, U64 base, U64 len) {
        memory->writeq(at + (U64)idx * 16 + 0, base);
        memory->writeq(at + (U64)idx * 16 + 8, len);
    };
    setIov(iovAt, 0, bufAt, 3);
    setIov(iovAt, 1, bufAt, 3);
    {
        std::vector<U8> zeros(16400, 0);
        memory->memcpyToGuest(bigIovAt, zeros.data(), (U32)zeros.size());
    }

    // write (1): the mode answer, and the descriptor check ahead
    // of the zero-count return.
    if (invoke(1, (U64)(U32)rfd, bufAt, 3, 0) != -K_EBADF) {
        testFail("write on a read-only descriptor was not -EBADF");
    }
    if (invoke(1, (U64)(U32)rfd, bufAt, 0, 0) != -K_EBADF) {
        testFail("zero-count write on a read-only descriptor was not -EBADF");
    }
    if (invoke(1, 9999, bufAt, 0, 0) != -K_EBADF) {
        testFail("zero-count write on a bad descriptor was not -EBADF");
    }
    // writev (20): mode ahead of the walk, the range check and
    // the iovec pointer check.
    if (invoke(20, (U64)(U32)rfd, iovAt, 1, 0) != -K_EBADF) {
        testFail("writev on a read-only descriptor was not -EBADF");
    }
    if (invoke(20, (U64)(U32)rfd, iovAt, 0, 0) != -K_EBADF) {
        testFail("writev with iovcnt 0 on a read-only descriptor was not -EBADF");
    }
    if (invoke(20, (U64)(U32)wfd, bigIovAt, 1025, 0) != -K_EINVAL) {
        testFail("writev with iovcnt IOV_MAX+1 was not -EINVAL");
    }
    // pwrite64 (18): negative offset EINVAL ahead of the
    // descriptor and mode answers; mode EBADF otherwise.
    if (invoke(18, (U64)(U32)rfd, bufAt, 3, 0) != -K_EBADF) {
        testFail("pwrite64 on a read-only descriptor was not -EBADF");
    }
    if (invoke(18, (U64)(U32)rfd, bufAt, 0, 0) != -K_EBADF) {
        testFail("zero-count pwrite64 on a read-only descriptor was not -EBADF");
    }
    if (invoke(18, (U64)(U32)rfd, bufAt, 3, (U64)-1) != -K_EINVAL) {
        testFail("pwrite64 with offset -1 on a read-only descriptor was not -EINVAL");
    }
    if (invoke(18, 9999, bufAt, 3, (U64)-1) != -K_EINVAL) {
        testFail("pwrite64 with offset -1 on a bad descriptor was not -EINVAL");
    }
    if (invoke(18, (U64)(U32)wfd, bufAt, 3, (U64)-1) != -K_EINVAL) {
        testFail("pwrite64 with offset -1 on a writable descriptor was not -EINVAL");
    }
    if (invoke(20, (U64)(U32)wfd, 0, 1, 0) != -K_EFAULT) {
        testFail("writev with a null iovec and iovcnt 1 was not -EFAULT");
    }
    // Controls: the writable descriptor still writes, and a
    // positioned write leaves the file position alone.
    if (invoke(1, (U64)(U32)wfd, bufAt, 3, 0) != 3) {
        testFail("write on a writable descriptor did not return 3");
    }
    if (invoke(1, (U64)(U32)wfd, bufAt, 0, 0) != 0) {
        testFail("zero-count write on a writable descriptor did not return 0");
    }
    if (invoke(20, (U64)(U32)wfd, iovAt, 2, 0) != 6) {
        testFail("two-segment writev on a writable descriptor did not return 6");
    }
    {
        S64 before = rdwrDesc->kobject->getPos();
        if (invoke(18, (U64)(U32)wfd, bufAt, 3, 5) != 3) {
            testFail("pwrite64 on a writable descriptor did not return 3");
        }
        if (rdwrDesc->kobject->getPos() != before) {
            testFail("pwrite64 moved the file position");
        }
    }

    process->close(rfd);
    process->close(wfd);
    rmdir(hostDir);
}


// FD-94: x64 pwritev (296) / pwritev2 (328). On the base both numbers
// are in the name table only -- no X64_SYS define, no dispatch case --
// so every call answers ENOSYS. Host-kernel reference behaviour
// (measured on the box's 6.17 kernel with dedicated reference
// programs, test-results/fd94-96/hostref-pwritev*.c):
//  - segments are written in order at successive offsets; the file
//    position does not move; a zero-length segment is skipped; a
//    short write ends the walk.
//  - validation order: a negative offset is EINVAL ahead of the
//    descriptor check (EBADF); the descriptor check is ahead of the
//    pwritev2 flags check (EOPNOTSUPP for bits outside the defined
//    RWF mask) and of the iovcnt range check (EINVAL outside
//    [0, IOV_MAX]); a bad iovec array with iovcnt > 0 is EFAULT.
//  - a later segment's failure returns the bytes already written
//    (the writev partial shape); a failing first segment returns
//    the error itself.
//  - pwritev2 offset -1 is the file-position mode (write at the
//    current position and advance it); offset below -1 is EINVAL.
//    RWF_APPEND writes at end of file ignoring the offset and,
//    outside the -1 mode, leaves the position untouched.
//  - a successful multi-segment call produces one watcher-visible
//    IN_MODIFY (the queue coalesces the per-segment events, as on
//    Linux); failed calls produce none.
void testPwritev64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;
    constexpr U64 page = 0x7A000000ULL;
    if (!memory->getPageFlags(page >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
        testFail("pwritev test could not map its fixture page");
        return;
    }
    char dirTemplate[] = "/tmp/k3bpwritevXXXXXX";
    char* hostDir = mkdtemp(dirTemplate);
    if (!hostDir) {
        testFail("pwritev test could not create its host temp dir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(hostDir), true, true, nullptr);
    Fs::clearPathCache();

    KFileDescriptorPtr fileDesc;
    {
        U32 result = process->openFile(B("/"), B("/data.bin"), K_O_RDWR | K_O_CREAT, fileDesc);
        if ((S32)result < 0 || !fileDesc) {
            testFail("pwritev test could not create /data.bin");
            return;
        }
        const char* text = "0123456789ABCDEFGHIJ";
        fileDesc->kobject->writeNative((U8*)text, 20);
        fileDesc->kobject->seek(0);
    }
    S32 fd = (S32)fileDesc->handle;
    const std::string hostPath = std::string(hostDir) + "/data.bin";
    auto hostContent = [&]() {
        std::string out;
        FILE* f = fopen(hostPath.c_str(), "rb");
        if (!f) return out;
        char chunk[4096];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) out.append(chunk, n);
        fclose(f);
        return out;
    };

    std::shared_ptr<KInotify> inotify = std::make_shared<KInotify>(false);
    KInotify::registerInstance(inotify);
    KFileDescriptorPtr inotifyDesc = process->allocFileDescriptor(inotify, K_O_RDONLY, 0, -1, 0);
    if (!inotifyDesc || !inotifyDesc->kobject) {
        testFail("pwritev test could not create its inotify instance");
        return;
    }
    S32 wd = (S32)process->inotify_add_watch((S32)inotifyDesc->handle, B("/data.bin"), 0xfff);
    if (wd < 1) {
        testFail("pwritev test could not watch /data.bin");
        return;
    }
    auto drainMasks = [&]() {
        std::vector<U32> masks;
        U8 buffer[8192];
        S32 total = (S32)inotifyDesc->kobject->readNative(buffer, sizeof(buffer));
        S32 offset = 0;
        while (total > 0 && offset + 16 <= total) {
            U32 mask, nameLen;
            memcpy(&mask, buffer + offset + 4, 4);
            memcpy(&nameLen, buffer + offset + 12, 4);
            masks.push_back(mask);
            offset += 16 + (S32)nameLen;
        }
        return masks;
    };
    auto modifyCount = [&](const std::vector<U32>& masks) {
        int n = 0;
        for (U32 m : masks) if (m & IN_MODIFY) n++;
        return n;
    };
    drainMasks(); // setup noise

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    constexpr U64 iovAt = page + 0x100;
    constexpr U64 seg0 = page + 0x400;
    constexpr U64 seg1 = page + 0x500;
    constexpr U64 seg2 = page + 0x600;
    auto setIov = [&](U32 idx, U64 base, U64 len) {
        memory->writeq(iovAt + idx * 16, base);
        memory->writeq(iovAt + idx * 16 + 8, len);
    };
    auto stageText = [&](U64 at, const char* text, U32 len) {
        memory->memcpyToGuest(at, text, len);
    };
    auto segText = [&](U64 at, U32 len) {
        std::string out;
        for (U32 i = 0; i < len; i++) out += (char)memory->readb(at + i);
        return out;
    };
    auto filePos = [&]() -> S64 { return invoke6(8, (U64)(U32)fd, 0, 1, 0, 0, 0); }; // lseek(fd, 0, SEEK_CUR)
    auto setPos = [&](S64 pos) -> S64 { return invoke6(8, (U64)(U32)fd, (U64)pos, 0, 0, 0, 0); }; // SEEK_SET

    // 1. Three segments at offset 2; position untouched; one IN_MODIFY.
    stageText(seg0, "abcd", 4); stageText(seg1, "efgh", 4); stageText(seg2, "ijkl", 4);
    setIov(0, seg0, 4); setIov(1, seg1, 4); setIov(2, seg2, 4);
    S64 r = invoke6(296, (U64)(U32)fd, iovAt, 3, 2, 0, 0);
    if (r != 12 || hostContent() != "01abcdefghijklEFGHIJ") {
        testFail("pwritev 3 segments at offset 2 returned %lld with content '%s'",
                 (long long)r, hostContent().c_str());
    }
    if (filePos() != 0) {
        testFail("pwritev moved the file position to %lld", (long long)filePos());
    }
    if (modifyCount(drainMasks()) != 1) {
        testFail("3-segment pwritev must generate exactly one watcher-visible IN_MODIFY");
    }

    // 2. A zero-length segment in the middle is skipped.
    stageText(seg0, "wxyz", 4); stageText(seg2, "WXYZ", 4);
    setIov(0, seg0, 4); setIov(1, seg1, 0); setIov(2, seg2, 4);
    r = invoke6(296, (U64)(U32)fd, iovAt, 3, 14, 0, 0);
    if (r != 8 || hostContent() != "01abcdefghijklwxyzWXYZ") {
        testFail("pwritev with a zero-length segment returned %lld with content '%s'",
                 (long long)r, hostContent().c_str());
    }
    drainMasks();

    // 3. Partial shape: a good first segment and a bad second segment
    // return the bytes already written, and only those bytes land.
    stageText(seg0, "xyz", 3);
    setIov(0, seg0, 3); setIov(1, 0x1000, 3); // 0x1000: unmapped guest page
    r = invoke6(296, (U64)(U32)fd, iovAt, 2, 0, 0, 0);
    if (r != 3 || hostContent().substr(0, 3) != "xyz") {
        testFail("pwritev with a bad second segment returned %lld with content prefix '%s'",
                 (long long)r, hostContent().substr(0, 3).c_str());
    }
    if (modifyCount(drainMasks()) != 1) {
        testFail("partial pwritev must generate exactly one IN_MODIFY");
    }

    // 4. A bad first segment returns the error itself.
    setIov(0, 0x1000, 3); setIov(1, seg0, 3);
    if (invoke6(296, (U64)(U32)fd, iovAt, 2, 0, 0, 0) != -K_EFAULT) {
        testFail("pwritev with a bad first segment was not -EFAULT");
    }
    if (modifyCount(drainMasks()) != 0) {
        testFail("failed pwritev generated IN_MODIFY");
    }

    // 5. Validation, in the measured order; none of it writes or
    // generates IN_MODIFY.
    if (invoke6(296, 9999, iovAt, 1, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0) != -K_EINVAL) {
        testFail("pwritev with a bad descriptor and offset -1 was not -EINVAL (offset precedes descriptor)");
    }
    if (invoke6(296, (U64)(U32)fd, iovAt, 1, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0) != -K_EINVAL) {
        testFail("pwritev with offset -1 was not -EINVAL");
    }
    if (invoke6(296, 9999, iovAt, 1, 0, 0, 0) != -9) {
        testFail("pwritev with a bad descriptor was not -EBADF");
    }
    if (invoke6(296, 9999, iovAt, 0, 0, 0, 0) != -9) {
        testFail("pwritev with a bad descriptor and iovcnt 0 was not -EBADF");
    }
    if (invoke6(296, (U64)(U32)fd, iovAt, 0, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0) != -K_EINVAL) {
        testFail("pwritev with iovcnt 0 and offset -1 was not -EINVAL");
    }
    if (invoke6(296, (U64)(U32)fd, iovAt, 0, 0, 0, 0) != 0) {
        testFail("pwritev with iovcnt 0 did not return 0");
    }
    if (invoke6(296, (U64)(U32)fd, iovAt, (U64)-1, 0, 0, 0) != -K_EINVAL) {
        testFail("pwritev with iovcnt -1 was not -EINVAL");
    }
    if (invoke6(296, (U64)(U32)fd, iovAt, 1025, 0, 0, 0) != -K_EINVAL) {
        testFail("pwritev with iovcnt IOV_MAX+1 was not -EINVAL");
    }
    if (invoke6(296, (U64)(U32)fd, 0, 1, 0, 0, 0) != -K_EFAULT) {
        testFail("pwritev with a null iovec and iovcnt 1 was not -EFAULT");
    }
    if (invoke6(328, (U64)(U32)fd, iovAt, 1, 0, 0, 0x40) != -K_EOPNOTSUPP) {
        testFail("pwritev2 with an undefined flag bit was not -EOPNOTSUPP");
    }
    if (invoke6(328, 9999, iovAt, 1, 0, 0, 0x40) != -9) {
        testFail("pwritev2 with a bad descriptor and a bad flag was not -EBADF (descriptor precedes flags)");
    }
    if (invoke6(328, 9999, iovAt, 1, 0xFFFFFFFEULL, 0xFFFFFFFFULL, 0) != -K_EINVAL) {
        testFail("pwritev2 with a bad descriptor and offset -2 was not -EINVAL (offset precedes descriptor)");
    }
    if (invoke6(328, (U64)(U32)fd, iovAt, 1, 0xFFFFFFFEULL, 0xFFFFFFFFULL, 0) != -K_EINVAL) {
        testFail("pwritev2 with offset -2 was not -EINVAL");
    }
    if (invoke6(328, (U64)(U32)fd, iovAt, (U64)-1, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0) != -K_EINVAL) {
        testFail("pwritev2 in position mode with iovcnt -1 was not -EINVAL");
    }
    if (modifyCount(drainMasks()) != 0) {
        testFail("rejected pwritev/pwritev2 calls generated IN_MODIFY");
    }

    // 6. pwritev2 offset -1 is the file-position mode: writes at the
    // current position and advances it.
    if (setPos(5) != 5) {
        testFail("pwritev test could not seek its fixture file");
        return;
    }
    stageText(seg0, "PQ", 2);
    setIov(0, seg0, 2);
    r = invoke6(328, (U64)(U32)fd, iovAt, 1, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0);
    if (r != 2 || hostContent().substr(5, 2) != "PQ") {
        testFail("pwritev2 offset -1 returned %lld with content '%s'",
                 (long long)r, hostContent().substr(5, 2).c_str());
    }
    if (filePos() != 7) {
        testFail("pwritev2 offset -1 left the position at %lld, expected 7", (long long)filePos());
    }
    drainMasks();

    // 7. RWF_APPEND ignores the offset, writes at end of file, and
    // leaves the position untouched outside the -1 mode.
    stageText(seg0, "ZZ", 2);
    setIov(0, seg0, 2);
    r = invoke6(328, (U64)(U32)fd, iovAt, 1, 2, 0, 0x10);
    if (r != 2 || hostContent().size() != 24 || hostContent().substr(22, 2) != "ZZ") {
        testFail("pwritev2 RWF_APPEND returned %lld with content '%s'",
                 (long long)r, hostContent().c_str());
    }
    if (filePos() != 7) {
        testFail("pwritev2 RWF_APPEND moved the position to %lld", (long long)filePos());
    }
    drainMasks();

    // 8. RWF_APPEND with offset -1 appends and the position follows
    // the written bytes to the new end of file.
    stageText(seg0, "Q", 1);
    setIov(0, seg0, 1);
    r = invoke6(328, (U64)(U32)fd, iovAt, 1, 0xFFFFFFFFULL, 0xFFFFFFFFULL, 0x10);
    if (r != 1 || hostContent().size() != 25 || hostContent().substr(24, 1) != "Q") {
        testFail("pwritev2 RWF_APPEND offset -1 returned %lld with content '%s'",
                 (long long)r, hostContent().c_str());
    }
    if (filePos() != 25) {
        testFail("pwritev2 RWF_APPEND offset -1 left the position at %lld, expected 25", (long long)filePos());
    }
    drainMasks();

    // 9. A read-only descriptor answers -EBADF, the write family's
    // settled answer (FD-93). This case originally pinned the house
    // -EINVAL and deferred the divergence to FD-93; the batch-3
    // merge decision (FD-95) aligned the vectored wrapper with the
    // family contract -- the mode check sits with the descriptor
    // check, ahead of the iovcnt and iovec checks -- so the whole
    // positioned-write family answers EBADF here, exactly as the
    // host does and as the preadv test pins the read side.
    {
        KFileDescriptorPtr rdesc;
        U32 result = process->openFile(B("/"), B("/data.bin"), K_O_RDONLY, rdesc);
        if ((S32)result < 0 || !rdesc) {
            testFail("pwritev test could not open /data.bin read-only");
            return;
        }
        setIov(0, seg0, 2);
        if (invoke6(296, (U64)(U32)rdesc->handle, iovAt, 1, 0, 0, 0) != -K_EBADF) {
            testFail("pwritev on a read-only descriptor was not -EBADF");
        }
        process->close(rdesc->handle);
    }

    // 10. pos_h is honored: offset 2^32 lands past 4 GiB (sparse),
    // verified by the file length and a guest pread64 readback.
    {
        KFileDescriptorPtr desc2;
        U32 result = process->openFile(B("/"), B("/data2.bin"), K_O_RDWR | K_O_CREAT, desc2);
        if ((S32)result < 0 || !desc2) {
            testFail("pwritev test could not create /data2.bin");
            return;
        }
        stageText(seg0, "END!", 4);
        setIov(0, seg0, 4);
        r = invoke6(296, (U64)(U32)desc2->handle, iovAt, 1, 0, 1, 0);
        if (r != 4) {
            testFail("pwritev at offset 2^32 returned %lld (pos_h ignored?)", (long long)r);
        }
        if (desc2->kobject->length() != (S64)((1ULL << 32) + 4)) {
            testFail("pwritev at offset 2^32 left the file length at %lld",
                     (long long)desc2->kobject->length());
        }
        // Read the tail back through the guest pread64 dispatch
        // (pread64 takes its offset in one 64-bit register, unlike
        // the vectored calls' lo/hi split).
        memory->writeq(seg0, 0);
        S64 pr = invoke6(17, (U64)(U32)desc2->handle, seg0, 4, (1ULL << 32), 0, 0);
        if (pr != 4 || segText(seg0, 4) != "END!") {
            testFail("pread64 at offset 2^32 returned %lld with bytes '%s'",
                     (long long)pr, segText(seg0, 4).c_str());
        }
        process->close(desc2->handle);
    }

    // 11. FD-100: the pwritev2 flags check runs LAST -- behind the
    // iovcnt range check and the iovec array check. An undefined flag
    // bit combined with an out-of-range iovcnt answers EINVAL, and
    // combined with a null or unmapped iovec array answers EFAULT;
    // only a call that passes those checks reaches EOPNOTSUPP (the
    // section-5 case). None of these writes or generates IN_MODIFY.
    if (invoke6(328, (U64)(U32)fd, iovAt, (U64)-1, 0, 0, 0x40) != -K_EINVAL) {
        testFail("pwritev2 with iovcnt -1 and an undefined flag was not -EINVAL");
    }
    if (invoke6(328, (U64)(U32)fd, iovAt, 1025, 0, 0, 0x40) != -K_EINVAL) {
        testFail("pwritev2 with iovcnt IOV_MAX+1 and an undefined flag was not -EINVAL");
    }
    if (invoke6(328, (U64)(U32)fd, 0, 1, 0, 0, 0x40) != -K_EFAULT) {
        testFail("pwritev2 with a null iovec and an undefined flag was not -EFAULT");
    }
    if (invoke6(328, (U64)(U32)fd, 0x1000, 1, 0, 0, 0x40) != -K_EFAULT) {
        testFail("pwritev2 with an unmapped iovec and an undefined flag was not -EFAULT");
    }
    if (modifyCount(drainMasks()) != 0) {
        testFail("rejected pwritev2 flag-order calls generated IN_MODIFY");
    }

    process->close((S32)inotifyDesc->handle);
    process->close(fd);
    rmdir(hostDir);
}



// FD-102: the sendmsg SCM_RIGHTS translation must validate the
// guest-supplied cmsg_len against the header size and the declared
// controllen (and the host's 253-descriptor cap) instead of deriving
// an unbounded record count from it. Host contract measured in
// test-results/fd102-104/hostref-cmsglen-fd102.c (kernel
// 6.17.0-1020-oracle): lengths 0/8/12/15, a length past controllen,
// and 254 descriptors all answer -EINVAL with nothing sent; lengths
// 18/22 deliver (18-16)/4 = 0 and (22-16)/4 = 1 descriptors; 253
// descriptors are accepted; the cmsg answer precedes a segment
// fault. The large-count cases run before the small-length ones on
// purpose: on the unfixed code a length under 16 underflows the
// count to ~2^30 loop iterations, which is the defect itself.
void testSendmsgCmsgLen64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 dataPage = 0x66000000ULL;
    constexpr U64 ctrlPage = 0x67000000ULL;
    constexpr U64 unmapped = 0x6200000000ULL;
    auto mapFixture = [&](U64 addr, U64 size, U32 prot) {
        if (!memory->getPageFlags(addr >> K64_PAGE_SHIFT) &&
            memory->mmapAnonymousFixed(addr, size, prot) != addr) {
            testFail("cmsg-len test could not map its fixture page at 0x%llx", (unsigned long long)addr);
            return false;
        }
        return true;
    };
    if (!mapFixture(dataPage, 4 * K64_PAGE_SIZE, 3) ||
        !mapFixture(ctrlPage, 2 * K64_PAGE_SIZE, 3)) return;
    if (memory->getPageFlags(unmapped >> K64_PAGE_SHIFT) != 0) {
        testFail("cmsg-len test fixture address is unexpectedly mapped");
        return;
    }

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

    constexpr U64 textAt = dataPage + 0x000;
    constexpr U64 svAt   = dataPage + 0x100;
    constexpr U64 recvAt = dataPage + 0x200;
    constexpr U64 pollAt = dataPage + 0x300;
    constexpr U64 hdrAt  = dataPage + 0x1000;
    constexpr U64 iovAt  = dataPage + 0x1800;
    constexpr U64 riovAt = dataPage + 0x1C00;
    constexpr U64 rhdrAt = dataPage + 0x1C80;
    constexpr U64 rctlAt = dataPage + 0x2000;
    const char* text = "0123456789ABCDEF";
    memory->memcpyToGuest(textAt, text, 16);
    memory->writeq(riovAt, recvAt);
    memory->writeq(riovAt + 8, 512);

    struct Pair { S32 tx = 0, rx = 0; };
    auto makePair = [&](U32 type, Pair& p) {
        if (invoke(53, 1, type, 0, svAt) != 0) {
            testFail("cmsg-len test could not create a socketpair (type %u)", type);
            return false;
        }
        p.tx = (S32)memory->readd(svAt);
        p.rx = (S32)memory->readd(svAt + 4);
        return true;
    };
    auto closePair = [&](Pair& p) {
        process->close((S32)p.tx);
        process->close((S32)p.rx);
    };
    // Send header over one data segment (textAt, segLen) plus a
    // rights cmsg at ctrlPage whose array repeats fdValue.
    auto buildSend = [&](U64 segBase, U64 segLen, U64 clen, U64 room, U32 fdValue, U32 fdCount) {
        memory->writeq(iovAt, segBase);
        memory->writeq(iovAt + 8, segLen);
        for (U32 i = 0; i < 56; i += 8) memory->writeq(hdrAt + i, 0);
        memory->writeq(hdrAt + 16, iovAt);
        memory->writeq(hdrAt + 24, 1);
        memory->writeq(ctrlPage + 0, clen);
        memory->writed(ctrlPage + 8, 1);  // SOL_SOCKET
        memory->writed(ctrlPage + 12, 1); // SCM_RIGHTS
        for (U32 i = 0; i < fdCount; i++) memory->writed(ctrlPage + 16 + 4 * i, fdValue);
        memory->writeq(hdrAt + 32, ctrlPage);
        memory->writeq(hdrAt + 40, room);
    };
    struct RecvRes {
        S64 rc = -1;
        std::string data;
        std::vector<U32> fds;
        U32 msgFlags = 0;
    };
    // One poll-gated recvmsg with a control area; -1 rc means the
    // peer held nothing at all.
    auto recvPeer = [&](S32 rx) {
        RecvRes res;
        memory->writed(pollAt, (U32)rx);
        memory->writew(pollAt + 4, 1); // POLLIN
        memory->writew(pollAt + 6, 0);
        if (invoke(7, pollAt, 1, 0, 0) <= 0 || !(memory->readw(pollAt + 6) & 1)) return res;
        for (U32 i = 0; i < 56; i += 8) memory->writeq(rhdrAt + i, 0);
        memory->writeq(rhdrAt + 16, riovAt);
        memory->writeq(rhdrAt + 24, 1);
        memory->writeq(rhdrAt + 32, rctlAt);
        memory->writeq(rhdrAt + 40, 2048);
        memory->writeq(rctlAt, 0);
        S64 rc = invoke(47, (U64)(U32)rx, rhdrAt, 0, 0);
        res.rc = rc;
        if (rc > 0) {
            std::vector<U8> buf((size_t)rc);
            memory->memcpyFromGuest(buf.data(), recvAt, (U64)rc);
            res.data.assign((const char*)buf.data(), (size_t)rc);
        }
        res.msgFlags = memory->readd(rhdrAt + 48);
        U64 ctlLen = memory->readq(rhdrAt + 40);
        if (ctlLen >= 16 && memory->readd(rctlAt + 8) == 1 && memory->readd(rctlAt + 12) == 1) {
            U64 len = memory->readq(rctlAt);
            if (len >= 16) {
                U32 n = (U32)((len - 16) / 4);
                for (U32 i = 0; i < n; i++) res.fds.push_back(memory->readd(rctlAt + 16 + 4 * i));
            }
        }
        return res;
    };
    auto caseSend = [&](const char* what, U64 clen, U64 room, U32 fdCount,
                        S64 wantRet, size_t wantFds, bool wantData) {
        Pair p;
        if (!makePair(1 /*SOCK_STREAM*/, p)) return;
        buildSend(textAt, 16, clen, room, (U32)p.tx, fdCount);
        S64 r = invoke(46, (U64)(U32)p.tx, hdrAt, 0, 0);
        RecvRes got = recvPeer(p.rx);
        bool dataOk = wantData ? (got.data == std::string(text, 16)) : got.data.empty();
        if (r != wantRet || got.fds.size() != wantFds || !dataOk) {
            testFail("%s returned %lld (want %lld), peer holds %u fds and %u data bytes",
                     what, (long long)r, (long long)wantRet,
                     (unsigned)got.fds.size(), (unsigned)got.data.size());
        }
        for (U32 f : got.fds) process->close((S32)f);
        closePair(p);
    };

    // Guards: shapes that already work and must not move.
    caseSend("rights cmsg with 1 fd", 20, 20, 1, 16, 1, true);
    caseSend("rights cmsg with 2 fds", 24, 24, 2, 16, 2, true);
    // Non-fd-multiple lengths truncate like the host's count does.
    caseSend("cmsg_len 18 carries 0 fds", 18, 20, 1, 16, 0, true);
    caseSend("cmsg_len 22 carries 1 fd", 22, 24, 2, 16, 1, true);
    // A length past the declared controllen: -EINVAL, nothing sent.
    caseSend("cmsg_len 20 with controllen 16", 20, 16, 1, -K_EINVAL, 0, false);
    caseSend("cmsg_len 24 with controllen 20", 24, 20, 1, -K_EINVAL, 0, false);
    caseSend("cmsg_len 4096 with controllen 20", 4096, 20, 1, -K_EINVAL, 0, false);
    // The host's descriptor cap: 253 accepted. The payload must
    // arrive intact -- on the unfixed layout the 253 translated
    // records (4048 bytes) overrun the 448-byte scratch cmsg area
    // and overwrite the gathered payload. The receive side of this
    // build holds 28 records per message (its scratch control area)
    // and reports MSG_CTRUNC for the rest.
    {
        Pair p;
        if (makePair(1, p)) {
            buildSend(textAt, 16, 16 + 253 * 4, 16 + 253 * 4, (U32)p.tx, 253);
            S64 r = invoke(46, (U64)(U32)p.tx, hdrAt, 0, 0);
            RecvRes got = recvPeer(p.rx);
            if (r != 16 || got.data != std::string(text, 16) || got.fds.size() != 28 ||
                !(got.msgFlags & 8 /*MSG_CTRUNC*/)) {
                testFail("253-fd sendmsg returned %lld, peer holds %u fds (flags 0x%x) and %u data bytes",
                         (long long)r, (unsigned)got.fds.size(), got.msgFlags, (unsigned)got.data.size());
            }
            for (U32 f : got.fds) process->close((S32)f);
            closePair(p);
        }
    }
    // 254 descriptors: -EINVAL, nothing sent.
    caseSend("254 fds over the host cap", 16 + 254 * 4, 16 + 254 * 4, 254, -K_EINVAL, 0, false);
    // Ordering: an invalid cmsg_len answers -EINVAL ahead of a bad
    // data segment's -EFAULT (the host processes the control first).
    {
        Pair p;
        if (makePair(1, p)) {
            buildSend(unmapped, 16, 8, 20, (U32)p.tx, 1);
            S64 r = invoke(46, (U64)(U32)p.tx, hdrAt, 0, 0);
            RecvRes got = recvPeer(p.rx);
            if (r != -K_EINVAL || !got.data.empty() || !got.fds.empty()) {
                testFail("bad cmsg_len with a bad segment returned %lld (want -EINVAL)", (long long)r);
            }
            closePair(p);
        }
    }
    // Lengths under the header size: -EINVAL, nothing sent. These
    // run last: on the unfixed code each one underflows the record
    // count toward ~2^30 iterations, which is the defect itself.
    caseSend("cmsg_len 0", 0, 20, 1, -K_EINVAL, 0, false);
    caseSend("cmsg_len 8", 8, 20, 1, -K_EINVAL, 0, false);
    caseSend("cmsg_len 12", 12, 20, 1, -K_EINVAL, 0, false);
    caseSend("cmsg_len 15", 15, 20, 1, -K_EINVAL, 0, false);
}


// FD-103: the 64-bit sendto copies its buffer through the
// zero-filling memcpyFromGuest with no readability check, so an
// unreadable buffer is sent as fabricated zeros and reported as
// success. Host contract measured in
// test-results/fd101/hostref-sendto-fd101.c: unmapped and
// page-crossing buffers answer -EFAULT with nothing delivered on
// stream and datagram sockets alike, and -ENOTSOCK on a file
// descriptor precedes the fault.
void testSendtoBadBuffer64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 dataPage = 0x6A000000ULL;
    constexpr U64 crossPage = 0x6B000000ULL;
    constexpr U64 unmapped = 0x6200000000ULL;
    auto mapFixture = [&](U64 addr, U64 size, U32 prot) {
        if (!memory->getPageFlags(addr >> K64_PAGE_SHIFT) &&
            memory->mmapAnonymousFixed(addr, size, prot) != addr) {
            testFail("sendto-bad-buffer test could not map its fixture page at 0x%llx", (unsigned long long)addr);
            return false;
        }
        return true;
    };
    if (!mapFixture(dataPage, 2 * K64_PAGE_SIZE, 3) ||
        !mapFixture(crossPage, K64_PAGE_SIZE, 3)) return;
    if (memory->getPageFlags(unmapped >> K64_PAGE_SHIFT) != 0 ||
        memory->getPageFlags((crossPage + K64_PAGE_SIZE) >> K64_PAGE_SHIFT) != 0) {
        testFail("sendto-bad-buffer test fixture addresses are unexpectedly mapped");
        return;
    }

    // A plain file descriptor for the -ENOTSOCK cases (the preadv
    // fixture pattern: a private VFS root over a host temp dir).
    char dirTemplate[] = "/tmp/k3bsendtoXXXXXX";
    char* hostDir = mkdtemp(dirTemplate);
    if (!hostDir) {
        testFail("sendto-bad-buffer test could not create its host temp dir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(hostDir), true, true, nullptr);
    Fs::clearPathCache();
    KFileDescriptorPtr fileDesc;
    {
        U32 result = process->openFile(B("/"), B("/data.bin"), K_O_RDWR | K_O_CREAT, fileDesc);
        if ((S32)result < 0 || !fileDesc) {
            testFail("sendto-bad-buffer test could not create /data.bin");
            return;
        }
    }
    const S32 fileFd = (S32)fileDesc->handle;

    CPU64 cpu(memory);
    cpu.thread = context.thread;
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };

    constexpr U64 textAt = dataPage + 0x000;
    constexpr U64 svAt   = dataPage + 0x100;
    constexpr U64 recvAt = dataPage + 0x200;
    constexpr U64 pollAt = dataPage + 0x300;
    constexpr U64 riovAt = dataPage + 0x1C00;
    constexpr U64 rhdrAt = dataPage + 0x1C80;
    const char* text = "0123456789ABCDEF";
    memory->memcpyToGuest(textAt, text, 16);
    std::vector<U8> pattern(K64_PAGE_SIZE);
    for (U32 i = 0; i < K64_PAGE_SIZE; i++) pattern[i] = (U8)('a' + (i % 26));
    memory->memcpyToGuest(crossPage, pattern.data(), K64_PAGE_SIZE);
    memory->writeq(riovAt, recvAt);
    memory->writeq(riovAt + 8, 512);
    for (U32 i = 0; i < 56; i += 8) memory->writeq(rhdrAt + i, 0);
    memory->writeq(rhdrAt + 16, riovAt);
    memory->writeq(rhdrAt + 24, 1);

    struct Pair { S32 tx = 0, rx = 0; };
    auto makePair = [&](U32 type, Pair& p) {
        if (invoke6(53, 1, type, 0, svAt, 0, 0) != 0) {
            testFail("sendto-bad-buffer test could not create a socketpair (type %u)", type);
            return false;
        }
        p.tx = (S32)memory->readd(svAt);
        p.rx = (S32)memory->readd(svAt + 4);
        return true;
    };
    auto closePair = [&](Pair& p) {
        process->close((S32)p.tx);
        process->close((S32)p.rx);
    };
    auto drain = [&](S32 rx, std::string& got) -> S64 {
        got.clear();
        S64 total = 0;
        for (int iter = 0; iter < 8; iter++) {
            memory->writed(pollAt, (U32)rx);
            memory->writew(pollAt + 4, 1); // POLLIN
            memory->writew(pollAt + 6, 0);
            if (invoke6(7, pollAt, 1, 0, 0, 0, 0) <= 0) break;
            if (!(memory->readw(pollAt + 6) & 1)) break;
            S64 rc = invoke6(47, (U64)(U32)rx, rhdrAt, 0, 0, 0, 0);
            if (rc <= 0) break;
            std::vector<U8> buf((size_t)rc);
            memory->memcpyFromGuest(buf.data(), recvAt, (U64)rc);
            got.append((const char*)buf.data(), (size_t)rc);
            total += rc;
        }
        return total;
    };
    auto sendtoCase = [&](const char* what, U32 type, U64 buf, U64 len,
                          S64 wantRet, const std::string& wantBytes) {
        Pair p;
        if (!makePair(type, p)) return;
        S64 r = invoke6(44, (U64)(U32)p.tx, buf, len, 0, 0, 0);
        std::string got;
        S64 held = drain(p.rx, got);
        if (r != wantRet || held != (S64)wantBytes.size() || got != wantBytes) {
            testFail("%s returned %lld (want %lld), peer holds %lld bytes",
                     what, (long long)r, (long long)wantRet, (long long)held);
        }
        closePair(p);
    };

    // 1-2. Unmapped and page-crossing buffers: -EFAULT, nothing arrives.
    sendtoCase("sendto stream from an unmapped buffer", 1, unmapped, 16, -K_EFAULT, "");
    sendtoCase("sendto stream from a crossing buffer", 1, crossPage + 4000, 200, -K_EFAULT, "");
    // Datagram sendto: only the answer is pinned here. Where the
    // object's connected write path puts datagram bytes is its own
    // pre-existing behaviour (they reach neither the peer's message
    // queue nor its stream buffer in a form poll/recvmsg/read can
    // see -- measured on the base while writing this test), and it
    // is not FD-103's claim; FD-103's claim is that the dispatcher
    // must fault the unreadable buffer instead of reporting a
    // successful fabricated send, which the answer alone decides.
    {
        Pair p;
        if (makePair(2, p)) {
            S64 r = invoke6(44, (U64)(U32)p.tx, unmapped, 16, 0, 0, 0);
            if (r != -K_EFAULT) {
                testFail("sendto dgram from an unmapped buffer returned %lld (want %lld)",
                         (long long)r, (long long)-K_EFAULT);
            }
            closePair(p);
        }
    }
    // 3. Control: a readable buffer lands its real bytes, both types.
    sendtoCase("control sendto stream", 1, textAt, 16, 16, std::string(text, 16));
    {
        Pair p;
        if (makePair(2, p)) {
            S64 r = invoke6(44, (U64)(U32)p.tx, textAt, 16, 0, 0, 0);
            if (r != 16) {
                testFail("control sendto dgram returned %lld (want 16)", (long long)r);
            }
            closePair(p);
        }
    }
    // 4. Ordering guards: the descriptor answers precede the fault.
    if (invoke6(44, (U64)(U32)fileFd, unmapped, 16, 0, 0, 0) != -K_ENOTSOCK) {
        testFail("sendto on a file fd with a bad buffer was not -ENOTSOCK");
    }
    if (invoke6(44, (U64)(U32)fileFd, textAt, 16, 0, 0, 0) != -K_ENOTSOCK) {
        testFail("sendto on a file fd with a good buffer was not -ENOTSOCK");
    }
    if (invoke6(44, 9999, unmapped, 16, 0, 0, 0) != -K_EBADF) {
        testFail("sendto with a bad fd and a bad buffer was not -EBADF");
    }
    // 5. A zero-length send never touches the buffer.
    {
        Pair p;
        if (makePair(1, p)) {
            if (invoke6(44, (U64)(U32)p.tx, unmapped, 0, 0, 0, 0) != 0) {
                testFail("zero-length sendto from an unmapped buffer was not 0");
            }
            closePair(p);
        }
    }
    process->close(fileFd);
}


// FD-104: the recvmsg scatter drops received bytes silently when a
// guest segment is unwritable -- the data is consumed from the
// socket and the call reports it delivered. Host contract measured
// in test-results/fd101/hostref-recvmsg-fd101.c and
// test-results/fd102-104/hostref-recvmsg2-fd104.c (kernel
// 6.17.0-1020-oracle): an unwritable receive range answers -EFAULT
// for stream and datagram; a datagram is consumed by the failed
// call; a short datagram that never reaches the bad range succeeds;
// an iovlen over the cap answers -EMSGSIZE; unreadable msghdr and
// iov array answer -EFAULT. The stream queue-survival half of the
// host shape is the closure's one settled divergence (the object's
// receive consumes into scratch with no pushback), so the stream
// cases pin the answer and the delivered prefix, not survival.
void testRecvmsgBadBuffer64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 dataPage = 0x6C000000ULL;
    constexpr U64 crossPage = 0x6D000000ULL;
    constexpr U64 restrictPage = 0x6E000000ULL;
    constexpr U64 roPage = 0x6F000000ULL;
    constexpr U64 bigArray = 0x69000000ULL;
    constexpr U64 hdrBlockPage = 0x68000000ULL;
    constexpr U64 unmapped = 0x6200000000ULL;
    auto mapFixture = [&](U64 addr, U64 size, U32 prot) {
        if (!memory->getPageFlags(addr >> K64_PAGE_SHIFT) &&
            memory->mmapAnonymousFixed(addr, size, prot) != addr) {
            testFail("recvmsg-bad-buffer test could not map its fixture page at 0x%llx", (unsigned long long)addr);
            return false;
        }
        return true;
    };
    if (!mapFixture(dataPage, 4 * K64_PAGE_SIZE, 3) ||
        !mapFixture(crossPage, K64_PAGE_SIZE, 3) ||
        !mapFixture(restrictPage, K64_PAGE_SIZE, 3) ||
        !mapFixture(hdrBlockPage, K64_PAGE_SIZE, 3) ||
        !mapFixture(roPage, K64_PAGE_SIZE, 1) ||
        !mapFixture(bigArray, 5 * K64_PAGE_SIZE, 3)) return;
    if (memory->getPageFlags(unmapped >> K64_PAGE_SHIFT) != 0 ||
        memory->getPageFlags((crossPage + K64_PAGE_SIZE) >> K64_PAGE_SHIFT) != 0) {
        testFail("recvmsg-bad-buffer test fixture addresses are unexpectedly mapped");
        return;
    }

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

    constexpr U64 textAt = dataPage + 0x000;
    constexpr U64 patAt  = dataPage + 0x400;
    constexpr U64 svAt   = dataPage + 0x100;
    constexpr U64 pollAt = dataPage + 0x300;
    constexpr U64 iovAt  = dataPage + 0x1800;
    constexpr U64 hdrAt  = dataPage + 0x1C80;
    constexpr U64 goodBuf = dataPage + 0x2000;
    constexpr U64 vecAt  = dataPage + 0x2800;
    constexpr U64 qHdrAt = dataPage + 0x2C00;
    constexpr U64 qIovAt = dataPage + 0x2C40;
    const char* text = "0123456789ABCDEF";
    memory->memcpyToGuest(textAt, text, 16);
    std::vector<U8> pattern(512);
    for (U32 i = 0; i < 512; i++) pattern[i] = (U8)('a' + (i % 26));
    memory->memcpyToGuest(patAt, pattern.data(), 512);
    std::vector<U8> pagePattern(K64_PAGE_SIZE);
    for (U32 i = 0; i < K64_PAGE_SIZE; i++) pagePattern[i] = (U8)('a' + (i % 26));
    memory->memcpyToGuest(crossPage, pagePattern.data(), K64_PAGE_SIZE);

    struct Pair { S32 tx = 0, rx = 0; };
    auto makePair = [&](U32 type, Pair& p) {
        if (invoke(53, 1, type, 0, svAt) != 0) {
            testFail("recvmsg-bad-buffer test could not create a socketpair (type %u)", type);
            return false;
        }
        p.tx = (S32)memory->readd(svAt);
        p.rx = (S32)memory->readd(svAt + 4);
        // The receiving end is nonblocking: a fault case must answer,
        // never park -- on the base several of them reach the receive
        // with nothing validated and nothing queued in the store the
        // receive waits on.
        invoke(72, (U64)(U32)p.rx, 4, 0x800, 0); // fcntl F_SETFL O_NONBLOCK
        return true;
    };
    auto closePair = [&](Pair& p) {
        process->close((S32)p.tx);
        process->close((S32)p.rx);
    };
    auto queue = [&](S32 tx, U64 src, U64 n) {
        // Queued with sendmsg, not sendto: the object's sendto is its
        // connected write path, which lands bytes in the peer's stream
        // buffer even for datagram sockets -- datagram cases queued
        // that way would never reach the message queue the receive
        // waits on.
        for (U32 i = 0; i < 56; i += 8) memory->writeq(qHdrAt + i, 0);
        memory->writeq(qHdrAt + 16, qIovAt);
        memory->writeq(qHdrAt + 24, 1);
        memory->writeq(qIovAt, src);
        memory->writeq(qIovAt + 8, n);
        if (invoke(46, (U64)(U32)tx, qHdrAt, 0, 0) != (S64)n) {
            testFail("recvmsg-bad-buffer test could not queue %llu bytes", (unsigned long long)n);
            return false;
        }
        return true;
    };
    auto setIov = [&](U32 idx, U64 base, U64 len) {
        memory->writeq(iovAt + idx * 16, base);
        memory->writeq(iovAt + idx * 16 + 8, len);
    };
    auto buildHdr = [&](U64 iovAddr, U64 iovlen) {
        for (U32 i = 0; i < 56; i += 8) memory->writeq(hdrAt + i, 0);
        memory->writeq(hdrAt + 16, iovAddr);
        memory->writeq(hdrAt + 24, iovlen);
    };
    auto pollReady = [&](S32 rx) {
        memory->writed(pollAt, (U32)rx);
        memory->writew(pollAt + 4, 1); // POLLIN
        memory->writew(pollAt + 6, 0);
        return invoke(7, pollAt, 1, 0, 0) > 0 && (memory->readw(pollAt + 6) & 1);
    };
    auto segText = [&](U64 at, U32 len) {
        std::string s;
        for (U32 i = 0; i < len; i++) s += (char)memory->readb(at + i);
        return s;
    };
    auto patSlice = [&](U32 n) {
        return std::string((const char*)pattern.data(), n);
    };

    // 1. Stream, single unmapped segment: -EFAULT (the base reports
    // the bytes delivered and drops them).
    {
        Pair p;
        if (makePair(1, p) && queue(p.tx, textAt, 16)) {
            setIov(0, unmapped, 16);
            buildHdr(iovAt, 1);
            if (invoke(47, (U64)(U32)p.rx, hdrAt, 0, 0) != -K_EFAULT) {
                testFail("recvmsg stream into an unmapped segment was not -EFAULT");
            }
            closePair(p);
        }
    }
    // 2. Stream [good 50][bad 50] with 100 queued: -EFAULT, and the
    // good segment keeps its 50 bytes.
    {
        Pair p;
        if (makePair(1, p) && queue(p.tx, patAt, 100)) {
            setIov(0, goodBuf, 50);
            setIov(1, unmapped, 50);
            buildHdr(iovAt, 2);
            S64 r = invoke(47, (U64)(U32)p.rx, hdrAt, 0, 0);
            if (r != -K_EFAULT || segText(goodBuf, 50) != patSlice(50)) {
                testFail("recvmsg stream [good][bad] returned %lld (want -EFAULT with the prefix delivered)",
                         (long long)r);
            }
            closePair(p);
        }
    }
    // 3. Stream segment crossing into an unmapped page: -EFAULT;
    // the 96 writable bytes ahead of the boundary land.
    {
        Pair p;
        if (makePair(1, p) && queue(p.tx, crossPage, 200)) {
            setIov(0, crossPage + 4000, 200);
            buildHdr(iovAt, 1);
            S64 r = invoke(47, (U64)(U32)p.rx, hdrAt, 0, 0);
            if (r != -K_EFAULT || segText(crossPage + 4000, 96) != patSlice(96)) {
                testFail("recvmsg stream crossing segment returned %lld (want -EFAULT with 96 bytes delivered)",
                         (long long)r);
            }
            closePair(p);
        }
    }
    // 4. Datagram [good 50][bad 50], 100-byte datagram: -EFAULT,
    // the prefix lands, and the datagram is consumed (the host's
    // measured consumption matches here exactly).
    {
        Pair p;
        if (makePair(2, p) && queue(p.tx, patAt, 100)) {
            setIov(0, goodBuf, 50);
            setIov(1, unmapped, 50);
            buildHdr(iovAt, 2);
            S64 r = invoke(47, (U64)(U32)p.rx, hdrAt, 0, 0);
            if (r != -K_EFAULT || segText(goodBuf, 50) != patSlice(50) || pollReady(p.rx)) {
                testFail("recvmsg dgram [good][bad] returned %lld (want -EFAULT, prefix delivered, datagram consumed)",
                         (long long)r);
            }
            closePair(p);
        }
    }
    // 5. Datagram shorter than the bad range's offset: the bad
    // range is never touched and the call succeeds.
    {
        Pair p;
        if (makePair(2, p) && queue(p.tx, patAt, 40)) {
            setIov(0, goodBuf, 50);
            setIov(1, unmapped, 50);
            buildHdr(iovAt, 2);
            S64 r = invoke(47, (U64)(U32)p.rx, hdrAt, 0, 0);
            if (r != 40 || segText(goodBuf, 40) != patSlice(40)) {
                testFail("recvmsg dgram short datagram returned %lld (want 40)", (long long)r);
            }
            closePair(p);
        }
    }
    // 6. Datagram, single unmapped segment: -EFAULT and consumed.
    {
        Pair p;
        if (makePair(2, p) && queue(p.tx, textAt, 16)) {
            setIov(0, unmapped, 16);
            buildHdr(iovAt, 1);
            S64 r = invoke(47, (U64)(U32)p.rx, hdrAt, 0, 0);
            if (r != -K_EFAULT || pollReady(p.rx)) {
                testFail("recvmsg dgram into an unmapped segment returned %lld (want -EFAULT, consumed)",
                         (long long)r);
            }
            closePair(p);
        }
    }
    // 7. A segment the guest itself restricted with mprotect
    // (guest-imposed PROT_NONE): the copy's own gate denies the
    // write, so this is a silent drop on the base: -EFAULT.
    {
        Pair p;
        if (makePair(1, p) && queue(p.tx, textAt, 16)) {
            if (invoke(10, restrictPage, K64_PAGE_SIZE, 0, 0) != 0) {
                testFail("recvmsg-bad-buffer test could not restrict its fixture page");
            } else {
                setIov(0, restrictPage, 16);
                buildHdr(iovAt, 1);
                if (invoke(47, (U64)(U32)p.rx, hdrAt, 0, 0) != -K_EFAULT) {
                    testFail("recvmsg stream into a guest-restricted segment was not -EFAULT");
                }
            }
            closePair(p);
        }
    }
    // 8. Boundary, pinned deliberately: a page the guest mapped
    // read-only at mmap time (never mprotect-restricted) is a legal
    // write target under this memory model -- the copy gate denies
    // only guest-imposed restrictions, the same design FD-97
    // recorded for the write family -- so delivery succeeds. The
    // host faults this case; the divergence is the memory model's,
    // not the scatter's, and is recorded with FD-104's closure.
    {
        Pair p;
        if (makePair(1, p) && queue(p.tx, textAt, 16)) {
            setIov(0, roPage, 16);
            buildHdr(iovAt, 1);
            S64 r = invoke(47, (U64)(U32)p.rx, hdrAt, 0, 0);
            if (r != 16 || segText(roPage, 16) != std::string(text, 16)) {
                testFail("recvmsg stream into a read-only-born page returned %lld (want 16, the model's boundary)",
                         (long long)r);
            }
            closePair(p);
        }
    }
    // 9. iovlen over UIO_MAXIOV: -EMSGSIZE (the base walks the
    // array unbounded and answers a fake empty success).
    {
        Pair p;
        if (makePair(1, p) && queue(p.tx, textAt, 16)) {
            for (U32 i = 0; i < 1025; i++) {
                memory->writeq(bigArray + i * 16, goodBuf);
                memory->writeq(bigArray + i * 16 + 8, 0);
            }
            buildHdr(bigArray, 1025);
            if (invoke(47, (U64)(U32)p.rx, hdrAt, 0, 0) != -K_EMSGSIZE) {
                testFail("recvmsg with iovlen 1025 was not -EMSGSIZE");
            }
            closePair(p);
        }
    }
    // 10. The iov array itself unreadable: -EFAULT.
    {
        Pair p;
        if (makePair(1, p) && queue(p.tx, textAt, 16)) {
            buildHdr(unmapped, 1);
            if (invoke(47, (U64)(U32)p.rx, hdrAt, 0, 0) != -K_EFAULT) {
                testFail("recvmsg with an unmapped iov array was not -EFAULT");
            }
            closePair(p);
        }
    }
    // 11. The msghdr itself unreadable: -EFAULT (the base reads it
    // as zeros and reports a successful empty receive). The header
    // sits on a page the guest itself restricted with mprotect, not
    // a never-mapped address: scalar reads of a never-mapped header
    // in this harness return stale table contents, and the base's
    // unbounded walk then spins on the garbage fields instead of
    // answering at all (measured with gdb on the base build) --
    // the restricted page is the same unreadable-header contract
    // with a base that stays responsive enough to record it.
    {
        Pair p;
        if (makePair(1, p) && queue(p.tx, textAt, 16)) {
            if (invoke(10, hdrBlockPage, K64_PAGE_SIZE, 0, 0) != 0) {
                testFail("recvmsg-bad-buffer test could not restrict its header page");
            } else if (invoke(47, (U64)(U32)p.rx, hdrBlockPage, 0, 0) != -K_EFAULT) {
                testFail("recvmsg with an unreadable msghdr was not -EFAULT");
            }
            closePair(p);
        }
    }
    // 12. Ordering guard: a bad fd answers -EBADF ahead of
    // everything (already true on the base via krecvmsg).
    {
        setIov(0, unmapped, 16);
        buildHdr(iovAt, 1);
        if (invoke(47, 9999, hdrAt, 0, 0) != -K_EBADF) {
            testFail("recvmsg with a bad fd and a bad segment was not -EBADF");
        }
    }
    // 13. Control: an ordinary receive lands its bytes.
    {
        Pair p;
        if (makePair(1, p) && queue(p.tx, textAt, 16)) {
            setIov(0, goodBuf, 512);
            buildHdr(iovAt, 1);
            S64 r = invoke(47, (U64)(U32)p.rx, hdrAt, 0, 0);
            if (r != 16 || segText(goodBuf, 16) != std::string(text, 16)) {
                testFail("control recvmsg returned %lld (want 16)", (long long)r);
            }
            closePair(p);
        }
    }
    // 14. recvmmsg inherits the contract through its loop: a good
    // message is delivered and counted, the bad one stops the loop
    // with its error instead of a fabricated delivery.
    {
        Pair p;
        if (makePair(1, p) && queue(p.tx, patAt, 8) && queue(p.tx, patAt + 8, 8)) {
            for (U32 i = 0; i < 128; i += 8) memory->writeq(vecAt + i, 0);
            setIov(0, goodBuf, 8);
            setIov(1, unmapped, 8);
            memory->writeq(vecAt + 16, iovAt);        // msg0 iov (slot 0)
            memory->writeq(vecAt + 24, 1);
            memory->writeq(vecAt + 64 + 16, iovAt + 16); // msg1 iov (slot 1)
            memory->writeq(vecAt + 64 + 24, 1);
            S64 r = invoke(299, (U64)(U32)p.rx, vecAt, 2, 0);
            if (r != 1 || memory->readd(vecAt + 56) != 8 || segText(goodBuf, 8) != patSlice(8)) {
                testFail("recvmmsg good+bad returned %lld (want 1 with msg_len 8)", (long long)r);
            }
            closePair(p);
        }
    }
}


// FD-105: the dispatcher's recvfrom branch receives into scratch and
// copies back through KMemory64::memcpyToGuest, whose access-check
// failure returns silently -- a receive into unwritable guest memory
// reports the full byte count and the bytes are gone. Host contract
// measured in test-results/fd105-108/hostref-name-fd107.c (N5/N6) and
// the filing lane's P10/P11: an unwritable buffer answers -EFAULT,
// and a buffer crossing into unwritable memory faults with the
// writable prefix delivered. Consumption follows FD-104's settled
// shape: the object receive consumes into scratch with no pushback,
// so a faulted plain receive consumes the bytes on a stream socket
// too (the host leaves them queued -- the one settled divergence),
// while MSG_PEEK consumes nothing on either. Coverage boundary: the
// emulator's datagram queue is reachable only through recvmsg
// (read/recvfrom drain only the stream buffer), so the datagram
// half of the host shape is pinned by FD-104's recvmsg cases; the
// copyback fixed here is the shared single-call path and is
// type-agnostic.
void testRecvfromBadBuffer64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 dataPage = 0x6B000000ULL;
    constexpr U64 crossPage = 0x6C000000ULL;
    constexpr U64 unmapped = 0x6200000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, 4 * K64_PAGE_SIZE, 3) != dataPage) {
        testFail("recvfrom test could not map its fixture page");
        return;
    }
    if (!memory->getPageFlags(crossPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(crossPage, 5 * K64_PAGE_SIZE, 3) != crossPage) {
        testFail("recvfrom test could not map its crossing page");
        return;
    }
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
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    // Page index 2 of the crossing region rejects writes.
    if (invoke(10, crossPage + 2 * K64_PAGE_SIZE, K64_PAGE_SIZE, 0, 0) != 0) {
        testFail("recvfrom test could not restrict its crossing page");
        return;
    }

    constexpr U64 textAt = dataPage + 0x000;
    constexpr U64 svAt   = dataPage + 0x100;
    constexpr U64 recvAt = dataPage + 0x200;
    constexpr U64 patAt  = dataPage + 0x600;
    const char* text = "0123456789ABCDEF";
    memory->memcpyToGuest(textAt, text, 16);
    std::string pattern;
    for (int i = 0; i < 200; i++) pattern.push_back((char)('a' + (i % 26)));
    memory->memcpyToGuest(patAt, pattern.data(), 200);

    struct Pair { S32 tx = 0, rx = 0; };
    auto makePair = [&](Pair& p) {
        if (invoke(53, 1, 1 /*SOCK_STREAM*/, 0, svAt) != 0) {
            testFail("recvfrom test could not create a stream socketpair");
            return false;
        }
        p.tx = (S32)memory->readd(svAt);
        p.rx = (S32)memory->readd(svAt + 4);
        if (invoke6(72, (U64)(U32)p.rx, 4 /*F_SETFL*/, 0x800 /*O_NONBLOCK*/, 0, 0, 0) != 0) {
            testFail("recvfrom test could not set the receiver nonblocking");
            return false;
        }
        return true;
    };
    auto closePair = [&](Pair& p) {
        process->close((S32)p.tx);
        process->close((S32)p.rx);
    };
    auto guestEq = [&](U64 addr, const char* want, U64 len) {
        std::vector<U8> buf((size_t)len);
        memory->memcpyFromGuest(buf.data(), addr, len);
        return memcmp(buf.data(), want, (size_t)len) == 0;
    };

    // Guard: an ordinary stream recvfrom still works.
    {
        Pair p;
        if (makePair(p)) {
            invoke(1, (U64)(U32)p.tx, textAt, 16, 0);
            S64 r = invoke(45, (U64)(U32)p.rx, recvAt, 512, 0);
            if (r != 16 || !guestEq(recvAt, text, 16)) {
                testFail("plain stream recvfrom returned %lld (want 16)", (long long)r);
            }
            closePair(p);
        }
    }
    // Unwritable buffer: -EFAULT. The bytes are consumed by the
    // failed call -- FD-104's settled divergence (the object
    // receive consumes into scratch with no pushback; the host
    // leaves stream data queued).
    {
        Pair p;
        if (makePair(p)) {
            invoke(1, (U64)(U32)p.tx, textAt, 16, 0);
            S64 r = invoke(45, (U64)(U32)p.rx, unmapped, 16, 0);
            S64 r2 = invoke(45, (U64)(U32)p.rx, recvAt, 512, 0);
            if (r != -K_EFAULT || r2 != -K_EWOULDBLOCK) {
                testFail("stream recvfrom to an unmapped buffer returned %lld then %lld (want -EFAULT, then -EWOULDBLOCK: consumed per FD-104's settled shape)",
                         (long long)r, (long long)r2);
            }
            closePair(p);
        }
    }
    // MSG_PEEK faults the same way and consumes nothing.
    {
        Pair p;
        if (makePair(p)) {
            invoke(1, (U64)(U32)p.tx, textAt, 16, 0);
            S64 r = invoke(45, (U64)(U32)p.rx, unmapped, 16, 2 /*MSG_PEEK*/);
            S64 r2 = invoke(45, (U64)(U32)p.rx, recvAt, 512, 0);
            if (r != -K_EFAULT || r2 != 16 || !guestEq(recvAt, text, 16)) {
                testFail("stream MSG_PEEK recvfrom to an unmapped buffer returned %lld then %lld (want -EFAULT then 16)",
                         (long long)r, (long long)r2);
            }
            closePair(p);
        }
    }
    // Crossing buffer: -EFAULT with the writable prefix delivered.
    {
        Pair p;
        if (makePair(p)) {
            invoke(1, (U64)(U32)p.tx, patAt, 200, 0);
            // 96 writable bytes, then the restricted page.
            const U64 start = crossPage + 2 * K64_PAGE_SIZE - 96;
            S64 r = invoke(45, (U64)(U32)p.rx, start, 200, 0);
            bool prefixOk = guestEq(start, pattern.data(), 96);
            if (r != -K_EFAULT || !prefixOk) {
                testFail("crossing stream recvfrom returned %lld (want -EFAULT) with the 96-byte prefix %s",
                         (long long)r, prefixOk ? "delivered" : "missing");
            }
            closePair(p);
        }
    }
}

// FD-106: sys_sendmsg64 examines only the first control header --
// nothing past it is validated or translated, where the host walks
// the whole chain. Host contract measured in
// test-results/fd105-108/hostref-chain-fd106.c,
// hostref-chain2-fd106.c and hostref-order-fd106.c: headers are
// walked at 8-byte-aligned steps while at least 16 bytes remain; a
// walked header with cmsg_len under 16 or past the remaining room
// answers -EINVAL; SCM_RIGHTS headers accumulate under one 253
// descriptor cap and their fds all arrive; SCM_CREDENTIALS requires
// cmsg_len exactly 28; any other SOL_SOCKET type answers -EINVAL;
// other levels are skipped; the whole control area is copied before
// validation, so unreadable bytes anywhere in msg_controllen answer
// -EFAULT ahead of every -EINVAL; and a bad descriptor value in an
// earlier rights header answers -EBADF ahead of a later header's
// malformed length.
void testSendmsgControlChain64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 dataPage = 0x6D000000ULL;
    constexpr U64 ctrlPage = 0x6E000000ULL;
    constexpr U64 tailPage = 0x6F000000ULL;
    constexpr U64 bigIovPage = 0x70000000ULL;
    auto mapFixture = [&](U64 addr, U64 size) {
        if (!memory->getPageFlags(addr >> K64_PAGE_SHIFT) &&
            memory->mmapAnonymousFixed(addr, size, 3) != addr) {
            testFail("cmsg-chain test could not map its fixture page at 0x%llx", (unsigned long long)addr);
            return false;
        }
        return true;
    };
    if (!mapFixture(dataPage, 4 * K64_PAGE_SIZE) ||
        !mapFixture(ctrlPage, 2 * K64_PAGE_SIZE) ||
        !mapFixture(tailPage, 2 * K64_PAGE_SIZE) ||
        !mapFixture(bigIovPage, 5 * K64_PAGE_SIZE)) return;

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
    // The tail region's second page rejects reads and writes: a
    // control area crossing into it is unreadable past the boundary.
    if (invoke(10, tailPage + K64_PAGE_SIZE, K64_PAGE_SIZE, 0, 0) != 0) {
        testFail("cmsg-chain test could not restrict its tail page");
        return;
    }

    constexpr U64 textAt = dataPage + 0x000;
    constexpr U64 svAt   = dataPage + 0x100;
    constexpr U64 recvAt = dataPage + 0x200;
    constexpr U64 pollAt = dataPage + 0x300;
    constexpr U64 hdrAt  = dataPage + 0x1000;
    constexpr U64 iovAt  = dataPage + 0x1800;
    constexpr U64 riovAt = dataPage + 0x1C00;
    constexpr U64 rhdrAt = dataPage + 0x1C80;
    constexpr U64 rctlAt = dataPage + 0x2000;
    const char* text = "0123456789ABCDEF";
    memory->memcpyToGuest(textAt, text, 16);
    memory->writeq(riovAt, recvAt);
    memory->writeq(riovAt + 8, 512);

    struct Pair { S32 tx = 0, rx = 0; };
    auto makePair = [&](U32 type, Pair& p) {
        if (invoke(53, 1, type, 0, svAt) != 0) {
            testFail("cmsg-chain test could not create a socketpair (type %u)", type);
            return false;
        }
        p.tx = (S32)memory->readd(svAt);
        p.rx = (S32)memory->readd(svAt + 4);
        return true;
    };
    auto closePair = [&](Pair& p) {
        process->close((S32)p.tx);
        process->close((S32)p.rx);
    };
    struct Hdr {
        U64 len; U32 level; U32 type; std::vector<U32> fds;
    };
    // Lay down a chain of headers at 8-aligned offsets in the ctrl
    // page (zeroed first), point the send msghdr at it with the
    // given controllen, one data segment (textAt, 16).
    auto buildChain = [&](U64 ctrlBase, const std::vector<Hdr>& hdrs, U64 room) {
        for (U32 i = 0; i < 2048; i += 8) memory->writeq(ctrlBase + i, 0);
        U64 off = 0;
        for (const Hdr& h : hdrs) {
            memory->writeq(ctrlBase + off, h.len);
            memory->writed(ctrlBase + off + 8, h.level);
            memory->writed(ctrlBase + off + 12, h.type);
            for (size_t i = 0; i < h.fds.size(); i++)
                memory->writed(ctrlBase + off + 16 + 4 * (U64)i, h.fds[i]);
            off += (h.len + 7) & ~7ULL;
        }
        memory->writeq(iovAt, textAt);
        memory->writeq(iovAt + 8, 16);
        for (U32 i = 0; i < 56; i += 8) memory->writeq(hdrAt + i, 0);
        memory->writeq(hdrAt + 16, iovAt);
        memory->writeq(hdrAt + 24, 1);
        memory->writeq(hdrAt + 32, ctrlBase);
        memory->writeq(hdrAt + 40, room);
    };
    struct RecvRes {
        S64 rc = -1;
        std::string data;
        std::vector<U32> fds;
        U32 msgFlags = 0;
    };
    auto recvPeer = [&](S32 rx) {
        RecvRes res;
        memory->writed(pollAt, (U32)rx);
        memory->writew(pollAt + 4, 1); // POLLIN
        memory->writew(pollAt + 6, 0);
        if (invoke(7, pollAt, 1, 0, 0) <= 0 || !(memory->readw(pollAt + 6) & 1)) return res;
        for (U32 i = 0; i < 56; i += 8) memory->writeq(rhdrAt + i, 0);
        memory->writeq(rhdrAt + 16, riovAt);
        memory->writeq(rhdrAt + 24, 1);
        memory->writeq(rhdrAt + 32, rctlAt);
        memory->writeq(rhdrAt + 40, 2048);
        memory->writeq(rctlAt, 0);
        S64 rc = invoke(47, (U64)(U32)rx, rhdrAt, 0, 0);
        res.rc = rc;
        if (rc > 0) {
            std::vector<U8> buf((size_t)rc);
            memory->memcpyFromGuest(buf.data(), recvAt, (U64)rc);
            res.data.assign((const char*)buf.data(), (size_t)rc);
        }
        res.msgFlags = memory->readd(rhdrAt + 48);
        U64 ctlLen = memory->readq(rhdrAt + 40);
        if (ctlLen >= 16 && memory->readd(rctlAt + 8) == 1 && memory->readd(rctlAt + 12) == 1) {
            U64 len = memory->readq(rctlAt);
            if (len >= 16) {
                U32 n = (U32)((len - 16) / 4);
                for (U32 i = 0; i < n; i++) res.fds.push_back(memory->readd(rctlAt + 16 + 4 * i));
            }
        }
        return res;
    };
    auto rights = [](U32 fdValue, U32 count) {
        Hdr h; h.len = 16 + 4ULL * count; h.level = 1; h.type = 1;
        h.fds.assign(count, fdValue);
        return h;
    };
    auto raw = [](U64 len, U32 level, U32 type) {
        Hdr h; h.len = len; h.level = level; h.type = type;
        return h;
    };
    auto caseChain = [&](const char* what, U32 type, const std::vector<Hdr>& hdrs, U64 room,
                         S64 wantRet, size_t wantFds, bool wantData) {
        Pair p;
        if (!makePair(type, p)) return;
        std::vector<Hdr> hs = hdrs;
        for (Hdr& h : hs) for (U32& v : h.fds) if (v == 0xFFFFFFFEu) v = (U32)p.tx;
        buildChain(ctrlPage, hs, room);
        S64 r = invoke(46, (U64)(U32)p.tx, hdrAt, 0, 0);
        RecvRes got = recvPeer(p.rx);
        bool dataOk = wantData ? (got.data == std::string(text, 16)) : got.data.empty();
        if (r != wantRet || got.fds.size() != wantFds || !dataOk) {
            testFail("%s returned %lld (want %lld), peer holds %u fds and %u data bytes",
                     what, (long long)r, (long long)wantRet,
                     (unsigned)got.fds.size(), (unsigned)got.data.size());
        }
        for (U32 f : got.fds) process->close((S32)f);
        closePair(p);
    };
    const U32 SELF = 0xFFFFFFFEu; // placeholder replaced with the pair's tx fd
    const U32 BADFD = 9999;

    // Guards: single-header and skip shapes that already match.
    caseChain("rights(1) with 8 bytes of trailing slack", 1, { rights(SELF, 1) }, 32, 16, 1, true);
    caseChain("rights(1) then credentials(28)", 1, { rights(SELF, 1), raw(28, 1, 2) }, 52, 16, 1, true);
    caseChain("rights(1) then a skipped IPPROTO_IP header", 1, { rights(SELF, 1), raw(16, 0, 0) }, 40, 16, 1, true);
    caseChain("credentials(28) alone", 1, { raw(28, 1, 2) }, 28, 16, 0, true);
    caseChain("IPPROTO_IP header alone", 1, { raw(16, 0, 0) }, 16, 16, 0, true);
    {
        Hdr trunc; trunc.len = 18; trunc.level = 1; trunc.type = 1; trunc.fds = { SELF };
        caseChain("rights(1) then a rights header of length 18", 1, { rights(SELF, 1), trunc }, 48, 16, 1, true);
    }
    // The chain itself: a second rights header's fds arrive too.
    caseChain("rights(1) then rights(1), stream", 1, { rights(SELF, 1), rights(SELF, 1) }, 44, 16, 2, true);
    caseChain("rights(1) then rights(1), datagram", 2, { rights(SELF, 1), rights(SELF, 1) }, 44, 16, 2, true);
    caseChain("rights(0) then rights(1)", 1, { rights(SELF, 0), rights(SELF, 1) }, 40, 16, 1, true);
    // Malformed shapes past the first header.
    caseChain("rights(1) then SOL_SOCKET type 999", 1, { rights(SELF, 1), raw(16, 1, 999) }, 40, -K_EINVAL, 0, false);
    caseChain("rights(1) then a header of length 8", 1, { rights(SELF, 1), raw(8, 1, 1) }, 40, -K_EINVAL, 0, false);
    caseChain("rights(1) then a header of length 4096", 1, { rights(SELF, 1), raw(4096, 1, 1) }, 48, -K_EINVAL, 0, false);
    caseChain("SOL_SOCKET type 999 alone", 1, { raw(16, 1, 999) }, 16, -K_EINVAL, 0, false);
    caseChain("credentials of length 16", 1, { raw(16, 1, 2) }, 16, -K_EINVAL, 0, false);
    caseChain("credentials of length 32", 1, { raw(32, 1, 2) }, 32, -K_EINVAL, 0, false);
    // The descriptor cap is cumulative across headers.
    caseChain("rights(200) then rights(100) over the cap", 1, { rights(SELF, 200), rights(SELF, 100) }, 1232, -K_EINVAL, 0, false);
    {
        // 253 across two headers: sent, payload intact; the receive
        // side holds 28 records and reports MSG_CTRUNC for the rest.
        Pair p;
        if (makePair(1, p)) {
            buildChain(ctrlPage, { rights((U32)p.tx, 200), rights((U32)p.tx, 53) }, 1044);
            S64 r = invoke(46, (U64)(U32)p.tx, hdrAt, 0, 0);
            RecvRes got = recvPeer(p.rx);
            if (r != 16 || got.data != std::string(text, 16) || got.fds.size() != 28 ||
                !(got.msgFlags & 8 /*MSG_CTRUNC*/)) {
                testFail("253 fds across two headers returned %lld, peer holds %u fds (flags 0x%x) and %u data bytes",
                         (long long)r, (unsigned)got.fds.size(), got.msgFlags, (unsigned)got.data.size());
            }
            for (U32 f : got.fds) process->close((S32)f);
            closePair(p);
        }
    }
    // Descriptor values resolve as the walk reaches their header: a
    // bad value in the first header answers -EBADF ahead of a later
    // header's malformed length, and one in the second header is
    // answered at all.
    {
        Hdr h1 = rights(BADFD, 1);
        caseChain("bad fd in header 1, malformed header 2", 1, { h1, raw(8, 1, 1) }, 40, -K_EBADF, 0, false);
    }
    caseChain("good header 1, bad fd in header 2", 1, { rights(SELF, 1), rights(BADFD, 1) }, 44, -K_EBADF, 0, false);
    // Import precedes validation: bytes of the control area past a
    // valid header are unreadable -> -EFAULT, nothing sent; the same
    // with a malformed first header -> still -EFAULT, not -EINVAL.
    {
        Pair p;
        if (makePair(1, p)) {
            const U64 ctl = tailPage + K64_PAGE_SIZE - 32;
            buildChain(ctl, { rights((U32)p.tx, 1) }, 64);
            S64 r = invoke(46, (U64)(U32)p.tx, hdrAt, 0, 0);
            RecvRes got = recvPeer(p.rx);
            if (r != -K_EFAULT || !got.data.empty() || !got.fds.empty()) {
                testFail("valid header with controllen crossing into an unreadable page returned %lld (want -EFAULT)", (long long)r);
            }
            closePair(p);
        }
    }
    {
        Pair p;
        if (makePair(1, p)) {
            const U64 ctl = tailPage + K64_PAGE_SIZE - 32;
            buildChain(ctl, { raw(8, 1, 1) }, 64);
            S64 r = invoke(46, (U64)(U32)p.tx, hdrAt, 0, 0);
            if (r != -K_EFAULT) {
                testFail("malformed header with controllen crossing into an unreadable page returned %lld (want -EFAULT)", (long long)r);
            }
            closePair(p);
        }
    }
    // Even a sub-header controllen is imported: an unreadable
    // control area of 8 bytes answers -EFAULT.
    {
        Pair p;
        if (makePair(1, p)) {
            buildChain(tailPage + K64_PAGE_SIZE, {}, 8);
            S64 r = invoke(46, (U64)(U32)p.tx, hdrAt, 0, 0);
            if (r != -K_EFAULT) {
                testFail("unreadable control area of 8 bytes returned %lld (want -EFAULT)", (long long)r);
            }
            closePair(p);
        }
    }
    // The iovlen cap is answered ahead of the control import:
    // 1025 segments plus an unreadable control area -> -EMSGSIZE.
    {
        Pair p;
        if (makePair(1, p)) {
            for (U32 i = 0; i < 1025; i++) {
                memory->writeq(bigIovPage + 16ULL * i, textAt);
                memory->writeq(bigIovPage + 16ULL * i + 8, 0);
            }
            for (U32 i = 0; i < 56; i += 8) memory->writeq(hdrAt + i, 0);
            memory->writeq(hdrAt + 16, bigIovPage);
            memory->writeq(hdrAt + 24, 1025);
            memory->writeq(hdrAt + 32, tailPage + K64_PAGE_SIZE);
            memory->writeq(hdrAt + 40, 32);
            S64 r = invoke(46, (U64)(U32)p.tx, hdrAt, 0, 0);
            if (r != -K_EMSGSIZE) {
                testFail("1025 segments with an unreadable control area returned %lld (want -EMSGSIZE)", (long long)r);
            }
            closePair(p);
        }
    }
}

// FD-107: sys_recvmsg64's source-name copyback goes through the
// same silent-drop accessor as FD-105 -- a peer address that cannot
// be written is lost while the call reports success. Host contract
// measured in test-results/fd105-108/hostref-name-fd107.c (N1/N2):
// the call answers -EFAULT, the received data and any passed
// descriptor are still delivered, and the msghdr's namelen / flags /
// controllen fields are left untouched. Staged through the message
// queue (a queued message is what makes the bridge attempt a name
// copyback at all); the sender name the bridge reports for queued
// messages is a separate known gap, filed on the register.
void testRecvmsgNameCopyback64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    // FD-109 fixture correction (lane-fd109-112): the sender below
    // is bound to a pathname, so the queued message carries a real
    // sender address and the name copyback has bytes to land. The
    // original socketpair fixture's sender was unnamed; its
    // reported length on the base was the offered capacity only
    // through FD-109's fabrication, and with the fabrication fixed
    // the reported length is 0, the copyback is empty, and the
    // unwritable-target scenarios cannot fault -- the host faults
    // only when a name is actually copied (FD-107's own host
    // reference used a named sender).
    char dirTemplate[] = "/tmp/bw64fd107XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("recvmsg-name test could not create its fixture dir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    constexpr U64 dataPage = 0x71000000ULL;
    constexpr U64 namePage = 0x72000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, 4 * K64_PAGE_SIZE, 3) != dataPage) {
        testFail("recvmsg-name test could not map its fixture page");
        return;
    }
    if (!memory->getPageFlags(namePage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(namePage, 5 * K64_PAGE_SIZE, 3) != namePage) {
        testFail("recvmsg-name test could not map its name page");
        return;
    }
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
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };
    if (invoke(10, namePage + 2 * K64_PAGE_SIZE, K64_PAGE_SIZE, 0, 0) != 0) {
        testFail("recvmsg-name test could not restrict its name page");
        return;
    }

    constexpr U64 textAt = dataPage + 0x000;
    constexpr U64 svAt   = dataPage + 0x100;
    constexpr U64 recvAt = dataPage + 0x200;
    constexpr U64 shdrAt = dataPage + 0x1000;
    constexpr U64 siovAt = dataPage + 0x1400;
    constexpr U64 sctlAt = dataPage + 0x1800;
    constexpr U64 rhdrAt = dataPage + 0x1C80;
    constexpr U64 riovAt = dataPage + 0x1C00;
    constexpr U64 rctlAt = dataPage + 0x2000;
    const char* text = "0123456789ABCDEF";
    memory->memcpyToGuest(textAt, text, 16);

    struct Pair { S32 tx = 0, rx = 0; };
    // A bound receiver and a bound, connected sender (FD-111's
    // datagram connect). The sender's name is 22 bytes long, so
    // its reported address (25 bytes) crosses a page end placed
    // 16 bytes ahead of the unwritable page in the scenario below.
    constexpr U64 saAt = dataPage + 0x300;
    auto writePathAddr = [&](U64 at, const char* path) -> U32 {
        memory->writew(at, 1); // AF_UNIX
        memory->memcpyToGuest(at + 2, path, strlen(path) + 1);
        return (U32)(2 + strlen(path) + 1);
    };
    auto makePair = [&](Pair& p) {
        p.rx = (S32)invoke(41, 1, 2 /*SOCK_DGRAM*/, 0, 0);
        U32 rl = writePathAddr(saAt, "/fd107r");
        invoke(87, saAt + 2, 0, 0, 0); // unlink first: a closed socket's node outlives it (FD-113), as on the host
        if (p.rx < 0 || invoke(49, (U64)(U32)p.rx, saAt, rl, 0) != 0) {
            testFail("recvmsg-name test could not bind its receiver");
            return false;
        }
        p.tx = (S32)invoke(41, 1, 2 /*SOCK_DGRAM*/, 0, 0);
        U32 sl = writePathAddr(saAt, "/fd107sender-long-name");
        invoke(87, saAt + 2, 0, 0, 0); // unlink first (FD-113)
        if (p.tx < 0 || invoke(49, (U64)(U32)p.tx, saAt, sl, 0) != 0) {
            testFail("recvmsg-name test could not bind its sender");
            return false;
        }
        U32 dl = writePathAddr(saAt, "/fd107r");
        if (invoke(42, (U64)(U32)p.tx, saAt, dl, 0) != 0) {
            testFail("recvmsg-name test could not connect its sender");
            return false;
        }
        if (invoke6(72, (U64)(U32)p.rx, 4 /*F_SETFL*/, 0x800 /*O_NONBLOCK*/, 0, 0, 0) != 0) {
            testFail("recvmsg-name test could not set the receiver nonblocking");
            return false;
        }
        return true;
    };
    auto closePair = [&](Pair& p) {
        process->close((S32)p.tx);
        process->close((S32)p.rx);
    };
    // Queue one datagram carrying the text and one descriptor.
    auto queueMsg = [&](S32 tx) {
        memory->writeq(siovAt, textAt);
        memory->writeq(siovAt + 8, 16);
        for (U32 i = 0; i < 56; i += 8) memory->writeq(shdrAt + i, 0);
        memory->writeq(shdrAt + 16, siovAt);
        memory->writeq(shdrAt + 24, 1);
        memory->writeq(sctlAt, 20);
        memory->writed(sctlAt + 8, 1);  // SOL_SOCKET
        memory->writed(sctlAt + 12, 1); // SCM_RIGHTS
        memory->writed(sctlAt + 16, (U32)tx);
        memory->writeq(shdrAt + 32, sctlAt);
        memory->writeq(shdrAt + 40, 20);
        return invoke(46, (U64)(U32)tx, shdrAt, 0, 0) == 16;
    };
    // Receive with the given name target; the msghdr's flags field
    // is pre-filled with a sentinel so a writeback is observable,
    // and the controllen field starts at 2048 like any caller.
    auto recvNamed = [&](S32 rx, U64 name, U64 nameLen, S64& rc, U32& installedFd) {
        for (U32 i = 0; i < 56; i += 8) memory->writeq(rhdrAt + i, 0);
        memory->writeq(rhdrAt, name);
        memory->writeq(rhdrAt + 8, nameLen);
        memory->writeq(riovAt, recvAt);
        memory->writeq(riovAt + 8, 512);
        memory->writeq(rhdrAt + 16, riovAt);
        memory->writeq(rhdrAt + 24, 1);
        memory->writeq(rhdrAt + 32, rctlAt);
        memory->writeq(rhdrAt + 40, 2048);
        memory->writed(rhdrAt + 48, 0x7777);
        memory->writeq(rctlAt, 0);
        rc = invoke(47, (U64)(U32)rx, rhdrAt, 0, 0);
        installedFd = 0;
        if (memory->readq(rhdrAt + 40) >= 20 && memory->readd(rctlAt + 12) == 1) {
            installedFd = memory->readd(rctlAt + 16);
        }
    };
    auto dataOk = [&]() {
        std::vector<U8> buf(16);
        memory->memcpyFromGuest(buf.data(), recvAt, 16);
        return memcmp(buf.data(), text, 16) == 0;
    };

    // Unwritable name target: -EFAULT, data and the passed
    // descriptor still delivered, and neither the flags nor the
    // controllen field of the msghdr is written back.
    {
        Pair p;
        if (makePair(p) && queueMsg(p.tx)) {
            S64 rc; U32 fd = 0;
            recvNamed(p.rx, namePage + 2 * K64_PAGE_SIZE, 128, rc, fd);
            U32 flagsAfter = memory->readd(rhdrAt + 48);
            U64 ctlAfter = memory->readq(rhdrAt + 40);
            if (rc != -K_EFAULT || !dataOk() || fd == 0 ||
                flagsAfter != 0x7777 || ctlAfter != 2048) {
                testFail("recvmsg with an unwritable name target returned %lld (want -EFAULT), data %s, fd %s, flags field 0x%x, controllen field %llu",
                         (long long)rc, dataOk() ? "delivered" : "lost",
                         fd ? "installed" : "missing", flagsAfter,
                         (unsigned long long)ctlAfter);
            }
            if (fd) process->close((S32)fd);
            closePair(p);
        }
    }
    // A name target crossing into an unwritable page faults the
    // same way.
    {
        Pair p;
        if (makePair(p) && queueMsg(p.tx)) {
            S64 rc; U32 fd = 0;
            recvNamed(p.rx, namePage + 2 * K64_PAGE_SIZE - 16, 128, rc, fd);
            if (rc != -K_EFAULT || !dataOk() || fd == 0) {
                testFail("recvmsg with a crossing name target returned %lld (want -EFAULT)", (long long)rc);
            }
            if (fd) process->close((S32)fd);
            closePair(p);
        }
    }
    // Guard: a writable name target receives normally, fields and
    // all.
    {
        Pair p;
        if (makePair(p) && queueMsg(p.tx)) {
            S64 rc; U32 fd = 0;
            recvNamed(p.rx, namePage, 128, rc, fd);
            U32 flagsAfter = memory->readd(rhdrAt + 48);
            if (rc != 16 || !dataOk() || fd == 0 || flagsAfter == 0x7777) {
                testFail("recvmsg with a writable name target returned %lld (want 16), flags field 0x%x",
                         (long long)rc, flagsAfter);
            }
            if (fd) process->close((S32)fd);
            closePair(p);
        }
    }
    // Guard: no name target at all still receives.
    {
        Pair p;
        if (makePair(p) && queueMsg(p.tx)) {
            S64 rc; U32 fd = 0;
            recvNamed(p.rx, 0, 0, rc, fd);
            if (rc != 16 || !dataOk() || fd == 0) {
                testFail("recvmsg with no name target returned %lld (want 16)", (long long)rc);
            }
            if (fd) process->close((S32)fd);
            closePair(p);
        }
    }
}

// FD-108: KUnixSocketObject::sendto routes every send to the
// stream write path, whose datagram branch discards the payload --
// a datagram sent on a connected socketpair is reported sent and
// never delivered. Host contract (filing lane's hostref and the
// host's datagram semantics): the send arrives as one datagram,
// boundaries and order preserved, an oversized datagram truncates
// at the receive buffer with the rest discarded.
void testSendtoDatagram64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 dataPage = 0x73000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, 4 * K64_PAGE_SIZE, 3) != dataPage) {
        testFail("sendto-datagram test could not map its fixture page");
        return;
    }
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
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };

    constexpr U64 textAt = dataPage + 0x000;
    constexpr U64 svAt   = dataPage + 0x100;
    constexpr U64 recvAt = dataPage + 0x200;
    constexpr U64 pollAt = dataPage + 0x300;
    constexpr U64 patAt  = dataPage + 0x600;
    constexpr U64 rhdrAt = dataPage + 0x1C80;
    constexpr U64 riovAt = dataPage + 0x1C00;
    const char* textA = "0123456789ABCDEF";
    const char* textB = "FEDCBA9876543210";
    memory->memcpyToGuest(textAt, textA, 16);
    memory->memcpyToGuest(textAt + 32, textB, 16);
    std::string pattern;
    for (int i = 0; i < 100; i++) pattern.push_back((char)('a' + (i % 26)));
    memory->memcpyToGuest(patAt, pattern.data(), 100);

    struct Pair { S32 tx = 0, rx = 0; };
    auto makePair = [&](U32 type, Pair& p) {
        if (invoke(53, 1, type, 0, svAt) != 0) {
            testFail("sendto-datagram test could not create a socketpair (type %u)", type);
            return false;
        }
        p.tx = (S32)memory->readd(svAt);
        p.rx = (S32)memory->readd(svAt + 4);
        if (invoke6(72, (U64)(U32)p.rx, 4 /*F_SETFL*/, 0x800 /*O_NONBLOCK*/, 0, 0, 0) != 0) {
            testFail("sendto-datagram test could not set the receiver nonblocking");
            return false;
        }
        return true;
    };
    auto closePair = [&](Pair& p) {
        process->close((S32)p.tx);
        process->close((S32)p.rx);
    };
    // One recvmsg with a receive iovec of the given length; returns
    // the syscall result and fills `out` with what landed.
    auto recvOne = [&](S32 rx, U64 iovLen, std::string& out) -> S64 {
        for (U32 i = 0; i < 56; i += 8) memory->writeq(rhdrAt + i, 0);
        memory->writeq(riovAt, recvAt);
        memory->writeq(riovAt + 8, iovLen);
        memory->writeq(rhdrAt + 16, riovAt);
        memory->writeq(rhdrAt + 24, 1);
        S64 rc = invoke(47, (U64)(U32)rx, rhdrAt, 0, 0);
        out.clear();
        if (rc > 0) {
            std::vector<U8> buf((size_t)rc);
            memory->memcpyFromGuest(buf.data(), recvAt, (U64)rc);
            out.assign((const char*)buf.data(), (size_t)rc);
        }
        return rc;
    };
    auto readable = [&](S32 rx) {
        memory->writed(pollAt, (U32)rx);
        memory->writew(pollAt + 4, 1); // POLLIN
        memory->writew(pollAt + 6, 0);
        return invoke(7, pollAt, 1, 0, 0) > 0 && (memory->readw(pollAt + 6) & 1);
    };

    // One datagram, delivered whole, exactly once.
    {
        Pair p;
        if (makePair(2 /*SOCK_DGRAM*/, p)) {
            S64 s = invoke6(44, (U64)(U32)p.tx, textAt, 16, 0, 0, 0);
            std::string got;
            S64 r = recvOne(p.rx, 512, got);
            std::string again;
            S64 r2 = recvOne(p.rx, 512, again);
            if (s != 16 || r != 16 || got != std::string(textA, 16) || r2 != -K_EWOULDBLOCK) {
                testFail("datagram sendto returned %lld, recvmsg %lld (%u bytes), second recvmsg %lld (want 16, 16 with the payload, -EWOULDBLOCK)",
                         (long long)s, (long long)r, (unsigned)got.size(), (long long)r2);
            }
            closePair(p);
        }
    }
    // Two datagrams keep their order and boundaries.
    {
        Pair p;
        if (makePair(2, p)) {
            invoke6(44, (U64)(U32)p.tx, textAt, 16, 0, 0, 0);
            invoke6(44, (U64)(U32)p.tx, textAt + 32, 16, 0, 0, 0);
            std::string g1, g2;
            S64 r1 = recvOne(p.rx, 512, g1);
            S64 r2 = recvOne(p.rx, 512, g2);
            if (r1 != 16 || g1 != std::string(textA, 16) || r2 != 16 || g2 != std::string(textB, 16)) {
                testFail("two datagrams arrived as %lld (%u bytes) then %lld (%u bytes)", (long long)r1, (unsigned)g1.size(), (long long)r2, (unsigned)g2.size());
            }
            closePair(p);
        }
    }
    // An oversized datagram truncates at the receive buffer; the
    // remainder is discarded, not delivered as a second message.
    {
        Pair p;
        if (makePair(2, p)) {
            invoke6(44, (U64)(U32)p.tx, patAt, 100, 0, 0, 0);
            std::string got;
            S64 r = recvOne(p.rx, 16, got);
            std::string again;
            S64 r2 = recvOne(p.rx, 512, again);
            if (r != 16 || got != pattern.substr(0, 16) || r2 != -K_EWOULDBLOCK) {
                testFail("oversized datagram recvmsg returned %lld (%u bytes), next %lld (want 16 truncated, then -EWOULDBLOCK)",
                         (long long)r, (unsigned)got.size(), (long long)r2);
            }
            closePair(p);
        }
    }
    // A zero-length datagram is a real, readable, empty message.
    {
        Pair p;
        if (makePair(2, p)) {
            S64 s = invoke6(44, (U64)(U32)p.tx, textAt, 0, 0, 0, 0);
            bool rd = readable(p.rx);
            std::string got;
            S64 r = recvOne(p.rx, 512, got);
            if (s != 0 || !rd || r != 0) {
                testFail("zero-length datagram: sendto %lld, readable %s, recvmsg %lld (want 0, readable, 0)",
                         (long long)s, rd ? "yes" : "no", (long long)r);
            }
            closePair(p);
        }
    }
    // MSG_DONTWAIT is an accepted flag and changes nothing.
    {
        Pair p;
        if (makePair(2, p)) {
            S64 s = invoke6(44, (U64)(U32)p.tx, textAt, 16, 0x40 /*MSG_DONTWAIT*/, 0, 0);
            std::string got;
            S64 r = recvOne(p.rx, 512, got);
            if (s != 16 || r != 16 || got != std::string(textA, 16)) {
                testFail("MSG_DONTWAIT datagram sendto returned %lld, recvmsg %lld", (long long)s, (long long)r);
            }
            closePair(p);
        }
    }
    // Guard: sendto on a stream pair still delivers through the
    // stream path.
    {
        Pair p;
        if (makePair(1 /*SOCK_STREAM*/, p)) {
            S64 s = invoke6(44, (U64)(U32)p.tx, textAt, 16, 0, 0, 0);
            std::string got;
            S64 r = recvOne(p.rx, 512, got);
            if (s != 16 || r != 16 || got != std::string(textA, 16)) {
                testFail("stream sendto returned %lld, recvmsg %lld (want 16 and the payload)", (long long)s, (long long)r);
            }
            closePair(p);
        }
    }
}

// FD-109: KUnixSocketObject::recvmsg never wrote msg_name/msg_namelen
// for a queued message, so the 64-bit bridge's initialised length --
// the guest's offered capacity -- survived as the reported length
// and that many bounce-buffer bytes were copied out as the sender's
// "address". Host contract (measured, test-results/fd109-112/
// hostref-names-fd109.c): a pathname sender is reported with length
// 2 + strlen(path) + 1 and the literal bound bytes, an abstract
// sender with 2 + 1 + namelen, an unbound or pair sender with
// length 0; a short offer truncates the copy, never the length.
void testRecvmsgSenderName64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    char dirTemplate[] = "/tmp/bw64fd109XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("recvmsg-sender-name test could not create its fixture dir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    constexpr U64 dataPage = 0x74000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, 4 * K64_PAGE_SIZE, 3) != dataPage) {
        testFail("recvmsg-sender-name test could not map its fixture page");
        return;
    }
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
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };

    constexpr U64 bufAt  = dataPage + 0x000;
    constexpr U64 destAt = dataPage + 0x100;
    constexpr U64 bindAt = dataPage + 0x200;
    constexpr U64 nameAt = dataPage + 0x400;
    constexpr U64 lenAt  = dataPage + 0x480;
    constexpr U64 recvAt = dataPage + 0x600;
    constexpr U64 svAt   = dataPage + 0x800;
    constexpr U64 shdrAt = dataPage + 0x1000;
    constexpr U64 siovAt = dataPage + 0x1080;
    constexpr U64 rhdrAt = dataPage + 0x1400;
    constexpr U64 riovAt = dataPage + 0x1480;
    memory->memcpyToGuest(bufAt, "pair", 4);

    auto writePathAddr = [&](U64 at, const char* path) -> U32 {
        memory->writew(at, 1); // AF_UNIX
        memory->memcpyToGuest(at + 2, path, strlen(path) + 1);
        return (U32)(2 + strlen(path) + 1);
    };
    auto writeAbsAddr = [&](U64 at, const char* name) -> U32 {
        memory->writew(at, 1);
        U8 zero = 0;
        memory->memcpyToGuest(at + 2, &zero, 1);
        memory->memcpyToGuest(at + 3, name, strlen(name));
        return (U32)(2 + 1 + strlen(name));
    };
    auto dgram = [&]() -> S32 { return (S32)invoke(41, 1, 2 /*SOCK_DGRAM*/, 0, 0); };
    auto nonblock = [&](S32 fd) { invoke6(72, (U64)(U32)fd, 4 /*F_SETFL*/, 0x800 /*O_NONBLOCK*/, 0, 0, 0); };
    auto nameIsPath = [&](U64 at, const char* path) -> bool {
        size_t n = strlen(path);
        std::vector<U8> buf(n + 3);
        memory->memcpyFromGuest(buf.data(), at, n + 3);
        return buf[0] == 1 && buf[1] == 0 && memcmp(buf.data() + 2, path, n) == 0 && buf[2 + n] == 0;
    };
    auto nameIsAbs = [&](U64 at, const char* name) -> bool {
        size_t n = strlen(name);
        std::vector<U8> buf(n + 3);
        memory->memcpyFromGuest(buf.data(), at, n + 3);
        return buf[0] == 1 && buf[1] == 0 && buf[2] == 0 && memcmp(buf.data() + 3, name, n) == 0;
    };
    // One sendmsg of the 4 bytes at bufAt, no name, no control.
    auto sendmsgText = [&](S32 tx) -> S64 {
        memory->writeq(siovAt, bufAt);
        memory->writeq(siovAt + 8, 4);
        for (U32 i = 0; i < 56; i += 8) memory->writeq(shdrAt + i, 0);
        memory->writeq(shdrAt + 16, siovAt);
        memory->writeq(shdrAt + 24, 1);
        return invoke(46, (U64)(U32)tx, shdrAt, 0, 0);
    };
    // One recvmsg offering `offer` name bytes; returns the syscall
    // result and the reported msg_namelen.
    auto recvNamed = [&](S32 rx, U64 offer, U32& reported) -> S64 {
        for (U32 i = 0; i < 56; i += 8) memory->writeq(rhdrAt + i, 0);
        memory->writeq(rhdrAt, nameAt);
        memory->writeq(rhdrAt + 8, offer);
        memory->writeq(riovAt, recvAt);
        memory->writeq(riovAt + 8, 512);
        memory->writeq(rhdrAt + 16, riovAt);
        memory->writeq(rhdrAt + 24, 1);
        S64 rc = invoke(47, (U64)(U32)rx, rhdrAt, 0, 0);
        reported = memory->readd(rhdrAt + 8);
        return rc;
    };
    auto boundReceiver = [&]() -> S32 {
        S32 rx = dgram();
        U32 len = writePathAddr(bindAt, "/fd109r");
        invoke(87, bindAt + 2, 0, 0, 0); // unlink first (FD-113)
        if (rx < 0 || invoke(49, (U64)(U32)rx, bindAt, len, 0) != 0) {
            testFail("recvmsg-sender-name test could not bind its receiver");
            return -1;
        }
        nonblock(rx);
        return rx;
    };

    // A socketpair sender is unnamed: the host reports length 0.
    // The base fabricated the offered capacity (128) instead.
    {
        if (invoke(53, 1, 2, 0, svAt) == 0) {
            S32 tx = (S32)memory->readd(svAt);
            S32 rx = (S32)memory->readd(svAt + 4);
            nonblock(rx);
            U32 reported = 0xFFFF;
            S64 rc = (sendmsgText(tx) == 4) ? recvNamed(rx, 128, reported) : -999;
            if (rc != 4 || reported != 0) {
                testFail("pair recvmsg sender report: rc %lld, namelen %u (want 4 and 0)",
                         (long long)rc, reported);
            }
            process->close(tx);
            process->close(rx);
        }
    }
    // The same report through recvfrom.
    {
        if (invoke(53, 1, 2, 0, svAt) == 0) {
            S32 tx = (S32)memory->readd(svAt);
            S32 rx = (S32)memory->readd(svAt + 4);
            nonblock(rx);
            memory->writed(lenAt, 128);
            S64 s = invoke6(44, (U64)(U32)tx, bufAt, 4, 0, 0, 0);
            S64 rc = invoke6(45, (U64)(U32)rx, recvAt, 512, 0, nameAt, lenAt);
            if (s != 4 || rc != 4 || memory->readd(lenAt) != 0) {
                testFail("pair recvfrom sender report: sendto %lld, rc %lld, addrlen %u (want 4, 4, 0)",
                         (long long)s, (long long)rc, memory->readd(lenAt));
            }
            process->close(tx);
            process->close(rx);
        }
    }
    // A pathname-bound sender is reported with its literal bound
    // name, length 2 + strlen + 1.
    {
        S32 rx = boundReceiver();
        S32 tx = dgram();
        U32 bl = writePathAddr(bindAt, "/fd109s");
        invoke(87, bindAt + 2, 0, 0, 0); // unlink first (FD-113)
        U32 dl = writePathAddr(destAt, "/fd109r");
        if (rx >= 0 && tx >= 0 && invoke(49, (U64)(U32)tx, bindAt, bl, 0) == 0) {
            S64 s = invoke6(44, (U64)(U32)tx, bufAt, 4, 0, destAt, dl);
            U32 reported = 0;
            S64 rc = recvNamed(rx, 128, reported);
            if (s != 4 || rc != 4 || reported != 10 || !nameIsPath(nameAt, "/fd109s")) {
                testFail("pathname sender report: sendto %lld, rc %lld, namelen %u (want 4, 4, 10 and /fd109s)",
                         (long long)s, (long long)rc, reported);
            }
            // A 4-byte offer truncates the copy, not the length.
            s = invoke6(44, (U64)(U32)tx, bufAt, 4, 0, destAt, dl);
            rc = recvNamed(rx, 4, reported);
            U8 head[4];
            memory->memcpyFromGuest(head, nameAt, 4);
            if (s != 4 || rc != 4 || reported != 10 ||
                head[0] != 1 || head[1] != 0 || head[2] != '/' || head[3] != 'f') {
                testFail("truncated sender report: sendto %lld, rc %lld, namelen %u (want 4, 4, 10 with the first 4 name bytes)",
                         (long long)s, (long long)rc, reported);
            }
        }
        if (tx >= 0) process->close(tx);
        if (rx >= 0) process->close(rx);
    }
    // An abstract-bound sender is reported with 2 + 1 + namelen.
    {
        S32 rx = boundReceiver();
        S32 tx = dgram();
        U32 bl = writeAbsAddr(bindAt, "fd109abs");
        U32 dl = writePathAddr(destAt, "/fd109r");
        if (rx >= 0 && tx >= 0 && invoke(49, (U64)(U32)tx, bindAt, bl, 0) == 0) {
            S64 s = invoke6(44, (U64)(U32)tx, bufAt, 4, 0, destAt, dl);
            U32 reported = 0;
            S64 rc = recvNamed(rx, 128, reported);
            if (s != 4 || rc != 4 || reported != 11 || !nameIsAbs(nameAt, "fd109abs")) {
                testFail("abstract sender report: sendto %lld, rc %lld, namelen %u (want 4, 4, 11 and the abstract name)",
                         (long long)s, (long long)rc, reported);
            }
        }
        if (tx >= 0) process->close(tx);
        if (rx >= 0) process->close(rx);
    }
    // An unbound sender has no name to report (the host does not
    // autobind datagram senders).
    {
        S32 rx = boundReceiver();
        S32 tx = dgram();
        U32 dl = writePathAddr(destAt, "/fd109r");
        if (rx >= 0 && tx >= 0) {
            S64 s = invoke6(44, (U64)(U32)tx, bufAt, 4, 0, destAt, dl);
            U32 reported = 0xFFFF;
            S64 rc = recvNamed(rx, 128, reported);
            if (s != 4 || rc != 4 || reported != 0) {
                testFail("unbound sender report: sendto %lld, rc %lld, namelen %u (want 4, 4, 0)",
                         (long long)s, (long long)rc, reported);
            }
        }
        if (tx >= 0) process->close(tx);
        if (rx >= 0) process->close(rx);
    }
}

// FD-110: data queued as a message (sendmsg, or an addressed
// datagram) was invisible to the plain read paths -- read() and
// recvfrom() drained only the stream byte buffer, so a receiver
// using them waited forever beside a full queue. Host contract
// (measured, test-results/fd109-112/hostref-read-fd110.c): read on
// a datagram socket returns the front datagram whole, segments
// concatenated, one datagram per call, an over-long datagram
// truncated with the rest discarded; MSG_PEEK repeats without
// consuming; on a stream socket the queued bytes read as bytes,
// partial reads included.
void testReadQueuedDatagram64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 dataPage = 0x75000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, 4 * K64_PAGE_SIZE, 3) != dataPage) {
        testFail("read-queued test could not map its fixture page");
        return;
    }
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
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };

    constexpr U64 segAt  = dataPage + 0x000;
    constexpr U64 recvAt = dataPage + 0x400;
    constexpr U64 svAt   = dataPage + 0x800;
    constexpr U64 shdrAt = dataPage + 0x1000;
    constexpr U64 siovAt = dataPage + 0x1080;
    memory->memcpyToGuest(segAt + 0x00, "AB", 2);
    memory->memcpyToGuest(segAt + 0x08, "CD", 2);
    memory->memcpyToGuest(segAt + 0x10, "ABCDEF", 6);
    memory->memcpyToGuest(segAt + 0x20, "HELLO", 5);
    memory->memcpyToGuest(segAt + 0x30, "ZW", 2);
    memory->memcpyToGuest(segAt + 0x40, "PK", 2);

    struct Pair { S32 tx = 0, rx = 0; };
    auto makePair = [&](U32 type, Pair& p) {
        if (invoke(53, 1, type, 0, svAt) != 0) {
            testFail("read-queued test could not create a socketpair (type %u)", type);
            return false;
        }
        p.tx = (S32)memory->readd(svAt);
        p.rx = (S32)memory->readd(svAt + 4);
        invoke6(72, (U64)(U32)p.rx, 4 /*F_SETFL*/, 0x800 /*O_NONBLOCK*/, 0, 0, 0);
        return true;
    };
    auto closePair = [&](Pair& p) {
        process->close(p.tx);
        process->close(p.rx);
    };
    // sendmsg with the given (address, length) segments.
    auto sendmsgSegs = [&](S32 tx, std::vector<std::pair<U64, U64>> segs) -> S64 {
        for (size_t i = 0; i < segs.size(); i++) {
            memory->writeq(siovAt + i * 16, segs[i].first);
            memory->writeq(siovAt + i * 16 + 8, segs[i].second);
        }
        for (U32 i = 0; i < 56; i += 8) memory->writeq(shdrAt + i, 0);
        memory->writeq(shdrAt + 16, siovAt);
        memory->writeq(shdrAt + 24, (U64)segs.size());
        return invoke(46, (U64)(U32)tx, shdrAt, 0, 0);
    };
    auto readStr = [&](S32 fd, U32 len, std::string& out) -> S64 {
        S64 rc = invoke(0, (U64)(U32)fd, recvAt, len, 0);
        out.clear();
        if (rc > 0) {
            std::vector<U8> buf((size_t)rc);
            memory->memcpyFromGuest(buf.data(), recvAt, (U64)rc);
            out.assign((const char*)buf.data(), (size_t)rc);
        }
        return rc;
    };

    // Two sendmsg segments arrive concatenated through one read.
    {
        Pair p;
        if (makePair(2 /*SOCK_DGRAM*/, p)) {
            S64 s = sendmsgSegs(p.tx, {{segAt + 0x00, 2}, {segAt + 0x08, 2}});
            std::string got;
            S64 r = readStr(p.rx, 64, got);
            std::string again;
            S64 r2 = readStr(p.rx, 64, again);
            if (s != 4 || r != 4 || got != "ABCD" || r2 != -K_EWOULDBLOCK) {
                testFail("queued segments through read: sendmsg %lld, read %lld (%s), next %lld (want 4, 4 ABCD, -EWOULDBLOCK)",
                         (long long)s, (long long)r, got.c_str(), (long long)r2);
            }
            closePair(p);
        }
    }
    // One datagram per read, in order.
    {
        Pair p;
        if (makePair(2, p)) {
            sendmsgSegs(p.tx, {{segAt + 0x00, 2}});
            sendmsgSegs(p.tx, {{segAt + 0x08, 2}});
            std::string g1, g2, g3;
            S64 r1 = readStr(p.rx, 64, g1);
            S64 r2 = readStr(p.rx, 64, g2);
            S64 r3 = readStr(p.rx, 64, g3);
            if (r1 != 2 || g1 != "AB" || r2 != 2 || g2 != "CD" || r3 != -K_EWOULDBLOCK) {
                testFail("queued datagrams one per read: %lld (%s), %lld (%s), %lld (want AB, CD, -EWOULDBLOCK)",
                         (long long)r1, g1.c_str(), (long long)r2, g2.c_str(), (long long)r3);
            }
            closePair(p);
        }
    }
    // A small buffer truncates the datagram; the rest is discarded.
    {
        Pair p;
        if (makePair(2, p)) {
            sendmsgSegs(p.tx, {{segAt + 0x10, 6}});
            std::string got;
            S64 r = readStr(p.rx, 3, got);
            std::string again;
            S64 r2 = readStr(p.rx, 64, again);
            if (r != 3 || got != "ABC" || r2 != -K_EWOULDBLOCK) {
                testFail("queued datagram truncation: read %lld (%s), next %lld (want 3 ABC, -EWOULDBLOCK)",
                         (long long)r, got.c_str(), (long long)r2);
            }
            closePair(p);
        }
    }
    // MSG_PEEK through recvfrom repeats without consuming.
    {
        Pair p;
        if (makePair(2, p)) {
            sendmsgSegs(p.tx, {{segAt + 0x40, 2}});
            auto peek = [&]() -> S64 {
                return invoke6(45, (U64)(U32)p.rx, recvAt, 64, 2 /*MSG_PEEK*/, 0, 0);
            };
            S64 r1 = peek();
            std::vector<U8> b1(2);
            memory->memcpyFromGuest(b1.data(), recvAt, 2);
            S64 r2 = peek();
            std::string got;
            S64 r3 = readStr(p.rx, 64, got);
            std::string again;
            S64 r4 = readStr(p.rx, 64, again);
            if (r1 != 2 || b1[0] != 'P' || b1[1] != 'K' || r2 != 2 ||
                r3 != 2 || got != "PK" || r4 != -K_EWOULDBLOCK) {
                testFail("queued MSG_PEEK: peek %lld, peek %lld, read %lld (%s), next %lld (want 2, 2, 2 PK, -EWOULDBLOCK)",
                         (long long)r1, (long long)r2, (long long)r3, got.c_str(), (long long)r4);
            }
            closePair(p);
        }
    }
    // A stream socket reads queued bytes as bytes, partially too.
    {
        Pair p;
        if (makePair(1 /*SOCK_STREAM*/, p)) {
            S64 s = sendmsgSegs(p.tx, {{segAt + 0x20, 5}});
            std::string g1, g2;
            S64 r1 = readStr(p.rx, 2, g1);
            S64 r2 = readStr(p.rx, 3, g2);
            std::string again;
            S64 r3 = readStr(p.rx, 64, again);
            if (s != 5 || r1 != 2 || g1 != "HE" || r2 != 3 || g2 != "LLO" || r3 != -K_EWOULDBLOCK) {
                testFail("queued stream bytes through read: sendmsg %lld, reads %lld (%s) %lld (%s) %lld (want 5, 2 HE, 3 LLO, -EWOULDBLOCK)",
                         (long long)s, (long long)r1, g1.c_str(), (long long)r2, g2.c_str(), (long long)r3);
            }
            closePair(p);
        }
    }
    // Guard: the plain stream-buffer write/read path is unchanged.
    {
        Pair p;
        if (makePair(1, p)) {
            S64 w = invoke(1, (U64)(U32)p.tx, segAt + 0x30, 2, 0);
            std::string got;
            S64 r = readStr(p.rx, 64, got);
            if (w != 2 || r != 2 || got != "ZW") {
                testFail("stream write/read guard: write %lld, read %lld (%s)", (long long)w, (long long)r, got.c_str());
            }
            closePair(p);
        }
    }
}

// FD-111: a datagram addressed to a named peer had no route --
// sendto with a destination answered -EDESTADDRREQ for every unix
// socket, and connect() on a datagram socket set the connected flag
// and answered -EISCONN without linking a peer, so nothing sent to
// a name could arrive anywhere. Host contract (measured,
// test-results/fd109-112/hostref-connect-fd111.c and
// hostref-tail-fd111.c): addressed sends deliver; connect returns
// 0, links the peer, and can be repeated or undone with AF_UNSPEC;
// a header destination wins over the connected peer for that call;
// the failure answers are ENOENT (missing path), ECONNREFUSED
// (missing abstract name, regular file, dead socket path),
// EPROTOTYPE (type mismatch), EOPNOTSUPP (stream sendto with a
// destination), EINVAL (non-unix or empty destination), ENOTCONN
// (unconnected send by any entry point). The /dev/log convention is
// preserved: connect answers -EISCONN and the write path prints.
void testNamedDatagram64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    char dirTemplate[] = "/tmp/bw64fd111XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("named-datagram test could not create its fixture dir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();
    // A regular file at a socket-looking path, for ECONNREFUSED.
    {
        std::string f = std::string(dir) + "/fd111f";
        FILE* fp = fopen(f.c_str(), "w");
        if (fp) fclose(fp);
    }

    constexpr U64 dataPage = 0x76000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, 4 * K64_PAGE_SIZE, 3) != dataPage) {
        testFail("named-datagram test could not map its fixture page");
        return;
    }
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
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };

    constexpr U64 bufAt  = dataPage + 0x000;
    constexpr U64 destAt = dataPage + 0x100;
    constexpr U64 bindAt = dataPage + 0x200;
    constexpr U64 nameAt = dataPage + 0x400;
    constexpr U64 lenAt  = dataPage + 0x480;
    constexpr U64 recvAt = dataPage + 0x600;
    constexpr U64 svAt   = dataPage + 0x800;
    constexpr U64 shdrAt = dataPage + 0x1000;
    constexpr U64 siovAt = dataPage + 0x1080;
    constexpr U64 rhdrAt = dataPage + 0x1400;
    constexpr U64 riovAt = dataPage + 0x1480;
    memory->memcpyToGuest(bufAt + 0x00, "PING", 4);
    memory->memcpyToGuest(bufAt + 0x10, "CNCT", 4);
    memory->memcpyToGuest(bufAt + 0x20, "WRTE", 4);

    auto writePathAddr = [&](U64 at, const char* path) -> U32 {
        memory->writew(at, 1);
        memory->memcpyToGuest(at + 2, path, strlen(path) + 1);
        return (U32)(2 + strlen(path) + 1);
    };
    auto writeAbsAddr = [&](U64 at, const char* name) -> U32 {
        memory->writew(at, 1);
        U8 zero = 0;
        memory->memcpyToGuest(at + 2, &zero, 1);
        memory->memcpyToGuest(at + 3, name, strlen(name));
        return (U32)(2 + 1 + strlen(name));
    };
    auto sock = [&](U32 type) -> S32 { return (S32)invoke(41, 1, type, 0, 0); };
    auto nonblock = [&](S32 fd) { invoke6(72, (U64)(U32)fd, 4 /*F_SETFL*/, 0x800 /*O_NONBLOCK*/, 0, 0, 0); };
    auto bindPath = [&](S32 fd, const char* path) -> bool {
        U32 len = writePathAddr(bindAt, path);
        return invoke(49, (U64)(U32)fd, bindAt, len, 0) == 0;
    };
    auto boundReceiver = [&](const char* path) -> S32 {
        S32 rx = sock(2 /*SOCK_DGRAM*/);
        if (rx < 0 || !bindPath(rx, path)) {
            testFail("named-datagram test could not bind receiver %s", path);
            return -1;
        }
        nonblock(rx);
        return rx;
    };
    auto recvOne = [&](S32 rx, std::string& out) -> S64 {
        for (U32 i = 0; i < 56; i += 8) memory->writeq(rhdrAt + i, 0);
        memory->writeq(riovAt, recvAt);
        memory->writeq(riovAt + 8, 512);
        memory->writeq(rhdrAt + 16, riovAt);
        memory->writeq(rhdrAt + 24, 1);
        S64 rc = invoke(47, (U64)(U32)rx, rhdrAt, 0, 0);
        out.clear();
        if (rc > 0) {
            std::vector<U8> buf((size_t)rc);
            memory->memcpyFromGuest(buf.data(), recvAt, (U64)rc);
            out.assign((const char*)buf.data(), (size_t)rc);
        }
        return rc;
    };
    // sendmsg of the 4 bytes at bufAt, with an optional header name
    // already written at destAt (nameLen 0 = no name).
    auto sendmsgNamed = [&](S32 tx, U32 nameLen) -> S64 {
        memory->writeq(siovAt, bufAt);
        memory->writeq(siovAt + 8, 4);
        for (U32 i = 0; i < 56; i += 8) memory->writeq(shdrAt + i, 0);
        if (nameLen) {
            memory->writeq(shdrAt, destAt);
            memory->writeq(shdrAt + 8, nameLen);
        }
        memory->writeq(shdrAt + 16, siovAt);
        memory->writeq(shdrAt + 24, 1);
        return invoke(46, (U64)(U32)tx, shdrAt, 0, 0);
    };

    // Addressed delivery: an unbound sender reaches the bound peer.
    {
        S32 rx = boundReceiver("/fd111a");
        S32 tx = sock(2);
        U32 dl = writePathAddr(destAt, "/fd111a");
        S64 s = (rx >= 0 && tx >= 0) ? invoke6(44, (U64)(U32)tx, bufAt, 4, 0, destAt, dl) : -999;
        std::string got;
        S64 r = (rx >= 0) ? recvOne(rx, got) : -999;
        if (s != 4 || r != 4 || got != "PING") {
            testFail("addressed datagram: sendto %lld, recvmsg %lld (%s) (want 4, 4 PING)",
                     (long long)s, (long long)r, got.c_str());
        }
        if (tx >= 0) process->close(tx);
        if (rx >= 0) process->close(rx);
    }
    // Connect links the peer, answers 0, and both sendto and write
    // deliver through it; getpeername reports the peer's name.
    {
        S32 rx = boundReceiver("/fd111b");
        S32 tx = sock(2);
        U32 dl = writePathAddr(destAt, "/fd111b");
        S64 c = (tx >= 0) ? invoke(42, (U64)(U32)tx, destAt, dl, 0) : -999;
        memory->writed(lenAt, 128);
        S64 gp = (tx >= 0) ? invoke(52, (U64)(U32)tx, nameAt, lenAt, 0) : -999;
        U32 peerLen = memory->readd(lenAt);
        U8 head[10];
        memory->memcpyFromGuest(head, nameAt, 10);
        S64 s1 = (tx >= 0) ? invoke6(44, (U64)(U32)tx, bufAt + 0x10, 4, 0, 0, 0) : -999;
        std::string g1;
        S64 r1 = (rx >= 0) ? recvOne(rx, g1) : -999;
        S64 s2 = (tx >= 0) ? invoke(1, (U64)(U32)tx, bufAt + 0x20, 4, 0) : -999;
        std::string g2;
        S64 r2 = (rx >= 0) ? recvOne(rx, g2) : -999;
        if (c != 0 || gp != 0 || peerLen != 10 || head[0] != 1 || memcmp(head + 2, "/fd111b", 8) != 0 ||
            s1 != 4 || r1 != 4 || g1 != "CNCT" || s2 != 4 || r2 != 4 || g2 != "WRTE") {
            testFail("connected datagram: connect %lld, getpeername %lld len %u, sendto %lld/%lld (%s), write %lld/%lld (%s) (want 0, 0 len 10 /fd111b, CNCT, WRTE)",
                     (long long)c, (long long)gp, peerLen, (long long)s1, (long long)r1, g1.c_str(),
                     (long long)s2, (long long)r2, g2.c_str());
        }
        if (tx >= 0) process->close(tx);
        if (rx >= 0) process->close(rx);
    }
    // The measured failure answers for bad destinations.
    {
        S32 tx = sock(2);
        U32 dl = writePathAddr(destAt, "/fd111none");
        S64 c1 = (tx >= 0) ? invoke(42, (U64)(U32)tx, destAt, dl, 0) : -999;
        S64 s1 = (tx >= 0) ? invoke6(44, (U64)(U32)tx, bufAt, 4, 0, destAt, dl) : -999;
        U32 al = writeAbsAddr(destAt, "fd111noabs");
        S64 c2 = (tx >= 0) ? invoke(42, (U64)(U32)tx, destAt, al, 0) : -999;
        dl = writePathAddr(destAt, "/fd111f");
        S64 s2 = (tx >= 0) ? invoke6(44, (U64)(U32)tx, bufAt, 4, 0, destAt, dl) : -999;
        S32 st = sock(1 /*SOCK_STREAM*/);
        bool bound = (st >= 0) && bindPath(st, "/fd111s");
        dl = writePathAddr(destAt, "/fd111s");
        S64 s3 = (tx >= 0) ? invoke6(44, (U64)(U32)tx, bufAt, 4, 0, destAt, dl) : -999;
        S64 c3 = (tx >= 0) ? invoke(42, (U64)(U32)tx, destAt, dl, 0) : -999;
        if (!bound || c1 != -K_ENOENT || s1 != -K_ENOENT || c2 != -K_ECONNREFUSED ||
            s2 != -K_ECONNREFUSED || s3 != -K_EPROTOTYPE || c3 != -K_EPROTOTYPE) {
            testFail("bad datagram destinations: connect-missing %lld, sendto-missing %lld, connect-abstract %lld, sendto-file %lld, sendto-stream %lld, connect-stream %lld (want ENOENT, ENOENT, ECONNREFUSED, ECONNREFUSED, EPROTOTYPE, EPROTOTYPE)",
                     (long long)c1, (long long)s1, (long long)c2, (long long)s2, (long long)s3, (long long)c3);
        }
        if (st >= 0) process->close(st);
        if (tx >= 0) process->close(tx);
    }
    // AF_UNSPEC disconnects; later sends answer ENOTCONN.
    {
        S32 rx = boundReceiver("/fd111d");
        S32 tx = sock(2);
        U32 dl = writePathAddr(destAt, "/fd111d");
        S64 c1 = (tx >= 0) ? invoke(42, (U64)(U32)tx, destAt, dl, 0) : -999;
        memory->writew(destAt, 0); // AF_UNSPEC
        S64 c2 = (tx >= 0) ? invoke(42, (U64)(U32)tx, destAt, 2, 0) : -999;
        S64 s = (tx >= 0) ? invoke6(44, (U64)(U32)tx, bufAt, 4, 0, 0, 0) : -999;
        S64 w = (tx >= 0) ? invoke(1, (U64)(U32)tx, bufAt, 4, 0) : -999;
        if (c1 != 0 || c2 != 0 || s != -K_ENOTCONN || w != -K_ENOTCONN) {
            testFail("AF_UNSPEC disconnect: connect %lld, disconnect %lld, sendto %lld, write %lld (want 0, 0, ENOTCONN, ENOTCONN)",
                     (long long)c1, (long long)c2, (long long)s, (long long)w);
        }
        if (tx >= 0) process->close(tx);
        if (rx >= 0) process->close(rx);
    }
    // A header destination wins over the connected peer, and an
    // unconnected sendmsg needs the header name.
    {
        S32 r1 = boundReceiver("/fd111h1");
        S32 r2 = boundReceiver("/fd111h2");
        S32 tx = sock(2);
        U32 dl = writePathAddr(destAt, "/fd111h1");
        S64 c = (tx >= 0) ? invoke(42, (U64)(U32)tx, destAt, dl, 0) : -999;
        U32 nl = writePathAddr(destAt, "/fd111h2");
        S64 s = (tx >= 0) ? sendmsgNamed(tx, nl) : -999;
        std::string g2, g1;
        S64 rr2 = (r2 >= 0) ? recvOne(r2, g2) : -999;
        S64 rr1 = (r1 >= 0) ? recvOne(r1, g1) : -999;
        S32 free2 = sock(2);
        S64 sn = (free2 >= 0) ? sendmsgNamed(free2, 0) : -999;
        S64 st = (free2 >= 0) ? invoke6(44, (U64)(U32)free2, bufAt, 4, 0, 0, 0) : -999;
        nl = writePathAddr(destAt, "/fd111h1");
        S64 sw = (free2 >= 0) ? sendmsgNamed(free2, nl) : -999;
        std::string g3;
        S64 rr3 = (r1 >= 0) ? recvOne(r1, g3) : -999;
        if (c != 0 || s != 4 || rr2 != 4 || g2 != "PING" || rr1 != -K_EWOULDBLOCK ||
            sn != -K_ENOTCONN || st != -K_ENOTCONN || sw != 4 || rr3 != 4 || g3 != "PING") {
            testFail("header destination: connect %lld, sendmsg-named %lld -> peer2 %lld (%s), peer1 %lld; unconnected sendmsg %lld, sendto %lld, sendmsg-named %lld -> peer1 %lld (%s)",
                     (long long)c, (long long)s, (long long)rr2, g2.c_str(), (long long)rr1,
                     (long long)sn, (long long)st, (long long)sw, (long long)rr3, g3.c_str());
        }
        if (free2 >= 0) process->close(free2);
        if (tx >= 0) process->close(tx);
        if (r1 >= 0) process->close(r1);
        if (r2 >= 0) process->close(r2);
    }
    // Abstract names deliver too, and recvfrom reports the abstract
    // sender (FD-109 through the named path).
    {
        S32 rx = sock(2);
        U32 bl = writeAbsAddr(bindAt, "fd111arx");
        bool bound = (rx >= 0) && invoke(49, (U64)(U32)rx, bindAt, bl, 0) == 0;
        if (bound) nonblock(rx);
        S32 tx = sock(2);
        bl = writeAbsAddr(bindAt, "fd111atx");
        bool boundTx = (tx >= 0) && invoke(49, (U64)(U32)tx, bindAt, bl, 0) == 0;
        U32 dl = writeAbsAddr(destAt, "fd111arx");
        S64 s = (bound && boundTx) ? invoke6(44, (U64)(U32)tx, bufAt, 4, 0, destAt, dl) : -999;
        memory->writed(lenAt, 128);
        S64 r = bound ? invoke6(45, (U64)(U32)rx, recvAt, 512, 0, nameAt, lenAt) : -999;
        U8 nm[12];
        memory->memcpyFromGuest(nm, nameAt, 12);
        if (!bound || !boundTx || s != 4 || r != 4 || memory->readd(lenAt) != 11 ||
            nm[0] != 1 || nm[1] != 0 || nm[2] != 0 || memcmp(nm + 3, "fd111atx", 8) != 0) {
            testFail("abstract delivery: sendto %lld, recvfrom %lld, addrlen %u (want 4, 4, 11 and the abstract sender)",
                     (long long)s, (long long)r, memory->readd(lenAt));
        }
        if (tx >= 0) process->close(tx);
        if (rx >= 0) process->close(rx);
    }
    // Guards: the /dev/log convention, stream sendto with a
    // destination, and a non-unix destination.
    {
        S32 tx = sock(2);
        U32 dl = writePathAddr(destAt, "/dev/log");
        S64 c = (tx >= 0) ? invoke(42, (U64)(U32)tx, destAt, dl, 0) : -999;
        S64 w = (tx >= 0) ? invoke(1, (U64)(U32)tx, bufAt, 1, 0) : -999;
        S32 rx = boundReceiver("/fd111i");
        if (invoke(53, 1, 1 /*SOCK_STREAM*/, 0, svAt) == 0) {
            S32 stx = (S32)memory->readd(svAt);
            S32 srx = (S32)memory->readd(svAt + 4);
            dl = writePathAddr(destAt, "/fd111i");
            S64 s = invoke6(44, (U64)(U32)stx, bufAt, 4, 0, destAt, dl);
            if (s != -K_EOPNOTSUPP) {
                testFail("stream sendto with a destination returned %lld (want -EOPNOTSUPP)", (long long)s);
            }
            process->close(stx);
            process->close(srx);
        }
        memory->writew(destAt, 2); // AF_INET
        for (U32 i = 0; i < 14; i++) {
            U8 z = 0;
            memory->memcpyToGuest(destAt + 2 + i, &z, 1);
        }
        S64 si = (tx >= 0) ? invoke6(44, (U64)(U32)tx, bufAt, 4, 0, destAt, 16) : -999;
        if (c != -K_EISCONN || w != 1 || si != -K_EINVAL) {
            testFail("guards: /dev/log connect %lld, write %lld, AF_INET sendto %lld (want -EISCONN, 1, -EINVAL)",
                     (long long)c, (long long)w, (long long)si);
        }
        if (tx >= 0) process->close(tx);
        if (rx >= 0) process->close(rx);
    }
}

// FD-112: a zero-length datagram sent with sendmsg vanished --
// KSocketObject::applySocketFilter answers the packet length (0)
// when no filter is installed, and sendmsg's queueing tail treated
// !kept as "filtered, queue nothing", while FD-108's sendto branch
// already distinguished the cases. Host contract (measured,
// test-results/fd109-112/hostref-tail-fd111.c): the empty datagram
// is a real message -- the receiver becomes readable, recvmsg
// returns 0 for it, descriptors sent with it are delivered, a read
// consumes it and returns 0 ahead of a following real datagram; a
// zero-length STREAM sendmsg queues nothing.
void testSendmsgZeroLength64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 dataPage = 0x77000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, 4 * K64_PAGE_SIZE, 3) != dataPage) {
        testFail("zero-length test could not map its fixture page");
        return;
    }
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
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };

    constexpr U64 bufAt  = dataPage + 0x000;
    constexpr U64 recvAt = dataPage + 0x200;
    constexpr U64 pollAt = dataPage + 0x300;
    constexpr U64 svAt   = dataPage + 0x800;
    constexpr U64 shdrAt = dataPage + 0x1000;
    constexpr U64 siovAt = dataPage + 0x1080;
    constexpr U64 sctlAt = dataPage + 0x1400;
    constexpr U64 rhdrAt = dataPage + 0x1800;
    constexpr U64 riovAt = dataPage + 0x1880;
    constexpr U64 rctlAt = dataPage + 0x2000;
    memory->memcpyToGuest(bufAt, "Z", 1);

    struct Pair { S32 tx = 0, rx = 0; };
    auto makePair = [&](U32 type, Pair& p) {
        if (invoke(53, 1, type, 0, svAt) != 0) {
            testFail("zero-length test could not create a socketpair (type %u)", type);
            return false;
        }
        p.tx = (S32)memory->readd(svAt);
        p.rx = (S32)memory->readd(svAt + 4);
        invoke6(72, (U64)(U32)p.rx, 4 /*F_SETFL*/, 0x800 /*O_NONBLOCK*/, 0, 0, 0);
        return true;
    };
    auto closePair = [&](Pair& p) {
        process->close(p.tx);
        process->close(p.rx);
    };
    auto readable = [&](S32 rx) {
        memory->writed(pollAt, (U32)rx);
        memory->writew(pollAt + 4, 1); // POLLIN
        memory->writew(pollAt + 6, 0);
        return invoke(7, pollAt, 1, 0, 0) > 0 && (memory->readw(pollAt + 6) & 1);
    };
    // sendmsg with a single zero-length iovec; withFd adds an
    // SCM_RIGHTS control carrying the pair's tx descriptor.
    auto sendEmpty = [&](Pair& p, bool withFd) -> S64 {
        memory->writeq(siovAt, bufAt);
        memory->writeq(siovAt + 8, 0);
        for (U32 i = 0; i < 56; i += 8) memory->writeq(shdrAt + i, 0);
        memory->writeq(shdrAt + 16, siovAt);
        memory->writeq(shdrAt + 24, 1);
        if (withFd) {
            memory->writeq(sctlAt, 20);
            memory->writed(sctlAt + 8, 1);  // SOL_SOCKET
            memory->writed(sctlAt + 12, 1); // SCM_RIGHTS
            memory->writed(sctlAt + 16, (U32)p.tx);
            memory->writeq(shdrAt + 32, sctlAt);
            memory->writeq(shdrAt + 40, 20);
        }
        return invoke(46, (U64)(U32)p.tx, shdrAt, 0, 0);
    };
    auto recvMsg = [&](S32 rx, U32& installedFd) -> S64 {
        for (U32 i = 0; i < 56; i += 8) memory->writeq(rhdrAt + i, 0);
        memory->writeq(riovAt, recvAt);
        memory->writeq(riovAt + 8, 512);
        memory->writeq(rhdrAt + 16, riovAt);
        memory->writeq(rhdrAt + 24, 1);
        memory->writeq(rhdrAt + 32, rctlAt);
        memory->writeq(rhdrAt + 40, 2048);
        memory->writeq(rctlAt, 0);
        S64 rc = invoke(47, (U64)(U32)rx, rhdrAt, 0, 0);
        installedFd = 0;
        if (memory->readq(rhdrAt + 40) >= 20 && memory->readd(rctlAt + 12) == 1) {
            installedFd = memory->readd(rctlAt + 16);
        }
        return rc;
    };

    // The empty datagram is delivered: readable, recvmsg returns 0,
    // and it is consumed exactly once.
    {
        Pair p;
        if (makePair(2 /*SOCK_DGRAM*/, p)) {
            S64 s = sendEmpty(p, false);
            bool rd = readable(p.rx);
            U32 fd = 0;
            S64 r = recvMsg(p.rx, fd);
            U32 fd2 = 0;
            S64 r2 = recvMsg(p.rx, fd2);
            if (s != 0 || !rd || r != 0 || r2 != -K_EWOULDBLOCK) {
                testFail("zero-length datagram: sendmsg %lld, readable %s, recvmsg %lld, next %lld (want 0, readable, 0, -EWOULDBLOCK)",
                         (long long)s, rd ? "yes" : "no", (long long)r, (long long)r2);
            }
            closePair(p);
        }
    }
    // Descriptors sent with the empty datagram are delivered.
    {
        Pair p;
        if (makePair(2, p)) {
            S64 s = sendEmpty(p, true);
            U32 fd = 0;
            S64 r = recvMsg(p.rx, fd);
            if (s != 0 || r != 0 || fd == 0) {
                testFail("zero-length datagram with rights: sendmsg %lld, recvmsg %lld, fd %s (want 0, 0, installed)",
                         (long long)s, (long long)r, fd ? "installed" : "missing");
            }
            if (fd) process->close((S32)fd);
            closePair(p);
        }
    }
    // A read consumes the empty datagram (returning 0) ahead of a
    // following real one (FD-110's read path).
    {
        Pair p;
        if (makePair(2, p)) {
            S64 s1 = sendEmpty(p, false);
            memory->writeq(siovAt, bufAt);
            memory->writeq(siovAt + 8, 1);
            for (U32 i = 0; i < 56; i += 8) memory->writeq(shdrAt + i, 0);
            memory->writeq(shdrAt + 16, siovAt);
            memory->writeq(shdrAt + 24, 1);
            S64 s2 = invoke(46, (U64)(U32)p.tx, shdrAt, 0, 0);
            S64 r1 = invoke(0, (U64)(U32)p.rx, recvAt, 64, 0);
            S64 r2 = invoke(0, (U64)(U32)p.rx, recvAt, 64, 0);
            U8 got = 0;
            memory->memcpyFromGuest(&got, recvAt, 1);
            S64 r3 = invoke(0, (U64)(U32)p.rx, recvAt, 64, 0);
            if (s1 != 0 || s2 != 1 || r1 != 0 || r2 != 1 || got != 'Z' || r3 != -K_EWOULDBLOCK) {
                testFail("empty-then-real through read: sends %lld/%lld, reads %lld, %lld (%c), %lld (want 0/1, 0, 1 Z, -EWOULDBLOCK)",
                         (long long)s1, (long long)s2, (long long)r1, (long long)r2, got ? got : '?', (long long)r3);
            }
            closePair(p);
        }
    }
    // Guard: a zero-length stream sendmsg queues nothing.
    {
        Pair p;
        if (makePair(1 /*SOCK_STREAM*/, p)) {
            S64 s = sendEmpty(p, false);
            U32 fd = 0;
            S64 r = recvMsg(p.rx, fd);
            if (s != 0 || r != -K_EWOULDBLOCK) {
                testFail("zero-length stream sendmsg: sendmsg %lld, recvmsg %lld (want 0, -EWOULDBLOCK)",
                         (long long)s, (long long)r);
            }
            closePair(p);
        }
    }
}


// FD-113: sendto/connect to a pathname whose socket has closed
// answers -ENOENT in the emulator, where the host answers
// -ECONNREFUSED for as long as the filesystem node remains (host
// contract measured, test-results/fd113-115/hostref-fd113.c: close
// never unlinks a bound pathname; addressed sends to the stale node
// answer ECONNREFUSED, a bind over it answers EADDRINUSE, and only
// an explicit unlink turns the answer into ENOENT).
void testStalePathnameSocket64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    char dirTemplate[] = "/tmp/bw64fd113XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("stale-path test could not create its fixture dir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    constexpr U64 dataPage = 0x78000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, 4 * K64_PAGE_SIZE, 3) != dataPage) {
        testFail("stale-path test could not map its fixture page");
        return;
    }
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
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };

    constexpr U64 bufAt  = dataPage + 0x000;
    constexpr U64 addrAt = dataPage + 0x100;
    constexpr U64 recvAt = dataPage + 0x600;
    memory->memcpyToGuest(bufAt, "x", 1);
    auto writePathAddr = [&](U64 at, const char* path) -> U32 {
        memory->writew(at, 1);
        memory->memcpyToGuest(at + 2, path, strlen(path) + 1);
        return (U32)(2 + strlen(path) + 1);
    };
    const U32 addrLen = writePathAddr(addrAt, "/fd113sock");
    auto sock = [&](U32 type) -> S32 { return (S32)invoke(41, 1, type, 0, 0); };
    auto nonblock = [&](S32 fd) { invoke6(72, (U64)(U32)fd, 4 /*F_SETFL*/, 0x800 /*O_NONBLOCK*/, 0, 0, 0); };

    // Bind a datagram socket to the path and close it. The host
    // keeps the node; the socket behind it is gone.
    S32 bound = sock(2 /*SOCK_DGRAM*/);
    S64 b = invoke(49, (U64)(U32)bound, addrAt, addrLen, 0);
    S64 c = invoke(3, (U64)(U32)bound, 0, 0, 0);
    if (bound < 0 || b != 0 || c != 0) {
        testFail("stale-path setup: socket %d, bind %lld, close %lld", bound, (long long)b, (long long)c);
        return;
    }

    // Addressed sends and connects to the stale path answer
    // ECONNREFUSED, not ENOENT.
    {
        S32 tx = sock(2);
        S64 s1 = invoke6(44, (U64)(U32)tx, bufAt, 1, 0, addrAt, addrLen);
        S32 cd = sock(2);
        S64 c1 = invoke(42, (U64)(U32)cd, addrAt, addrLen, 0);
        S32 cs = sock(1 /*SOCK_STREAM*/);
        S64 c2 = invoke(42, (U64)(U32)cs, addrAt, addrLen, 0);
        if (s1 != -K_ECONNREFUSED || c1 != -K_ECONNREFUSED || c2 != -K_ECONNREFUSED) {
            testFail("stale pathname answers: sendto %lld, connect dgram %lld, connect stream %lld (want -ECONNREFUSED x3)",
                     (long long)s1, (long long)c1, (long long)c2);
        }
        process->close(tx);
        process->close(cd);
        process->close(cs);
    }
    // A bind over the stale node answers EADDRINUSE, as over any
    // existing filesystem entry.
    {
        S32 reb = sock(2);
        S64 b2 = invoke(49, (U64)(U32)reb, addrAt, addrLen, 0);
        if (b2 != -K_EADDRINUSE) {
            testFail("bind over stale node: %lld (want -EADDRINUSE)", (long long)b2);
        }
        process->close(reb);
    }
    // After an explicit unlink the same send answers ENOENT, and a
    // fresh bind restores delivery.
    {
        S32 tx = sock(2);
        S64 u = invoke(87, addrAt + 2, 0, 0, 0);
        S64 s2 = invoke6(44, (U64)(U32)tx, bufAt, 1, 0, addrAt, addrLen);
        if (u != 0 || s2 != -K_ENOENT) {
            testFail("after unlink: unlink %lld, sendto %lld (want 0, -ENOENT)",
                     (long long)u, (long long)s2);
        }
        S32 live = sock(2);
        S64 b3 = invoke(49, (U64)(U32)live, addrAt, addrLen, 0);
        nonblock(live);
        S64 s3 = invoke6(44, (U64)(U32)tx, bufAt, 1, 0, addrAt, addrLen);
        S64 r3 = invoke6(45, (U64)(U32)live, recvAt, 16, 0, 0, 0);
        U8 got = (r3 == 1) ? memory->readb(recvAt) : 0;
        if (b3 != 0 || s3 != 1 || r3 != 1 || got != 'x') {
            testFail("rebound delivery: bind %lld, sendto %lld, recv %lld, byte %c (want 0, 1, 1, x)",
                     (long long)b3, (long long)s3, (long long)r3, got ? got : '-');
        }
        process->close(tx);
        process->close(live);
    }
}

// FD-114: stream bytes queued by sendmsg and bytes in the stream
// buffer do not keep call order at the read paths -- the queue is
// always served first, so write("Z") followed by sendmsg("W12")
// reads back "W12Z". Host contract (measured,
// test-results/fd113-115/hostref-fd114.c): write() and sendmsg()
// bytes arrive in call order at read, recvfrom and recvmsg; a
// descriptor sent with a chunk is reported by the recvmsg that
// reads the chunk's first byte; a plain read across the chunk
// discards the descriptor.
void testStreamWriteSendmsgOrder64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 dataPage = 0x79000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, 4 * K64_PAGE_SIZE, 3) != dataPage) {
        testFail("stream-order test could not map its fixture page");
        return;
    }
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
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };

    constexpr U64 segAt  = dataPage + 0x000;
    constexpr U64 recvAt = dataPage + 0x400;
    constexpr U64 svAt   = dataPage + 0x800;
    constexpr U64 shdrAt = dataPage + 0x1000;
    constexpr U64 siovAt = dataPage + 0x1080;
    constexpr U64 rhdrAt = dataPage + 0x1400;
    constexpr U64 riovAt = dataPage + 0x1480;
    constexpr U64 sctlAt = dataPage + 0x1800;
    constexpr U64 rctlAt = dataPage + 0x1C00;
    memory->memcpyToGuest(segAt + 0x00, "Z", 1);
    memory->memcpyToGuest(segAt + 0x08, "W12", 3);
    memory->memcpyToGuest(segAt + 0x10, "A", 1);
    memory->memcpyToGuest(segAt + 0x18, "B", 1);
    memory->memcpyToGuest(segAt + 0x20, "C", 1);
    memory->memcpyToGuest(segAt + 0x28, "D", 1);

    struct Pair { S32 tx = 0, rx = 0; };
    auto makePair = [&](U32 type, Pair& p) {
        if (invoke(53, 1, type, 0, svAt) != 0) {
            testFail("stream-order test could not create a socketpair (type %u)", type);
            return false;
        }
        p.tx = (S32)memory->readd(svAt);
        p.rx = (S32)memory->readd(svAt + 4);
        invoke6(72, (U64)(U32)p.rx, 4 /*F_SETFL*/, 0x800 /*O_NONBLOCK*/, 0, 0, 0);
        return true;
    };
    auto closePair = [&](Pair& p) {
        process->close(p.tx);
        process->close(p.rx);
    };
    auto writeStr = [&](S32 fd, U64 at, U64 len) -> S64 {
        return invoke(1, (U64)(U32)fd, at, len, 0);
    };
    auto sendmsgSegs = [&](S32 tx, std::vector<std::pair<U64, U64>> segs, S32 passFd) -> S64 {
        for (size_t i = 0; i < segs.size(); i++) {
            memory->writeq(siovAt + i * 16, segs[i].first);
            memory->writeq(siovAt + i * 16 + 8, segs[i].second);
        }
        for (U32 i = 0; i < 56; i += 8) memory->writeq(shdrAt + i, 0);
        memory->writeq(shdrAt + 16, siovAt);
        memory->writeq(shdrAt + 24, (U64)segs.size());
        if (passFd >= 0) {
            memory->writeq(sctlAt, 20);
            memory->writed(sctlAt + 8, 1);  // SOL_SOCKET
            memory->writed(sctlAt + 12, 1); // SCM_RIGHTS
            memory->writed(sctlAt + 16, (U32)passFd);
            memory->writeq(shdrAt + 32, sctlAt);
            memory->writeq(shdrAt + 40, 20);
        }
        return invoke(46, (U64)(U32)tx, shdrAt, 0, 0);
    };
    auto readStr = [&](S32 fd, U32 len, std::string& out) -> S64 {
        S64 rc = invoke(0, (U64)(U32)fd, recvAt, len, 0);
        out.clear();
        if (rc > 0) {
            std::vector<U8> buf((size_t)rc);
            memory->memcpyFromGuest(buf.data(), recvAt, (U64)rc);
            out.assign((const char*)buf.data(), (size_t)rc);
        }
        return rc;
    };
    auto recvMsg = [&](S32 rx, std::string& out, U32& installedFd) -> S64 {
        for (U32 i = 0; i < 56; i += 8) memory->writeq(rhdrAt + i, 0);
        memory->writeq(riovAt, recvAt);
        memory->writeq(riovAt + 8, 512);
        memory->writeq(rhdrAt + 16, riovAt);
        memory->writeq(rhdrAt + 24, 1);
        memory->writeq(rhdrAt + 32, rctlAt);
        memory->writeq(rhdrAt + 40, 2048);
        memory->writeq(rctlAt, 0);
        memory->writeq(rctlAt + 8, 0);
        memory->writeq(rctlAt + 16, 0);
        S64 rc = invoke(47, (U64)(U32)rx, rhdrAt, 0, 0);
        out.clear();
        if (rc > 0) {
            std::vector<U8> buf((size_t)rc);
            memory->memcpyFromGuest(buf.data(), recvAt, (U64)rc);
            out.assign((const char*)buf.data(), (size_t)rc);
        }
        installedFd = 0;
        if (memory->readq(rhdrAt + 40) >= 20 && memory->readd(rctlAt + 12) == 1) {
            installedFd = memory->readd(rctlAt + 16);
        }
        return rc;
    };

    // write() then sendmsg(): the buffered byte is older and must
    // be served first.
    {
        Pair p;
        if (makePair(1 /*SOCK_STREAM*/, p)) {
            S64 w = writeStr(p.tx, segAt + 0x00, 1);
            S64 s = sendmsgSegs(p.tx, {{segAt + 0x08, 3}}, -1);
            std::string got;
            S64 r = readStr(p.rx, 4, got);
            std::string again;
            S64 r2 = readStr(p.rx, 4, again);
            if (w != 1 || s != 3 || r != 4 || got != "ZW12" || r2 != -K_EWOULDBLOCK) {
                testFail("write-then-sendmsg order: write %lld, sendmsg %lld, read %lld (%s), next %lld (want 1, 3, 4 ZW12, -EWOULDBLOCK)",
                         (long long)w, (long long)s, (long long)r, got.c_str(), (long long)r2);
            }
            closePair(p);
        }
    }
    // sendmsg() then write(): the queued bytes are older.
    {
        Pair p;
        if (makePair(1, p)) {
            S64 s = sendmsgSegs(p.tx, {{segAt + 0x08, 3}}, -1);
            S64 w = writeStr(p.tx, segAt + 0x00, 1);
            std::string got;
            S64 r = readStr(p.rx, 4, got);
            std::string again;
            S64 r2 = readStr(p.rx, 4, again);
            if (s != 3 || w != 1 || r != 4 || got != "W12Z" || r2 != -K_EWOULDBLOCK) {
                testFail("sendmsg-then-write order: sendmsg %lld, write %lld, read %lld (%s), next %lld (want 3, 1, 4 W12Z, -EWOULDBLOCK)",
                         (long long)s, (long long)w, (long long)r, got.c_str(), (long long)r2);
            }
            closePair(p);
        }
    }
    // A four-way interleave reads back in call order.
    {
        Pair p;
        if (makePair(1, p)) {
            writeStr(p.tx, segAt + 0x10, 1);
            sendmsgSegs(p.tx, {{segAt + 0x18, 1}}, -1);
            writeStr(p.tx, segAt + 0x20, 1);
            sendmsgSegs(p.tx, {{segAt + 0x28, 1}}, -1);
            std::string got;
            S64 r = readStr(p.rx, 4, got);
            if (r != 4 || got != "ABCD") {
                testFail("four-way interleave: read %lld (%s) (want 4 ABCD)",
                         (long long)r, got.c_str());
            }
            closePair(p);
        }
    }
    // A descriptor sent with the second chunk is reported by the
    // recvmsg that reads that chunk, not the one that reads the
    // older buffered byte.
    {
        Pair p;
        if (makePair(1, p)) {
            writeStr(p.tx, segAt + 0x00, 1);
            S64 s = sendmsgSegs(p.tx, {{segAt + 0x08, 3}}, p.tx);
            std::string g1, g2;
            U32 fd1 = 0, fd2 = 0;
            S64 r1 = recvMsg(p.rx, g1, fd1);
            S64 r2 = recvMsg(p.rx, g2, fd2);
            if (s != 3 || r1 != 1 || g1 != "Z" || fd1 != 0 ||
                r2 != 3 || g2 != "W12" || fd2 == 0) {
                testFail("ancillary position: sendmsg %lld, recvmsg %lld (%s, fd %u), recvmsg %lld (%s, fd %u) (want 3, 1 Z no-fd, 3 W12 with-fd)",
                         (long long)s, (long long)r1, g1.c_str(), fd1,
                         (long long)r2, g2.c_str(), fd2);
            }
            if (fd2) process->close((S32)fd2);
            closePair(p);
        }
    }
    // A plain read across the ancillary chunk serves the bytes in
    // order and discards the descriptor.
    {
        Pair p;
        if (makePair(1, p)) {
            writeStr(p.tx, segAt + 0x00, 1);
            sendmsgSegs(p.tx, {{segAt + 0x08, 3}}, p.tx);
            std::string got;
            S64 r = readStr(p.rx, 4, got);
            std::string rest;
            U32 fd = 0;
            S64 r2 = recvMsg(p.rx, rest, fd);
            if (r != 4 || got != "ZW12" || r2 != -K_EWOULDBLOCK || fd != 0) {
                testFail("plain read over ancillary chunk: read %lld (%s), recvmsg %lld (want 4 ZW12, -EWOULDBLOCK)",
                         (long long)r, got.c_str(), (long long)r2);
            }
            closePair(p);
        }
    }
    // MSG_PEEK sees the same order without consuming it.
    {
        Pair p;
        if (makePair(1, p)) {
            writeStr(p.tx, segAt + 0x00, 1);
            sendmsgSegs(p.tx, {{segAt + 0x08, 3}}, -1);
            S64 pk = invoke6(45, (U64)(U32)p.rx, recvAt, 2, 2 /*MSG_PEEK*/, 0, 0);
            std::string peeked;
            if (pk > 0) {
                std::vector<U8> buf((size_t)pk);
                memory->memcpyFromGuest(buf.data(), recvAt, (U64)pk);
                peeked.assign((const char*)buf.data(), (size_t)pk);
            }
            std::string got;
            S64 r = readStr(p.rx, 4, got);
            if (pk != 2 || peeked != "ZW" || r != 4 || got != "ZW12") {
                testFail("peek order: peek %lld (%s), read %lld (%s) (want 2 ZW, 4 ZW12)",
                         (long long)pk, peeked.c_str(), (long long)r, got.c_str());
            }
            closePair(p);
        }
    }
}

// FD-115: writev on a connected datagram socket queues one
// datagram per iovec segment, where the host delivers a single
// datagram for the whole gather. Host contract (measured,
// test-results/fd113-115/hostref-fd115.c): segments arrive as one
// datagram with the segments concatenated; a zero-length segment
// contributes nothing; a gather totalling zero queues nothing (a
// plain zero-length write DOES queue an empty datagram -- a
// different call); an unreadable segment answers -EFAULT with
// nothing queued.
void testWritevDatagram64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 dataPage = 0x7a000000ULL;
    constexpr U64 unmapped = 0x6200000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, 4 * K64_PAGE_SIZE, 3) != dataPage) {
        testFail("writev-datagram test could not map its fixture page");
        return;
    }
    if (memory->getPageFlags(unmapped >> K64_PAGE_SHIFT)) {
        testFail("writev-datagram test fixture address is unexpectedly mapped");
        return;
    }
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
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };

    constexpr U64 segAt  = dataPage + 0x000;
    constexpr U64 recvAt = dataPage + 0x400;
    constexpr U64 svAt   = dataPage + 0x800;
    constexpr U64 iovAt  = dataPage + 0x1000;
    memory->memcpyToGuest(segAt + 0x00, "AB", 2);
    memory->memcpyToGuest(segAt + 0x08, "CD", 2);
    memory->memcpyToGuest(segAt + 0x10, "EF", 2);
    memory->memcpyToGuest(segAt + 0x18, "GH", 2);
    memory->memcpyToGuest(segAt + 0x20, "IJ", 2);

    struct Pair { S32 tx = 0, rx = 0; };
    auto makePair = [&](U32 type, Pair& p) {
        if (invoke(53, 1, type, 0, svAt) != 0) {
            testFail("writev-datagram test could not create a socketpair (type %u)", type);
            return false;
        }
        p.tx = (S32)memory->readd(svAt);
        p.rx = (S32)memory->readd(svAt + 4);
        invoke6(72, (U64)(U32)p.rx, 4 /*F_SETFL*/, 0x800 /*O_NONBLOCK*/, 0, 0, 0);
        return true;
    };
    auto closePair = [&](Pair& p) {
        process->close(p.tx);
        process->close(p.rx);
    };
    auto writevSegs = [&](S32 fd, std::vector<std::pair<U64, U64>> segs) -> S64 {
        for (size_t i = 0; i < segs.size(); i++) {
            memory->writeq(iovAt + i * 16, segs[i].first);
            memory->writeq(iovAt + i * 16 + 8, segs[i].second);
        }
        return invoke(20, (U64)(U32)fd, iovAt, (U64)segs.size(), 0);
    };
    auto recvStr = [&](S32 fd, std::string& out) -> S64 {
        S64 rc = invoke6(45, (U64)(U32)fd, recvAt, 64, 0, 0, 0);
        out.clear();
        if (rc > 0) {
            std::vector<U8> buf((size_t)rc);
            memory->memcpyFromGuest(buf.data(), recvAt, (U64)rc);
            out.assign((const char*)buf.data(), (size_t)rc);
        }
        return rc;
    };
    auto expectOneDatagram = [&](Pair& p, S64 wrote, S64 wantWrote, const char* want, const char* label) {
        std::string got;
        S64 r = recvStr(p.rx, got);
        std::string again;
        S64 r2 = recvStr(p.rx, again);
        if (wrote != wantWrote || r != (S64)strlen(want) || got != want || r2 != -K_EWOULDBLOCK) {
            testFail("%s: writev %lld, recv %lld (%s), next %lld (want %lld, %s, -EWOULDBLOCK)",
                     label, (long long)wrote, (long long)r, got.c_str(), (long long)r2,
                     (long long)wantWrote, want);
        }
    };

    // Two segments arrive as one datagram.
    {
        Pair p;
        if (makePair(2 /*SOCK_DGRAM*/, p)) {
            S64 w = writevSegs(p.tx, {{segAt + 0x00, 2}, {segAt + 0x08, 2}});
            expectOneDatagram(p, w, 4, "ABCD", "writev two segments");
            closePair(p);
        }
    }
    // A zero-length segment in the middle contributes nothing.
    {
        Pair p;
        if (makePair(2, p)) {
            S64 w = writevSegs(p.tx, {{segAt + 0x00, 2}, {segAt + 0x10, 0}, {segAt + 0x08, 2}});
            expectOneDatagram(p, w, 4, "ABCD", "writev zero-length middle segment");
            closePair(p);
        }
    }
    // Three segments arrive as one datagram.
    {
        Pair p;
        if (makePair(2, p)) {
            S64 w = writevSegs(p.tx, {{segAt + 0x10, 2}, {segAt + 0x18, 2}, {segAt + 0x20, 2}});
            expectOneDatagram(p, w, 6, "EFGHIJ", "writev three segments");
            closePair(p);
        }
    }
    // A gather totalling zero queues nothing (unlike a plain
    // zero-length write, which queues an empty datagram).
    {
        Pair p;
        if (makePair(2, p)) {
            S64 w = writevSegs(p.tx, {{segAt + 0x00, 0}, {segAt + 0x08, 0}});
            std::string got;
            S64 r = recvStr(p.rx, got);
            if (w != 0 || r != -K_EWOULDBLOCK) {
                testFail("writev all-zero gather: writev %lld, recv %lld (want 0, -EWOULDBLOCK)",
                         (long long)w, (long long)r);
            }
            closePair(p);
        }
    }
    // A zero-length segment is skipped without touching its base,
    // even a bad one.
    {
        Pair p;
        if (makePair(2, p)) {
            S64 w = writevSegs(p.tx, {{unmapped, 0}, {segAt + 0x00, 2}});
            expectOneDatagram(p, w, 2, "AB", "writev bad zero-length segment skipped");
            closePair(p);
        }
    }
    // An unreadable segment answers -EFAULT and queues nothing:
    // the gather is atomic.
    {
        Pair p;
        if (makePair(2, p)) {
            S64 w = writevSegs(p.tx, {{segAt + 0x00, 2}, {unmapped, 2}});
            std::string got;
            S64 r = recvStr(p.rx, got);
            if (w != -K_EFAULT || r != -K_EWOULDBLOCK) {
                testFail("writev bad segment: writev %lld, recv %lld (want -EFAULT, -EWOULDBLOCK)",
                         (long long)w, (long long)r);
            }
            closePair(p);
        }
    }
    // Guard: stream writev still delivers its bytes in order.
    {
        Pair p;
        if (makePair(1 /*SOCK_STREAM*/, p)) {
            S64 w = writevSegs(p.tx, {{segAt + 0x00, 2}, {segAt + 0x08, 2}});
            S64 r = invoke(0, (U64)(U32)p.rx, recvAt, 4, 0);
            std::string got;
            if (r > 0) {
                std::vector<U8> buf((size_t)r);
                memory->memcpyFromGuest(buf.data(), recvAt, (U64)r);
                got.assign((const char*)buf.data(), (size_t)r);
            }
            if (w != 4 || r != 4 || got != "ABCD") {
                testFail("stream writev guard: writev %lld, read %lld (%s) (want 4, 4 ABCD)",
                         (long long)w, (long long)r, got.c_str());
            }
            closePair(p);
        }
    }
}

// FD-116: a plain zero-length write on a 64-bit datagram socket
// queues a real empty datagram, behind the descriptor checks the
// host runs first. Host contract (measured,
// test-results/fd116-117/hostref-fd116.c, kernel
// 6.17.0-1020-oracle): write(fd, buf, 0) on a connected datagram
// socket returns 0 and the peer's recv returns 0 for an empty
// datagram; an unknown descriptor answers -EBADF; a wrong-mode
// descriptor answers the mode answer (this tree's write family
// answers -K_EINVAL, the nonzero path's standing value); a
// buffer address outside the user range answers -EFAULT with
// nothing queued, while NULL and unmapped user addresses queue
// the empty datagram; an unconnected datagram socket answers
// -ENOTCONN; a stream socket queues nothing and answers 0, and
// a stream socket whose peer is gone answers -EPIPE. On the
// base, sys_write64 returns 0 on a zero count before resolving
// the descriptor, so every case below that expects a queue or
// an error fails.
void testWriteZeroLength64Syscalls() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    if (!process->memory64) process->memory64 = new KMemory64(process.get());
    KMemory64* memory = process->memory64;

    constexpr U64 dataPage = 0x7d000000ULL;
    constexpr U64 unmapped = 0x6200000000ULL;
    if (!memory->getPageFlags(dataPage >> K64_PAGE_SHIFT) &&
        memory->mmapAnonymousFixed(dataPage, 4 * K64_PAGE_SIZE, 3) != dataPage) {
        testFail("write-zero-length test could not map its fixture page");
        return;
    }
    if (memory->getPageFlags(unmapped >> K64_PAGE_SHIFT)) {
        testFail("write-zero-length test fixture address is unexpectedly mapped");
        return;
    }
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
    auto invoke6 = [&](U64 nr, U64 a1, U64 a2, U64 a3, U64 a4, U64 a5, U64 a6) -> S64 {
        cpu.reg[X64_RAX].setU64(nr);
        cpu.reg[X64_RDI].setU64(a1);
        cpu.reg[X64_RSI].setU64(a2);
        cpu.reg[X64_RDX].setU64(a3);
        cpu.reg[X64_R10].setU64(a4);
        cpu.reg[X64_R8].setU64(a5);
        cpu.reg[X64_R9].setU64(a6);
        ksyscall64(&cpu);
        return (S64)cpu.reg[X64_RAX].u64;
    };

    constexpr U64 bufAt  = dataPage + 0x000;
    constexpr U64 recvAt = dataPage + 0x400;
    constexpr U64 svAt   = dataPage + 0x800;
    memory->memcpyToGuest(bufAt, "Z", 1);

    struct Pair { S32 tx = 0, rx = 0; };
    auto makePair = [&](U32 type, Pair& p) {
        if (invoke(53, 1, type, 0, svAt) != 0) {
            testFail("write-zero-length test could not create a socketpair (type %u)", type);
            return false;
        }
        p.tx = (S32)memory->readd(svAt);
        p.rx = (S32)memory->readd(svAt + 4);
        invoke6(72, (U64)(U32)p.rx, 4 /*F_SETFL*/, 0x800 /*O_NONBLOCK*/, 0, 0, 0);
        return true;
    };
    auto closePair = [&](Pair& p) {
        process->close(p.tx);
        process->close(p.rx);
    };
    auto write0 = [&](S32 fd, U64 buf) -> S64 {
        return invoke(1, (U64)(U32)fd, buf, 0, 0);
    };
    auto recvRc = [&](S32 fd) -> S64 {
        return invoke6(45, (U64)(U32)fd, recvAt, 64, 0, 0, 0);
    };
    // A zero-length write that must queue exactly one empty
    // datagram: write answers 0, the first recv answers 0 (the
    // empty datagram), the next answers -EWOULDBLOCK.
    auto expectEmptyDatagram = [&](Pair& p, S64 w, const char* label) {
        S64 r1 = recvRc(p.rx);
        S64 r2 = recvRc(p.rx);
        if (w != 0 || r1 != 0 || r2 != -K_EWOULDBLOCK) {
            testFail("%s: write0 %lld, recv %lld, next %lld (want 0, 0, -EWOULDBLOCK)",
                     label, (long long)w, (long long)r1, (long long)r2);
        }
    };
    // A zero-length write that must queue nothing.
    auto expectNothingQueued = [&](Pair& p, S64 w, S64 wantW, const char* label) {
        S64 r1 = recvRc(p.rx);
        if (w != wantW || r1 != -K_EWOULDBLOCK) {
            testFail("%s: write0 %lld, recv %lld (want %lld, -EWOULDBLOCK)",
                     label, (long long)w, (long long)r1, (long long)wantW);
        }
    };

    // Connected datagram pair, valid buffer: one empty datagram
    // per zero-length write.
    {
        Pair p;
        if (makePair(2 /*SOCK_DGRAM*/, p)) {
            S64 w = write0(p.tx, bufAt);
            expectEmptyDatagram(p, w, "dgram write0 valid buffer");
            w = write0(p.tx, bufAt);
            expectEmptyDatagram(p, w, "dgram write0 valid buffer, second");
            closePair(p);
        }
    }
    // NULL and unmapped user buffers queue the empty datagram
    // too: a zero-length import checks only that the address is
    // a user address.
    {
        Pair p;
        if (makePair(2, p)) {
            S64 w = write0(p.tx, 0);
            expectEmptyDatagram(p, w, "dgram write0 NULL buffer");
            w = write0(p.tx, unmapped);
            expectEmptyDatagram(p, w, "dgram write0 unmapped buffer");
            closePair(p);
        }
    }
    // A buffer address outside the user range answers -EFAULT
    // and queues nothing.
    {
        Pair p;
        if (makePair(2, p)) {
            S64 w = write0(p.tx, 0xFFFFFFFFFFFFFFFFULL);
            expectNothingQueued(p, w, -K_EFAULT, "dgram write0 buffer -1");
            closePair(p);
        }
    }
    // The descriptor checks run before the queue: an unknown
    // descriptor answers -EBADF (the base answered 0).
    if (write0(9999, bufAt) != -K_EBADF) {
        testFail("write0 on an unknown descriptor: want -EBADF");
    }
    // A wrong-mode descriptor answers the write family's mode
    // answer and queues nothing; a write-only descriptor over
    // the same socket queues the empty datagram.
    {
        Pair p;
        if (makePair(2, p)) {
            KFileDescriptorPtr txDesc = process->getFileDescriptor((FD)p.tx);
            if (!txDesc) {
                testFail("write-zero-length test could not fetch the pair descriptor");
            } else {
                KFileDescriptorPtr roDesc = process->allocFileDescriptor(txDesc->kobject, K_O_RDONLY, 0, -1, 0);
                S64 w = write0(roDesc->handle, bufAt);
                expectNothingQueued(p, w, -K_EINVAL, "dgram write0 read-only descriptor");
                process->close(roDesc->handle);
                KFileDescriptorPtr woDesc = process->allocFileDescriptor(txDesc->kobject, K_O_WRONLY, 0, -1, 0);
                w = write0(woDesc->handle, bufAt);
                expectEmptyDatagram(p, w, "dgram write0 write-only descriptor");
                process->close(woDesc->handle);
            }
            closePair(p);
        }
    }
    // An unconnected datagram socket answers -ENOTCONN.
    {
        S32 fd = (S32)invoke(41, 1, 2 /*SOCK_DGRAM*/, 0, 0);
        if (fd < 0) {
            testFail("write-zero-length test could not create a datagram socket");
        } else {
            if (write0(fd, bufAt) != -K_ENOTCONN) {
                testFail("write0 on an unconnected datagram socket: want -ENOTCONN");
            }
            process->close(fd);
        }
    }
    // A stream socket queues nothing and answers 0.
    {
        Pair p;
        if (makePair(1 /*SOCK_STREAM*/, p)) {
            S64 w = write0(p.tx, bufAt);
            expectNothingQueued(p, w, 0, "stream write0");
            closePair(p);
        }
    }
    // A stream socket whose peer is gone answers -EPIPE. The
    // socket layer keys its broken-pipe split (a clean -EPIPE
    // for a 64-bit thread vs the 32-bit SIGPIPE path) on the
    // current thread's process width, so flag the shared test
    // process 64-bit for this case, as a real 64-bit guest
    // process would be (the testDevInputJoystick pattern).
    {
        Pair p;
        if (makePair(1, p)) {
            process->close(p.rx);
            bool was64 = process->is64Bit;
            process->is64Bit = true;
            S64 w0 = write0(p.tx, bufAt);
            S64 w1 = invoke(1, (U64)(U32)p.tx, bufAt, 1, 0);
            process->is64Bit = was64;
            if (w0 != -K_EPIPE || w1 != -K_EPIPE) {
                testFail("stream socket with a closed peer: write0 %lld, write1 %lld (want -EPIPE, -EPIPE)",
                         (long long)w0, (long long)w1);
            }
            process->close(p.tx);
        }
    }
    // A datagram socket whose peer is gone: the object is
    // destroyed with the descriptor, so the write answers
    // -ENOTCONN, the same answer the nonzero write gives on
    // this tree (the host's one-shot -ECONNREFUSED for the
    // first send after a peer close is the socket layer's
    // peer accounting for every count, not this dispatcher's
    // zero-count shape).
    {
        Pair p;
        if (makePair(2, p)) {
            process->close(p.rx);
            if (write0(p.tx, bufAt) != -K_ENOTCONN) {
                testFail("write0 on a datagram socket with a closed peer: want -ENOTCONN");
            }
            process->close(p.tx);
        }
    }
}
#endif

// FD-94: 32-bit pwritev (334) / pwritev2 (378). On the base both
// table slots are nullptr, so every call answers ENOSYS. Driven
// through the real int 0x80 dispatch, following the FD-92 readv
// test's staging pattern (guest memory is wiped by
// testNewInstruction, so segments and payloads are staged inside
// invoke()). The host contract is the one measured for the x64
// test above: offset EINVAL ahead of the descriptor check,
// descriptor ahead of the iovcnt range check and the iovec array
// check with the flags check last (FD-100), the partial shape,
// and pwritev2's -1 position mode and RWF_APPEND.
void testPwritev32Dispatch() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    char dirTemplate[] = "/tmp/bw64kpwritev32XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for 32-bit pwritev test");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    KFileDescriptorPtr fileDesc;
    U32 result = process->openFile(B("/"), B("/data.bin"), K_O_RDWR | K_O_CREAT, fileDesc);
    if ((S32)result < 0 || !fileDesc) {
        testFail("32-bit pwritev test could not create /data.bin");
        return;
    }
    const char* text = "0123456789ABCDEFGHIJ";
    fileDesc->kobject->writeNative((U8*)text, 20);
    fileDesc->kobject->seek(0);
    const S32 fd = (S32)fileDesc->handle;
    const std::string hostPath = std::string(dir) + "/data.bin";
    auto hostContent = [&]() {
        std::string out;
        FILE* f = fopen(hostPath.c_str(), "rb");
        if (!f) return out;
        char chunk[4096];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) out.append(chunk, n);
        fclose(f);
        return out;
    };

    constexpr U32 iovAt = TEST_HEAP_ADDRESS + 0x400;
    constexpr U32 buf0 = TEST_HEAP_ADDRESS + 0x800;
    constexpr U32 buf1 = TEST_HEAP_ADDRESS + 0x900;
    constexpr U32 buf2 = TEST_HEAP_ADDRESS + 0xA00;
    U32 stagedBase[8];
    U32 stagedLen[8];
    const char* stagedText[8];
    U32 stagedCount = 0;
    auto setIov = [&](U32 idx, U32 base, U32 len, const char* payload) {
        stagedBase[idx] = base;
        stagedLen[idx] = len;
        stagedText[idx] = payload;
        if (idx + 1 > stagedCount) stagedCount = idx + 1;
    };
    // Guest memory for the dispatched calls is context.memory: the
    // dispatcher runs on context.cpu.
    auto invoke = [&](U32 nr, S32 callFd, S32 iovcnt, U32 posLo, U32 posHi, U32 flags) -> S32 {
        testNewInstruction(0);
        for (U32 i = 0; i < stagedCount; i++) {
            context.memory->writed(iovAt + i * 8 + 0, stagedBase[i]);
            context.memory->writed(iovAt + i * 8 + 4, stagedLen[i]);
            if (stagedText[i] && stagedLen[i]) {
                context.memory->memcpy(stagedBase[i], (void*)stagedText[i], stagedLen[i]);
            }
        }
        testPushCode8(0xB8); testPushCode32(nr); // mov eax, nr
        testPushCode8(0xCD); testPushCode8(0x80); // int 0x80
        context.cpu->reg[3].u32 = (U32)callFd;  // EBX
        context.cpu->reg[1].u32 = iovAt;        // ECX
        context.cpu->reg[2].u32 = (U32)iovcnt;  // EDX
        context.cpu->reg[6].u32 = posLo;        // ESI
        context.cpu->reg[7].u32 = posHi;        // EDI
        context.cpu->reg[5].u32 = flags;        // EBP
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };

    // 1. Three segments at offset 2 through 334; position untouched.
    setIov(0, buf0, 4, "abcd"); setIov(1, buf1, 4, "efgh"); setIov(2, buf2, 4, "ijkl");
    if (invoke(334, fd, 3, 2, 0, 0) != 12 || hostContent() != "01abcdefghijklEFGHIJ") {
        testFail("32-bit pwritev of three segments did not return 12 with content '%s'", hostContent().c_str());
    }
    if (fileDesc->kobject->getPos() != 0) {
        testFail("32-bit pwritev moved the file position");
    }

    // 2. Validation, in the measured order.
    setIov(0, buf0, 3, "xyz");
    if (invoke(334, 9999, 1, 0xFFFFFFFF, 0xFFFFFFFF, 0) != -K_EINVAL) {
        testFail("32-bit pwritev with a bad descriptor and offset -1 was not -EINVAL");
    }
    if (invoke(334, fd, 1, 0xFFFFFFFF, 0xFFFFFFFF, 0) != -K_EINVAL) {
        testFail("32-bit pwritev with offset -1 was not -EINVAL");
    }
    if (invoke(334, 9999, 1, 0, 0, 0) != -K_EBADF) {
        testFail("32-bit pwritev with a bad descriptor was not -EBADF");
    }
    if (invoke(334, fd, -1, 0, 0, 0) != -K_EINVAL) {
        testFail("32-bit pwritev with iovcnt -1 was not -EINVAL");
    }
    if (invoke(334, fd, 1025, 0, 0, 0) != -K_EINVAL) {
        testFail("32-bit pwritev with iovcnt IOV_MAX+1 was not -EINVAL");
    }
    if (invoke(334, fd, 0, 0, 0, 0) != 0) {
        testFail("32-bit pwritev with iovcnt 0 did not return 0");
    }
    {
        // A null iovec with iovcnt 1: invoke() always passes iovAt,
        // so drive this one by hand.
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(334);
        testPushCode8(0xCD); testPushCode8(0x80);
        context.cpu->reg[3].u32 = (U32)fd;
        context.cpu->reg[1].u32 = 0;
        context.cpu->reg[2].u32 = 1;
        context.cpu->reg[6].u32 = 0;
        context.cpu->reg[7].u32 = 0;
        context.cpu->reg[5].u32 = 0;
        testRunCPU();
        if ((S32)context.cpu->reg[0].u32 != -K_EFAULT) {
            testFail("32-bit pwritev with a null iovec was not -EFAULT");
        }
    }

    // 2b. FD-100: the pwritev2 flags check runs last -- behind the
    // iovcnt range check and the iovec array check. An undefined flag
    // bit combined with an out-of-range iovcnt answers EINVAL, and
    // combined with a null or unmapped iovec array answers EFAULT.
    if (invoke(378, fd, -1, 0, 0, 0x40) != -K_EINVAL) {
        testFail("32-bit pwritev2 with iovcnt -1 and an undefined flag was not -EINVAL");
    }
    if (invoke(378, fd, 1025, 0, 0, 0x40) != -K_EINVAL) {
        testFail("32-bit pwritev2 with iovcnt IOV_MAX+1 and an undefined flag was not -EINVAL");
    }
    {
        // Null and unmapped iovec arrays with an undefined flag:
        // invoke() always passes iovAt, so drive these by hand.
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(378);
        testPushCode8(0xCD); testPushCode8(0x80);
        context.cpu->reg[3].u32 = (U32)fd;
        context.cpu->reg[1].u32 = 0;
        context.cpu->reg[2].u32 = 1;
        context.cpu->reg[6].u32 = 0;
        context.cpu->reg[7].u32 = 0;
        context.cpu->reg[5].u32 = 0x40;
        testRunCPU();
        if ((S32)context.cpu->reg[0].u32 != -K_EFAULT) {
            testFail("32-bit pwritev2 with a null iovec and an undefined flag was not -EFAULT");
        }
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(378);
        testPushCode8(0xCD); testPushCode8(0x80);
        context.cpu->reg[3].u32 = (U32)fd;
        context.cpu->reg[1].u32 = 0x1000;
        context.cpu->reg[2].u32 = 1;
        context.cpu->reg[6].u32 = 0;
        context.cpu->reg[7].u32 = 0;
        context.cpu->reg[5].u32 = 0x40;
        testRunCPU();
        if ((S32)context.cpu->reg[0].u32 != -K_EFAULT) {
            testFail("32-bit pwritev2 with an unmapped iovec and an undefined flag was not -EFAULT");
        }
    }

    // 3. Partial shape: good first segment, bad second segment.
    setIov(0, buf0, 3, "xyz"); setIov(1, 0x1000, 3, nullptr);
    if (invoke(334, fd, 2, 0, 0, 0) != 3 || hostContent().substr(0, 3) != "xyz") {
        testFail("32-bit pwritev with a bad second segment did not return the 3 bytes written");
    }

    // 4. pwritev2 (378): flags, the -1 position mode, RWF_APPEND.
    setIov(0, buf0, 2, "PQ");
    if (invoke(378, fd, 1, 0, 0, 0x40) != -K_EOPNOTSUPP) {
        testFail("32-bit pwritev2 with an undefined flag bit was not -EOPNOTSUPP");
    }
    fileDesc->kobject->seek(5);
    if (invoke(378, fd, 1, 0xFFFFFFFF, 0xFFFFFFFF, 0) != 2 || hostContent().substr(5, 2) != "PQ") {
        testFail("32-bit pwritev2 offset -1 did not write at the file position");
    }
    if (fileDesc->kobject->getPos() != 7) {
        testFail("32-bit pwritev2 offset -1 left the position at %lld, expected 7",
                 (long long)fileDesc->kobject->getPos());
    }
    setIov(0, buf0, 2, "ZZ");
    if (invoke(378, fd, 1, 2, 0, 0x10) != 2 || hostContent().size() != 22 ||
        hostContent().substr(20, 2) != "ZZ") {
        testFail("32-bit pwritev2 RWF_APPEND did not append at end of file");
    }
    if (fileDesc->kobject->getPos() != 7) {
        testFail("32-bit pwritev2 RWF_APPEND moved the file position");
    }

    process->close(fd);
    rmdir(dir);
}

// FD-98: 32-bit preadv (333) / preadv2 (377). On the base both
// table slots are nullptr, so every call answers ENOSYS. Driven
// through the real int 0x80 dispatch, following the FD-94
// pwritev test's staging pattern (guest memory is wiped by
// testNewInstruction, so the iovec array is staged inside
// invoke()). The host contract was measured fresh on the box's
// 6.17 kernel (test-results/fd98/hostref-preadv*.c):
//  - segments fill in order at successive offsets; the file
//    position does not move (preadv2's offset -1 is the
//    exception: it reads at the position and advances it); a
//    short read ends the walk; a zero-length segment is
//    skipped; a later segment's failure returns the bytes
//    already read; a failing first segment returns the error.
//  - exactly one IN_ACCESS per successful call, including
//    0-byte successes (at EOF, with iovcnt 0, with
//    all-zero-length segments); failed calls generate none
//    (FD-83's contract).
//  - validation order: a negative offset is EINVAL ahead of
//    the descriptor check (preadv2: only below -1; -1 is the
//    position mode, so a bad descriptor with offset -1
//    answers EBADF); a descriptor that cannot be read answers
//    EBADF, ahead of the iovcnt range check (EINVAL outside
//    [0, IOV_MAX]), the iovec array check (EFAULT for a
//    null/unmapped array with iovcnt > 0) and the preadv2
//    flags check (EOPNOTSUPP outside the defined RWF mask
//    0x3f), which runs last.
void testPreadv32Dispatch() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    char dirTemplate[] = "/tmp/bw64kpreadv32XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for 32-bit preadv test");
        return;
    }
    if (mkdir((std::string(dir) + "/sub").c_str(), 0755) != 0) {
        testFail("32-bit preadv test could not create its subdir");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    KFileDescriptorPtr fileDesc;
    U32 result = process->openFile(B("/"), B("/data.bin"), K_O_RDWR | K_O_CREAT, fileDesc);
    if ((S32)result < 0 || !fileDesc) {
        testFail("32-bit preadv test could not create /data.bin");
        return;
    }
    const char* text = "0123456789ABCDEFGHIJ";
    fileDesc->kobject->writeNative((U8*)text, 20);
    fileDesc->kobject->seek(0);
    const S32 fd = (S32)fileDesc->handle;

    std::shared_ptr<KInotify> inotify = std::make_shared<KInotify>(false);
    KInotify::registerInstance(inotify);
    KFileDescriptorPtr inotifyDesc = process->allocFileDescriptor(inotify, K_O_RDONLY, 0, -1, 0);
    if (!inotifyDesc || !inotifyDesc->kobject) {
        testFail("32-bit preadv test could not create its inotify instance");
        return;
    }
    S32 wd = (S32)process->inotify_add_watch((S32)inotifyDesc->handle, B("/data.bin"), 0xfff);
    if (wd < 1) {
        testFail("32-bit preadv test could not watch /data.bin");
        return;
    }
    auto drainMasks = [&]() {
        std::vector<U32> masks;
        U8 buffer[8192];
        S32 total = (S32)inotifyDesc->kobject->readNative(buffer, sizeof(buffer));
        S32 offset = 0;
        while (total > 0 && offset + 16 <= total) {
            U32 mask, nameLen;
            memcpy(&mask, buffer + offset + 4, 4);
            memcpy(&nameLen, buffer + offset + 12, 4);
            masks.push_back(mask);
            offset += 16 + (S32)nameLen;
        }
        return masks;
    };
    auto accessCount = [&](const std::vector<U32>& masks) {
        int n = 0;
        for (U32 m : masks) if (m & K_IN_ACCESS) n++;
        return n;
    };
    drainMasks(); // setup noise

    constexpr U32 iovAt = TEST_HEAP_ADDRESS + 0x400;
    constexpr U32 buf0 = TEST_HEAP_ADDRESS + 0x800;
    constexpr U32 buf1 = TEST_HEAP_ADDRESS + 0x900;
    constexpr U32 buf2 = TEST_HEAP_ADDRESS + 0xA00;
    U32 stagedBase[8];
    U32 stagedLen[8];
    U32 stagedCount = 0;
    auto setIov = [&](U32 idx, U32 base, U32 len) {
        stagedBase[idx] = base;
        stagedLen[idx] = len;
        if (idx + 1 > stagedCount) stagedCount = idx + 1;
    };
    // Guest memory for the dispatched calls is context.memory: the
    // dispatcher runs on context.cpu. Destination buffers are
    // zeroed at staging so a segment the call never touches reads
    // back as zeros, never as a previous case's bytes.
    auto invoke = [&](U32 nr, S32 callFd, S32 iovcnt, U32 posLo, U32 posHi, U32 flags) -> S32 {
        testNewInstruction(0);
        static const U8 zeros[256] = {};
        for (U32 i = 0; i < stagedCount; i++) {
            context.memory->writed(iovAt + i * 8 + 0, stagedBase[i]);
            context.memory->writed(iovAt + i * 8 + 4, stagedLen[i]);
            if (stagedLen[i] && stagedBase[i] >= TEST_HEAP_ADDRESS) {
                U32 n = stagedLen[i] <= 256 ? stagedLen[i] : 256;
                context.memory->memcpy(stagedBase[i], (void*)zeros, n);
            }
        }
        testPushCode8(0xB8); testPushCode32(nr); // mov eax, nr
        testPushCode8(0xCD); testPushCode8(0x80); // int 0x80
        context.cpu->reg[3].u32 = (U32)callFd;  // EBX
        context.cpu->reg[1].u32 = iovAt;        // ECX
        context.cpu->reg[2].u32 = (U32)iovcnt;  // EDX
        context.cpu->reg[6].u32 = posLo;        // ESI
        context.cpu->reg[7].u32 = posHi;        // EDI
        context.cpu->reg[5].u32 = flags;        // EBP
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    auto segText = [&](U32 at, U32 len) {
        std::string s;
        for (U32 i = 0; i < len; i++) s += (char)context.memory->readb(at + i);
        return s;
    };

    // 1. Three segments at offset 2 through 333; position
    // untouched; exactly one IN_ACCESS.
    setIov(0, buf0, 4); setIov(1, buf1, 4); setIov(2, buf2, 4);
    if (invoke(333, fd, 3, 2, 0, 0) != 12 || segText(buf0, 4) != "2345" ||
        segText(buf1, 4) != "6789" || segText(buf2, 4) != "ABCD") {
        testFail("32-bit preadv of three segments did not return 12 with contents '%s' '%s' '%s'",
                 segText(buf0, 4).c_str(), segText(buf1, 4).c_str(), segText(buf2, 4).c_str());
    }
    if (fileDesc->kobject->getPos() != 0) {
        testFail("32-bit preadv moved the file position");
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("32-bit preadv must generate exactly one IN_ACCESS");
    }

    // 2. A short read across the EOF boundary ends the walk.
    setIov(0, buf0, 4); setIov(1, buf1, 4);
    if (invoke(333, fd, 2, 18, 0, 0) != 2 || segText(buf0, 4) != std::string("IJ\0\0", 4) ||
        segText(buf1, 4) != std::string(4, '\0')) {
        testFail("32-bit preadv across EOF did not return 2 with the tail in segment 0 only");
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("short 32-bit preadv must generate exactly one IN_ACCESS");
    }

    // 3. At EOF the call returns 0 and still generates IN_ACCESS
    // once (the vectored path's measured host behaviour).
    setIov(0, buf0, 4);
    if (invoke(333, fd, 1, 1000, 0, 0) != 0) {
        testFail("32-bit preadv at EOF did not return 0");
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("32-bit preadv at EOF must still generate one IN_ACCESS");
    }

    // 4. iovcnt 0 returns 0 and generates IN_ACCESS once.
    if (invoke(333, fd, 0, 0, 0, 0) != 0) {
        testFail("32-bit preadv with iovcnt 0 did not return 0");
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("32-bit preadv with iovcnt 0 must generate one IN_ACCESS");
    }

    // 5. All-zero-length segments return 0 with one IN_ACCESS.
    setIov(0, buf0, 0); setIov(1, buf1, 0);
    if (invoke(333, fd, 2, 0, 0, 0) != 0) {
        testFail("32-bit preadv with all-zero-length segments did not return 0");
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("32-bit preadv with all-zero-length segments must generate one IN_ACCESS");
    }

    // 6. A zero-length segment in the middle is skipped.
    setIov(0, buf0, 4); setIov(1, buf1, 0); setIov(2, buf2, 4);
    if (invoke(333, fd, 3, 0, 0, 0) != 8 || segText(buf0, 4) != "0123" || segText(buf2, 4) != "4567") {
        testFail("32-bit preadv with a zero-length segment did not return 8 with contents '%s' '%s'",
                 segText(buf0, 4).c_str(), segText(buf2, 4).c_str());
    }
    drainMasks();

    // 7. pos_h is honored: offset 2^32 + 2 is past EOF (a
    // low-word-only implementation would read at offset 2).
    setIov(0, buf0, 4);
    if (invoke(333, fd, 1, 2, 1, 0) != 0) {
        testFail("32-bit preadv at offset 2^32+2 did not return 0 (pos_h ignored?)");
    }
    drainMasks();

    // 8. Partial shape: a good first segment and a bad second
    // segment return the bytes already read; a bad first segment
    // returns the error itself.
    setIov(0, buf0, 4); setIov(1, 0x1000, 4);
    if (invoke(333, fd, 2, 0, 0, 0) != 4 || segText(buf0, 4) != "0123") {
        testFail("32-bit preadv with a bad second segment did not return the 4 bytes read");
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("partial 32-bit preadv must generate exactly one IN_ACCESS");
    }
    setIov(0, 0x1000, 4);
    if (invoke(333, fd, 1, 0, 0, 0) != -K_EFAULT) {
        testFail("32-bit preadv with a bad first segment was not -EFAULT");
    }
    if (accessCount(drainMasks()) != 0) {
        testFail("failed 32-bit preadv generated IN_ACCESS");
    }

    // 9. Validation, in the measured order; none of it generates
    // IN_ACCESS.
    setIov(0, buf0, 4);
    if (invoke(333, fd, -1, 0, 0, 0) != -K_EINVAL) {
        testFail("32-bit preadv with iovcnt -1 was not -EINVAL");
    }
    if (invoke(333, fd, 1025, 0, 0, 0) != -K_EINVAL) {
        testFail("32-bit preadv with iovcnt IOV_MAX+1 was not -EINVAL");
    }
    if (invoke(333, fd, 1, 0xFFFFFFFF, 0xFFFFFFFF, 0) != -K_EINVAL) {
        testFail("32-bit preadv with offset -1 was not -EINVAL");
    }
    if (invoke(333, fd, 1, 0xFFFFFFFE, 0xFFFFFFFF, 0) != -K_EINVAL) {
        testFail("32-bit preadv with offset -2 was not -EINVAL");
    }
    if (invoke(333, 9999, 1, 0xFFFFFFFF, 0xFFFFFFFF, 0) != -K_EINVAL) {
        testFail("32-bit preadv with a bad descriptor and offset -1 was not -EINVAL (offset precedes descriptor)");
    }
    if (invoke(333, 9999, 1, 0, 0, 0) != -K_EBADF) {
        testFail("32-bit preadv with a bad descriptor was not -EBADF");
    }
    if (invoke(377, 9999, 1, 0xFFFFFFFF, 0xFFFFFFFF, 0) != -K_EBADF) {
        testFail("32-bit preadv2 with a bad descriptor and offset -1 was not -EBADF (the -1 position mode is legal)");
    }
    if (invoke(377, 9999, 1, 0xFFFFFFFE, 0xFFFFFFFF, 0) != -K_EINVAL) {
        testFail("32-bit preadv2 with a bad descriptor and offset -2 was not -EINVAL");
    }
    if (invoke(377, fd, -1, 0, 0, 0x40) != -K_EINVAL) {
        testFail("32-bit preadv2 with iovcnt -1 and an undefined flag bit was not -EINVAL (range precedes flags)");
    }
    if (invoke(377, fd, 1, 0, 0, 0x40) != -K_EOPNOTSUPP) {
        testFail("32-bit preadv2 with an undefined flag bit was not -EOPNOTSUPP");
    }
    {
        // A null iovec: invoke() always passes iovAt, so drive
        // these two by hand.
        auto invokeNullIov = [&](U32 nr, U32 flags) -> S32 {
            testNewInstruction(0);
            testPushCode8(0xB8); testPushCode32(nr);
            testPushCode8(0xCD); testPushCode8(0x80);
            context.cpu->reg[3].u32 = (U32)fd;
            context.cpu->reg[1].u32 = 0;
            context.cpu->reg[2].u32 = 1;
            context.cpu->reg[6].u32 = 0;
            context.cpu->reg[7].u32 = 0;
            context.cpu->reg[5].u32 = flags;
            testRunCPU();
            return (S32)context.cpu->reg[0].u32;
        };
        if (invokeNullIov(333, 0) != -K_EFAULT) {
            testFail("32-bit preadv with a null iovec was not -EFAULT");
        }
        if (invokeNullIov(377, 0x40) != -K_EFAULT) {
            testFail("32-bit preadv2 with a null iovec and an undefined flag bit was not -EFAULT (array precedes flags)");
        }
    }
    {
        // A descriptor that cannot be read answers EBADF, ahead
        // of the iovcnt-0 success return and of the flags check.
        KFileDescriptorPtr wdesc;
        U32 wr = process->openFile(B("/"), B("/data.bin"), K_O_WRONLY, wdesc);
        if ((S32)wr < 0 || !wdesc) {
            testFail("32-bit preadv test could not open /data.bin write-only");
            return;
        }
        if (invoke(333, (S32)wdesc->handle, 1, 0, 0, 0) != -K_EBADF) {
            testFail("32-bit preadv on a write-only descriptor was not -EBADF");
        }
        if (invoke(333, (S32)wdesc->handle, 0, 0, 0, 0) != -K_EBADF) {
            testFail("32-bit preadv on a write-only descriptor with iovcnt 0 was not -EBADF");
        }
        if (invoke(377, (S32)wdesc->handle, 1, 0, 0, 0x40) != -K_EBADF) {
            testFail("32-bit preadv2 on a write-only descriptor with an undefined flag bit was not -EBADF (mode precedes flags)");
        }
        process->close(wdesc->handle);
    }
    if (accessCount(drainMasks()) != 0) {
        testFail("rejected 32-bit preadv calls generated IN_ACCESS");
    }

    // 10. preadv2 (377): defined flags are accepted on a read
    // (0x20 included — the read side's mask is 0x3f on this
    // kernel), and offset -1 is the file-position mode.
    setIov(0, buf0, 4);
    if (invoke(377, fd, 1, 4, 0, 0) != 4 || segText(buf0, 4) != "4567") {
        testFail("32-bit preadv2 with flags 0 did not return 4 with content '%s'", segText(buf0, 4).c_str());
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("32-bit preadv2 must generate exactly one IN_ACCESS");
    }
    if (invoke(377, fd, 1, 4, 0, 0x8) != 4) { // RWF_NOWAIT
        testFail("32-bit preadv2 with RWF_NOWAIT failed");
    }
    drainMasks();
    if (invoke(377, fd, 1, 4, 0, 0x20) != 4) { // RWF_NOAPPEND: defined, meaningless on a read
        testFail("32-bit preadv2 with flag 0x20 failed");
    }
    drainMasks();
    fileDesc->kobject->seek(5);
    if (invoke(377, fd, 1, 0xFFFFFFFF, 0xFFFFFFFF, 0) != 4 || segText(buf0, 4) != "5678") {
        testFail("32-bit preadv2 offset -1 did not read at the file position");
    }
    if (fileDesc->kobject->getPos() != 9) {
        testFail("32-bit preadv2 offset -1 left the position at %lld, expected 9",
                 (long long)fileDesc->kobject->getPos());
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("32-bit preadv2 offset -1 must generate exactly one IN_ACCESS");
    }
    fileDesc->kobject->seek(0);

    // 11. A directory descriptor follows the positioned core's
    // answer (-EISDIR, the host's answer too).
    {
        KFileDescriptorPtr ddesc;
        U32 dr = process->openFile(B("/"), B("/sub"), K_O_RDONLY, ddesc);
        if ((S32)dr < 0 || !ddesc) {
            testFail("32-bit preadv test could not open /sub");
        } else {
            setIov(0, buf0, 4);
            if (invoke(333, (S32)ddesc->handle, 1, 0, 0, 0) != -K_EISDIR) {
                testFail("32-bit preadv on a directory was not -EISDIR");
            }
            process->close(ddesc->handle);
        }
    }

    process->close((S32)inotifyDesc->handle);
    process->close(fd);
    rmdir((std::string(dir) + "/sub").c_str());
    rmdir(dir);
}

// FD-96: the 32-bit vectored write walk (KObject::writev, reached
// through KProcess::writev) returned a later segment's error even
// when earlier segments had already written; the host returns the
// bytes written (measured: a good 3-byte first segment and a bad
// second segment return 3). The walk's one caller-visible failure
// shape is pinned here with an eventfd, whose counter overflow
// fails a segment deterministically (-EAGAIN) once earlier
// segments have landed.
void testWritevPartialError() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    KThread* thread = context.thread;
    std::shared_ptr<KEvent> event = std::make_shared<KEvent>();
    event->blocking = false;
    KFileDescriptorPtr eventDesc = process->allocFileDescriptor(event, K_O_RDWR, 0, -1, 0);
    if (!eventDesc || !eventDesc->kobject) {
        testFail("writev partial test could not create its eventfd");
        return;
    }
    const S32 fd = (S32)eventDesc->handle;
    constexpr U32 iovAt = TEST_HEAP_ADDRESS + 0x400;
    constexpr U32 val0 = TEST_HEAP_ADDRESS + 0x800;
    constexpr U32 val1 = TEST_HEAP_ADDRESS + 0x900;
    auto stage = [&](U64 v0, U64 v1) {
        thread->memory->writed(iovAt + 0, val0);
        thread->memory->writed(iovAt + 4, 8);
        thread->memory->writed(iovAt + 8, val1);
        thread->memory->writed(iovAt + 12, 8);
        thread->memory->writed(val0 + 0, (U32)v0);
        thread->memory->writed(val0 + 4, (U32)(v0 >> 32));
        thread->memory->writed(val1 + 0, (U32)v1);
        thread->memory->writed(val1 + 4, (U32)(v1 >> 32));
    };

    // Partial: the first segment lands (counter 10 -> 15), the
    // second overflows the counter and fails. The answer is the
    // 8 bytes already written, not the second segment's error.
    event->counter = 10;
    stage(5, 0xFFFFFFFFFFFFFFFFULL);
    U32 r = process->writev(thread, fd, iovAt, 2);
    if ((S32)r != 8) {
        testFail("writev with a failing second segment returned %d, expected the 8 bytes already written", (S32)r);
    }
    if (event->counter != 15) {
        testFail("writev partial case left the eventfd counter at %llu, expected 15",
                 (unsigned long long)event->counter);
    }

    // A failing first segment still returns its error: nothing was
    // written, so there is no byte count to report.
    event->counter = 0xFFFFFFFFFFFFFFF0ULL;
    stage(0xFF, 1);
    r = process->writev(thread, fd, iovAt, 2);
    if ((S32)r != -K_EAGAIN) {
        testFail("writev with a failing first segment returned %d, expected -EAGAIN", (S32)r);
    }

    // Every segment lands: the total byte count.
    event->counter = 0;
    stage(5, 7);
    r = process->writev(thread, fd, iovAt, 2);
    if ((S32)r != 16 || event->counter != 12) {
        testFail("writev of two good segments returned %d with counter %llu, expected 16 and 12",
                 (S32)r, (unsigned long long)event->counter);
    }

    process->close(fd);
}

// Native-suite variant: same event contract driven through the
// width-agnostic KProcess API (the x64 test above drives the syscall
// layer; the KProcess methods are what both syscall layers forward to).
void testInotifyEventsProcessApi() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    char dirTemplate[] = "/tmp/bw64kinotifyXXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for inotify process-api test");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    // Guest-visible gate pin: init1 is enabled (the FD-38 enablement
    // park is fixed) and hands out a descriptor that takes watches
    // through the same KProcess path the syscall layers forward to.
    S32 initFd = (S32)process->inotify_init1(0);
    if (initFd < 0) {
        testFail("KProcess::inotify_init1 did not return a descriptor with the FD-38 gate open: %d", initFd);
        return;
    }
    if ((S32)process->inotify_add_watch(initFd, B("/"), 0xfff) < 1) {
        testFail("inotify_add_watch on the inotify_init1 descriptor failed");
        return;
    }
    process->close(initFd);
    // The machinery itself is exercised directly: build the instance
    // the way inotify_init1 would (0x800 == K_O_NONBLOCK, so
    // non-blocking) and register it; everything below then drives the
    // real KInotify add/read path.
    std::shared_ptr<KInotify> inotify = std::make_shared<KInotify>(false);
    KInotify::registerInstance(inotify);
    KFileDescriptorPtr descriptor = process->allocFileDescriptor(inotify, K_O_RDONLY, 0, -1, 0);
    if (!descriptor || !descriptor->kobject) {
        testFail("allocFileDescriptor for the inotify instance failed");
        return;
    }
    S32 fd = (S32)descriptor->handle;
    S32 wd = (S32)process->inotify_add_watch(fd, B("/"), 0xfff /* IN_ALL_EVENTS */);
    if (wd < 1) {
        testFail("KProcess::inotify_add_watch failed: %d", wd);
        return;
    }

    KFileDescriptorPtr created;
    U32 result = process->openFile(B("/"), B("/k3bfile.txt"), K_O_WRONLY | K_O_CREAT | K_O_TRUNC, created);
    if ((S32)result < 0 || !created) {
        testFail("openFile(O_CREAT) failed: %d", (S32)result);
        return;
    }
    const char* text = "hello";
    U32 textAt = TEST_HEAP_ADDRESS + 0x200;
    context.thread->memory->memcpy(textAt, (void*)text, 5);
    std::shared_ptr<KFile> file = std::dynamic_pointer_cast<KFile>(created->kobject);
    file->write(context.thread, textAt, 5);
    process->close(created->handle);
    file.reset();
    created.reset();
    if ((S32)process->rename(B("/k3bfile.txt"), B("/k3brenamed.txt")) < 0) {
        testFail("rename failed");
        return;
    }
    if ((S32)process->unlinkFile(B("/k3brenamed.txt")) < 0) {
        testFail("unlink failed");
        return;
    }

    U8 buffer[4096];
    S32 total = (S32)descriptor->kobject->readNative(buffer, sizeof(buffer));
    if (total <= 0) {
        testFail("no inotify events delivered through the KProcess API: %d", total);
        return;
    }
    bool sawCreate = false, sawModify = false, sawCloseWrite = false, sawDelete = false;
    bool sawMovedFrom = false, sawMovedTo = false;
    U32 fromCookie = 0, toCookie = 0;
    S32 offset = 0;
    while (offset + 16 <= total) {
        S32 eventWd;
        U32 mask, cookie, nameLen;
        memcpy(&eventWd, buffer + offset, 4);
        memcpy(&mask, buffer + offset + 4, 4);
        memcpy(&cookie, buffer + offset + 8, 4);
        memcpy(&nameLen, buffer + offset + 12, 4);
        if (eventWd == wd && nameLen >= 12 && memcmp(buffer + offset + 16, "k3bfile.txt", 11) == 0 && mask & 0x100) sawCreate = true;
        if (eventWd == wd && mask & 0x2) sawModify = true;
        if (eventWd == wd && mask & 0x8) sawCloseWrite = true;
        if (eventWd == wd && mask & 0x40) { sawMovedFrom = true; fromCookie = cookie; }
        if (eventWd == wd && mask & 0x80) { sawMovedTo = true; toCookie = cookie; }
        if (eventWd == wd && mask & 0x200) sawDelete = true;
        offset += 16 + (S32)nameLen;
    }
    if (!sawCreate) testFail("no IN_CREATE through the KProcess API");
    if (!sawModify) testFail("no IN_MODIFY through the KProcess API");
    if (!sawCloseWrite) testFail("no IN_CLOSE_WRITE through the KProcess API");
    if (!sawMovedFrom || !sawMovedTo || !fromCookie || fromCookie != toCookie) {
        testFail("rename cookie pair missing through the KProcess API (from=%d to=%d)", fromCookie, toCookie);
    }
    if (!sawDelete) testFail("no IN_DELETE through the KProcess API");

    if ((S32)process->inotify_rm_watch(fd, wd) != 0) {
        testFail("KProcess::inotify_rm_watch failed");
        return;
    }
    total = (S32)descriptor->kobject->readNative(buffer, sizeof(buffer));
    bool sawIgnored = false;
    offset = 0;
    while (total > 0 && offset + 16 <= total) {
        U32 mask, nameLen;
        memcpy(&mask, buffer + offset + 4, 4);
        memcpy(&nameLen, buffer + offset + 12, 4);
        if (mask & 0x8000) sawIgnored = true;
        offset += 16 + (S32)nameLen;
    }
    if (!sawIgnored) {
        testFail("rm_watch produced no IN_IGNORED through the KProcess API");
    }
    process->close(fd);
}

// FD-58 / FD-59 / FD-60 kernel semantics, driven through the KProcess
// API (plus the x64 syscall layer for fchmod/fchmodat, whose 64-bit
// handlers used to answer success without doing anything). Host-kernel
// reference behaviour (measured on the box's 6.17 kernel with dedicated
// reference programs, preserved under test-results/fd58-60/):
//  - chmod stores the mode verbatim, setuid/setgid/sticky bits
//    included; fchmod does the same by descriptor; chmod generates
//    IN_ATTRIB on the file's watchers; fchmodat with
//    AT_SYMLINK_NOFOLLOW on a symlink fails with ENOTSUP.
//  - read/pread/readv and getdents generate IN_ACCESS (a plain
//    read/pread that returns 0 bytes does not; the vectored calls
//    differ there -- FD-83); writes through memory mappings
//    generate no IN_MODIFY at write, msync, fsync, munmap, or close.
//  - link() gives both names one inode and one link count, generates
//    IN_ATTRIB on the source and IN_CREATE in the target directory,
//    and a write through one name is visible through the other;
//    unlinking one name drops the count to 1 and keeps the content.
void testChmodAccessHardLink() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    char dirTemplate[] = "/tmp/bw64kchmodXXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for chmod/access/link test");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    std::shared_ptr<KInotify> inotify = std::make_shared<KInotify>(false);
    KInotify::registerInstance(inotify);
    KFileDescriptorPtr descriptor = process->allocFileDescriptor(inotify, K_O_RDONLY, 0, -1, 0);
    if (!descriptor || !descriptor->kobject) {
        testFail("allocFileDescriptor for the inotify instance failed");
        return;
    }
    S32 ifd = (S32)descriptor->handle;
    S32 wdDir = (S32)process->inotify_add_watch(ifd, B("/"), 0xfff);
    if (wdDir < 1) {
        testFail("inotify_add_watch on / failed: %d", wdDir);
        return;
    }

    struct Ev { S32 wd; U32 mask; BString name; };
    auto drainEvents = [&]() {
        std::vector<Ev> events;
        U8 buffer[8192];
        S32 total = (S32)descriptor->kobject->readNative(buffer, sizeof(buffer));
        S32 offset = 0;
        while (total > 0 && offset + 16 <= total) {
            Ev e;
            U32 nameLen;
            memcpy(&e.wd, buffer + offset, 4);
            memcpy(&e.mask, buffer + offset + 4, 4);
            memcpy(&nameLen, buffer + offset + 12, 4);
            if (nameLen) {
                e.name = BString::copy((const char*)(buffer + offset + 16), (S32)nameLen);
                // names are NUL-padded to the record length
                while (e.name.length() && e.name.charAt(e.name.length() - 1) == 0) {
                    e.name = e.name.substr(0, e.name.length() - 1);
                }
            }
            events.push_back(e);
            offset += 16 + (S32)nameLen;
        }
        return events;
    };
    auto hasEvent = [](const std::vector<Ev>& events, S32 wd, U32 bit, const char* name) {
        for (auto& e : events) {
            if (e.wd == wd && (e.mask & bit) && e.name == name) return true;
        }
        return false;
    };

    // struct stat64 (i386 large-file layout written by KSystem::writeStat
    // with is64=true): ino @12 (also @88), mode @16, nlink @20.
    constexpr U32 statAt = TEST_HEAP_ADDRESS + 0x400;
    struct StatInfo { U32 mode; U32 ino; U32 nlink; };
    auto statOf = [&](const char* path, StatInfo& out) -> bool {
        if ((S32)process->stat64(B(path), statAt) < 0) return false;
        out.ino = context.thread->memory->readd(statAt + 12);
        out.mode = context.thread->memory->readd(statAt + 16);
        out.nlink = context.thread->memory->readd(statAt + 20);
        return true;
    };

    // Create /a.txt with known content.
    KFileDescriptorPtr created;
    U32 result = process->openFile(B("/"), B("/a.txt"), K_O_RDWR | K_O_CREAT, created);
    if ((S32)result < 0 || !created) {
        testFail("openFile(O_CREAT) for /a.txt failed: %d", (S32)result);
        return;
    }
    std::shared_ptr<KFile> file = std::dynamic_pointer_cast<KFile>(created->kobject);
    const char* text = "hello";
    U32 textAt = TEST_HEAP_ADDRESS + 0x200;
    context.thread->memory->memcpy(textAt, (void*)text, 5);
    file->write(context.thread, textAt, 5);
    S32 fileFd = (S32)created->handle;

    StatInfo st;
    if (!statOf("/a.txt", st)) {
        testFail("stat64 of /a.txt failed");
        return;
    }
    const U32 inoA = st.ino;
    if (st.nlink != 1) {
        testFail("fresh file must have nlink 1, got %d", st.nlink);
    }

    // ---- FD-58: chmod applies the mode, verbatim including suid.
    if ((S32)process->chmod(B("/a.txt"), 0600) != 0) {
        testFail("KProcess::chmod returned an error");
        return;
    }
    if (!statOf("/a.txt", st) || (st.mode & 07777) != 0600) {
        testFail("chmod 0600 did not change the reported mode (now %04o)", st.mode & 07777);
    }
    process->chmod(B("/a.txt"), 04755);
    if (!statOf("/a.txt", st) || (st.mode & 07777) != 04755) {
        testFail("chmod 04755 did not preserve the setuid bit (now %04o)", st.mode & 07777);
    }
    if ((S32)process->chmod(B("/missing.txt"), 0600) != -K_ENOENT) {
        testFail("chmod on a missing path must return -ENOENT");
    }

    // Watch the file itself; everything below is measured from a clean queue.
    S32 wdFile = (S32)process->inotify_add_watch(ifd, B("/a.txt"), 0xfff);
    if (wdFile < 1) {
        testFail("inotify_add_watch on /a.txt failed: %d", wdFile);
        return;
    }
    drainEvents();
    // A chmod after the watch exists must surface as IN_ATTRIB on
    // both watches (also exercised through fchmod/fchmodat below on
    // the 64-bit layer).
    process->chmod(B("/a.txt"), 0644);

#ifdef BOXEDWINE_GUEST_X64
    // ---- FD-58: fchmod / fchmodat through the real x64 syscall layer.
    {
        if (!process->memory64) process->memory64 = new KMemory64(process.get());
        KMemory64* memory64 = process->memory64;
        constexpr U64 page = 0x77000000ULL;
        if (!memory64->getPageFlags(page >> K64_PAGE_SHIFT) &&
            memory64->mmapAnonymousFixed(page, K64_PAGE_SIZE, 3) != page) {
            testFail("fchmod fixture could not map its page");
            return;
        }
        CPU64 cpu(memory64);
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
        if (invoke(91, (U64)(U32)fileFd, 0640, 0, 0) != 0) {
            testFail("x64 fchmod syscall did not return 0");
        }
        if (!statOf("/a.txt", st) || (st.mode & 07777) != 0640) {
            testFail("fchmod 0640 did not change the reported mode (now %04o)", st.mode & 07777);
        }
        const char* rel = "a.txt";
        memory64->memcpyToGuest(page, rel, 6);
        constexpr U64 AT_FDCWD = (U64)-100;
        if (invoke(268, AT_FDCWD, page, 0604, 0) != 0) {
            testFail("x64 fchmodat syscall did not return 0");
        }
        if (!statOf("/a.txt", st) || (st.mode & 07777) != 0604) {
            testFail("fchmodat 0604 did not change the reported mode (now %04o)", st.mode & 07777);
        }
        // fchmodat AT_SYMLINK_NOFOLLOW on a symlink: ENOTSUP on the host.
        process->symlink(B("/a.txt"), B("/l.txt"));
        const char* lname = "l.txt";
        memory64->memcpyToGuest(page, lname, 6);
        S64 rc = invoke(268, AT_FDCWD, page, 0600, 0x100);
        if (rc != -K_ENOTSUP) {
            testFail("fchmodat AT_SYMLINK_NOFOLLOW on a symlink must return -ENOTSUP, got %lld", (long long)rc);
        }
        if (!statOf("/a.txt", st) || (st.mode & 07777) != 0604) {
            testFail("the NOFOLLOW fchmodat must not have touched the target (now %04o)", st.mode & 07777);
        }
        process->unlinkFile(B("/l.txt"));
    }
#endif

    // ---- FD-58: chmod/fchmod generated IN_ATTRIB (host: yes, both).
    {
        auto events = drainEvents();
        if (!hasEvent(events, wdFile, 0x4, "")) {
            testFail("chmod/fchmod produced no IN_ATTRIB on the file watch");
        }
        if (!hasEvent(events, wdDir, 0x4, "a.txt")) {
            testFail("chmod/fchmod produced no IN_ATTRIB on the parent watch");
        }
    }

    // ---- FD-59: read and pread generate IN_ACCESS; an EOF read does not.
    file->seek(0);
    U32 bufAt = TEST_HEAP_ADDRESS + 0x300;
    if ((S32)process->read(context.thread, fileFd, bufAt, 5) != 5) {
        testFail("process read of /a.txt did not return 5");
        return;
    }
    {
        auto events = drainEvents();
        if (!hasEvent(events, wdFile, 0x1, "")) {
            testFail("read produced no IN_ACCESS on the file watch");
        }
        if (!hasEvent(events, wdDir, 0x1, "a.txt")) {
            testFail("read produced no IN_ACCESS on the parent watch");
        }
    }
    if ((S32)process->pread64(context.thread, fileFd, bufAt, 5, 0) != 5) {
        testFail("process pread of /a.txt did not return 5");
        return;
    }
    {
        auto events = drainEvents();
        if (!hasEvent(events, wdFile, 0x1, "")) {
            testFail("pread produced no IN_ACCESS on the file watch");
        }
    }
    file->seek(5);
    if ((S32)process->read(context.thread, fileFd, bufAt, 5) != 0) {
        testFail("read at EOF must return 0");
        return;
    }
    {
        auto events = drainEvents();
        if (hasEvent(events, wdFile, 0x1, "")) {
            testFail("a read returning 0 bytes must not generate IN_ACCESS");
        }
    }

    // ---- FD-84: wrong-mode reads answer -EBADF through the
    // KProcess API (host-measured on the 6.17 kernel; reference in
    // test-results/fd83-84/). Plain read used to answer -EINVAL;
    // pread64 had no mode check at all and fell through to the
    // backing read, whose host error was swallowed into a silent 0.
    // The mode answer also precedes the zero-length return.
    {
        KFileDescriptorPtr wDesc;
        U32 wresult = process->openFile(B("/"), B("/a.txt"), K_O_WRONLY, wDesc);
        if ((S32)wresult < 0 || !wDesc) {
            testFail("openFile(O_WRONLY) for /a.txt failed: %d", (S32)wresult);
            return;
        }
        if ((S32)process->read(context.thread, (FD)wDesc->handle, bufAt, 5) != -K_EBADF) {
            testFail("plain read on a write-only descriptor must return -EBADF");
        }
        if ((S32)process->read(context.thread, (FD)wDesc->handle, bufAt, 0) != -K_EBADF) {
            testFail("zero-length read on a write-only descriptor must return -EBADF");
        }
        if ((S32)process->pread64(context.thread, (FD)wDesc->handle, bufAt, 5, 0) != -K_EBADF) {
            testFail("pread64 on a write-only descriptor must return -EBADF");
        }
        process->close(wDesc->handle);
    }

    // ---- FD-59: getdents generates IN_ACCESS on the directory.
    {
        KFileDescriptorPtr dirDesc;
        U32 r = process->openFile(B("/"), B("/"), K_O_RDONLY, dirDesc);
        if ((S32)r < 0 || !dirDesc) {
            testFail("openFile of / failed: %d", (S32)r);
            return;
        }
        drainEvents();
        U32 n = process->getdents((FD)dirDesc->handle, TEST_HEAP_ADDRESS + 0x600, 4096, false);
        if ((S32)n <= 0) {
            testFail("getdents returned no entries: %d", (S32)n);
        }
        auto events = drainEvents();
        if (!hasEvent(events, wdDir, 0x1, "")) {
            testFail("getdents produced no IN_ACCESS on the directory watch");
        }
        process->close(dirDesc->handle);
    }

    // ---- FD-60: link() creates a true hard link.
    drainEvents();
    if ((S32)process->link(B("/a.txt"), B("/b.txt")) != 0) {
        testFail("KProcess::link returned an error");
        return;
    }
    StatInfo stb;
    if (!statOf("/b.txt", stb)) {
        testFail("stat64 of /b.txt failed");
        return;
    }
    if (stb.ino != inoA) {
        testFail("hard link must share the inode (a=%d b=%d)", inoA, stb.ino);
    }
    if (stb.nlink != 2) {
        testFail("hard link target must report nlink 2, got %d", stb.nlink);
    }
    if (!statOf("/a.txt", st) || st.nlink != 2) {
        testFail("hard link source must report nlink 2, got %d", st.nlink);
    }
    {
        auto events = drainEvents();
        if (!hasEvent(events, wdDir, 0x100, "b.txt")) {
            testFail("link produced no IN_CREATE for the new name");
        }
        if (!hasEvent(events, wdFile, 0x4, "")) {
            testFail("link produced no IN_ATTRIB on the source watch");
        }
    }
    // A write through one name is visible through the other.
    {
        KFileDescriptorPtr bDesc;
        U32 r = process->openFile(B("/"), B("/b.txt"), K_O_RDWR, bDesc);
        if ((S32)r < 0 || !bDesc) {
            testFail("openFile of /b.txt failed: %d", (S32)r);
            return;
        }
        std::shared_ptr<KFile> bFile = std::dynamic_pointer_cast<KFile>(bDesc->kobject);
        const char* repl = "HELLO";
        context.thread->memory->memcpy(textAt, (void*)repl, 5);
        bFile->write(context.thread, textAt, 5);
        process->close(bDesc->handle);
        file->seek(0);
        U32 got = process->read(context.thread, fileFd, bufAt, 5);
        U32 word = context.thread->memory->readd(bufAt);
        if (got != 5 || word != (U32)('H' | ('E' << 8) | ('L' << 16) | ('L' << 24)) ||
            context.thread->memory->readb(bufAt + 4) != 'O') {
            testFail("write through /b.txt is not visible through /a.txt");
        }
    }
    if ((S32)process->link(B("/a.txt"), B("/b.txt")) != -K_EEXIST) {
        testFail("link onto an existing name must return -EEXIST");
    }
    if ((S32)process->link(B("/"), B("/c.txt")) != -K_EPERM) {
        testFail("link of a directory must return -EPERM");
    }
    if ((S32)process->link(B("/missing.txt"), B("/c.txt")) != -K_ENOENT) {
        testFail("link of a missing source must return -ENOENT");
    }
    // Unlinking a third name reports the link-count change on the
    // surviving names' watches too (one inode on Linux).
    if ((S32)process->link(B("/a.txt"), B("/c.txt")) != 0) {
        testFail("hard link to /c.txt failed");
        return;
    }
    drainEvents();
    if ((S32)process->unlinkFile(B("/c.txt")) != 0) {
        testFail("unlinkFile of /c.txt failed");
        return;
    }
    {
        auto events = drainEvents();
        if (!hasEvent(events, wdFile, 0x4, "")) {
            testFail("unlink of /c.txt produced no IN_ATTRIB on the /a.txt watch");
        }
    }
    // Unlinking one name drops the count and keeps the content.
    if ((S32)process->unlinkFile(B("/a.txt")) != 0) {
        testFail("unlinkFile of /a.txt failed");
        return;
    }
    if (!statOf("/b.txt", stb) || stb.nlink != 1 || stb.ino != inoA) {
        testFail("after unlinking one name: nlink=%d ino=%d (expected 1 and %d)", stb.nlink, stb.ino, inoA);
    }
    {
        KFileDescriptorPtr bDesc;
        U32 r = process->openFile(B("/"), B("/b.txt"), K_O_RDONLY, bDesc);
        if ((S32)r < 0 || !bDesc) {
            testFail("openFile of surviving /b.txt failed: %d", (S32)r);
            return;
        }
        std::shared_ptr<KFile> bFile = std::dynamic_pointer_cast<KFile>(bDesc->kobject);
        U8 out[8] = {};
        U32 got = bFile->readNative(out, 5);
        if (got != 5 || memcmp(out, "HELLO", 5) != 0) {
            testFail("content did not survive unlinking the other name");
        }
        process->close(bDesc->handle);
    }
    // A hard link to a symlink is a second name for the symlink itself.
    process->symlink(B("/some-target"), B("/s.txt"));
    if ((S32)process->link(B("/s.txt"), B("/s2.txt")) != 0) {
        testFail("link of a symlink returned an error");
    } else {
        std::shared_ptr<FsNode> s2 = Fs::getNodeFromLocalPath(B("/"), B("/s2.txt"), false);
        if (!s2 || !s2->isLink() || s2->getLink() != B("/some-target")) {
            testFail("hard link of a symlink must itself be a symlink to the same target");
        }
    }

    process->close(fileFd);
    process->close(ifd);
}

// FD-79: IN_CLOSE_WRITE / IN_CLOSE_NOWRITE timing against mapping
// lifetime. Host-kernel reference behaviour (measured on the box's
// 6.17 kernel with a dedicated reference program): any live mapping
// of an open file description -- shared or private, read or write --
// defers the description's close event until the last such mapping
// is unmapped; with no mapping live it fires at close; a second,
// unmapped description of the same file fires at its own close while
// the first description's event stays deferred.
void testCloseWriteDeferral() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    KThread* thread = context.thread;
    char dirTemplate[] = "/tmp/bw64kcloseXXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for close-write deferral test");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    std::shared_ptr<KInotify> inotify = std::make_shared<KInotify>(false);
    KInotify::registerInstance(inotify);
    KFileDescriptorPtr inotifyDesc = process->allocFileDescriptor(inotify, K_O_RDONLY, 0, -1, 0);
    if (!inotifyDesc || !inotifyDesc->kobject) {
        testFail("allocFileDescriptor for the inotify instance failed");
        return;
    }
    S32 ifd = (S32)inotifyDesc->handle;

    // Create /w.txt with known content before any watch exists.
    {
        KFileDescriptorPtr created;
        U32 result = process->openFile(B("/"), B("/w.txt"), K_O_RDWR | K_O_CREAT, created);
        if ((S32)result < 0 || !created) {
            testFail("openFile(O_CREAT) for /w.txt failed: %d", (S32)result);
            return;
        }
        const char* text = "0123456789abcdef";
        created->kobject->writeNative((U8*)text, 16);
        process->close(created->handle);
    }

    S32 wd = (S32)process->inotify_add_watch(ifd, B("/w.txt"), 0xfff);
    if (wd < 1) {
        testFail("inotify_add_watch on /w.txt failed: %d", wd);
        return;
    }

    struct Ev { S32 wd; U32 mask; };
    auto drainEvents = [&]() {
        std::vector<Ev> events;
        U8 buffer[8192];
        S32 total = (S32)inotifyDesc->kobject->readNative(buffer, sizeof(buffer));
        S32 offset = 0;
        while (total > 0 && offset + 16 <= total) {
            Ev e;
            U32 nameLen;
            memcpy(&e.wd, buffer + offset, 4);
            memcpy(&e.mask, buffer + offset + 4, 4);
            memcpy(&nameLen, buffer + offset + 12, 4);
            events.push_back(e);
            offset += 16 + (S32)nameLen;
        }
        return events;
    };
    auto hasClose = [](const std::vector<Ev>& events, S32 wd_, U32 bit) {
        for (auto& e : events) {
            if (e.wd == wd_ && (e.mask & bit)) return true;
        }
        return false;
    };
    constexpr U32 IN_CLOSE_WRITE_BIT = 0x8;
    constexpr U32 IN_CLOSE_NOWRITE_BIT = 0x10;

    auto openWatched = [&](U32 flags, S32& handleOut) -> bool {
        KFileDescriptorPtr d;
        U32 result = process->openFile(B("/"), B("/w.txt"), flags, d);
        if ((S32)result < 0 || !d) return false;
        handleOut = (S32)d->handle;
        return true; // the local reference drops here; the fd table holds the description
    };
    auto mapWatched = [&](S32 h, S32 prot, S32 flags) -> U32 {
        return process->memory->mmap(thread, 0, K_PAGE_SIZE, prot, flags, h, 0);
    };

    // Case A: a MAP_SHARED writable mapping live across close defers
    // IN_CLOSE_WRITE to the munmap.
    {
        S32 h;
        if (!openWatched(K_O_RDWR, h)) { testFail("case A open failed"); return; }
        drainEvents(); // IN_OPEN
        U32 addr = mapWatched(h, K_PROT_READ | K_PROT_WRITE, K_MAP_SHARED);
        if ((S32)addr < 0) { testFail("case A mmap failed: %d", (S32)addr); return; }
        process->memory->writeb(addr, 'X');
        process->close(h);
        if (hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case A: IN_CLOSE_WRITE fired at fd close while a writable shared mapping is still open");
        }
        process->memory->unmap(addr, K_PAGE_SIZE);
        if (!hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case A: IN_CLOSE_WRITE did not arrive at munmap");
        }
    }

    // Case B: mapping unmapped before close -- nothing at munmap, the
    // event fires at close.
    {
        S32 h;
        if (!openWatched(K_O_RDWR, h)) { testFail("case B open failed"); return; }
        drainEvents();
        U32 addr = mapWatched(h, K_PROT_READ | K_PROT_WRITE, K_MAP_SHARED);
        if ((S32)addr < 0) { testFail("case B mmap failed: %d", (S32)addr); return; }
        process->memory->unmap(addr, K_PAGE_SIZE);
        if (hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case B: IN_CLOSE_WRITE fired at munmap while the fd is still open");
        }
        process->close(h);
        if (!hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case B: IN_CLOSE_WRITE did not fire at close after the mapping was gone");
        }
    }

    // Case C: two mappings of one description -- the event waits for
    // the second munmap.
    {
        S32 h;
        if (!openWatched(K_O_RDWR, h)) { testFail("case C open failed"); return; }
        drainEvents();
        U32 addr1 = mapWatched(h, K_PROT_READ | K_PROT_WRITE, K_MAP_SHARED);
        U32 addr2 = mapWatched(h, K_PROT_READ | K_PROT_WRITE, K_MAP_SHARED);
        if ((S32)addr1 < 0 || (S32)addr2 < 0 || addr1 == addr2) { testFail("case C mmaps failed: %d %d", (S32)addr1, (S32)addr2); return; }
        process->close(h);
        if (hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case C: IN_CLOSE_WRITE fired at fd close with two mappings open");
        }
        process->memory->unmap(addr1, K_PAGE_SIZE);
        if (hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case C: IN_CLOSE_WRITE fired at the first of two munmaps");
        }
        process->memory->unmap(addr2, K_PAGE_SIZE);
        if (!hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case C: IN_CLOSE_WRITE did not arrive at the last munmap");
        }
    }

    // Case D: a read-only description with a MAP_PRIVATE read mapping
    // defers IN_CLOSE_NOWRITE the same way.
    {
        S32 h;
        if (!openWatched(K_O_RDONLY, h)) { testFail("case D open failed"); return; }
        drainEvents();
        U32 addr = mapWatched(h, K_PROT_READ, K_MAP_PRIVATE);
        if ((S32)addr < 0) { testFail("case D mmap failed: %d", (S32)addr); return; }
        process->close(h);
        std::vector<Ev> atClose = drainEvents();
        if (hasClose(atClose, wd, IN_CLOSE_NOWRITE_BIT) || hasClose(atClose, wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case D: a close event fired at fd close while a private read mapping is still open");
        }
        process->memory->unmap(addr, K_PAGE_SIZE);
        if (!hasClose(drainEvents(), wd, IN_CLOSE_NOWRITE_BIT)) {
            testFail("case D: IN_CLOSE_NOWRITE did not arrive at munmap");
        }
    }

    // Case E: no mapping at all -- the event still fires at close.
    {
        S32 h;
        if (!openWatched(K_O_RDWR, h)) { testFail("case E open failed"); return; }
        drainEvents();
        process->close(h);
        if (!hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case E: plain close produced no IN_CLOSE_WRITE");
        }
    }

    // Case F: a second, unmapped description fires at its own close
    // while the first description's event stays deferred.
    {
        S32 h1;
        if (!openWatched(K_O_RDWR, h1)) { testFail("case F open h1 failed"); return; }
        drainEvents();
        U32 addr = mapWatched(h1, K_PROT_READ | K_PROT_WRITE, K_MAP_SHARED);
        if ((S32)addr < 0) { testFail("case F mmap failed: %d", (S32)addr); return; }
        S32 h2;
        if (!openWatched(K_O_RDWR, h2)) { testFail("case F open h2 failed"); return; }
        drainEvents(); // h2's IN_OPEN
        process->close(h2);
        if (!hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case F: the unmapped description did not fire at its own close");
        }
        process->close(h1);
        if (hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case F: the mapped description fired at fd close");
        }
        process->memory->unmap(addr, K_PAGE_SIZE);
        if (!hasClose(drainEvents(), wd, IN_CLOSE_WRITE_BIT)) {
            testFail("case F: the mapped description's IN_CLOSE_WRITE did not arrive at munmap");
        }
    }

    process->close(ifd);
}


void testTempFileLinkPattern() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    char dirTemplate[] = "/tmp/bw64ktemplinkXXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for temp-file link pattern test");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    // Same stat64 layout as testChmodAccessHardLink: ino @12,
    // mode @16, nlink @20.
    constexpr U32 statAt = TEST_HEAP_ADDRESS + 0x400;
    struct StatInfo { U32 mode; U32 ino; U32 nlink; };
    auto statOf = [&](const char* path, StatInfo& out) -> bool {
        if ((S32)process->stat64(B(path), statAt) < 0) return false;
        out.ino = context.thread->memory->readd(statAt + 12);
        out.mode = context.thread->memory->readd(statAt + 16);
        out.nlink = context.thread->memory->readd(statAt + 20);
        return true;
    };
    // The host-side census of the fixture directory: a true
    // shared backing keeps exactly ONE host file per guest file,
    // a copy would show two.
    auto hostEntryCount = [&]() -> int {
        int n = 0;
        DIR* d = opendir(dir);
        if (!d) return -1;
        while (dirent* e = readdir(d)) {
            if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) n++;
        }
        closedir(d);
        return n;
    };
    auto createWithContent = [&](const char* path, const char* data, U32 len) -> bool {
        KFileDescriptorPtr created;
        U32 r = process->openFile(B("/"), B(path), K_O_RDWR | K_O_CREAT, created);
        if ((S32)r < 0 || !created) return false;
        std::shared_ptr<KFile> f = std::dynamic_pointer_cast<KFile>(created->kobject);
        U32 at = TEST_HEAP_ADDRESS + 0x200;
        context.thread->memory->memcpy(at, (void*)data, len);
        f->write(context.thread, at, len);
        process->close(created->handle);
        return true;
    };
    auto writeFile = [&](const char* path, const char* data, U32 len) -> bool {
        KFileDescriptorPtr desc;
        U32 r = process->openFile(B("/"), B(path), K_O_RDWR, desc);
        if ((S32)r < 0 || !desc) return false;
        std::shared_ptr<KFile> f = std::dynamic_pointer_cast<KFile>(desc->kobject);
        U32 at = TEST_HEAP_ADDRESS + 0x200;
        context.thread->memory->memcpy(at, (void*)data, len);
        f->write(context.thread, at, len);
        process->close(desc->handle);
        return true;
    };
    auto readFile = [&](const char* path, char* out, U32 len) -> U32 {
        KFileDescriptorPtr desc;
        U32 r = process->openFile(B("/"), B(path), K_O_RDONLY, desc);
        if ((S32)r < 0 || !desc) return 0;
        std::shared_ptr<KFile> f = std::dynamic_pointer_cast<KFile>(desc->kobject);
        U32 got = f->readNative((U8*)out, len);
        process->close(desc->handle);
        return got;
    };

    // ---- FD-81: the fontconfig cache-lock pattern. A library
    // creates a temp file in a directory, links it to a second
    // name in the same directory, unlinks the temp name and
    // keeps using the file through the second name. On the host
    // every step succeeds and both names are one file. The
    // browser backing store cannot hard-link at all, so
    // KProcess::link falls back to linkSharedBacking there -
    // the entry this test proves.
    const char* tmpName = "/CACHEDIR.TAG.TMP-X7f2Qa";
    const char* lckName = "/CACHEDIR.TAG.LCK";
    if (!createWithContent(tmpName, "lock", 4)) {
        testFail("could not create the temp file");
        return;
    }
    if (hostEntryCount() != 1) {
        testFail("one backing file expected after create, found %d", hostEntryCount());
    }
    StatInfo stTmp;
    if (!statOf(tmpName, stTmp) || stTmp.nlink != 1) {
        testFail("fresh temp file must have nlink 1");
        return;
    }
    const U32 inoTmp = stTmp.ino;

    if ((S32)process->linkSharedBacking(B(tmpName), B(lckName)) != 0) {
        testFail("linkSharedBacking of the temp-to-lock pattern failed");
        return;
    }
    if (hostEntryCount() != 1) {
        testFail("shared-backing link must keep exactly one backing file, found %d", hostEntryCount());
    }
    StatInfo stLck;
    if (!statOf(lckName, stLck)) {
        testFail("stat64 of the lock name failed");
        return;
    }
    if (stLck.ino != inoTmp) {
        testFail("lock name must share the temp file's inode (tmp=%d lck=%d)", inoTmp, stLck.ino);
    }
    if (stLck.nlink != 2) {
        testFail("lock name must report nlink 2, got %d", stLck.nlink);
    }
    if (!statOf(tmpName, stTmp) || stTmp.nlink != 2) {
        testFail("temp name must report nlink 2 after the link, got %d", stTmp.nlink);
    }

    // A write through the lock name is visible through the temp name.
    if (!writeFile(lckName, "held", 4)) {
        testFail("write through the lock name failed");
        return;
    }
    {
        char out[8] = {};
        if (readFile(tmpName, out, 4) != 4 || memcmp(out, "held", 4) != 0) {
            testFail("write through the lock name is not visible through the temp name");
        }
    }
    // Unlinking the temp name keeps the content under the lock name.
    if ((S32)process->unlinkFile(B(tmpName)) != 0) {
        testFail("unlinkFile of the temp name failed");
        return;
    }
    {
        StatInfo st;
        if (statOf(tmpName, st)) {
            testFail("temp name must be gone after unlink");
        }
        if (!statOf(lckName, st) || st.ino != inoTmp || st.nlink != 1) {
            testFail("after unlinking the temp name: ino=%d nlink=%d (expected %d and 1)", st.ino, st.nlink, inoTmp);
        }
        char out[8] = {};
        if (readFile(lckName, out, 4) != 4 || memcmp(out, "held", 4) != 0) {
            testFail("content did not survive unlinking the temp name");
        }
    }
    if (hostEntryCount() != 1) {
        testFail("backing must survive while the lock name exists, found %d", hostEntryCount());
    }
    // Unlinking the last name deletes the backing file.
    if ((S32)process->unlinkFile(B(lckName)) != 0) {
        testFail("unlinkFile of the lock name failed");
        return;
    }
    if (hostEntryCount() != 0) {
        testFail("backing must be deleted with the last name, found %d", hostEntryCount());
    }

    // The error shape mirrors link()'s.
    if ((S32)process->linkSharedBacking(B("/missing.TMP-zz"), B("/x.LCK")) != -K_ENOENT) {
        testFail("linkSharedBacking of a missing source must return -ENOENT");
    }
    if ((S32)process->linkSharedBacking(B("/"), B("/x.LCK")) != -K_EPERM) {
        testFail("linkSharedBacking of a directory must return -EPERM");
    }

    // Renaming one name of a shared-backing pair moves the name,
    // not the file: the other name keeps the content and the
    // identity they share.
    if (!createWithContent("/mv.TMP-zz", "data", 4)) {
        testFail("could not create the rename-pair temp file");
        return;
    }
    if ((S32)process->linkSharedBacking(B("/mv.TMP-zz"), B("/mv.LCK")) != 0) {
        testFail("linkSharedBacking of the rename pair failed");
        return;
    }
    StatInfo before;
    if (!statOf("/mv.TMP-zz", before)) {
        testFail("stat64 of the rename-pair temp file failed");
        return;
    }
    if ((S32)process->rename(B("/mv.LCK"), B("/mv.LCK2")) != 0) {
        testFail("rename of a shared-backing name failed");
        return;
    }
    {
        StatInfo a, b;
        if (!statOf("/mv.TMP-zz", a) || !statOf("/mv.LCK2", b)) {
            testFail("stat64 after the rename failed");
            return;
        }
        if (a.ino != before.ino || b.ino != before.ino || a.nlink != 2 || b.nlink != 2) {
            testFail("rename must keep one shared file with nlink 2 (ino %d/%d vs %d, nlink %d/%d)", a.ino, b.ino, before.ino, a.nlink, b.nlink);
        }
        if (!writeFile("/mv.LCK2", "kept", 4)) {
            testFail("write through the renamed name failed");
            return;
        }
        char out[8] = {};
        if (readFile("/mv.TMP-zz", out, 4) != 4 || memcmp(out, "kept", 4) != 0) {
            testFail("write through the renamed name is not visible through the other name");
        }
    }
    process->unlinkFile(B("/mv.TMP-zz"));
    process->unlinkFile(B("/mv.LCK2"));
    if (hostEntryCount() != 0) {
        testFail("backing must be deleted once both names of the renamed pair are unlinked, found %d", hostEntryCount());
    }
}

void testInotifyPollNotifyStress() {
    // FD-38 enablement-park mechanism pin. A poll scan holds the polling
    // thread's poll condition mutex while it registers on every object in
    // its set and probes each one for readiness (internal_poll_impl in
    // kpoll.cpp does exactly this, in this order). The inotify notify
    // sweep, which runs on file open/close funnels in every process once
    // any instance exists, queues the event and signals the instance's
    // parents while holding the instance wait condition (KInotify::sweep).
    // If the readiness probe also takes the instance wait condition, the
    // two paths lock in opposite orders (poll cond -> instance cond vs
    // instance cond -> poll cond) and a session whose server polls an
    // inotify descriptor while other processes churn files deadlocks
    // outright: that is the Chromium session park that kept
    // inotify_init1 behind ENOSYS. This test runs both real lock paths
    // concurrently at high frequency; it passes only by completing, and
    // on the pre-fix code it wedges (observed as a hang, not a failure
    // line -- a wedged run IS the red result).
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    char dirTemplate[] = "/tmp/bw64kinotifystressXXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for inotify poll/notify stress test");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();
    std::shared_ptr<FsNode> rootNode = Fs::rootNode;

    std::shared_ptr<KInotify> inotify = std::make_shared<KInotify>(false);
    KInotify::registerInstance(inotify);
    KFileDescriptorPtr descriptor = process->allocFileDescriptor(inotify, K_O_RDONLY, 0, -1, 0);
    if (!descriptor || !descriptor->kobject) {
        testFail("allocFileDescriptor for the inotify stress instance failed");
        return;
    }
    S32 fd = (S32)descriptor->handle;
    if ((S32)process->inotify_add_watch(fd, B("/"), 0xfff) < 1) {
        testFail("inotify_add_watch failed for the stress instance");
        return;
    }

    const int iterations = 200000;
    BOXEDWINE_CONDITION pollCond = context.thread->pollCond;
    std::thread poller([&]() {
        for (int i = 0; i < iterations; i++) {
            // The internal_poll_impl scan sequence, lock for lock: hold
            // the poll condition's mutex, register as a wait parent on
            // the object, probe readiness.
            BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(pollCond);
            inotify->waitForEvents(pollCond, K_POLLIN);
            (void)inotify->isReadReady();
        }
        inotify->waitForEvents(pollCond, 0);
    });
    std::thread sweeper([&]() {
        for (int i = 0; i < iterations; i++) {
            // The exact notify funnels every guest file open and close
            // call once an inotify instance exists.
            KInotify::notifyOpened(rootNode.get());
            KInotify::notifyClosed(rootNode.get(), true);
        }
    });
    poller.join();
    sweeper.join();
    process->close(fd);
}
// FD-93: the write family's wrong-mode answers at the KProcess
// layer both syscall layers forward to (the x64 syscall layer is
// pinned by testWriteFamily64Syscalls). Host behaviour as in that
// test's header: EBADF for write / writev / pwrite64 on a
// descriptor opened read-only, count 0 and iovcnt 0 included;
// EINVAL for a negative pwrite64 offset, ahead of the mode
// answer; EINVAL for an out-of-range writev iovcnt and EFAULT
// for a null iovec on a writable descriptor (the mode answer
// precedes both in the vectored walk's validation order).
void testWriteFamilyWrongMode() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    KThread* thread = context.thread;
    char dirTemplate[] = "/tmp/bw64kwritefamXXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for write-family test");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    KFileDescriptorPtr rdwrDesc;
    U32 result = process->openFile(B("/"), B("/data.bin"), K_O_RDWR | K_O_CREAT, rdwrDesc);
    if ((S32)result < 0 || !rdwrDesc) {
        testFail("write-family test could not create /data.bin");
        return;
    }
    const char* text = "0123456789ABCDEFGHIJ";
    rdwrDesc->kobject->writeNative((U8*)text, 20);
    rdwrDesc->kobject->seek(0);
    KFileDescriptorPtr rdonlyDesc;
    result = process->openFile(B("/"), B("/data.bin"), K_O_RDONLY, rdonlyDesc);
    if ((S32)result < 0 || !rdonlyDesc) {
        testFail("write-family test could not open /data.bin read-only");
        return;
    }
    const S32 wfd = (S32)rdwrDesc->handle;
    const S32 rfd = (S32)rdonlyDesc->handle;

    constexpr U32 bufAt = TEST_HEAP_ADDRESS + 0x200;
    constexpr U32 iovAt = TEST_HEAP_ADDRESS + 0x400;
    thread->memory->memcpy(bufAt, (void*)"xyz", 3);
    thread->memory->writed(iovAt + 0, bufAt);
    thread->memory->writed(iovAt + 4, 3);
    thread->memory->writed(iovAt + 8, bufAt);
    thread->memory->writed(iovAt + 12, 3);

    if ((S32)process->write(thread, rfd, bufAt, 3) != -K_EBADF) {
        testFail("KProcess::write on a read-only descriptor was not -EBADF");
    }
    if ((S32)process->write(thread, rfd, bufAt, 0) != -K_EBADF) {
        testFail("zero-count KProcess::write on a read-only descriptor was not -EBADF");
    }
    if ((S32)process->writev(thread, rfd, iovAt, 1) != -K_EBADF) {
        testFail("KProcess::writev on a read-only descriptor was not -EBADF");
    }
    if ((S32)process->writev(thread, rfd, iovAt, 0) != -K_EBADF) {
        testFail("KProcess::writev with iovcnt 0 on a read-only descriptor was not -EBADF");
    }
    if ((S32)process->writev(thread, wfd, iovAt, -1) != -K_EINVAL) {
        testFail("KProcess::writev with iovcnt -1 was not -EINVAL");
    }
    if ((S32)process->pwrite64(thread, rfd, bufAt, 3, 0) != -K_EBADF) {
        testFail("KProcess::pwrite64 on a read-only descriptor was not -EBADF");
    }
    if ((S32)process->pwrite64(thread, rfd, bufAt, 0, 0) != -K_EBADF) {
        testFail("zero-count KProcess::pwrite64 on a read-only descriptor was not -EBADF");
    }
    if ((S32)process->pwrite64(thread, rfd, bufAt, 3, (U64)-1) != -K_EINVAL) {
        testFail("KProcess::pwrite64 with offset -1 on a read-only descriptor was not -EINVAL");
    }
    if ((S32)process->pwrite64(thread, wfd, bufAt, 3, (U64)-1) != -K_EINVAL) {
        testFail("KProcess::pwrite64 with offset -1 on a writable descriptor was not -EINVAL");
    }
    // Controls: the writable descriptor still writes through all
    // three shapes, and the positioned write leaves the position.
    if ((S32)process->write(thread, wfd, bufAt, 3) != 3) {
        testFail("KProcess::write on a writable descriptor did not return 3");
    }
    if ((S32)process->writev(thread, wfd, iovAt, 2) != 6) {
        testFail("two-segment KProcess::writev on a writable descriptor did not return 6");
    }
    {
        S64 before = rdwrDesc->kobject->getPos();
        if ((S32)process->pwrite64(thread, wfd, bufAt, 3, 5) != 3) {
            testFail("KProcess::pwrite64 on a writable descriptor did not return 3");
        }
        if (rdwrDesc->kobject->getPos() != before) {
            testFail("KProcess::pwrite64 moved the file position");
        }
    }

    // Last: on the base this case never returns -- the walk
    // dereferences the null iovec and the harness takes a guest
    // fault at address 0 -- so it must not shadow the cases above.
    if ((S32)process->writev(thread, wfd, 0, 1) != -K_EFAULT) {
        testFail("KProcess::writev with a null iovec and iovcnt 1 was not -EFAULT");
    }

    process->close(rfd);
    process->close(wfd);
    rmdir(dir);
}

#ifndef BOXEDWINE_GUEST_X64
// FD-92: the 32-bit dispatcher's readv (145) was a null slot and
// answered -ENOSYS; only the 64-bit readv existed. The contract
// is FD-83's, measured on the host kernel (see
// testPreadv64Syscalls' header and lane-fd83-84's host reference
// matrix): exactly one IN_ACCESS per successful call -- one
// segment or three, data or 0 bytes (at EOF, with iovcnt 0, with
// all-zero-length segments) -- none on failure; a short read ends
// the walk; validation answers EBADF (bad descriptor, wrong
// mode), EINVAL (iovcnt outside 0..IOV_MAX), EFAULT (null iovec
// with iovcnt > 0). Plain read at EOF stays silent (FD-59).
void testReadv32Syscall() {
    TestContext& context = testContext();
    KProcessPtr process = context.process;
    KThread* thread = context.thread;
    char dirTemplate[] = "/tmp/bw64kreadv32XXXXXX";
    char* dir = mkdtemp(dirTemplate);
    if (!dir) {
        testFail("mkdtemp failed for 32-bit readv test");
        return;
    }
    std::shared_ptr<FsFileNode> previousRoot = Fs::rootNode;
    struct RootRestore {
        std::shared_ptr<FsFileNode> previous;
        ~RootRestore() { Fs::rootNode = previous; Fs::clearPathCache(); }
    } rootRestore{previousRoot};
    Fs::rootNode = std::make_shared<FsFileNode>(1, 0, B("/"), B(""), BString::copy(dir), true, true, nullptr);
    Fs::clearPathCache();

    KFileDescriptorPtr fileDesc;
    U32 result = process->openFile(B("/"), B("/data.bin"), K_O_RDWR | K_O_CREAT, fileDesc);
    if ((S32)result < 0 || !fileDesc) {
        testFail("32-bit readv test could not create /data.bin");
        return;
    }
    const char* text = "0123456789ABCDEFGHIJ";
    fileDesc->kobject->writeNative((U8*)text, 20);
    fileDesc->kobject->seek(0);
    KFileDescriptorPtr rdonlyDesc;
    result = process->openFile(B("/"), B("/data.bin"), K_O_RDONLY, rdonlyDesc);
    if ((S32)result < 0 || !rdonlyDesc) {
        testFail("32-bit readv test could not open /data.bin read-only");
        return;
    }
    KFileDescriptorPtr wronlyDesc;
    result = process->openFile(B("/"), B("/data.bin"), K_O_WRONLY, wronlyDesc);
    if ((S32)result < 0 || !wronlyDesc) {
        testFail("32-bit readv test could not open /data.bin write-only");
        return;
    }
    const S32 rfd = (S32)rdonlyDesc->handle;
    const S32 wfd = (S32)wronlyDesc->handle;

    std::shared_ptr<KInotify> inotify = std::make_shared<KInotify>(false);
    KInotify::registerInstance(inotify);
    KFileDescriptorPtr inotifyDesc = process->allocFileDescriptor(inotify, K_O_RDONLY, 0, -1, 0);
    if (!inotifyDesc || !inotifyDesc->kobject) {
        testFail("32-bit readv test could not create its inotify instance");
        return;
    }
    S32 wd = (S32)process->inotify_add_watch((S32)inotifyDesc->handle, B("/data.bin"), 0xfff);
    if (wd < 1) {
        testFail("32-bit readv test could not watch /data.bin");
        return;
    }
    auto drainMasks = [&]() {
        std::vector<U32> masks;
        U8 buffer[8192];
        S32 total = (S32)inotifyDesc->kobject->readNative(buffer, sizeof(buffer));
        S32 offset = 0;
        while (total > 0 && offset + 16 <= total) {
            U32 mask, nameLen;
            memcpy(&mask, buffer + offset + 4, 4);
            memcpy(&nameLen, buffer + offset + 12, 4);
            masks.push_back(mask);
            offset += 16 + (S32)nameLen;
        }
        return masks;
    };
    auto accessCount = [&](const std::vector<U32>& masks) {
        int n = 0;
        for (U32 m : masks) if (m & 0x00000001) n++; // IN_ACCESS
        return n;
    };
    drainMasks(); // setup noise

    constexpr U32 iovAt = TEST_HEAP_ADDRESS + 0x400;
    constexpr U32 buf0 = TEST_HEAP_ADDRESS + 0x800;
    constexpr U32 buf1 = TEST_HEAP_ADDRESS + 0x900;
    constexpr U32 buf2 = TEST_HEAP_ADDRESS + 0xA00;
    // Segments are staged host-side and flushed by invoke():
    // testNewInstruction wipes the whole test heap, so anything
    // written to guest memory before it is gone by the time the
    // dispatched call runs.
    U32 stagedBase[8];
    U32 stagedLen[8];
    U32 stagedCount = 0;
    auto setIov = [&](U32 idx, U32 base, U32 len) {
        stagedBase[idx] = base;
        stagedLen[idx] = len;
        if (idx + 1 > stagedCount) stagedCount = idx + 1;
    };
    // Guest memory for the dispatched calls is context.memory:
    // the dispatcher runs on context.cpu, and in this harness
    // that is not context.thread's memory view.
    auto invoke = [&](S32 fd, U32 iov, S32 iovcnt) -> S32 {
        testNewInstruction(0);
        for (U32 i = 0; i < stagedCount; i++) {
            context.memory->writed(iovAt + i * 8 + 0, stagedBase[i]);
            context.memory->writed(iovAt + i * 8 + 4, stagedLen[i]);
        }
        testPushCode8(0xB8); testPushCode32(145); // __NR_readv
        testPushCode8(0xCD); testPushCode8(0x80);
        context.cpu->reg[3].u32 = (U32)fd;     // EBX
        context.cpu->reg[1].u32 = iov;         // ECX
        context.cpu->reg[2].u32 = (U32)iovcnt; // EDX
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    auto bufEquals = [&](U32 at, const char* expected, U32 len) {
        char got[32];
        context.memory->memcpy(got, at, len);
        return memcmp(got, expected, len) == 0;
    };

    // One segment, with data: bytes land, exactly one IN_ACCESS.
    setIov(0, buf0, 4);
    if (invoke(rfd, iovAt, 1) != 4 || !bufEquals(buf0, "0123", 4)) {
        testFail("32-bit readv of one segment did not return its 4 bytes");
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("one-segment 32-bit readv did not generate exactly one IN_ACCESS");
    }
    // Three segments fill in order; still exactly one event.
    setIov(0, buf0, 4); setIov(1, buf1, 4); setIov(2, buf2, 4);
    if (invoke(rfd, iovAt, 3) != 12 || !bufEquals(buf0, "4567", 4) ||
        !bufEquals(buf1, "89AB", 4) || !bufEquals(buf2, "CDEF", 4)) {
        testFail("three-segment 32-bit readv did not return its 12 bytes in order");
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("three-segment 32-bit readv did not generate exactly one IN_ACCESS");
    }
    // A short final segment ends the walk (4 bytes remain).
    setIov(0, buf0, 4); setIov(1, buf1, 4);
    if (invoke(rfd, iovAt, 2) != 4 || !bufEquals(buf0, "GHIJ", 4)) {
        testFail("32-bit readv at the last 4 bytes did not stop at EOF");
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("short 32-bit readv did not generate exactly one IN_ACCESS");
    }
    // At EOF: a 0-byte success still generates the event.
    setIov(0, buf0, 4);
    if (invoke(rfd, iovAt, 1) != 0) {
        testFail("32-bit readv at EOF did not return 0");
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("32-bit readv at EOF generated no IN_ACCESS");
    }
    // iovcnt 0: a 0-byte success, with the event.
    if (invoke(rfd, iovAt, 0) != 0) {
        testFail("32-bit readv with iovcnt 0 did not return 0");
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("32-bit readv with iovcnt 0 generated no IN_ACCESS");
    }
    // All-zero-length segments: a 0-byte success, with the event.
    setIov(0, buf0, 0); setIov(1, buf1, 0);
    if (invoke(rfd, iovAt, 2) != 0) {
        testFail("32-bit readv of zero-length segments did not return 0");
    }
    if (accessCount(drainMasks()) != 1) {
        testFail("32-bit readv of zero-length segments generated no IN_ACCESS");
    }
    // Failures: the host's answers, and no events.
    if (invoke(9999, iovAt, 1) != -K_EBADF) {
        testFail("32-bit readv on a bad descriptor was not -EBADF");
    }
    if (invoke(wfd, iovAt, 1) != -K_EBADF) {
        testFail("32-bit readv on a write-only descriptor was not -EBADF");
    }
    if (invoke(wfd, iovAt, 0) != -K_EBADF) {
        testFail("32-bit readv with iovcnt 0 on a write-only descriptor was not -EBADF");
    }
    if (invoke(rfd, iovAt, -1) != -K_EINVAL) {
        testFail("32-bit readv with iovcnt -1 was not -EINVAL");
    }
    if (invoke(rfd, iovAt, 1025) != -K_EINVAL) {
        testFail("32-bit readv with iovcnt IOV_MAX+1 was not -EINVAL");
    }
    if (invoke(rfd, 0, 1) != -K_EFAULT) {
        testFail("32-bit readv with a null iovec and iovcnt 1 was not -EFAULT");
    }

    // FD-93, same route: the dispatcher's pwrite64 (181) answers
    // a negative offset EINVAL ahead of even the descriptor check
    // -- the positioned family's measured order, the same one the
    // 64-bit sys_pwrite64 keeps.
    auto invokePwrite64 = [&](S32 fd, U32 buf, U32 count, U64 offset) -> S32 {
        testNewInstruction(0);
        testPushCode8(0xB8); testPushCode32(181); // __NR_pwrite64
        testPushCode8(0xCD); testPushCode8(0x80);
        context.cpu->reg[3].u32 = (U32)fd;          // EBX
        context.cpu->reg[1].u32 = buf;              // ECX
        context.cpu->reg[2].u32 = count;            // EDX
        context.cpu->reg[6].u32 = (U32)offset;      // ESI = low
        context.cpu->reg[7].u32 = (U32)(offset >> 32); // EDI = high
        testRunCPU();
        return (S32)context.cpu->reg[0].u32;
    };
    if (invokePwrite64(9999, buf0, 3, (U64)-1) != -K_EINVAL) {
        testFail("32-bit pwrite64 with offset -1 on a bad descriptor was not -EINVAL");
    }
    if (invokePwrite64(wfd, buf0, 3, (U64)-1) != -K_EINVAL) {
        testFail("32-bit pwrite64 with offset -1 was not -EINVAL");
    }
    if (accessCount(drainMasks()) != 0) {
        testFail("failed 32-bit readv generated IN_ACCESS");
    }
    // Control: plain read at EOF stays silent (FD-59). The seek
    // puts the descriptor at EOF directly, so the control does
    // not depend on the walk above having advanced the position.
    rdonlyDesc->kobject->seek(20);
    if ((S32)process->read(thread, rfd, buf0, 4) != 0) {
        testFail("plain read at EOF did not return 0");
    }
    if (accessCount(drainMasks()) != 0) {
        testFail("plain read at EOF generated IN_ACCESS");
    }

    process->close((S32)inotifyDesc->handle);
    process->close(rfd);
    process->close(wfd);
    process->close((S32)fileDesc->handle);
    rmdir(dir);
}
#endif


#endif
