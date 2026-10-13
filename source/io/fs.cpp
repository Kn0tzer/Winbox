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

#include "fsfilenode.h"
#include "fsvirtualnode.h"
#include "fsdynamiclinknode.h"
#include "bufferaccess.h"

#ifdef BOXEDWINE_ZLIB
#include "fszip.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

#include UNISTD
#include MKDIR_INCLUDE

std::atomic_int Fs::nextNodeId=1;

std::shared_ptr<FsFileNode> Fs::rootNode;
BString Fs::nativePathSeperator;

#define FS_PATH_CACHE_MAX 4096
#define FS_PATH_CACHE_STATS_INTERVAL 50000

std::unordered_map<Fs::FsPathCacheKey, Fs::FsPathCacheEntry, Fs::FsPathCacheKeyHash> Fs::pathCache;
std::list<Fs::FsPathCacheKey> Fs::pathCacheLru;
BOXEDWINE_MUTEX Fs::pathCacheMutex;
std::atomic<U64> Fs::pathCacheHits(0);
std::atomic<U64> Fs::pathCacheMisses(0);

void Fs::shutDown() {
	Fs::clearPathCache();
	rootNode = nullptr;
}
bool Fs::initFileSystem(const BString& rootPath) {
    Fs::nextNodeId = 1;
    Fs::clearPathCache();
    BString path;
    Fs::nativePathSeperator = (char)std::filesystem::path::preferred_separator;
    if (rootPath.endsWith("/")) {
        path = rootPath.substr(0, rootPath.length()-1);
    } else if (rootPath.endsWith("\\")) {
        path = rootPath.substr(0, rootPath.length()-1);
    } else {
        path = rootPath;
    }

    if (MKDIR(rootPath.c_str())==0) {
        klog_fmt("Created root directory: %s", rootPath.c_str());
    }

    std::shared_ptr<FsNode> parent(nullptr);
    rootNode = std::make_shared<FsFileNode>(Fs::nextNodeId++, 0, B("/"), B(""), path, true, true, parent);

    std::shared_ptr<FsNode> dir = Fs::getNodeFromLocalPath(B(""), B("/tmp/del"), false, nullptr);
    if (dir) {
        std::vector<std::shared_ptr<FsNode> > children;
        dir->getAllChildren(children);
        for (U32 i=0;i<children.size();i++) {
            children[i]->remove();
        }
    }
    std::shared_ptr<FsNode> lock = Fs::getNodeFromLocalPath(B(""), B("/tmp/.X0-lock"), false, nullptr);
    if (lock) {
        lock->remove();
    }
    return true;
}

void Fs::remoteNameToLocal(BString& path) {
    path.replace(Fs::nativePathSeperator, "/");
    path.replace("(_colon_)", ":");
    path.replace("(_at_)", "@");
}

void Fs::localNameToRemote(BString& path) {
    path.replace("/", Fs::nativePathSeperator);
    path.replace(":", "(_colon_)");
    path.replace("@", "(_at_)");
}

BString Fs::localFromNative(const BString& path) {
    BString result = path;
    remoteNameToLocal(result);
    return result;
}

BString Fs::nativeFromLocal(const BString& path) {
    BString result = path;
    localNameToRemote(result);
    return result;
}

std::shared_ptr<FsNode> Fs::addVirtualFile(const BString& path, std::function<FsOpenNode* (const std::shared_ptr<FsNode>& node, U32 flags, U32 data)> func, U32 mode, U32 rdev, const std::shared_ptr<FsNode>& parent, U32 data) {
    std::shared_ptr<FsNode> result = std::make_shared<FsVirtualNode>(Fs::nextNodeId++, rdev, path, func, mode, parent, data);
    parent->addChild(result);
    return result;
}

std::shared_ptr<FsNode> Fs::addVirtualFile(const BString& path, U32 mode, U32 rdev, const std::shared_ptr<FsNode>& parent, const BString& value) {
    std::shared_ptr<FsNode> result = std::make_shared<FsVirtualNode>(Fs::nextNodeId++, rdev, path, [value](const std::shared_ptr<FsNode>& node, U32 flags, U32 data) {
        return new BufferAccess(node, flags, value);
        }, mode, parent, 0);
    parent->addChild(result);
    return result;
}

