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

#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

#include UNISTD
#include UTIME
#include MKDIR_INCLUDE

#include "fsfilenode.h"
#include "kinotify.h"
#include "fsdiropennode.h"
#include "fsfileopennode.h"
#include "kstat.h"
#include "fszipnode.h"
#include "knativethread.h"

std::set<BString> FsFileNode::nonExecFileFullPaths;

FsFileNode::FsFileNode(U32 id, U32 rdev, BString path, BString link, BString nativePath, bool isDirectory, bool isRootPath, std::shared_ptr<FsNode> parent) : FsNode(Type::File, id, rdev, path, link, nativePath, isDirectory, parent), isRootPath(isRootPath) {
}

BString FsFileNode::getNativeTmpPath() {
    BString localPath;
    BString nativePath;
    getTmpPath(nativePath, localPath);
    return nativePath;
}

BString FsFileNode::getLocalTmpPath() {
    BString localPath;
    BString nativePath;
    getTmpPath(nativePath, localPath);
    return localPath;
}

static U32 nextTmpId;

void FsFileNode::getTmpPath(BString& nativePath, BString& localPath) {
    BOXEDWINE_CRITICAL_SECTION;
    Fs::makeLocalDirs(B("/tmp/del"));
    std::shared_ptr<FsNode> delDir = Fs::getNodeFromLocalPath(B(""), B("/tmp/del"), true);
    if (!delDir) {
        // The old code dereferenced the null node on the next line.
        klog_fmt("BW64-FAIL fs-tmp-path outcome=no /tmp/del node; tmp path left empty");
        nativePath = BString::empty;
        localPath = BString::empty;
        return;
    }

    for (;nextTmpId<100000000;nextTmpId++) {
        BString name = "del"+BString::valueOf(nextTmpId)+".tmp";
        Fs::localNameToRemote(name);
        nativePath = delDir->nativePath.stringByApppendingPath(name);
        if (!Fs::doesNativePathExist(nativePath)) {
            localPath = "/tmp/del/"+name;
            return;
        }
    }
    // Every tmp id is taken. The old loop fell out with nativePath still
    // holding the last (existing) candidate, and the caller renamed the
    // file being deleted onto that live path.
    klog_fmt("BW64-FAIL fs-tmp-path outcome=tmp id space exhausted; tmp path left empty");
    nativePath = BString::empty;
    localPath = BString::empty;
}

namespace {
// Host-open failures repeat (guests probe many paths), so report with
// the counting-cap shape: first 64, then every 1024th, always count=.
void logHostOpenFailure(const BString& path, const BString& nativePath, int err) {
    static std::atomic<unsigned long long> failures{0};
    unsigned long long n = ++failures;
    if (n <= 64 || n % 1024 == 0) {
        klog_fmt("BW64-FAIL host-open path=%s native=%s errno=%d (%s) outcome=open refused count=%llu", path.c_str(), nativePath.c_str(), err, strerror(err), n);
    }
}
}

namespace {
// FD-81: true when this node belongs to a shared-backing-path
// link group (see FsNodeLinkGroup) and at least one OTHER member
// is still attached to a parent - i.e. this name is not the last
// name for the shared backing file.
bool hasAttachedLinkSiblings(const FsNode* node) {
    if (!node->linkGroup || !node->linkGroup->sharedBackingPath) {
        return false;
    }
    for (auto& m : const_cast<FsNode*>(node)->linkGroupMembers()) {
        if (m.get() != node && !m->removedFromParent.load(std::memory_order_acquire)) {
            return true;
        }
    }
    return false;
}
}

