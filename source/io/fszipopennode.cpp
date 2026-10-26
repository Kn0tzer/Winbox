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

#ifdef BOXEDWINE_ZLIB

#include "fszipopennode.h"
#include "fszipnode.h"
#include "fszip.h"

#include <cstring>


FsZipOpenNode::FsZipOpenNode(std::shared_ptr<FsNode> node, std::shared_ptr<FsZipNode>& zipNode, U32 flags, U64 offset) : FsOpenNode(node, flags), zipNode(zipNode), pos(0), offset(offset) {
}

FsZipOpenNode::~FsZipOpenNode() {
    this->closeStream();
}

S64 FsZipOpenNode::length() {
    return this->zipNode->length();
}

bool FsZipOpenNode::setLength(S64 len) {
    // if this file was open for write, it would have been copied to the file system
    kwarn("FsZipOpenNode::setLength called on a read-only zip node; length unchanged");
    return false;
}

S64 FsZipOpenNode::getFilePointer() {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->readMutex);
    return this->pos;
}

S64 FsZipOpenNode::seek(S64 pos) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->readMutex);
    S64 len = (S64)this->zipNode->length();
    if (pos > len)
        this->pos = len;
    else
        this->pos = pos;
    return this->pos;
}

void FsZipOpenNode::close() {
}

bool FsZipOpenNode::isOpen() {
    return true;
}

U32 FsZipOpenNode::ioctl(KThread* thread, U32 request) {
    return -K_ENODEV;
}

void FsZipOpenNode::setAsync(bool isAsync) {
    this->applyAsyncFlag(isAsync);
}

bool FsZipOpenNode::isAsync() {
    return this->asyncEnabled;
}

void FsZipOpenNode::waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) {
    // Readiness for fs nodes is static (driven by the open mode), so mirror
    // DevDsp::waitForEvents: if the requested readiness already holds, wake
    // the poller instead of leaving it to time out.
    if (((events & K_POLLIN) && this->isReadReady()) || ((events & K_POLLOUT) && this->isWriteReady())) {
        BOXEDWINE_CONDITION_SIGNAL_ALL(parentCondition);
    }
}

bool FsZipOpenNode::isWriteReady() {
    return (this->flags & K_O_ACCMODE)!=K_O_RDONLY;
}

bool FsZipOpenNode::isReadReady() {
    return (this->flags & K_O_ACCMODE)!=K_O_WRONLY;
}

U32 FsZipOpenNode::map(KThread* thread, U32 address, U32 len, S32 prot, S32 flags, U64 off) {
    return 0;
}

bool FsZipOpenNode::canMap() {
    return true;
}

bool FsZipOpenNode::ensureStream() {
    if (this->stream) {
        return true;
    }
    std::shared_ptr<FsZip> fsZip = this->zipNode->fsZip.lock();
    if (!fsZip) {
        return false;
    }
    this->stream = fsZip->openEntryStream(this->offset);
    if (!this->stream) {
        return false;
    }
    this->streamPos = 0;
    return true;
}

void FsZipOpenNode::closeStream() {
    if (this->stream) {
        unzCloseCurrentFile(this->stream);
        unzClose(this->stream);
        this->stream = nullptr;
    }
}

U32 FsZipOpenNode::readFromStream(U8* dst, U32 len) {
    U32 total = 0;
    while (total < len) {
        int got = unzReadCurrentFile(this->stream, dst + total, len - total);
        if (got <= 0) {
            break;
        }
        total += (U32)got;
    }
    this->streamPos += total;
    std::shared_ptr<FsZip> fsZip = this->zipNode->fsZip.lock();
    if (fsZip) {
        fsZip->statBytesDecompressed += total;
    }
    return total;
}

