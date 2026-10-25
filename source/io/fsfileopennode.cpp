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
#include "fsfileopennode.h"
#include UNISTD
#include <fcntl.h>
#include "fsfilenode.h"

FsFileOpenNode::FsFileOpenNode(const std::shared_ptr<FsFileNode>& node, U32 flags, U32 handle) : FsOpenNode(node, flags), fileNode(node), handle(handle) {
}

FsFileOpenNode::~FsFileOpenNode() {
    FsFileOpenNode::close();
}

S64 FsFileOpenNode::length() {
    S64 currentPos = lseek64(this->handle, 0, SEEK_CUR);
    S64 size = lseek64(this->handle, 0, SEEK_END);
    lseek64(this->handle, currentPos, SEEK_SET);
    return size;
}

bool FsFileOpenNode::setLength(S64 len) {
    return ftruncate(this->handle, (S32)len)==0;
}

S64 FsFileOpenNode::getFilePointer() {
    if (this->readBufferPos >= 0) {
        // Buffer is active: return the logical position, not the
        // actual file position (which is ahead due to read-ahead)
        return this->expectedPos;
    }
    return lseek64(this->handle, 0, SEEK_CUR);
}

S64 FsFileOpenNode::seek(S64 pos) {
    this->invalidateReadBuffer();
    S64 result = lseek64(this->handle, pos, SEEK_SET);
    if (result >= 0) {
        this->expectedPos = result;
    }
    return result;
}

void FsFileOpenNode::close() {
    if (this->handle!=0xFFFFFFFF)
        ::close(this->handle);
    this->handle = 0xFFFFFFFF;
}

bool FsFileOpenNode::isOpen() {
    return this->handle!=0xFFFFFFFF;
}

void FsFileOpenNode::reopen() {
    int openFlags = O_BINARY;
    int flags = this->flags;

    if ((flags & K_O_ACCMODE)==K_O_RDONLY) {
        openFlags|=O_RDONLY;
    } else if ((flags & K_O_ACCMODE)==K_O_WRONLY) {
        openFlags|=O_WRONLY;
    } else {
        openFlags|=O_RDWR;
    }
    if (flags & K_O_APPEND) {
        openFlags|=O_APPEND;
    }

    this->handle = ::open(this->fileNode->nativePath.c_str(), openFlags, 0666);
    this->invalidateReadBuffer();
    this->expectedPos = 0;
}

U32 FsFileOpenNode::ioctl(KThread* thread, U32 request) {
    return -K_ENODEV;
}

void FsFileOpenNode::setAsync(bool isAsync) {
    FsOpenNode::setAsync(isAsync);
}

bool FsFileOpenNode::isAsync() {
    return FsOpenNode::isAsync();
}

void FsFileOpenNode::waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) {
    kdebug("FsFileOpenNode::::waitForEvents not implemented");
}

bool FsFileOpenNode::isWriteReady() {
    return (this->flags & K_O_ACCMODE)!=K_O_RDONLY;
}

bool FsFileOpenNode::isReadReady() {
    return (this->flags & K_O_ACCMODE)!=K_O_WRONLY;
}

U32 FsFileOpenNode::map(KThread* thread, U32 address, U32 len, S32 prot, S32 flags, U64 off) {
    return 0;
}

bool FsFileOpenNode::canMap() {
    return true;
}

U32 FsFileOpenNode::readNative(U8* buffer, U32 len) {
    return (U32)::read(this->handle, buffer, len);
}

U32 FsFileOpenNode::writeNative(U8* buffer, U32 len) {
    this->invalidateReadBuffer();
    U32 result = (U32)::write(this->handle, buffer, len);
    if ((S32)result > 0) {
        this->expectedPos += result;
    }
    return result;
}

// Buffered read implementation for FsFileOpenNode.
// Uses a 64KB read-ahead buffer to reduce syscall overhead.
// - Small reads (52B/56B from ELF/PE loader) become memcpy hits after first fill
// - Large reads use 1 syscall per 64KB instead of 16 per 4KB chunk
// - Backward seeks within the buffered range are free
// Stats for measuring buffer effectiveness
static U64 g_bufHits = 0;
static U64 g_bufMisses = 0;
static U64 g_bufBytesFromBuffer = 0;
static U64 g_bufBytesFromFile = 0;

U32 FsFileOpenNode::internalRead(KThread* thread, U32 address, U32 len) {
    U32 result = 0;
    KMemory* memory = thread->memory;

    memory->performOnMemory(address, len, false, [&result, this](U8* ram, U32 chunkLen) {
        // Fast path: check if [expectedPos, expectedPos+chunkLen) is in buffer
        if (this->readBufferPos >= 0 &&
            this->expectedPos >= this->readBufferPos &&
            this->expectedPos + (S64)chunkLen <= this->readBufferPos + (S64)this->readBufferLen) {
            memcpy(ram, this->readBuffer + (this->expectedPos - this->readBufferPos), chunkLen);
            this->expectedPos += chunkLen;
            result += chunkLen;
            g_bufHits++;
            g_bufBytesFromBuffer += chunkLen;
            return true;
        }

        // Slow path: buffer miss.
        // The file position is already at expectedPos (invariant maintained by
        // seek/write/buffer-fill), so no seek is needed before reading.
        S64 actualPos = this->expectedPos;

        // Fill the buffer with a large read
        this->invalidateReadBuffer();
        U32 n = this->readNative(this->readBuffer, READ_BUFFER_SIZE);
        if ((S32)n <= 0) {
            return false; // EOF or error
        }
        this->readBufferPos = actualPos;
        this->readBufferLen = n;
        // Note: file position is now at actualPos + n, but our logical position
        // is actualPos + toCopy. This is fine: the next buffer hit does not touch
        // the file, and the next miss will seek to the correct position.

        U32 toCopy = chunkLen < n ? chunkLen : n;
        memcpy(ram, this->readBuffer, toCopy);
        this->expectedPos = actualPos + toCopy;
        result += toCopy;
        g_bufMisses++;
        g_bufBytesFromFile += n;
        // Log stats periodically
        if ((g_bufHits + g_bufMisses) % 10000 == 0) {
            U64 total = g_bufHits + g_bufMisses;
            klog_fmt("FsFileOpenNode buffer: hits=%llu misses=%llu hit_rate=%.1f%% bytes_from_buffer=%lluMB bytes_from_file=%lluMB",
                g_bufHits, g_bufMisses,
                total ? (100.0 * g_bufHits / total) : 0.0,
                g_bufBytesFromBuffer / (1024*1024),
                g_bufBytesFromFile / (1024*1024));
        }
        return toCopy == chunkLen;
    });
    return result;
}
