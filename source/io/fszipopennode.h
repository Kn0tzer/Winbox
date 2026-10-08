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

#ifndef __FSZIPOPENNODE_H__
#define __FSZIPOPENNODE_H__

#include "fsopennode.h"
#include "fszip.h"

#include <vector>

class FsZipNode;

class FsZipOpenNode : public FsOpenNode {
public:
    FsZipOpenNode(std::shared_ptr<FsNode> node, std::shared_ptr<FsZipNode>& zipNode, U32 flags, U64 offset);
    ~FsZipOpenNode() override;

    // From FsOpenNode
    S64 length() override;
    bool setLength(S64 length) override;
    S64 getFilePointer() override;
    S64 seek(S64 pos) override;
    U32 map(KThread* thread, U32 address, U32 len, S32 prot, S32 flags, U64 off) override;
    bool canMap() override;
    U32 ioctl(KThread* thread, U32 request) override;
    void setAsync(bool isAsync) override;
    bool isAsync() override;
    void waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) override;
    bool isWriteReady() override;
    bool isReadReady() override;
    U32 readNative(U8* buffer, U32 len) override;
    U32 writeNative(U8* buffer, U32 len) override;
    void close() override;
    void reopen() override;
    bool isOpen() override;
    BOXEDWINE_MUTEX* getReadMutex() override;

private:
    std::shared_ptr<FsZipNode> zipNode;
    S64 pos;
    U64 offset;
    // Per-node lock. FsOpenNode::read() used to serialize ALL zip reads on the
    // single FsZip::readMutex ("reduce thrashing in the zip file"); with each
    // node owning a private decompression stream there is no shared stream left
    // to thrash, so the lock is per-node now.
    BOXEDWINE_MUTEX readMutex;

    // Independent decompression stream for this open node, opened lazily by
    // ensureStream() at the start of our entry. In cached mode it only ever
    // moves forward to extend `cache` (always exactly at cache.size()); in
    // streaming mode positionStream() moves it as needed.
    unzFile stream = nullptr;
    U64 streamPos = 0; // decompressed offset the private stream is positioned at
    // Decompressed prefix [0, cache.size()) of the entry. Entries of CACHE_CAP
    // bytes or less are cached whole as they are read: backward seeks become
    // memcpy and every byte is decompressed at most once per node.
    std::vector<U8> cache;
    bool cacheComplete = false;
    static const U64 CACHE_CAP = 8 * 1024 * 1024; // 8MB: covers all but the huge DLLs

    bool ensureStream();
    void closeStream();
    // Moves the private stream to decompressed offset target, reopening the
    // entry and skipping forward when target < streamPos (zlib cannot seek
    // backward). Streaming mode only; cached mode never calls this.
    void positionStream(U64 target);
    // Reads up to len bytes from the private stream (looping to EOF/error);
    // advances streamPos; counts statBytesDecompressed.
    U32 readFromStream(U8* dst, U32 len);
};

#endif