std::shared_ptr<FsNode> Fs::addDynamicLinkFile(const BString& path, U32 rdev, const std::shared_ptr<FsNode>& parent, bool isDirectory, const BString& link) {
    std::shared_ptr<FsNode> result = std::make_shared<FsDynamicLinkNode>(Fs::nextNodeId++, rdev, path, parent, isDirectory, [link]() {
        return link;
        });
    parent->addChild(result);
    return result;
}

std::shared_ptr<FsNode> Fs::addDynamicLinkFile(const BString& path, U32 rdev, const std::shared_ptr<FsNode>& parent, bool isDirectory, std::function<BString(void)> fnGetLink) {
    std::shared_ptr<FsNode> result = std::make_shared<FsDynamicLinkNode>(Fs::nextNodeId++, rdev, path, parent, isDirectory, fnGetLink);
    parent->addChild(result);
    return result;
}

std::shared_ptr<FsNode> Fs::getNodeFromLocalPath(const BString& currentDirectory, const BString& path, bool followLink, bool* isLink) {
    return Fs::getNodeFromLocalPath(currentDirectory, path, nullptr, nullptr, followLink, isLink);
}

bool Fs::isPathCacheEnabled() {
    static bool disabled = (getenv("BW64_NOPATHCACHE") != nullptr);
    return !disabled;
}

static bool isPathCacheStatsEnabled() {
    static bool enabled = (getenv("BW64_PATHCACHE_STATS") != nullptr);
    return enabled;
}

void Fs::clearPathCache() {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(Fs::pathCacheMutex);
    Fs::pathCache.clear();
    Fs::pathCacheLru.clear();
}

void Fs::logPathCacheStats() {
    U64 hits = Fs::pathCacheHits.load(std::memory_order_relaxed);
    U64 misses = Fs::pathCacheMisses.load(std::memory_order_relaxed);
    U64 total = hits + misses;
    double rate = total ? (100.0 * (double)hits / (double)total) : 0.0;
    U32 entries;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(Fs::pathCacheMutex);
        entries = (U32)Fs::pathCache.size();
    }
    klog_fmt("pathcache: %llu hits, %llu misses (%.1f%% hit rate), %u entries", hits, misses, rate, entries);
}

void Fs::invalidatePathCachePrefix(const BString& prefix) {
    if (!Fs::isPathCacheEnabled() || prefix.length() == 0) {
        return;
    }
    BString dirPrefix = prefix + "/";
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(Fs::pathCacheMutex);
    for (auto it = Fs::pathCache.begin(); it != Fs::pathCache.end();) {
        const BString& p = it->first.path;
        if (p == prefix || p.startsWith(dirPrefix)) {
            Fs::pathCacheLru.erase(it->second.lruIt);
            it = Fs::pathCache.erase(it);
        } else {
            ++it;
        }
    }
}

static void pathCacheTouch(std::unordered_map<Fs::FsPathCacheKey, Fs::FsPathCacheEntry, Fs::FsPathCacheKeyHash>& cache,
                           std::list<Fs::FsPathCacheKey>& lru, Fs::FsPathCacheEntry& e) {
    lru.splice(lru.begin(), lru, e.lruIt);
}

static void pathCacheEvictOne(std::unordered_map<Fs::FsPathCacheKey, Fs::FsPathCacheEntry, Fs::FsPathCacheKeyHash>& cache,
                              std::list<Fs::FsPathCacheKey>& lru) {
    const Fs::FsPathCacheKey& victim = lru.back();
    cache.erase(victim);
    lru.pop_back();
}

