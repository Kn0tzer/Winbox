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
#include "kinotify.h"

#include "kstat.h"

FsNode::FsNode(Type type, U32 id, U32 rdev, BString path, BString link, BString nativePath, bool isDirectory, std::shared_ptr<FsNode> parent) :
    path(path),
    nativePath(nativePath),
    name(Fs::getFileNameFromPath(path)),    
    link(link),    
    id(id), 
    rdev(rdev),
    hardLinkCount(1),
    type(type),  
    removedFromParent(false),
    childrenVersion(0),
    parent(parent),
    isDir(isDirectory),      
    hasLoadedChildrenFromFileSystem(false), childrenLoadInProgress(false),
    locksCS(std::make_shared<BoxedWineCondition>(B("FsNode.lockCS")))
 {   
}

namespace {
BOXEDWINE_MUTEX bw64LinkGroupMutex;
}

U32 FsNode::getHardLinkCount() {
    if (this->linkGroup) {
        U32 n = (U32)this->linkGroupMembers().size();
        return n ? n : 1;
    }
    return this->hardLinkCount;
}

std::vector<std::shared_ptr<FsNode>> FsNode::linkGroupMembers() {
    std::vector<std::shared_ptr<FsNode>> result;
    if (!this->linkGroup) {
        result.push_back(shared_from_this());
        return result;
    }
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->linkGroup->mutex);
    auto& members = this->linkGroup->members;
    for (auto it = members.begin(); it != members.end();) {
        if (std::shared_ptr<FsNode> m = it->lock()) {
            result.push_back(m);
            ++it;
        } else {
            it = members.erase(it);
        }
    }
    return result;
}

void FsNode::joinLinkGroup(const std::shared_ptr<FsNode>& other) {
    // One file-scope mutex serializes group formation and merging;
    // membership is then read under each group's own mutex. Lock
    // order is always bw64LinkGroupMutex before a group's mutex.
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(bw64LinkGroupMutex);
    if (!this->linkGroup && !other->linkGroup) {
        this->linkGroup = std::make_shared<FsNodeLinkGroup>();
        other->linkGroup = this->linkGroup;
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->linkGroup->mutex);
        this->linkGroup->members.push_back(shared_from_this());
        this->linkGroup->members.push_back(other);
    } else if (this->linkGroup && !other->linkGroup) {
        other->linkGroup = this->linkGroup;
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->linkGroup->mutex);
        this->linkGroup->members.push_back(other);
    } else if (!this->linkGroup && other->linkGroup) {
        this->linkGroup = other->linkGroup;
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->linkGroup->mutex);
        this->linkGroup->members.push_back(shared_from_this());
    } else if (this->linkGroup != other->linkGroup) {
        std::vector<std::shared_ptr<FsNode>> others = other->linkGroupMembers();
        for (auto& m : others) {
            m->linkGroup = this->linkGroup;
        }
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->linkGroup->mutex);
        for (auto& m : others) {
            this->linkGroup->members.push_back(m);
        }
    }
    std::vector<std::shared_ptr<FsNode>> members = this->linkGroupMembers();
    for (auto& m : members) {
        m->hardLinkCount = (U32)members.size();
    }
}

void FsNode::removeOpenNode(FsOpenNode* node) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->openNodesMutex);
    node->listNode.remove();
}

bool FsNode::canRead() {
    return (getMode() & K__S_IREAD)!=0;
}

bool FsNode::canWrite() {
    return (getMode() & K__S_IWRITE)!=0;
}

void FsNode::removeNodeFromParent(bool notify) {
    this->removedFromParent.store(true, std::memory_order_release);
    BString oldPath = this->path;
    std::shared_ptr<FsNode> parent = this->getParent().lock();
    if (parent) {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(parent->childrenByNameMutex);
        parent->childrenByName.remove(this->name);
        parent->childrenVersion.fetch_add(1, std::memory_order_relaxed);
    }
    // Invalidate this path and any descendant paths (no node lock held here).
    Fs::invalidatePathCachePrefix(oldPath);
    if (notify) {
        KInotify::notifyNodeRemoved(parent.get(), this);
    }
}

