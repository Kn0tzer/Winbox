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
#include "fsmemopennode.h"

FsMemOpenNode::FsMemOpenNode(U32 flags, std::shared_ptr<FsNode> node) : FsOpenNode(node, flags), seals(0), pos(0), isClosed(false) {
    this->lastModifiedTime = KSystem::getSystemTimeAsMicroSeconds() / 1000l;
}

FsMemOpenNode::~FsMemOpenNode() {
    FsMemOpenNode::close();
}

S64 FsMemOpenNode::length() {
    return (S64)this->buffer.size();
}

bool FsMemOpenNode::setLength(S64 length) {
    if (length < 0 || (length < (S64)this->buffer.size() && this->hasSeal(K_F_SEAL_SHRINK)) ||
        (length > (S64)this->buffer.size() && this->hasSeal(K_F_SEAL_GROW))) return false;
    this->lastModifiedTime = KSystem::getSystemTimeAsMicroSeconds() / 1000l;
    this->buffer.resize((U32)length, 0);
    KInotify::notifyAttrib(this->node.get());
    return true;
}

S64 FsMemOpenNode::getFilePointer() {
    return pos;
}

S64 FsMemOpenNode::seek(S64 pos) {
    this->pos = pos;
    return this->pos;
}

U32 FsMemOpenNode::map(KThread* thread, U32 address, U32 len, S32 prot, S32 flags, U64 off) {
    return 0;
}

bool FsMemOpenNode::canMap() {
    return true;
}

U32 FsMemOpenNode::ioctl(KThread* thread, U32 request) {
    return -K_ENODEV;
}

void FsMemOpenNode::setAsync(bool isAsync) {
    this->applyAsyncFlag(isAsync);
}

bool FsMemOpenNode::isAsync() {
    return this->asyncEnabled;
}

void FsMemOpenNode::waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) {
    // Readiness for fs nodes is static (driven by the open mode), so mirror
    // DevDsp::waitForEvents: if the requested readiness already holds, wake
    // the poller instead of leaving it to time out.
    if (((events & K_POLLIN) && this->isReadReady()) || ((events & K_POLLOUT) && this->isWriteReady())) {
        BOXEDWINE_CONDITION_SIGNAL_ALL(parentCondition);
    }
}

bool FsMemOpenNode::isWriteReady() {
    return (this->flags & K_O_ACCMODE)!=K_O_RDONLY;
}

bool FsMemOpenNode::isReadReady() {
    return (this->flags & K_O_ACCMODE)!=K_O_WRONLY;
}

U32 FsMemOpenNode::readNative(U8* buffer, U32 len) {
    S32 todo = (S32)len;
    if (todo > (S32)this->buffer.size() - this->pos) {
        todo = (S32)(this->buffer.size()-this->pos);
    }
    if (todo>0) {
        memcpy(buffer, &this->buffer[(U32)this->pos], todo);
        this->pos+=todo;
        return (U32)todo;
    }    
    return 0;
}

U32 FsMemOpenNode::writeNative(U8* buffer, U32 len) {
    if (this->hasSeal(K_F_SEAL_WRIT) || this->hasSeal(K_F_SEAL_FUTURE_WRITE)) return (U32)-K_EPERM;
    if (len==0)
        return 0;
    U32 remaining = len;
    this->lastModifiedTime = KSystem::getSystemTimeAsMicroSeconds() / 1000l;
    if (this->pos < (S64)this->buffer.size()) {
        U32 todo = remaining;
        if (this->buffer.size()-(U64)this->pos < remaining) {
            todo = (U32)(this->buffer.size()-this->pos);
        }
        remaining -= todo;
        memcpy(&this->buffer[(U32)this->pos], buffer, todo);
        this->pos+=todo;
        buffer+=todo;
    }
    if (remaining) {
        std::copy(buffer, buffer+remaining, std::back_inserter(this->buffer));
        this->pos = (S64)this->buffer.size();
    }
    KInotify::notifyModified(this->node.get());
    return len;
}

void FsMemOpenNode::close() {
    if (!this->isClosed) {
        KInotify::notifyClosed(this->node.get(), (this->flags & K_O_ACCMODE) != K_O_RDONLY);
    }
    this->isClosed = true;
}

void FsMemOpenNode::reopen() {
    this->isClosed = false;
}

bool FsMemOpenNode::isOpen() {
    return !this->isClosed;
}

U32 FsMemOpenNode::addSeals(U32 seals) {
    constexpr U32 validSeals = K_F_SEAL_SEAL | K_F_SEAL_SHRINK | K_F_SEAL_GROW |
                               K_F_SEAL_WRIT | K_F_SEAL_FUTURE_WRITE;
    if (seals & ~validSeals) return (U32)-K_EINVAL;
    if (this->seals & K_F_SEAL_SEAL) {
        return (U32)-K_EPERM;
    }
    this->seals |= seals;
    return 0;
}