bool Fs::lookupPathCache(const BString& fullpath, bool followLink, bool allowNegative, bool* isLink, std::shared_ptr<FsNode>& result) {
    result = nullptr;
    if (!Fs::isPathCacheEnabled()) {
        return false;
    }
    FsPathCacheKey key{fullpath, followLink};
    bool resolved = false;
    bool wantStats = false;
    {
        BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(Fs::pathCacheMutex);
        auto it = Fs::pathCache.find(key);
        if (it == Fs::pathCache.end()) {
            Fs::pathCacheMisses.fetch_add(1, std::memory_order_relaxed);
        } else {
            FsPathCacheEntry& e = it->second;
            bool valid = false;
            std::shared_ptr<FsNode> node;
            if (e.isNegative) {
                // Negative entries are only usable when the caller doesn't need
                // miss details (lastNode/missingParts). Coherency comes from the
                // parent's childrenVersion: any add/remove bumps it.
                if (allowNegative) {
                    std::shared_ptr<FsNode> parent = e.negParent.lock();
                    if (parent && parent->childrenVersion.load(std::memory_order_acquire) == e.negVersion) {
                        valid = true;
                    }
                }
            } else {
                node = e.node.lock();
                if (node && !node->removedFromParent.load(std::memory_order_acquire) && node->path == e.nodePath) {
                    valid = true;
                    for (auto& hop : e.linkHops) {
                        std::shared_ptr<FsNode> linkNode = hop.linkNode.lock();
                        if (!linkNode || linkNode->removedFromParent.load(std::memory_order_acquire) || linkNode->getLink() != hop.target) {
                            valid = false;
                            break;
                        }
                    }
                }
            }
            if (!valid) {
                Fs::pathCacheLru.erase(e.lruIt);
                Fs::pathCache.erase(it);
                Fs::pathCacheMisses.fetch_add(1, std::memory_order_relaxed);
            } else {
                pathCacheTouch(Fs::pathCache, Fs::pathCacheLru, e);
                Fs::pathCacheHits.fetch_add(1, std::memory_order_relaxed);
                if (!e.isNegative && isLink) {
                    *isLink = e.finalWasLink;
                }
                result = node;
                resolved = true;
            }
        }
        U64 total = Fs::pathCacheHits.load(std::memory_order_relaxed) + Fs::pathCacheMisses.load(std::memory_order_relaxed);
        wantStats = isPathCacheStatsEnabled() && (total % FS_PATH_CACHE_STATS_INTERVAL) == 0 && total > 0;
    }
    if (wantStats) {
        Fs::logPathCacheStats();
    }
    return resolved;
}

void Fs::storePositivePathCache(const BString& fullpath, bool followLink, const std::shared_ptr<FsNode>& node,
                                const std::vector<FsPathCacheLinkHop>& linkHops, bool finalWasLink) {
    if (!Fs::isPathCacheEnabled() || !node) {
        return;
    }
    FsPathCacheKey key{fullpath, followLink};
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(Fs::pathCacheMutex);
    if (Fs::pathCache.find(key) != Fs::pathCache.end()) {
        return; // already cached (raced); keep the existing entry
    }
    while (Fs::pathCache.size() >= FS_PATH_CACHE_MAX) {
        pathCacheEvictOne(Fs::pathCache, Fs::pathCacheLru);
    }
    FsPathCacheEntry e;
    e.isNegative = false;
    e.node = node;
    e.nodePath = node->path;
    e.linkHops = linkHops;
    e.finalWasLink = finalWasLink;
    Fs::pathCacheLru.push_front(key);
    e.lruIt = Fs::pathCacheLru.begin();
    Fs::pathCache.emplace(std::move(key), std::move(e));
}

void Fs::storeNegativePathCache(const BString& fullpath, bool followLink, const std::shared_ptr<FsNode>& parent, U32 parentVersion) {
    if (!Fs::isPathCacheEnabled() || !parent) {
        return;
    }
    FsPathCacheKey key{fullpath, followLink};
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(Fs::pathCacheMutex);
    if (Fs::pathCache.find(key) != Fs::pathCache.end()) {
        return; // already cached (raced); keep the existing entry
    }
    while (Fs::pathCache.size() >= FS_PATH_CACHE_MAX) {
        pathCacheEvictOne(Fs::pathCache, Fs::pathCacheLru);
    }
    FsPathCacheEntry e;
    e.isNegative = true;
    e.negParent = parent;
    e.negVersion = parentVersion;
    Fs::pathCacheLru.push_front(key);
    e.lruIt = Fs::pathCacheLru.begin();
    Fs::pathCache.emplace(std::move(key), std::move(e));
}

BString Fs::getFullPath(const BString& currentDirectory, const BString& path) {
    BString fullpath;

    if (path.startsWith("/"))
        fullpath = path;
    else
        fullpath = currentDirectory+"/"+path;
    if (fullpath.endsWith("/."))
        fullpath = fullpath.substr(0, fullpath.length()-2);
    fullpath.replace("//", "/");
    return fullpath;
}

bool cleanPath(std::vector<BString>& parts) {
    for (U32 i=0;i<parts.size();) {
        if (parts[i].length()==0) { // ignore double slashes
            parts.erase(parts.begin()+i, parts.begin()+i+1);
            continue;
        }
        if (parts[i]==".") {
            i++;
            continue;
        }
        if (parts[i]=="..") {
            if (i==0)
                return false;
            parts.erase(parts.begin()+i-1, parts.begin()+i+1);
            i--;
            continue;
        }
        i++;
    }
    return true;
}

