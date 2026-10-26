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

#include "ksocketobject.h"
#include "kscheduler.h"
#include <algorithm>

U32 KSocketObject::applySocketFilter(const U8* packet, U32 length) const {
    if (this->socketFilter.empty()) return length;
    U32 a = 0, x = 0, mem[16] = {};
    U32 pc = 0;
    while (pc < this->socketFilter.size()) {
        const KClassicBpfInsn& ins = this->socketFilter[pc++];
        const U16 cls = ins.code & 0x07;
        const U16 size = ins.code & 0x18;
        const U16 mode = ins.code & 0xe0;
        auto loadPacket = [&](U32 offset, U32 bytes, U32& out) {
            if (offset > length || bytes > length - offset) return false;
            out = 0;
            for (U32 i = 0; i < bytes; ++i) out = (out << 8) | packet[offset + i];
            return true;
        };
        if (cls == 0x00 || cls == 0x01) { // LD / LDX
            U32 value = 0;
            if (mode == 0x00) value = ins.k;
            else if (mode == 0x20 || mode == 0x40) {
                U32 bytes = size == 0x00 ? 4 : size == 0x08 ? 2 : size == 0x10 ? 1 : 0;
                if (!bytes) return 0;
                U64 offset = ins.k;
                if (mode == 0x40) offset += x;
                if (offset > UINT32_MAX || !loadPacket((U32)offset, bytes, value)) return 0;
            } else if (mode == 0x60) {
                if (ins.k >= 16) return 0;
                value = mem[ins.k];
            } else if (mode == 0x80 && cls == 0x00 && size == 0x00) value = length;
            else if (mode == 0xa0 && cls == 0x01 && size == 0x10) {
                if (!loadPacket(ins.k, 1, value)) return 0;
                value = (value & 0x0f) << 2;
            } else return 0;
            if (cls == 0x00) a = value; else x = value;
        } else if (cls == 0x02 || cls == 0x03) { // ST / STX
            if (ins.k >= 16) return 0;
            mem[ins.k] = cls == 0x02 ? a : x;
        } else if (cls == 0x04) { // ALU
            const U16 op = ins.code & 0xf0;
            const U32 rhs = (ins.code & 0x08) ? x : ins.k;
            switch (op) {
                case 0x00: a += rhs; break;
                case 0x10: a -= rhs; break;
                case 0x20: a *= rhs; break;
                case 0x30: if (!rhs) return 0; a /= rhs; break;
                case 0x40: a |= rhs; break;
                case 0x50: a &= rhs; break;
                case 0x60: a = rhs >= 32 ? 0 : a << rhs; break;
                case 0x70: a = rhs >= 32 ? 0 : a >> rhs; break;
                case 0x80: a = 0u - a; break;
                case 0x90: if (!rhs) return 0; a %= rhs; break;
                case 0xa0: a ^= rhs; break;
                default: return 0;
            }
        } else if (cls == 0x05) { // JMP
            const U16 op = ins.code & 0xf0;
            if (op == 0x00) {
                if (ins.k >= this->socketFilter.size() - pc) return 0;
                pc += ins.k;
            } else {
                const U32 rhs = (ins.code & 0x08) ? x : ins.k;
                bool take = false;
                switch (op) {
                    case 0x10: take = a == rhs; break;
                    case 0x20: take = a > rhs; break;
                    case 0x30: take = a >= rhs; break;
                    case 0x40: take = (a & rhs) != 0; break;
                    default: return 0;
                }
                const U32 jump = take ? ins.jt : ins.jf;
                if (jump >= this->socketFilter.size() - pc) return 0;
                pc += jump;
            }
        } else if (cls == 0x06) { // RET
            const U32 value = (ins.code & 0x18) == 0x10 ? a : ins.k;
            return std::min(value, length);
        } else if (cls == 0x07) { // MISC
            if ((ins.code & 0xf8) == 0x00) x = a; // TAX
            else if ((ins.code & 0xf8) == 0x80) a = x; // TXA
            else return 0;
        } else return 0;
    }
    return 0;
}

void KSocketObject::readMsgHdr(KThread* thread, U32 address, MsgHdr* hdr) {
    KMemory* memory = thread->memory;

    hdr->msg_name = memory->readd(address);address+=4;
    hdr->msg_namelen = memory->readd(address); address += 4;
    hdr->msg_iov = memory->readd(address); address += 4;
    hdr->msg_iovlen = memory->readd(address); address += 4;
    hdr->msg_control = memory->readd(address); address += 4;
    hdr->msg_controllen = memory->readd(address); address += 4;
    hdr->msg_flags = memory->readd(address);
}

void KSocketObject::readCMsgHdr(KThread* thread, U32 address, CMsgHdr* hdr) {
    KMemory* memory = thread->memory;

    hdr->cmsg_len = memory->readd(address); address += 4;
    hdr->cmsg_level = memory->readd(address); address += 4;
    hdr->cmsg_type = memory->readd(address);
}

void KSocketObject::writeCMsgHdr(KThread* thread, U32 address, U32 len, U32 level, U32 type) {
    KMemory* memory = thread->memory;

    memory->writed(address, len); address += 4;
    memory->writed(address, level); address += 4;
    memory->writed(address, type);
}
