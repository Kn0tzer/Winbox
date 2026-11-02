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
//  - any negative offset (including -1; there is no readv fallback),
//    a negative iovcnt, or iovcnt > IOV_MAX is EINVAL; an unknown
//    preadv2 flag bit is EOPNOTSUPP; every defined RWF flag is
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

#endif

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
#endif
