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

#include "../common/common_sse.h"

#undef SSE_0
#define SSE_0(name) void OPCALL normal_##name(CPU* cpu, DecodedOp* op) {START_OP(cpu, op); common_##name(cpu);NEXT();}
#undef SSE_RR
#define SSE_RR(name) void OPCALL normal_##name(CPU* cpu, DecodedOp* op) {START_OP(cpu, op); common_##name(cpu, op->reg, op->rm);NEXT();}
#undef SSE_RR_SETS_FLAGS
#define SSE_RR_SETS_FLAGS(name) void OPCALL normal_##name(CPU* cpu, DecodedOp* op) {START_OP(cpu, op); common_##name(cpu, op->reg, op->rm);NEXT();}
#undef SSE_RE
#define SSE_RE(name) void OPCALL normal_##name(CPU* cpu, DecodedOp* op) {START_OP(cpu, op); common_##name(cpu, op->reg, eaa(cpu, op));NEXT();}
#undef SSE_RE_SETS_FLAGS
#define SSE_RE_SETS_FLAGS(name) void OPCALL normal_##name(CPU* cpu, DecodedOp* op) {START_OP(cpu, op); common_##name(cpu, op->reg, eaa(cpu, op));NEXT();}
#undef SSE_RR_I8
#define SSE_RR_I8(name) void OPCALL normal_##name(CPU* cpu, DecodedOp* op) {START_OP(cpu, op); common_##name(cpu, op->reg, op->rm, (U8)op->imm);NEXT();}
#undef SSE_RE_I8
#define SSE_RE_I8(name) void OPCALL normal_##name(CPU* cpu, DecodedOp* op) {START_OP(cpu, op); common_##name(cpu, op->reg, eaa(cpu, op), (U8)op->imm);NEXT();}

#undef SSE_RR_EDI
#define SSE_RR_EDI(name) void OPCALL normal_##name(CPU* cpu, DecodedOp* op) {START_OP(cpu, op); common_##name(cpu, op->reg, op->rm, EDI+cpu->seg[op->base].address);NEXT();}

#include "../common/common_sse_def.h"

void OPCALL normal_sse41_3ARR(CPU* cpu, DecodedOp* op) {
    START_OP(cpu, op);
    common_sse41_3aRR(cpu, op->reg, op->rm, op->imm);
    NEXT();
}

void OPCALL normal_sse41_3ARE(CPU* cpu, DecodedOp* op) {
    START_OP(cpu, op);
    common_sse41_3aRE(cpu, op->reg, eaa(cpu, op), op->imm);
    NEXT();
}

void OPCALL normal_crc32RR(CPU* cpu, DecodedOp* op) {
    START_OP(cpu, op);
    common_crc32RR(cpu, op->reg, op->rm, op->imm);
    NEXT();
}

void OPCALL normal_crc32RE(CPU* cpu, DecodedOp* op) {
    START_OP(cpu, op);
    common_crc32RE(cpu, op->reg, eaa(cpu, op), op->imm);
    NEXT();
}

void OPCALL normal_popcntR16R16(CPU* cpu, DecodedOp* op) {
    START_OP(cpu, op);
    common_popcntR16R16(cpu, op->reg, op->rm);
    NEXT();
}

void OPCALL normal_popcntR16E16(CPU* cpu, DecodedOp* op) {
    START_OP(cpu, op);
    common_popcntR16E16(cpu, op->reg, eaa(cpu, op));
    NEXT();
}

void OPCALL normal_popcntR32R32(CPU* cpu, DecodedOp* op) {
    START_OP(cpu, op);
    common_popcntR32R32(cpu, op->reg, op->rm);
    NEXT();
}

void OPCALL normal_popcntR32E32(CPU* cpu, DecodedOp* op) {
    START_OP(cpu, op);
    common_popcntR32E32(cpu, op->reg, eaa(cpu, op));
    NEXT();
}

void OPCALL normal_sse42PcmpRR(CPU* cpu, DecodedOp* op) {
    START_OP(cpu, op);
    common_sse41_3aRR(cpu, op->reg, op->rm, op->imm);
    NEXT();
}

void OPCALL normal_sse42PcmpRE(CPU* cpu, DecodedOp* op) {
    START_OP(cpu, op);
    common_sse41_3aRE(cpu, op->reg, eaa(cpu, op), op->imm);
    NEXT();
}

void OPCALL normal_ldmxcsr(CPU* cpu, DecodedOp* op) {
    START_OP(cpu, op);
    if (!common_ldmxcsr(cpu, op->reg, eaa(cpu, op))) {
        NEXT_DONE();
        return;
    }
    NEXT();
}
