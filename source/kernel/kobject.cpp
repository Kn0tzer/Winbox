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
#include "ksignal.h"
#include "kobject.h"

KObject::KObject(U32 type) : type(type) {
}

U32 KObject::writev(KThread* thread, U32 iov, S32 iovcnt) {
    U32 len=0;
    KMemory* memory = thread->memory;

    for (S32 i=0;i<iovcnt;i++) {
        U32 buf = memory->readd(iov + i * 8);
        U32 toWrite = memory->readd(iov + i * 8 + 4);
        S32 result;

        result = this->write(thread, buf, toWrite);
        if (result<0) {
            if (i>0) {
                kwarn("writev partial fail: TODO file pointer should not change");
            }
            return result;
        }
        len+=result;
    }
    return len;
}

U32 KObject::read(KThread* thread, U32 address, U32 len) {
    U32 result = 0;
    KMemory* memory = thread->memory;

    memory->performOnMemory(address, len, true, [&result, this](U8* ram, U32 len) {
        U32 read = this->readNative(ram, len);
        if ((S32)read < 0) {
            result = read;
            return false;
        }
        result += read;
        return read == len;
        });
    return result;
}

U32 KObject::write(KThread* thread, U32 address, U32 len) {
    U32 result = 0;
    KMemory* memory = thread->memory;

    memory->performOnMemory(address, len, false, [&result, this](U8* ram, U32 len) {
        U32 written = this->writeNative(ram, len);
        if ((S32)written < 0) {
            result = written;
            return false;
        }
        result += written;
        return written == len;
        });
    return result;
}

// fcntl O_ASYNC state (kernel round 3B). The enabled flag, the F_SETOWN
// owner and the F_SETSIG signal live on the file description (this object),
// matching Linux: F_GETFL reports O_ASYNC from here, and readiness
// delivery goes to the recorded owner, not to whoever happens to poll.
void KObject::setAsync(bool isAsync) {
    this->asyncEnabled = isAsync;
}

bool KObject::isAsync() {
    return this->asyncEnabled;
}

void KObject::setAsyncOwner(S32 owner) {
    this->asyncOwner = owner;
    this->asyncOwnerType = owner < 0 ? 2 : 1; // F_OWNER_PGRP / F_OWNER_PID
}

S32 KObject::getAsyncOwner() {
    return this->asyncOwner;
}

void KObject::setAsyncOwnerEx(S32 type, S32 pid) {
    this->asyncOwnerType = (U32)type;
    this->asyncOwner = type == 2 ? -pid : pid;
}

void KObject::getAsyncOwnerEx(S32* type, S32* pid) {
    *type = (S32)this->asyncOwnerType;
    *pid = this->asyncOwner < 0 ? -this->asyncOwner : this->asyncOwner;
}

void KObject::setAsyncSignal(U32 signo) {
    this->asyncSignal = signo;
}

U32 KObject::getAsyncSignal() {
    return this->asyncSignal;
}

bool KObject::notifyAsyncReady(U32 code) {
    if (!this->asyncEnabled || this->asyncOwner == 0) {
        return false;
    }
    U32 signo = this->asyncSignal ? this->asyncSignal : K_SIGIO;
    bool delivered = false;
    if (this->asyncOwner > 0) {
        KProcessPtr process = KSystem::getProcess((U32)this->asyncOwner);
        if (process) {
            process->signalAsyncEvent(signo, code, this, nullptr);
            delivered = true;
        }
    } else {
        std::vector<KProcessPtr> processes;
        KSystem::getProcessesInGroup((U32)(-this->asyncOwner), processes);
        for (auto& process : processes) {
            process->signalAsyncEvent(signo, code, this, nullptr);
            delivered = true;
        }
    }
    return delivered;
}
