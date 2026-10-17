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

#ifndef __FS_H__
#define __FS_H__

#include "platformBoxedwine.h"
#include "fsnode.h"
#include "fsopennode.h"
#include <unordered_map>

#define K_O_RDONLY   0x0000
#define K_O_WRONLY   0x0001
#define K_O_RDWR     0x0002
#define K_O_ACCMODE  0x0003

#define K_O_CREAT	   0x0040
#define K_O_EXCL	   0x0080
#define K_O_TRUNC	   0x0200
#define K_O_APPEND     0x0400

// can change after open
#define K_O_NONBLOCK  0x000800
#define K_O_ASYNC     0x002000
#define K_O_LARGEFILE 0x008000
#define K_O_DIRECTORY 0x010000
#define K_O_CLOEXEC   0x080000	
#define K_O_TMPFILE   0x400000

#define FD_CLOEXEC 1

#define K_F_SEAL_SEAL           0x01
#define K_F_SEAL_SHRINK         0x02
#define K_F_SEAL_GROW           0x04
#define K_F_SEAL_WRIT           0x08
#define K_F_SEAL_FUTURE_WRITE   0x10

// type of lock
#define K_F_RDLCK	   0
#define K_F_WRLCK	   1
#define K_F_UNLCK	   2

#define IOCTL_ARG1 EDX
#define IOCTL_ARG2 ESI
#define IOCTL_ARG3 EDI
#define IOCTL_ARG4 EBP

#define FS_BLOCK_SIZE 8192

typedef FsOpenNode* (*OpenVirtualNode)(const std::shared_ptr<FsNode>& node, U32 flags, U32 data);

class FsFileNode;

#define k_mdev(x,y) ((x << 8) | y)

class Fs {
public:   
    static bool initFileSystem(const BString& rootPath);
    static std::shared_ptr<FsNode> getNodeFromLocalPath(const BString& currentDirectory, const BString& path, bool followLink, bool* isLink=nullptr);    
    static std::shared_ptr<FsFileNode> addFileNode(const BString& path, const BString& link, const BString& nativePath, bool isDirectory, const std::shared_ptr<FsNode>& parent);
    static std::shared_ptr<FsNode> addVirtualFile(const BString& path, std::function<FsOpenNode*(const std::shared_ptr<FsNode>& node, U32 flags, U32 data)> func, U32 mode, U32 rdev, const std::shared_ptr<FsNode>& parent, U32 data=0);
    static std::shared_ptr<FsNode> addVirtualFile(const BString& path, U32 mode, U32 rdev, const std::shared_ptr<FsNode>& parent, const BString& value);
    static std::shared_ptr<FsNode> addDynamicLinkFile(const BString& path, U32 rdev, const std::shared_ptr<FsNode>& parent, bool isDirectory, std::function<BString(void)> fnGetLink);
    static std::shared_ptr<FsNode> addDynamicLinkFile(const BString& path, U32 rdev, const std::shared_ptr<FsNode>& parent, bool isDirectory, const BString& link);
    static std::shared_ptr<FsNode> addRootDirectoryNode(const BString& path, const BString& nativePath, const std::shared_ptr<FsNode>& parent);
    static void remoteNameToLocal(BString& path);
    static void localNameToRemote(BString& path);
    static BString localFromNative(const BString& path);
    static BString nativeFromLocal(const BString& path);
    static U32 makeLocalDirs(const BString& path);
    static bool makeNativeDirs(const BString& path);
    static U32 deleteNativeFile(const BString& path);
    static U32 deleteNativeDirAndAllFilesInDir(const BString& path);
    static U32 iterateAllNativeFiles(const BString& path, bool recursive, bool includeDirs, std::function<U32(BString filePath,bool isDir)> f);
    static BString getParentPath(const BString& path);
    static BString getNativeParentPath(const BString& path);
    static BString getFileNameFromPath(const BString& path);
    static BString getFileNameFromNativePath(const BString& path);
    static U32 readNativeFile(const BString& nativePath, U8* buffer, U32 bufferLen);
    static void splitPath(const BString& path, std::vector<BString>& parts);
    static bool doesNativePathExist(const BString& path);
    static bool isNativeDirectoryEmpty(const BString& path);
    static U64 getNativeDirectorySize(const BString& path, bool recursive);
    static U64 getNativeFileSize(const BString& path);
    static bool isNativePathDirectory(const BString& path);
    static BString getFullPath(const BString& currentDirectory, const BString& path);
    static BString getNativePathFromParentAndLocalFilename(const std::shared_ptr<FsNode>& parent, const BString& fileName);    
    static std::vector<BString> getFilesInNativeDirectoryWhereFileMatches(const BString& dirPath, const BString& startsWith, const BString& endsWith, bool ignoreCase);
    static BString trimTrailingSlash(const BString& s);