bool FsFileNode::remove() {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->openNodesMutex);
    // FD-81: in a shared-backing-path link group, unlinking one
    // name must not delete the backing file while another name
    // still names it (POSIX unlink removes a name, not the
    // file). Detach only; the last name's remove() deletes the
    // backing through the normal path below. Open handles on
    // this node keep working: they hold host descriptors to the
    // still-existing backing file, as on Linux.
    if (hasAttachedLinkSiblings(this)) {
        this->removeNodeFromParent();
        return true;
    }
    bool result = false;
    bool exists = Fs::doesNativePathExist(this->nativePath);

    if (!exists) {
        this->removeNodeFromParent();
        return true;
    }
#ifdef BOXEDWINE_ZLIB
    if (zipNode) {
        std::shared_ptr<FsZip> fsZip = zipNode->fsZip.lock();
        if (fsZip) {
            fsZip->remove(this->path);
        }
        zipNode = nullptr;
        result = true;
    }
#endif
    if (exists) {
        BString dosAttrib = nativePath + EXT_DOSATTRIB;
        unlink(dosAttrib.c_str());
        result = unlink(nativePath.c_str()) == 0;        
    }
    // if the file failed to be deleted and it exists then its because someone else has it open, 
    // so we need to close all references, move the file then re-open the file for those handles
    // that still have it open.  By moving the file it will appear that it was deleted, but handles
    // that have it open can still use it, just like Linux does.
    if (!result && exists && this->openNodes.size()) {
        S64* tmpPos = new S64[this->openNodes.size()];
        U32 i=0;

        this->openNodes.for_each([&tmpPos,&i](KListNode<FsOpenNode*>* n) {
            FsOpenNode* openNode = n->data;
            if (openNode->isOpen()) {
                tmpPos[i++] = openNode->getFilePointer();
                openNode->close();
            }
        });
        
        Fs::makeLocalDirs(B("/tmp/del"));
        std::shared_ptr<FsNode> delDir = Fs::getNodeFromLocalPath(B(""), B("/tmp/del"), true);

        BString newNativePath = FsFileNode::getNativeTmpPath();

        if (::rename(nativePath.c_str(), newNativePath.c_str())!=0) {
            int err = errno;
            klog_fmt("BW64-FAIL fs-remove-rename path=%s native=%s dest=%s errno=%d (%s) outcome=file stays at old path; open handles keep the old location", this->path.c_str(), nativePath.c_str(), newNativePath.c_str(), err, strerror(err));
        }

        this->nativePath = newNativePath;

        i=0;
        this->openNodes.for_each([&tmpPos,&i](KListNode<FsOpenNode*>* n) {
            FsOpenNode* openNode = n->data;
            openNode->reopen();
            if (openNode->isOpen()) {
                openNode->seek(tmpPos[i++]);
            }
        });
        result = true;
        delete[] tmpPos;
    }
    if (result) {
        this->removeNodeFromParent();
    }
    return result;
}

U64 FsFileNode::lastModified() {
    PLATFORM_STAT_STRUCT buf;

    if (PLATFORM_STAT(this->nativePath.c_str(), &buf)==0) {
        if (buf.st_mtime == 0) {
            // I've seen this happen with the Age of Empires installer on Raspberry Pi 5
            return ((U64)buf.st_ctime) * 1000l;
        }
        return ((U64)buf.st_mtime)*1000l;
    }
#ifdef BOXEDWINE_ZLIB
    if (this->zipNode)
        return this->zipNode->lastModified();
#endif
    return 0;
}

U64 FsFileNode::length() {    
    if (this->isDirectory())
        return 4096;

    PLATFORM_STAT_STRUCT buf;
    if (PLATFORM_STAT(this->nativePath.c_str(), &buf)==0) {
        return buf.st_size;
    }
#ifdef BOXEDWINE_ZLIB
    if (this->zipNode)
        return this->zipNode->length();
#endif
    return 0;
}

BString FsFileNode::getLink() {
#if defined(BOXEDWINE_ZLIB) && !defined(__EMSCRIPTEN__)
    if (KSystem::cacheReads) {
        ensurePathIsLocal(false);
    }
#endif
    return this->link; 
}

