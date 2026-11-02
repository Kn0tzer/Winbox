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

#ifndef __FSNODE_H__
#define __FSNODE_H__

#include "platformBoxedwine.h"
#include "kfilelock.h"
#include <atomic>

// on windows one.txt and One.txt are the same file but on Linux they are different
#define EXT_MIXED ".mixed"
#define EXT_DOSATTRIB ".user.DOSATTRIB"
#define EXT_LINK ".link"

class FsOpenNode;
class KProcess;

// One hard-link group (FD-60): every FsNode created by link() from one
// source shares a group. Membership is by weak pointer, so a name whose
// node is destroyed drops out of the group.
struct FsNodeLinkGroup {
    BOXEDWINE_MUTEX mutex;
    std::vector<std::weak_ptr<FsNode>> members;
    // FD-81: true when the group's content identity is a single
    // shared backing path (an emulated hard link, used when the
    // backing store cannot hard-link) rather than one host hard
    // link per name. Members must then consult the group - not
    // the host - for link counts and backing-file lifetime.
    bool sharedBackingPath = false;
};
class KThread;
class KObject;

class FsNode : public std::enable_shared_from_this<FsNode> {
public:
    enum class Type
    {
        File,
        Zip,
        Virtual,
        Socket,
        Memory,
        Timer
    };
    FsNode(Type type, U32 id, U32 rdev, BString path, BString link, BString nativePath, bool isDirectory, std::shared_ptr<FsNode> parent);
    virtual ~FsNode() {}

    virtual U32 rename(BString path)=0; //return 0 if success, else errno
    virtual bool remove()=0;
    virtual U64 lastModified()=0; // returns ms since 1970
    virtual U64 length()=0;
    virtual FsOpenNode* open(U32 flags)=0;    
    virtual U32 getType(bool checkForLink)=0;
    virtual U32 getMode()=0;
    virtual U32 removeDir()=0;
    virtual U32 setTimes(U64 lastAccessTime, U32 lastAccessTimeNano, U64 lastModifiedTime, U32 lastModifiedTimeNano)=0;

    virtual bool canRead();
    virtual bool canWrite();

    virtual BString getLink() {return this->link;}
    virtual bool isLink() { return this->link.length() > 0; }
    // True for links whose target is computed dynamically per call (e.g. /proc/self/fd).
    // The path cache never caches walks that traverse these.
    virtual bool isDynamicLink() { return false; }

    virtual U32 getHardLinkCount();
    // FD-58: a successful chmod/fchmod stores the guest-visible
    // permission bits here (0..07777); -1 means no chmod has happened
    // and the node's synthetic getMode() answer stands. Stat writers
    // report getStatMode(); access checks (canRead/canWrite) keep
    // using getMode(), so a chmod never changes what the emulator
    // itself allows - exactly the boundary the FD-58 record states.
    std::atomic<S32> chmodMode{-1};
    void applyChmodMode(U32 mode) { this->chmodMode.store((S32)(mode & 07777), std::memory_order_relaxed); }
    U32 getStatMode() {
        S32 c = this->chmodMode.load(std::memory_order_relaxed);
        U32 m = this->getMode();
        return c >= 0 ? (m & ~0xFFFu) | (U32)c : m;
    }
    // FD-60 hard-link group (see FsNodeLinkGroup above). Members
    // report the group's live size as their link count and receive
    // the group's file-level inotify events, as Linux delivers them.
    std::shared_ptr<FsNodeLinkGroup> linkGroup;
    std::vector<std::shared_ptr<FsNode>> linkGroupMembers();
    void joinLinkGroup(const std::shared_ptr<FsNode>& other);    
    bool isDirectory() {return this->isDir;}
    std::weak_ptr<FsNode> getParent() {return this->parent;}

    void removeOpenNode(FsOpenNode* node);
    // notify=false detaches silently: a rename moves a node between
    // parents without deleting it, and the only notifications a rename
    // may produce are the move pair (and IN_MOVE_SELF) that
    // KInotify::notifyRenamed issues.
    void removeNodeFromParent(bool notify = true);

    BString path; 
    BString nativePath;
    BString name;
    BString link;
    const U32 id;
    const U32 rdev;  
    U32 hardLinkCount;    
    const Type type;
    std::weak_ptr<KObject> kobject;

    // Path-cache coherency (see Fs::pathCache in fs.h):
    // - removedFromParent is set when the node is detached from its parent and
    //   cleared when (re-)added via addChild. Lets cache hits fail fast.
    // - childrenVersion is bumped on every addChild/removeChildByName so cached
    //   negative lookups ("not found") can detect the directory changed.
    std::atomic<bool> removedFromParent;
    std::atomic<U32> childrenVersion;

    std::shared_ptr<FsNode> getChildByName(BString name);
    std::shared_ptr<FsNode> getChildByNameIgnoreCase(BString name);

    U32 getChildCount();
    void addChild(std::shared_ptr<FsNode> node, bool notify = true);
    void removeChildByName(BString name);
    void getAllChildren(std::vector<std::shared_ptr<FsNode> > & results);

    U32 addLock(KFileLock* lock);
    bool unlock(KFileLock* lock);
    KFileLock* getLock(KFileLock* lock, bool otherProcess);
    U32 addLockAndWait(KFileLock* lock, bool otherProcess);
    bool hasLock(U32 pid);
    void unlockAll(U32 pid);

    void addOpenNode(KListNode<FsOpenNode*>* node);
protected:
    std::weak_ptr<FsNode> parent; // the parent holds a strong reference to the children

    KList<FsOpenNode*> openNodes;
    BOXEDWINE_MUTEX openNodesMutex;

private:
    const bool isDir;
    bool hasLoadedChildrenFromFileSystem;
    bool childrenLoadInProgress;    

    BHashTable<BString, std::shared_ptr<FsNode> > childrenByName;
    BOXEDWINE_MUTEX childrenByNameMutex;

    std::vector<KFileLock> locks;       
    BOXEDWINE_CONDITION locksCS;    

    void loadChildren();
    KFileLock* internalGetLock(KFileLock* lock, bool otherProcess);
};

#endif