std::shared_ptr<FsNode> Fs::getNodeFromLocalPath(const BString& currentDirectory, const BString& path, std::shared_ptr<FsNode>* lastNode, std::vector<BString>* missingParts, bool followLink, bool* isLink) {
    BString fullpath = Fs::getFullPath(currentDirectory, path);

    if (fullpath.length()==0 || fullpath=="/")
        return Fs::rootNode;

    // Negative cache entries can't fill lastNode/missingParts, so only allow
    // them when the caller doesn't need miss details.
    bool allowNegative = (lastNode == nullptr && missingParts == nullptr);
    std::shared_ptr<FsNode> cachedNode;
    if (Fs::lookupPathCache(fullpath, followLink, allowNegative, isLink, cachedNode)) {
        return cachedNode;
    }

    std::vector<BString> parts;
    Fs::splitPath(fullpath, parts);
    std::shared_ptr<FsNode> node = Fs::rootNode;
    if (!node) {
        return nullptr;
    }
    std::vector<std::shared_ptr<FsNode> > nodes;

    nodes.push_back(node);

    // Path-cache bookkeeping. Walks that traverse dynamic links are never cached.
    bool traversedDynamicLink = false;
    bool finalWasLink = false;
    std::vector<FsPathCacheLinkHop> linkHops;

    if (!cleanPath(parts))
        return nullptr;
    for (U32 i=0;i<parts.size();) {
        if (parts[i].length()==0) { // ignore double slashes
            i++;
            continue;
        }
        if (parts[i]==".") {
            i++;
            continue;
        }
        std::shared_ptr<FsNode> parentNode = node;
        // Version check around the lookup: if the directory changes concurrently,
        // don't cache a negative result for it.
        U32 v0 = parentNode->childrenVersion.load(std::memory_order_acquire);
        node = parentNode->getChildByName(parts[i]);
        U32 v1 = parentNode->childrenVersion.load(std::memory_order_acquire);
        if (!node) {
            if (missingParts) {
                for (; i < parts.size(); i++) {
                    missingParts->push_back(parts[i]);
                }
            }
            if (lastNode) {
                *lastNode = nodes.back();
            }
            if (allowNegative && !traversedDynamicLink && v0 == v1) {
                Fs::storeNegativePathCache(fullpath, followLink, parentNode, v1);
            }
            return nullptr;
        }
        nodes.push_back(node);
        if (node->isLink() && (followLink || i<parts.size()-1)) {
            if (node->isDynamicLink()) {
                traversedDynamicLink = true;
            } else {
                FsPathCacheLinkHop hop;
                hop.linkNode = node;
                hop.target = node->getLink();
                linkHops.push_back(std::move(hop));
            }
            if (i==parts.size()-1) {
                finalWasLink = true;
                if (isLink) {
                    *isLink = true;
                }
            }

            std::vector<BString> linkParts;
            Fs::splitPath(node->getLink(), linkParts);
            parts.erase(parts.begin()+i, parts.begin()+i+1);
            parts.insert(parts.begin()+i, linkParts.begin(), linkParts.end());
            if (node->getLink().startsWith("/")) {
                parts.erase(parts.begin(), parts.begin()+i);
            }   
            if (!cleanPath(parts))
                return nullptr;

            nodes.clear();
            node = Fs::rootNode;
            nodes.push_back(node);
            i=0;
            continue;
        }
        i++;
    }
    if (!traversedDynamicLink) {
        Fs::storePositivePathCache(fullpath, followLink, node, linkHops, finalWasLink);
    }
    return node;
}

std::shared_ptr<FsFileNode> Fs::addFileNode(const BString& path, const BString& link, const BString& nativePath, bool isDirectory, const std::shared_ptr<FsNode>& parent) {
    std::shared_ptr<FsFileNode> result = std::make_shared<FsFileNode>(Fs::nextNodeId++, 0, path, link, nativePath, isDirectory, false, parent);
    parent->addChild(result);
    return result;
}

std::shared_ptr<FsNode> Fs::addRootDirectoryNode(const BString& path, const BString& nativePath, const std::shared_ptr<FsNode>& parent) {
    std::shared_ptr<FsFileNode> result = std::make_shared<FsFileNode>(Fs::nextNodeId++, 0, path, B(""), nativePath, true, false, parent);
    parent->addChild(result);
    result->isRootPath = true;
    return result;
}