void FsFileNode::ensurePathIsLocal(bool prepareForWrite) {
#ifdef BOXEDWINE_ZLIB
    BOXEDWINE_CRITICAL_SECTION;
    if (this->zipNode && !Fs::doesNativePathExist(this->nativePath)) {
        if (this->isDirectory()) {
            Fs::makeLocalDirs(this->path);
        } else {
            BString parentPath = Fs::getParentPath(this->path);
            Fs::makeLocalDirs(parentPath);
            this->zipNode->moveToFileSystem(shared_from_this());
        }
    } else if (prepareForWrite) {
        std::shared_ptr<FsNode> parent = this->getParent().lock();
        if (parent && parent->type == Type::File) {
            BString parentPath = Fs::getParentPath(this->path);
            Fs::makeLocalDirs(parentPath);
        }
    }
#endif
}

FsOpenNode* FsFileNode::open(U32 flags) {
    U32 openFlags = O_BINARY;
            
    if (this->isDirectory()) {
        std::shared_ptr<FsNode> n = Fs::getNodeFromLocalPath(B(""), this->path, true);
        if (!n) {
            return nullptr;
        }
        return new FsDirOpenNode(n, flags);
    }
    if ((flags & K_O_ACCMODE)==K_O_RDONLY) {
        openFlags|=O_RDONLY;
        // make the file local so that it will load faster (no inflate and weird seeking logic)
#if defined(BOXEDWINE_ZLIB) && !defined(__EMSCRIPTEN__)
        if (KSystem::cacheReads && this->zipNode) {
            ensurePathIsLocal(false);
        }
#endif
    } else {
        if ((flags & K_O_ACCMODE)==K_O_WRONLY) {
            openFlags|=O_WRONLY;        
        } else {
            openFlags|=O_RDWR;            
        }
        BString parentPath = Fs::getNativeParentPath(this->nativePath);
        if (!Fs::doesNativePathExist(parentPath)) {
            ensurePathIsLocal(true);
        }
#ifdef BOXEDWINE_ZLIB
        else if (this->zipNode) {
            ensurePathIsLocal(false);
        }
#endif
    }
    if (flags & K_O_CREAT) {
        openFlags|=O_CREAT;
    }
    if (flags & K_O_EXCL) {
        openFlags|=O_EXCL;
    }
    if (flags & K_O_TRUNC) {
        openFlags|=O_TRUNC;
    }
    if (flags & K_O_APPEND) {
        openFlags|=O_APPEND;
    }
    U32 f;

#ifdef BOXEDWINE_MSVC
    if (this->nativePath.length() > 255) {
        BString path = "\\\\?\\" + nativePath;
        f = ::open(path.c_str(), openFlags, 0666);
    } else
#endif
    f = ::open(this->nativePath.c_str(), openFlags, 0666);	
    if (f==0xFFFFFFFF) {
        int openErr = errno;
#ifdef BOXEDWINE_ZLIB
        if (this->zipNode && (flags & K_O_ACCMODE)==K_O_RDONLY)
            return this->zipNode->open(shared_from_this(), flags);
#endif
        // The caller turns errno into the guest error, so it must
        // survive this function; but a failed host open used to leave
        // no trace at all. ENOENT/ENOTDIR are routine guest probing
        // (existence checks) and are not failures worth a line.
        if (openErr != ENOENT && openErr != ENOTDIR) {
            logHostOpenFailure(this->path, this->nativePath, openErr);
        }
        errno = openErr;
        return nullptr;
    }
    return new FsFileOpenNode(std::dynamic_pointer_cast<FsFileNode>(shared_from_this()), flags, f);
}

U32 FsFileNode::getType(bool checkForLink) {	
    if (this->isDirectory())
        return 4; // DT_DIR
    if (checkForLink && this->isLink()) 
        return 10; // DT_LNK
    return 8; // DT_REG
}