void FsZipOpenNode::positionStream(U64 target) {
    // Stored entries permit direct seeks, including large VPKs and ZIP64.
    // Compressed entries keep the forward decompression path below.
    if (unzSeekStoredFile64(this->stream, target) == UNZ_OK) {
        this->streamPos = target;
        return;
    }
    // Streaming mode only: in cached mode the stream only moves forward
    // to extend the cache, so it is always exactly at cache.size().
    if (target < this->streamPos) {
        // zlib cannot seek backward: reopen the entry and skip forward.
        unzCloseCurrentFile(this->stream);
        unzOpenCurrentFile(this->stream);
        this->streamPos = 0;
        std::shared_ptr<FsZip> fsZip = this->zipNode->fsZip.lock();
        if (fsZip) {
            fsZip->statStreamReopens++;
        }
    }
    if (target > this->streamPos) {
        char tmp[4096];
        U64 todo = target - this->streamPos;
        while (todo > 0) {
            U32 chunk = todo > sizeof(tmp) ? (U32)sizeof(tmp) : (U32)todo;
            U32 got = this->readFromStream((U8*)tmp, chunk);
            if (got == 0) {
                break;
            }
            todo -= got;
        }
    }
}

U32 FsZipOpenNode::readNative(U8* buffer, U32 len) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->readMutex);
    if (len == 0) {
        return 0;
    }
    // Use zipNode->length() directly: FsFileNode::length() can return 0 for
    // ZIP entries (e.g. via a stale nativePath stat), but the zipInfo from the
    // scan has the correct uncompressed_size.
    U64 fileLen = this->zipNode->length();
    if ((U64)this->pos >= fileLen) {
        return 0;
    }
    U64 endPos = (U64)this->pos + len;
    if (endPos > fileLen) {
        endPos = fileLen;
    }
    // No stream needed once the entry is fully cached.
    if (!this->cacheComplete && !this->ensureStream()) {
        return 0;
    }
    std::shared_ptr<FsZip> fsZip = this->zipNode->fsZip.lock();

    if (fileLen <= CACHE_CAP) {
        // Cached mode: the entry fits in the prefix cache. The private
        // stream only moves forward to extend the cache, so each byte is
        // decompressed at most once and backward seeks are pure memcpy.
        if (!this->cacheComplete && endPos > this->cache.size()) {
            size_t oldSize = this->cache.size();
            this->cache.resize((size_t)endPos);
            U32 got = this->readFromStream(this->cache.data() + oldSize, (U32)(endPos - oldSize));
            this->cache.resize(oldSize + got);
            if (this->cache.size() >= fileLen) {
                this->cacheComplete = true;
                this->closeStream(); // fully cached; stream no longer needed
            }
        }
        U64 avail = this->cache.size() > (U64)this->pos ? this->cache.size() - (U64)this->pos : 0;
        U64 want = endPos - (U64)this->pos;
        U32 take = (U32)(avail < want ? avail : want);
        if (take) {
            memcpy(buffer, this->cache.data() + (size_t)this->pos, take);
            this->pos += take;
            if (fsZip) {
                fsZip->statCacheServedBytes += take;
            }
        }
        return take;
    }

    // Streaming mode: entry exceeds CACHE_CAP (rare; e.g. the 26MB
    // mshtml.dll). No cache; the private stream is repositioned as needed.
    // Backward seeks still re-decompress, but there is no cross-node
    // thrashing anymore.
    this->positionStream((U64)this->pos);
    U32 got = this->readFromStream(buffer, (U32)(endPos - (U64)this->pos));
    this->pos += got;
    return got;
}

U32 FsZipOpenNode::writeNative(U8* buffer, U32 len) {
    kwarn("FsZipOpenNode::writeNative called on a read-only zip node; nothing written");
    return 0;
}

void FsZipOpenNode::reopen() {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(this->readMutex);
    this->pos = 0;
    // The decompressed prefix cache stays valid: the zip entry did not
    // change, so a subsequent seek+read is served from cache for free.
}

BOXEDWINE_MUTEX* FsZipOpenNode::getReadMutex() {
    return &this->readMutex;
}

#endif