BString Fs::getParentPath(const BString& path) {
    return path.substr(0, path.lastIndexOf('/', path.length()-2));
}

BString Fs::getNativeParentPath(const BString& path) {
    int pos = path.lastIndexOf(Fs::nativePathSeperator);
    if (pos != -1) {
        return path.substr(0, pos);
    }
    pos = path.lastIndexOf('/');
    if (pos != -1) {
        return path.substr(0, pos);
    }
    return path;
}

BString Fs::getFileNameFromPath(const BString& path) {
    int pos = path.lastIndexOf('/');
    if (pos==-1) {
        return path;
    }
    return path.substr(pos+1);
}

BString Fs::getFileNameFromNativePath(const BString& path) {
    int pos = path.lastIndexOf(Fs::nativePathSeperator);
    if (pos==-1) {
        return path;
    }
    return path.substr(pos+1);
}

bool Fs::doesNativePathExist(const BString& path) {
    if (::access(path.c_str(), 0)!=-1) {
        return true;
    }
    return false;
}

bool Fs::isNativeDirectoryEmpty(const BString& path) {
    bool result = true;
    iterateAllNativeFiles(path, false, true, [&result](BString fileName, bool isDir)->U32 {
        result = false;
        return 1; // don't need to continue;
    });
    return result;
}

U64 Fs::getNativeFileSize(const BString& path) {
    PLATFORM_STAT_STRUCT buf;
    if (PLATFORM_STAT(path.c_str(), &buf)==0) {
        return buf.st_size;
    }
    return 0;
}

U64 Fs::getNativeDirectorySize(const BString& path, bool recursive) {
    U64 result = 0;
    iterateAllNativeFiles(path, recursive, true, [&result, path](BString fileName, bool isDir)->U32 {
        if (!isDir) {
            result += Fs::getNativeFileSize(fileName);
        }
        return 0;
    });
    return result;
}

bool Fs::isNativePathDirectory(const BString& path) {
    PLATFORM_STAT_STRUCT buf = {};

#ifdef BOXEDWINE_MSVC
    if (path.length()<3) {
        BString msvc = path + "\\";
        if (PLATFORM_STAT(msvc.c_str(), &buf)==0) {
            return S_ISDIR(buf.st_mode);
        }
    }
#endif
    if (PLATFORM_STAT(path.c_str(), &buf)==0) {
        return S_ISDIR(buf.st_mode);
    }
    return false;
}

U32 Fs::makeLocalDirs(const BString& path) {
    std::shared_ptr<FsNode> lastNode;
    std::vector<BString> missingParts;
    std::shared_ptr<FsNode> node = Fs::getNodeFromLocalPath(B(""), path, &lastNode, &missingParts, false);    
    std::vector<std::shared_ptr<FsNode> > nodes;
    bool notFound = false;

    if (!node) {
        node = lastNode;
        notFound = true;
    }
    while (node) {
        nodes.push_back(node);
        node = node->getParent().lock();
    }
    std::reverse(nodes.begin(), nodes.end());
    for (U32 i=0;i<nodes.size();i++) {
        std::shared_ptr<FsNode> nodePart = nodes[i];
        if (!Fs::doesNativePathExist(nodePart->nativePath)) {
            U32 result = MKDIR(nodePart->nativePath.c_str());
            if (result) {
                return -translateErr(errno);
            }
        }
    }
    if (notFound) {
        BString base = lastNode->path;        
        for (U32 i=0;i<missingParts.size();i++) {
            node = Fs::getNodeFromLocalPath(B(""), base, false);

            if (!base.endsWith("/")) // will be / if root
                base = base + "/";
            base = base + missingParts[i];
            BString nativePath = missingParts[i];
            Fs::localNameToRemote(nativePath);
            nativePath = node->nativePath.stringByApppendingPath(nativePath);
            U32 result = MKDIR(nativePath.c_str());
            if (result) {
                return -translateErr(errno);
            }
            std::shared_ptr<FsNode> childNode = Fs::addFileNode(base, B(""), nativePath, true, node);
        }
    }
    return 0;
}

void Fs::splitPath(const BString& path, std::vector<BString>& parts) {
    if (path.startsWith("/")) {
        BString tmp = path.substr(1);
        tmp.split('/', parts);
    } else {
        path.split('/', parts);
    }
}