U32 FsFileNode::getHardLinkCount() {
    if (this->linkGroup) {
        if (this->linkGroup->sharedBackingPath) {
            // FD-81: one shared backing file carries a host link
            // count of 1 no matter how many guest names point at
            // it; the group's live membership is the truth (the
            // same liveness rule FsNode applies to non-file
            // nodes, with the same stated lag while an unlinked
            // name's node is still open).
            return FsNode::getHardLinkCount();
        }
        // FD-60: the guest's names are mirrored by host hard links
        // (see KProcess::link), so the host link count is the truth -
        // including right after an unlink drops a name while this
        // node stays alive through an open descriptor.
        PLATFORM_STAT_STRUCT buf;
        if (PLATFORM_STAT(this->nativePath.c_str(), &buf) == 0) {
            return (U32)buf.st_nlink;
        }
        // Own host path is gone (this name was unlinked while open):
        // count the surviving names.
        U32 n = 0;
        for (auto& m : this->linkGroupMembers()) {
            if (m.get() != this && Fs::doesNativePathExist(m->nativePath)) {
                n++;
            }
        }
        return n;
    }
    return FsNode::getHardLinkCount();
}

U32 FsFileNode::getMode() {
    U32 result = K__S_IREAD | (FsFileNode::getType(false) << 12);
    if (this->path == "/etc/sudoers") {
        return result | K__S_IRGRP;
    }
    if (this->path.endsWith("/.wine/wineserver")) {
        return result; // Debian Wine server rendezvous file: must report exactly 0400
    }
    if (!FsFileNode::nonExecFileFullPaths.count(this->path)) {
        result |= K__S_IEXEC;
        if (this->path.startsWith("/usr/local/bin") || this->path.startsWith("/usr/bin") || this->path.startsWith("/bin") || this->path.startsWith("/opt/wine/bin")) {
            result |= K__S_IXGRP | K__S_IXOTH;
        }
    }
    if (KThread::currentThread()->process->userId == 0 || this->path.startsWith("/tmp") || this->path.startsWith("/var") || this->path.startsWith("/home")) {
        result |= K__S_IWRITE;
        // wine server needs to be private, but winetricks check "-w" in the script on /tmp which needs these 2
        if (!this->path.startsWith("/tmp/.wine") && !this->path.startsWith("/tmp/wine-")) {
            result |= K__S_IWGRP | K__S_IWOTH;
        }
    }
    return result;
}

S32 translateErr(U32 e) {
    switch (e) {
    case EPERM: return K_EPERM;
    case ENOENT: return K_ENOENT;
    case ESRCH: return K_ESRCH;
    case EINTR: return K_EINTR;
    case EIO: return K_EIO;
    case ENXIO: return K_ENXIO;
    //case E2BIG: return K_E2BIG;
    case ENOEXEC: return K_ENOEXEC;
    case EBADF: return K_EBADF;
    case ECHILD: return K_ECHILD;
    case EAGAIN: return K_EAGAIN;
    case ENOMEM: return K_ENOMEM;
    case EACCES: return K_EACCES;
    case EFAULT: return K_EFAULT;
    case EBUSY: return K_EBUSY;
    case EEXIST: return K_EEXIST;
    case EXDEV: return K_EXDEV;
    case ENODEV: return K_ENODEV;
    case ENOTDIR: return K_ENOTDIR;
    case EISDIR: return K_EISDIR;
    case ENFILE: return K_ENFILE;
    case EMFILE: return K_EMFILE;
    case ENOTTY: return K_ENOTTY;
    case EFBIG: return K_EFBIG;
    case ENOSPC: return K_ENOSPC;
    case ESPIPE: return K_ESPIPE;
    case EROFS: return K_EROFS;
    //case EMLINK: return K_EMLINK;
    case EPIPE: return K_EPIPE;
    //case EDOM: return K_EDOM;
    //case EDEADLK: return K_EDEADLK;
    case ENAMETOOLONG: return K_ENAMETOOLONG;
    case ENOLCK: return K_ENOLCK;
    case ENOSYS: return K_ENOSYS;
    case ENOTEMPTY: return K_ENOTEMPTY;
    default: return K_EIO;
    }
}

