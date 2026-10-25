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
#include "kinotify.h"

#include <atomic>

// Coverage (kernel round 3B): IN_CREATE / IN_DELETE come from the FsNode
// child funnels (addChild / removeNodeFromParent); IN_MOVED_FROM /
// IN_MOVED_TO / IN_MOVE_SELF from KProcess::rename/renameat;
// IN_MODIFY / IN_OPEN / IN_CLOSE_WRITE / IN_CLOSE_NOWRITE / IN_ATTRIB
// from the file open nodes' write/construct/close/setLength paths (both
// the host-backed FsFileOpenNode and the memory-backed FsMemOpenNode).
// IN_DELETE_SELF + IN_IGNORED follow a watched node's removal, and
// rm_watch produces IN_IGNORED, as on Linux. Consecutive identical
// events coalesce; the queue is bounded at 16384 records with a single
// IN_Q_OVERFLOW record when it fills.
//
// Documented gaps: IN_ACCESS is not generated (no read-path hook; reads
// vastly outnumber the other operations and taxing every read for a
// rarely used event is not justified), and writes through a memory
// mapping do not produce IN_MODIFY (the mapping write path has no
// per-write funnel either). IN_CREATE can also fire for pre-existing
// children when a directory's children are first loaded from the host
// (FsNode::loadChildren funnels through addChild); watches registered
// after that load, or on directories whose children are already loaded,
// see only real creations.

std::vector<std::weak_ptr<KInotify>> KInotify::instances;
BOXEDWINE_MUTEX KInotify::instancesMutex;
static std::atomic<U32> bw64InotifyRenameCookie(1);

KInotify::KInotify(bool blocking) : KObject(KTYPE_INOTIFY), blocking(blocking), waitCond(std::make_shared<BoxedWineCondition>(B("KInotify::waitCond"))) {
}

KInotify::~KInotify() {
}

U32 KInotify::ioctl(KThread* thread, U32 request) {
    return -K_ENOTSUP;
}

S64 KInotify::seek(S64 pos) {
    return 0;
}

S64 KInotify::length() {
    return 0;
}

S64 KInotify::getPos() {
    return 0;
}

void KInotify::setBlocking(bool blocking) {
    this->blocking = blocking;
}

bool KInotify::isBlocking() {
    return this->blocking;
}

KFileLock* KInotify::getLock(KFileLock* lock) {
    return nullptr;
}

U32 KInotify::setLock(KFileLock* lock, bool wait) {
    return -K_ENOTSUP;
}

void KInotify::unlockAll(U32 pid) {
}

bool KInotify::supportsLocks() {
    return false;
}

bool KInotify::isOpen() {
    return true;
}

bool KInotify::isReadReady() {
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(waitCond);
    return !this->pendingEvents.empty();
}

bool KInotify::isWriteReady() {
    return false;
}

void KInotify::waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) {
    if (events & K_POLLIN) {
        BOXEDWINE_CONDITION_ADD_PARENT(this->waitCond, parentCondition);
    } else {
        BOXEDWINE_CONDITION_REMOVE_PARENT(this->waitCond, parentCondition);
    }
}

U32 KInotify::write(KThread* thread, U32 buffer, U32 len) {
    return -K_ENOTSUP;
}

U32 KInotify::writeNative(U8* buffer, U32 len) {
    return -K_ENOTSUP;
}

U32 KInotify::read(KThread* thread, U32 buffer, U32 len) {
    if (len > 1024 * 1024) {
        len = 1024 * 1024; // records are small; a larger buffer adds nothing
    }
    std::vector<U8> data(len);
    U32 result = this->readNative(data.data(), len);
    if ((S32)result > 0) {
        thread->memory->memcpy(buffer, data.data(), result);
    }
    return result;
}