U32 Fs::readNativeFile(const BString& nativePath, U8* buffer, U32 bufferLen) {
    int f = ::open(nativePath.c_str(), O_RDONLY | O_BINARY);
    if (f>=0) {
        U32 result = (U32)::read(f, buffer, bufferLen);
        ::close(f);
        return result;
    }
    return 0;
}

BString Fs::getNativePathFromParentAndLocalFilename(const std::shared_ptr<FsNode>& parent, const BString& fileName) {
    BString nativeFileName = fileName;
    Fs::localNameToRemote(nativeFileName);
    if (parent) {
        return parent->nativePath.stringByApppendingPath(nativeFileName);
    }
    return nativeFileName;
}

bool Fs::makeNativeDirs(const BString& path) {
    std::vector<BString> parts;

    BString tmp;
    if (path.startsWith(Fs::nativePathSeperator)) {
        path.substr(1).split(Fs::nativePathSeperator, parts);
        tmp = Fs::nativePathSeperator + parts[0];
    } else {
        path.split(Fs::nativePathSeperator, parts);
        tmp = parts[0];
    }
    parts.erase(parts.begin(), parts.begin()+1);

    for (auto& part : parts) {
        tmp = tmp.stringByApppendingPath(part);
        if (!Fs::doesNativePathExist(tmp)) {
            U32 result = MKDIR(tmp.c_str());
            if (result!=0) {
                return false;
            }
        }
    }
    return true;
}

U32 Fs::deleteNativeFile(const BString& path) {
    return unlink(path.c_str());
}

U32 Fs::deleteNativeDirAndAllFilesInDir(const BString& path) {
    std::error_code e; // will prevent it from throwing an error
    return (U32)std::filesystem::remove_all(path.c_str(), e);
}

U32 Fs::iterateAllNativeFiles(const BString& path, bool recursive, bool includeDirs, std::function<U32(BString, bool isDir)> f) {
    std::vector<Platform::ListNodeResult> results;
    Platform::listNodes(path, results);
    for (auto& n : results) {
        if (recursive && n.isDirectory) {
            U32 result = iterateAllNativeFiles(path.stringByApppendingPath(n.name), true, includeDirs, f);
            if (result) {
                return result;
            }
        }
        if (!includeDirs && n.isDirectory) {
            continue;
        }
        U32 result = f(path.stringByApppendingPath(n.name), n.isDirectory);
        if (result) {
            return result;
        }
    }
    return 0;
}

std::vector<BString> Fs::getFilesInNativeDirectoryWhereFileMatches(const BString& dirPath, const BString& startsWith, const BString& endsWith, bool ignoreCase) {
    std::vector<BString> results;
    Fs::iterateAllNativeFiles(dirPath, false, false, [&results, startsWith, endsWith, ignoreCase](BString filePath, bool isDir) {
        BString name = Fs::getFileNameFromNativePath(filePath);
        if ((startsWith.isEmpty() || name.startsWith(startsWith, ignoreCase)) && (endsWith.isEmpty() || name.endsWith(endsWith, ignoreCase))) {
            results.push_back(filePath);
        }
        return 0;
    });
    return results;
}

BString Fs::trimTrailingSlash(const BString& s) {
    if (s.endsWith('/') || s.endsWith('\\')) {
        return s.substr(0, s.length() - 1);
    }
    return s;
}

/*
Read - Only = 0x1
Hidden = 0x2
System = 0x4
Archive = 0x20
*/
BString Fs::getDosAttrib(const std::shared_ptr<FsNode>& file) {
    U8 buffer[1024] = { 0 };
    if (Fs::readNativeFile(file->nativePath + EXT_DOSATTRIB, buffer, 1024)) {
        return BString::copy((const char*)buffer);
    }
    return BString::empty;
}

void Fs::setDosAttrib(const std::shared_ptr<FsNode>& file, const BString& attrib) {
    BWriteFile w(file->nativePath + EXT_DOSATTRIB, true);
    if (w.isOpen()) {
        w.write(attrib);
    }
}

U32 Fs::removeDosAttrib(const std::shared_ptr<FsNode>& file) {
    BString nativePath = file->nativePath + EXT_DOSATTRIB;
    if (Fs::doesNativePathExist(nativePath)) {
        deleteNativeFile(nativePath);
        return 0;
    } else {
        return -K_ENODATA; // ENOATTR
    }
}