U32 FsFileNode::rename(BString path) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->openNodesMutex);
    U32 result=0;
    BString oldPath = this->path;
    std::vector<S64> tmpPos;

    this->ensurePathIsLocal(false);
    if (this->openNodes.size()) {
        int i=0;
        for (U32 x=0;x<this->openNodes.size();x++) {
            tmpPos.push_back(-1);
        }
        this->openNodes.for_each([&tmpPos,&i](KListNode<FsOpenNode*>* n) {
            FsOpenNode* openNode = n->data;
            if (openNode->isOpen()) {
                tmpPos[i] = openNode->getFilePointer();
                openNode->close();
            }
            i++;
        });
    }

    std::shared_ptr<FsNode> parent = Fs::getNodeFromLocalPath(B(""), Fs::getParentPath(path), true);
    if (!parent) {
        return -K_ENOENT;
    }

    BString fileName = Fs::getFileNameFromPath(path);
    if (hasAttachedLinkSiblings(this)) {
        // FD-81: renaming one name of a shared-backing-path link
        // group must not move the shared backing file out from
        // under the other names. POSIX rename moves a directory
        // entry, not the file, so move only the guest tree entry
        // and keep this node's native path (the shared backing)
        // unchanged: content, identity and counts stay shared.
        std::shared_ptr<FsNode> existingNode = Fs::getNodeFromLocalPath(B(""), path, false);
        if (existingNode && existingNode.get() != this) {
            if (existingNode->isDirectory() != this->isDirectory()) {
                if (existingNode->isDirectory())
                    return -K_EISDIR;
                return -K_ENOTDIR;
            }
            if (existingNode->isDirectory()) {
                result = existingNode->removeDir();
                if (result != 0)
                    return result;
            } else {
                existingNode->remove();
            }
        }
        this->removeNodeFromParent(false);
        this->path = path;
        this->name = fileName;
        std::shared_ptr<FsNode> parentNode = Fs::getNodeFromLocalPath(B(""), Fs::getParentPath(path), false);
        parentNode->addChild(shared_from_this(), false);
        this->parent = parentNode;
        if (oldPath != this->path) {
            Fs::invalidatePathCachePrefix(oldPath);
        }
        int i = 0;
        this->openNodes.for_each([&tmpPos, &i](KListNode<FsOpenNode*>* n) {
            if (i < (int)tmpPos.size() && tmpPos.at(i) != -1) {
                FsOpenNode* openNode = n->data;
                if (openNode) {
                    openNode->reopen();
                    openNode->seek(tmpPos.at(i));
                }
            }
            i++;
        });
        return 0;
    }
    Fs::localNameToRemote(fileName);
    BString nativePath = parent->nativePath.stringByApppendingPath(fileName);
    BString originalPath;

    if (this->isLink()) {
        nativePath = nativePath + EXT_LINK;
    }

    for (U32 i=0;i<3;i++) {
        if (Fs::doesNativePathExist(nativePath)) {
            std::shared_ptr<FsNode> existingNode = Fs::getNodeFromLocalPath(B(""), path, false);
            if (!existingNode) {
                // we must be on windows, nativePath and path are case insensitive the same, but are different case
                //            
                originalPath = path;
            } else {
                if (existingNode->isDirectory()!=this->isDirectory()) {
                    if (existingNode->isDirectory())
                        return -K_EISDIR;
                    return -K_ENOTDIR;
                }
                if (existingNode->isDirectory()) {
                    result = existingNode->removeDir();
                    if (result!=0)
                        return result;
                } else {
                    existingNode->remove();
                }
            }
        }
        if (originalPath.length()) {     
            nativePath = nativePath+EXT_MIXED;
        }    
        result = ::rename(this->nativePath.c_str(), nativePath.c_str());
        if (result==0) {
            BString dosAttrib = this->nativePath + EXT_DOSATTRIB;
            if (Fs::doesNativePathExist(dosAttrib)) {
                BString dosAttribDst = nativePath + EXT_DOSATTRIB;
                ::rename(dosAttrib.c_str(), dosAttribDst.c_str());
            }
            this->removeNodeFromParent(false);
#ifdef BOXEDWINE_ZLIB
            if (zipNode) {
                std::shared_ptr<FsZip> fsZip = zipNode->fsZip.lock();
                if (fsZip) {
                    fsZip->remove(this->path);
                }
                zipNode = nullptr;
            }
#endif
            this->path = path;
            this->nativePath = nativePath;
            this->name = Fs::getFileNameFromPath(path);
            BString parentPath = Fs::getParentPath(path);
            std::shared_ptr<FsNode> parentNode = Fs::getNodeFromLocalPath(B(""), parentPath, false);
            parentNode->addChild(shared_from_this(), false);
            this->parent = parentNode;            
        } else {
            result = -translateErr(errno);
            KNativeThread::sleep(100); // not sure why on Windows doesNativePathExist fails sometimes, debian stretch /usr/bin/dpkg -i /var/local/xbitmaps_1.1.1-2_all.deb seems to cause this in a reproducible way
            continue;
        }
        break;
    }
    if (result == 0 && oldPath != this->path) {
        // The node (and any descendants) moved: drop cached paths under the old prefix.
        // No node lock is held here.
        Fs::invalidatePathCachePrefix(oldPath);
    }
    int i=0;
    this->openNodes.for_each([&tmpPos,&i](KListNode<FsOpenNode*>* n) {
        if (i<(int)tmpPos.size() && tmpPos.at(i)!=-1) {
            FsOpenNode* openNode = n->data;
            if (openNode) {
                openNode->reopen();
                openNode->seek(tmpPos.at(i));
            }
        }
        i++;
    });
    return result;
}