void FsNode::loadChildren() {
    // The loaded flag also prevents recursion through addChild. Keep it under
    // the directory lock so another thread cannot see it before publication.
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(childrenByNameMutex);
    if (!this->hasLoadedChildrenFromFileSystem) {
        this->hasLoadedChildrenFromFileSystem = true;
        // Children inserted by this load already exist on the host; they
        // are not creations. addChild must not report them to inotify
        // (Linux never reports pre-existing files), so the load loop runs
        // with notifications suppressed for this directory.
        this->childrenLoadInProgress = true;
        bool traceLink = getenv("BW64_LINKTRACE") && this->path.contains("dosdevices");
        if (this->nativePath.length()) {
            std::vector<Platform::ListNodeResult> results;
            bool listed = Platform::listNodes(nativePath, results);
            if (!listed && Fs::doesNativePathExist(nativePath)) {
                // The host path exists but could not be listed, so the
                // guest is about to see an empty directory that is not
                // empty. (Zip-backed nodes legitimately have no
                // materialized native path; those do not reach this
                // line because the existence check fails first.)
                static std::atomic<unsigned long long> listFailures{0};
                unsigned long long n = ++listFailures;
                if (n <= 64 || n % 1024 == 0) {
                    klog_fmt("BW64-FAIL dir-list path=%s native=%s outcome=directory presented as empty count=%llu", this->path.c_str(), nativePath.c_str(), n);
                }
            }
            if (traceLink) {
                klog_fmt("LINKTRACE loadChildren path='%s' native='%s' found %d entries",
                         this->path.c_str(), this->nativePath.c_str(), (int)results.size());
            }
            for (auto& n : results) {
                if (traceLink) {
                    klog_fmt("LINKTRACE   entry '%s' isDir=%d", n.name.c_str(), n.isDirectory?1:0);
                }
                BString localPath = this->path;
                BString remotePath = this->nativePath.stringByApppendingPath(n.name);
                if (!localPath.endsWith("/")) {
                    localPath += "/";
                }
                Fs::remoteNameToLocal(n.name);
                localPath+=n.name;
                if (localPath.endsWith(EXT_MIXED)) {
                    localPath.remove(localPath.length() - 6);
                }
                if (localPath.endsWith(EXT_DOSATTRIB)) {
                    continue;
                }
                if (localPath.contains(EXT_XATTR)) {
                    // FD-165: a generic xattr sidecar is storage
                    // for its sibling file, never a guest file.
                    continue;
                }
                if (!localPath.endsWith(EXT_LINK)) {
                    Fs::addFileNode(localPath, B(""), remotePath, n.isDirectory, shared_from_this());
                } else {
                    U8 tmp[MAX_FILEPATH_LEN];
                    U32 result = Fs::readNativeFile(remotePath, tmp, MAX_FILEPATH_LEN-1);
                    tmp[result]=0;
                    if (result==0) {
                        kwarn_fmt("Could not read link file from filesystem: %s", localPath.c_str());
                        continue;
                    }
                    localPath = localPath.substr(0, localPath.length()-5);
                    if (traceLink) {
                        klog_fmt("LINKTRACE   -> link node localPath='%s' target='%s'",
                                 localPath.c_str(), (const char*)tmp);
                    }
                    Fs::addFileNode(localPath, BString::copy((const char*)tmp), remotePath, n.isDirectory, shared_from_this());
                }
            }
        }
        this->childrenLoadInProgress = false;
    }
}


std::shared_ptr<FsNode> FsNode::getChildByName(BString name) {
    this->loadChildren();
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->childrenByNameMutex);
    std::shared_ptr<FsNode> result = this->childrenByName[name];
    if (!result) {
        // fallback: VFS name variations for dosdevices symlinks (z: <-> z(_colon_), .link suffix)
        if (name.contains(":")) {
            result = this->childrenByName[name.replace(":", "(_colon_)")];
        }
        if (!result && !name.endsWith(".link")) {
            result = this->childrenByName[name + ".link"];
        }
        if (!result && name.endsWith(".link")) {
            result = this->childrenByName[name.substr(0, name.length() - 5)];
        }
        if (!result && name.contains(":")) {
            result = this->childrenByName[name.replace(":", "(_colon_)") + ".link"];
        }
    }
    return result;
}

std::shared_ptr<FsNode> FsNode::getChildByNameIgnoreCase(BString name) {
    this->loadChildren();
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->childrenByNameMutex);
    for (auto& n : this->childrenByName) {
        if (n.key.compareTo(name, true) == 0) {
            return n.value;
        }
    }
    return nullptr;
}

U32 FsNode::getChildCount() {    
    this->loadChildren();
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->childrenByNameMutex);
    return (U32)this->childrenByName.size();
}