U32 KInotify::readNative(U8* buffer, U32 len) {
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(waitCond);
    while (true) {
        if (!this->pendingEvents.empty()) {
            U32 written = 0;
            while (!this->pendingEvents.empty()) {
                const PendingEvent& event = this->pendingEvents.front();
                U32 nameLen = (event.name.length() + 1 + 3) & ~3u;
                U32 recordSize = 16 + nameLen;
                if (written + recordSize > len) {
                    if (written == 0) {
                        return -K_EINVAL; // next record can never fit this buffer
                    }
                    break;
                }
                U8* out = buffer + written;
                memcpy(out, &event.wd, 4);
                memcpy(out + 4, &event.mask, 4);
                memcpy(out + 8, &event.cookie, 4);
                memcpy(out + 12, &nameLen, 4);
                memset(out + 16, 0, nameLen);
                if (event.name.length()) {
                    memcpy(out + 16, event.name.c_str(), event.name.length());
                }
                written += recordSize;
                this->pendingEvents.pop_front();
            }
            if (this->pendingEvents.empty()) {
                this->overflowPending = false;
            }
            return written;
        }
        if (!this->blocking) {
            return -K_EAGAIN;
        }
        BOXEDWINE_CONDITION_WAIT(waitCond);
#ifdef BOXEDWINE_MULTI_THREADED
        KThread* thread = KThread::currentThread();
        if (thread && thread->terminating) {
            return -K_EINTR;
        }
        if (thread && thread->startSignal) {
            thread->startSignal = false;
            return -K_CONTINUE;
        }
#endif
    }
}

U32 KInotify::stat(KProcess* process, U32 address, bool is64) {
    return -K_ENOTSUP;
}

U32 KInotify::map(KThread* thread, U32 address, U32 len, S32 prot, S32 flags, U64 off) {
    return -K_ENOTSUP;
}

bool KInotify::canMap() {
    return false;
}

BString KInotify::selfFd() {
    return B("anon_inode:inotify");
}

S32 KInotify::addWatch(const std::shared_ptr<FsNode>& node, U32 mask) {
    if ((mask & K_IN_ONLYDIR) && !node->isDirectory()) {
        return -K_ENOTDIR;
    }
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(waitCond);
    for (auto& entry : this->watches) {
        std::shared_ptr<FsNode> watched = entry.second.node.lock();
        if (watched && watched.get() == node.get()) {
            if (mask & K_IN_MASK_CREATE) {
                return -K_EEXIST;
            }
            entry.second.mask |= mask; // re-adding merges, as on Linux
            return (S32)entry.first;
        }
    }
    U32 wd = this->nextWatchDescriptor++;
    Watch watch;
    watch.node = node;
    watch.mask = mask;
    this->watches[wd] = watch;
    return (S32)wd;
}

S32 KInotify::removeWatch(U32 wd) {
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(waitCond);
    auto it = this->watches.find(wd);
    if (it == this->watches.end()) {
        return -K_EINVAL;
    }
    this->watches.erase(it);
    this->queueLocked((S32)wd, K_IN_IGNORED, 0, B(""));
    BOXEDWINE_CONDITION_SIGNAL_ALL(this->waitCond);
    return 0;
}

void KInotify::queueLocked(S32 wd, U32 mask, U32 cookie, const BString& name) {
    if (!this->pendingEvents.empty()) {
        const PendingEvent& last = this->pendingEvents.back();
        if (last.wd == wd && last.mask == mask && last.cookie == cookie && last.name == name) {
            return; // coalesce consecutive identical events, as on Linux
        }
    }
    if (this->pendingEvents.size() >= 16384) {
        if (!this->overflowPending) {
            this->overflowPending = true;
            PendingEvent overflow;
            overflow.wd = -1;
            overflow.mask = K_IN_Q_OVERFLOW;
            overflow.cookie = 0;
            this->pendingEvents.push_back(overflow);
        }
        return;
    }
    PendingEvent event;
    event.wd = wd;
    event.mask = mask;
    event.cookie = cookie;
    event.name = name;
    this->pendingEvents.push_back(event);
}