    static BString nativePathSeperator;

    static BString getDosAttrib(const std::shared_ptr<FsNode>& file);
    static void setDosAttrib(const std::shared_ptr<FsNode>& file, const BString& attrib);
    static U32 removeDosAttrib(const std::shared_ptr<FsNode>& file);

    static std::shared_ptr<FsFileNode> rootNode;
	static void shutDown();

    // --- VFS path cache ---
    // Maps a canonical full path (+followLink flag) to the node it resolves to,
    // so repeated opens of the same path don't walk from the root.
    // Coherency:
    //  * Positive entries are validated on hit (node alive, not detached via
    //    FsNode::removedFromParent, node->path unchanged, traversed static link
    //    targets unchanged). Walks that traverse dynamic links are never cached.
    //  * Negative entries ("not found") carry the parent dir's childrenVersion;
    //    any addChild/removeChildByName bumps it, so stale negatives fail fast.
    //  * removeNodeFromParent/removeChildByName/rename do prefix invalidation
    //    for the removed subtree, except dynamic links whose hits validate
    //    lazily as above. shutDown/initFileSystem clear everything.
    // Locking: pathCacheMutex is a leaf lock - it is never held while acquiring
    // a node lock, so no lock-ordering issues with childrenByNameMutex.
    struct FsPathCacheKey {
        BString path;
        bool followLink;
        bool operator==(const FsPathCacheKey& o) const { return followLink == o.followLink && path == o.path; }
    };
    struct FsPathCacheKeyHash {
        std::size_t operator()(const FsPathCacheKey& k) const {
            std::size_t h = std::hash<BString>()(k.path);
            return k.followLink ? h ^ 0x9e3779b97f4a7c15ULL : h;
        }
    };
    struct FsPathCacheLinkHop {
        std::weak_ptr<FsNode> linkNode; // static link traversed during the walk
        BString target;                 // its target at cache time
    };
    struct FsPathCacheEntry {
        bool isNegative = false;
        std::weak_ptr<FsNode> node;                  // positive only
        BString nodePath;                            // positive only: node->path at store time
        std::vector<FsPathCacheLinkHop> linkHops;    // positive only
        bool finalWasLink = false;                   // positive only: for the isLink out-param
        std::weak_ptr<FsNode> negParent;             // negative only
        U32 negVersion = 0;                          // negative only: parent childrenVersion
        std::list<FsPathCacheKey>::iterator lruIt;
    };

    static void clearPathCache();
    static void logPathCacheStats();
    // Erase entries for exactly `prefix` and any path under `prefix + "/"`.
    static void invalidatePathCachePrefix(const BString& prefix);
private:
    friend class KUnixSocketObject;

    static std::shared_ptr<FsNode> getNodeFromLocalPath(const BString& currentDirectory, const BString& path, std::shared_ptr<FsNode>* lastNode, std::vector<BString>* missingParts, bool followLink, bool* isLink= nullptr);

    static std::atomic_int nextNodeId;

    static std::unordered_map<FsPathCacheKey, FsPathCacheEntry, FsPathCacheKeyHash> pathCache;
    static std::list<FsPathCacheKey> pathCacheLru;
    static BOXEDWINE_MUTEX pathCacheMutex;
    static std::atomic<U64> pathCacheHits;
    static std::atomic<U64> pathCacheMisses;

    static bool isPathCacheEnabled();
    // Returns true if the cache resolved the lookup (result set; nullptr = not found).
    static bool lookupPathCache(const BString& fullpath, bool followLink, bool allowNegative, bool* isLink, std::shared_ptr<FsNode>& result);
    static void storePositivePathCache(const BString& fullpath, bool followLink, const std::shared_ptr<FsNode>& node,
                                       const std::vector<FsPathCacheLinkHop>& linkHops, bool finalWasLink);
    static void storeNegativePathCache(const BString& fullpath, bool followLink, const std::shared_ptr<FsNode>& parent, U32 parentVersion);
};

#endif