U32 FsFileNode::removeDir() {
    if (!this->isDirectory() || this->isLink())
        return -K_ENOTDIR;
    if (this->getChildCount()) {
        return -K_ENOTEMPTY;
    }    
    if (Fs::doesNativePathExist(this->nativePath) && ::rmdir(this->nativePath.c_str()) < 0) {
        return -translateErr(errno);
    }
    std::shared_ptr<FsNode> parent = this->getParent().lock();
    if (parent) {
        parent->removeChildByName(this->name);
    }
    return 0;
}

U32 FsFileNode::setTimes(U64 lastAccessTime, U32 lastAccessTimeNano, U64 lastModifiedTime, U32 lastModifiedTimeNano) {
    struct utimbuf settime = {0, 0};

    this->ensurePathIsLocal(false);
    if (lastAccessTime) {
        settime.actime = lastAccessTime;
    }
    if (lastModifiedTime) {
        settime.modtime = lastModifiedTime;
    }       
    if (utime(this->nativePath.c_str(), &settime) != 0) {
        // The syscall callers return this value to the guest, so a
        // failed utime used to be reported as success. Be honest, and
        // leave a trace: a guest that sets times on every file it
        // extracts would otherwise have no way to know none of them
        // stuck.
        int err = errno;
        static std::atomic<unsigned long long> failures{0};
        unsigned long long n = ++failures;
        if (n <= 64 || n % 1024 == 0) {
            klog_fmt("BW64-FAIL fs-settimes path=%s native=%s errno=%d (%s) outcome=guest told the truth count=%llu", this->path.c_str(), this->nativePath.c_str(), err, strerror(err), n);
        }
        return -translateErr(err);
    }
    KInotify::notifyAttrib(this);
    return 0;
}