void FsNode::addChild(std::shared_ptr<FsNode> node, bool notify) {
    this->loadChildren();
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->childrenByNameMutex);
    this->childrenByName.set(node->name, node);
    node->removedFromParent.store(false, std::memory_order_release);
    this->childrenVersion.fetch_add(1, std::memory_order_relaxed);
#ifdef BW64_HAS_KINOTIFY
    if (notify && !this->childrenLoadInProgress) {
        KInotify::notifyChildAdded(this, node.get());
    }
#endif
}

void FsNode::removeChildByName(BString name) {
    this->loadChildren();
    std::shared_ptr<FsNode> child;
    BString childPath;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->childrenByNameMutex);
        child = this->childrenByName[name];
        if (child) {
            child->removedFromParent.store(true, std::memory_order_release);
            childPath = child->path;
        }
        this->childrenByName.remove(name);
        this->childrenVersion.fetch_add(1, std::memory_order_relaxed);
    }
    // Followed dynamic links (including descendants) are never path-cached.
    // Unfollowed hits validate removedFromParent, and negative hits validate
    // childrenVersion. Closing a /proc/<pid>/fd link needs no cache-wide scan.
    if (child && !child->isDynamicLink()) {
        Fs::invalidatePathCachePrefix(childPath);
    }
}

void FsNode::getAllChildren(std::vector<std::shared_ptr<FsNode> > & results) {
    this->loadChildren();
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->childrenByNameMutex);
    for (auto& n : this->childrenByName) {
        results.push_back(n.value);
    }
}

bool FsNode::unlock(KFileLock* lock) {
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->locksCS);
    KFileLock* found = internalGetLock(lock, false);
    bool result = false;

    while (found) {
        this->locks.erase(std::remove(locks.begin(), locks.end(), *found), locks.end());
        found = internalGetLock(lock, false);
        result = true;
    }
    if (result) {
        BOXEDWINE_CONDITION_SIGNAL(this->locksCS);
    }
    return result;
}

U32 FsNode::addLock(KFileLock* lock) {
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->locksCS);
    KFileLock* found = internalGetLock(lock, true);
    if (found) {
        return -K_EAGAIN;
    }
    this->locks.push_back(*lock);    
    return 0;
}

U32 FsNode::addLockAndWait(KFileLock* lock, bool otherProcess) {
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->locksCS);
    KFileLock* found = internalGetLock(lock, otherProcess);

    while (found) {
        BOXEDWINE_CONDITION_WAIT(this->locksCS);
        found = internalGetLock(lock, otherProcess);
    }
    this->locks.push_back(*lock);
    return 0;
}

bool FsNode::hasLock(U32 pid) {
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->locksCS);

    for (auto& n : this->locks) {
        KFileLock* next = &n;
        if (next->l_pid == pid) {
            return true;
        }
    }
    return false;
}

void FsNode::unlockAll(U32 pid) {
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->locksCS);
    auto it = std::remove_if(locks.begin(), locks.end(), [pid](auto&& item)
        { return (item.l_pid == pid); });
    locks.erase(it, locks.end());
    BOXEDWINE_CONDITION_SIGNAL(this->locksCS);
}

KFileLock* FsNode::getLock(KFileLock* lock, bool otherProcess) {
    BOXEDWINE_CRITICAL_SECTION_WITH_CONDITION(this->locksCS);
    return internalGetLock(lock, otherProcess);
}

KFileLock* FsNode::internalGetLock(KFileLock* lock, bool otherProcess) {    
    U64 l1 = lock->l_start;
    U64 l2 = l1+lock->l_len;
    U32 pid = KThread::currentThread()->process->id;

    if (lock->l_len == 0)
        l2 = 0xFFFFFFFF;
    for( auto& n : this->locks ) {
        KFileLock* next = &n;
        U64 s1 = next->l_start;
        U64 s2 = s1+next->l_len;
        
        if (next->l_len == 0)
            s2 = 0xFFFFFFFF;
        if (((s1>=l1 && s1<=l2) || (s2>=l1 && s2<=l2)) && ((otherProcess && next->l_pid != pid) || (!otherProcess && next->l_pid == pid))) {
            return next;
        }
    }
    return nullptr;
 }

void FsNode::addOpenNode(KListNode<FsOpenNode*>* node) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->openNodesMutex);
    this->openNodes.addToBack(node);
}
