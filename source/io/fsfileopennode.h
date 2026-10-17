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

#ifndef __FSFILEOPENNODE_H__
#define __FSFILEOPENNODE_H__

#include "fsopennode.h"

class FsFileNode;

class FsFileOpenNode : public FsOpenNode {
public:
    FsFileOpenNode(const std::shared_ptr<FsFileNode>& node, U32 flags, U32 handle);
    virtual ~FsFileOpenNode();

    // from FsOpenNode
    S64 length() override;
    bool setLength(S64 length) override;
    S64 getFilePointer() override;
    S64 seek(S64 pos) override;
    U32 map(KThread* thread, U32 address, U32 len, S32 prot, S32 flags, U64 off) override;
    bool canMap() override;
    U32 ioctl(KThread*, U32 request) override;
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

protected:
    U32 internalRead(KThread* thread, U32 address, U32 len) override;

private:
    std::shared_ptr<FsFileNode> fileNode;
    U32 handle;

    // Read-ahead buffer: reduces syscall overhead for small/sequential reads.
    // The ELF/PE loader does 52B/56B reads with backward seeks; buffering 64KB
    // turns those into memcpy hits. Large reads also benefit (1 syscall for
    // 64KB instead of 16 for 4KB chunks).
    static const U32 READ_BUFFER_SIZE = 65536;
    U8 readBuffer[READ_BUFFER_SIZE];
    S64 readBufferPos = -1; // file offset of readBuffer[0], -1 = invalid
    U32 readBufferLen = 0;  // valid bytes in readBuffer
    S64 expectedPos = 0;    // tracked file position (avoids getFilePointer syscall)
    void invalidateReadBuffer() { readBufferPos = -1; readBufferLen = 0; }
};

#endif