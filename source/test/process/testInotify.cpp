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
    // __NR_inotify_init1 = 294. Guest-visible contract while the FD-38
    // enablement park is open: the syscall refuses loudly with ENOSYS
    // and guests fall back to polling.
    if (invoke(294, IN_NONBLOCK | IN_CLOEXEC, 0, 0, 0) != -K_ENOSYS) {
        testFail("inotify_init1 did not refuse with ENOSYS while the FD-38 gate is closed");
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

    // Guest-visible gate pin: init1 refuses loudly while the FD-38
    // enablement park is open.
    if ((S32)process->inotify_init1(0) != -K_ENOSYS) {
        testFail("KProcess::inotify_init1 did not refuse with ENOSYS while the FD-38 gate is closed");
        return;
    }
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
#endif
