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

#ifndef __KINOTIFY_H__
#define __KINOTIFY_H__

// Feature marker for the one call site that cannot name KInotify
// unconditionally: FsNode::addChild lives in the fsnode.cpp region that
// web/tests/suite/unit/fs-dynamic-link-cache.test.mjs compiles against an
// in-memory mock with no kernel objects. fsnode.cpp includes this header,
// so production builds define the macro and make the call; the mock
// extraction does not include it and compiles the call out.
#define BW64_HAS_KINOTIFY 1

#include "kobject.h"

class FsNode;

// inotify event mask bits (Linux values; the record layout struct
// inotify_event { int wd; uint32_t mask, cookie, len; char name[] } is
// identical for 32-bit and 64-bit guests).
#define K_IN_ACCESS        0x00000001
#define K_IN_MODIFY        0x00000002
#define K_IN_ATTRIB        0x00000004
#define K_IN_CLOSE_WRITE   0x00000008
#define K_IN_CLOSE_NOWRITE 0x00000010
#define K_IN_OPEN          0x00000020
#define K_IN_MOVED_FROM    0x00000040
#define K_IN_MOVED_TO      0x00000080
#define K_IN_CREATE        0x00000100
#define K_IN_DELETE        0x00000200
#define K_IN_DELETE_SELF   0x00000400
#define K_IN_MOVE_SELF     0x00000800
#define K_IN_UNMOUNT       0x00002000
#define K_IN_Q_OVERFLOW    0x00004000
#define K_IN_IGNORED       0x00008000
#define K_IN_ONLYDIR       0x01000000
#define K_IN_DONT_FOLLOW   0x02000000
#define K_IN_EXCL_UNLINK   0x04000000
#define K_IN_MASK_CREATE   0x10000000
#define K_IN_MASK_ADD      0x20000000
#define K_IN_ONESHOT       0x80000000
#define K_IN_ISDIR         0x40000000

// inotify (kernel round 3B): watches bind to FsNodes and the emulator's
// filesystem mutation funnels report into every live instance through
// the static notify entry points below. See kinotify.cpp for the exact
// coverage and the documented gaps (IN_ACCESS, mmap-write IN_MODIFY).
class KInotify : public KObject {
public:
    explicit KInotify(bool blocking);
    ~KInotify() override;

    // From KObject
    U32 ioctl(KThread* thread, U32 request) override;
    S64 seek(S64 pos) override;
    S64 length() override;
    S64 getPos() override;
    void setBlocking(bool blocking) override;
    bool isBlocking() override;
    KFileLock* getLock(KFileLock* lock) override;
    U32 setLock(KFileLock* lock, bool wait) override;
    void unlockAll(U32 pid) override;
    bool supportsLocks() override;
    bool isOpen() override;
    bool isReadReady() override;
    bool isWriteReady() override;
    void waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) override;
    U32 write(KThread* thread, U32 buffer, U32 len) override;
    U32 writeNative(U8* buffer, U32 len) override;
    U32 read(KThread* thread, U32 buffer, U32 len) override;
    U32 readNative(U8* buffer, U32 len) override;
    U32 stat(KProcess* process, U32 address, bool is64) override;
    U32 map(KThread* thread, U32 address, U32 len, S32 prot, S32 flags, U64 off) override;
    bool canMap() override;
    BString selfFd() override;

    S32 addWatch(const std::shared_ptr<FsNode>& node, U32 mask);
    S32 removeWatch(U32 wd);

    static void registerInstance(const std::shared_ptr<KInotify>& instance);
    static void notifyChildAdded(FsNode* parent, FsNode* child);
    static void notifyNodeRemoved(FsNode* parent, FsNode* node);
    static void notifyRenamed(FsNode* fromParent, const BString& fromName, FsNode* toParent, const BString& toName, FsNode* node);
    static void notifyModified(FsNode* node);
    static void notifyOpened(FsNode* node);
    static void notifyClosed(FsNode* node, bool wasWrite);
    static void notifyAttrib(FsNode* node);

private:
    struct Watch {
        std::weak_ptr<FsNode> node;
        U32 mask = 0;
    };
    struct PendingEvent {
        S32 wd;
        U32 mask;
        U32 cookie;
        BString name;
    };

    void sweep(FsNode* parent, FsNode* subject, U32 childType, const BString& childName, bool childIsDir, U32 selfType, U32 cookie);
    static void fileEvent(FsNode* node, U32 type);
    static void sweepAll(FsNode* parent, FsNode* subject, U32 childType, const BString& childName, bool childIsDir, U32 selfType, U32 cookie);
    void queueLocked(S32 wd, U32 mask, U32 cookie, const BString& name);

    bool blocking;
    BOXEDWINE_CONDITION waitCond;
    std::map<U32, Watch> watches;
    U32 nextWatchDescriptor = 1;
    std::deque<PendingEvent> pendingEvents;
    bool overflowPending = false;

    static std::vector<std::weak_ptr<KInotify>> instances;
    static BOXEDWINE_MUTEX instancesMutex;
};

#endif