void KInotify::sweep(FsNode* parent, FsNode* subject, U32 childType, const BString& childName, bool childIsDir, U32 selfType, U32 cookie) {
    std::vector<U32> dropWds;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(waitCond);
        for (auto& entry : this->watches) {
            Watch& watch = entry.second;
            std::shared_ptr<FsNode> watched = watch.node.lock();
            if (!watched) {
                continue;
            }
            if (parent && childType && watched.get() == parent) {
                if (watch.mask & childType) {
                    this->queueLocked((S32)entry.first, childType | (childIsDir ? K_IN_ISDIR : 0), cookie, childName);
                    if (watch.mask & K_IN_ONESHOT) {
                        dropWds.push_back(entry.first);
                    }
                }
            }
            if (subject && selfType && watched.get() == subject) {
                if (watch.mask & selfType) {
                    this->queueLocked((S32)entry.first, selfType | (watched->isDirectory() ? K_IN_ISDIR : 0), cookie, B(""));
                    if (watch.mask & K_IN_ONESHOT) {
                        dropWds.push_back(entry.first);
                    }
                }
                if (selfType == K_IN_DELETE_SELF) {
                    // Linux removes the watch after IN_DELETE_SELF.
                    dropWds.push_back(entry.first);
                }
            }
        }
        for (U32 wd : dropWds) {
            if (this->watches.erase(wd)) {
                this->queueLocked((S32)wd, K_IN_IGNORED, 0, B(""));
            }
        }
        BOXEDWINE_CONDITION_SIGNAL_ALL(this->waitCond);
    }
}

void KInotify::registerInstance(const std::shared_ptr<KInotify>& instance) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(instancesMutex);
    instances.push_back(instance);
}

void KInotify::sweepAll(FsNode* parent, FsNode* subject, U32 childType, const BString& childName, bool childIsDir, U32 selfType, U32 cookie) {
    std::vector<std::shared_ptr<KInotify>> live;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(KInotify::instancesMutex);
        auto& instances = KInotify::instances;
        for (auto it = instances.begin(); it != instances.end();) {
            if (std::shared_ptr<KInotify> instance = it->lock()) {
                live.push_back(instance);
                ++it;
            } else {
                it = instances.erase(it);
            }
        }
    }
    for (auto& instance : live) {
        instance->sweep(parent, subject, childType, childName, childIsDir, selfType, cookie);
    }
}

void KInotify::notifyChildAdded(FsNode* parent, FsNode* child) {
    if (!parent || !child) {
        return;
    }
    sweepAll(parent, nullptr, K_IN_CREATE, child->name, child->isDirectory(), 0, 0);
}

void KInotify::notifyNodeRemoved(FsNode* parent, FsNode* node) {
    if (!node) {
        return;
    }
    sweepAll(parent, node, parent ? K_IN_DELETE : 0, node->name, node->isDirectory(), K_IN_DELETE_SELF, 0);
}

void KInotify::notifyRenamed(FsNode* fromParent, const BString& fromName, FsNode* toParent, const BString& toName, FsNode* node) {
    if (!node) {
        return;
    }
    U32 cookie = bw64InotifyRenameCookie.fetch_add(1);
    if (fromParent) {
        sweepAll(fromParent, nullptr, K_IN_MOVED_FROM, fromName, node->isDirectory(), 0, cookie);
    }
    if (toParent) {
        sweepAll(toParent, nullptr, K_IN_MOVED_TO, toName, node->isDirectory(), 0, cookie);
    }
    sweepAll(nullptr, node, 0, B(""), false, K_IN_MOVE_SELF, 0);
}

// File-level events (modify/open/close/attrib) are reported both to
// watches on the file itself (empty name) and to watches on its parent
// directory (with the file's name), per inotify(7).
void KInotify::fileEvent(FsNode* node, U32 type) {
    if (!node) {
        return;
    }
    std::shared_ptr<FsNode> parent = node->getParent().lock();
    if (parent) {
        sweepAll(parent.get(), nullptr, type, node->name, node->isDirectory(), 0, 0);
    }
    sweepAll(nullptr, node, 0, B(""), false, type, 0);
}

void KInotify::notifyModified(FsNode* node) {
    fileEvent(node, K_IN_MODIFY);
}

void KInotify::notifyOpened(FsNode* node) {
    fileEvent(node, K_IN_OPEN);
}

void KInotify::notifyClosed(FsNode* node, bool wasWrite) {
    fileEvent(node, wasWrite ? K_IN_CLOSE_WRITE : K_IN_CLOSE_NOWRITE);
}

void KInotify::notifyAttrib(FsNode* node) {
    fileEvent(node, K_IN_ATTRIB);
}
