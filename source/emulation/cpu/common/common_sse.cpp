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
#include "common_sse.h"
#include <math.h>

static U32 commonSseRoundToI32(double value, U32 mxcsr, bool truncate) {
    if (!isfinite(value)) {
        return 0x80000000;
    }

    double rounded;
    if (truncate) {
        rounded = value < 0 ? ceil(value) : floor(value);
    } else {
        switch ((mxcsr >> 13) & 3) {
        case ROUND_Nearest: {
            double floorValue = floor(value);
            double fraction = value - floorValue;
            if (fraction > 0.5) {
                rounded = floorValue + 1.0;
            } else if (fraction < 0.5) {
                rounded = floorValue;
            } else {
                rounded = (((S64)floorValue) & 1) ? floorValue + 1.0 : floorValue;
            }
            break;
        }
        case ROUND_Down:
            rounded = floor(value);
            break;
        case ROUND_Up:
            rounded = ceil(value);
            break;
        case ROUND_Chop:
        default:
            rounded = value < 0 ? ceil(value) : floor(value);
            break;
        }
    }

    if (rounded < -2147483648.0 || rounded > 2147483647.0) {
        return 0x80000000;
    }
    return (U32)(S32)rounded;
}

static U32 commonSseAddSubF32(U32 aBits, U32 bBits, bool subtract) {
    const U32 exponentMask = 0x7f800000;
    const U32 fractionMask = 0x007fffff;
    const U32 quietBit = 0x00400000;
    const bool aNan = (aBits & exponentMask) == exponentMask && (aBits & fractionMask) != 0;
    const bool bNan = (bBits & exponentMask) == exponentMask && (bBits & fractionMask) != 0;
    if (aNan) return aBits | quietBit;
    if (bNan) return bBits | quietBit;
    if ((aBits & exponentMask) == exponentMask && (bBits & exponentMask) == exponentMask) {
        const bool oppositeSigns = ((aBits ^ bBits) & 0x80000000) != 0;
        if (subtract ? !oppositeSigns : oppositeSigns) return 0xffc00000;
    }
    union { U32 bits; float value; } a{aBits}, b{bBits}, result{};
    result.value = subtract ? a.value - b.value : a.value + b.value;
    return result.bits;
}

static U64 commonSseAddSubF64(U64 aBits, U64 bBits, bool subtract) {
    const U64 exponentMask = 0x7ff0000000000000ULL;
    const U64 fractionMask = 0x000fffffffffffffULL;
    const U64 quietBit = 0x0008000000000000ULL;
    const bool aNan = (aBits & exponentMask) == exponentMask && (aBits & fractionMask) != 0;
    const bool bNan = (bBits & exponentMask) == exponentMask && (bBits & fractionMask) != 0;
    if (aNan) return aBits | quietBit;
    if (bNan) return bBits | quietBit;
    if ((aBits & exponentMask) == exponentMask && (bBits & exponentMask) == exponentMask) {
        const bool oppositeSigns = ((aBits ^ bBits) & 0x8000000000000000ULL) != 0;
        if (subtract ? !oppositeSigns : oppositeSigns) return 0xfff8000000000000ULL;
    }
    union { U64 bits; double value; } a{aBits}, b{bBits}, result{};
    result.value = subtract ? a.value - b.value : a.value + b.value;
    return result.bits;
}

static U32 commonSSSE3ReadLane(const U8* data, U32 index, U32 width) {
    U32 result = 0;
    for (U32 i = 0; i < width; ++i) result |= (U32)data[index * width + i] << (i * 8);
    return result;
}

static void commonSSSE3WriteLane(U8* data, U32 index, U32 width, U32 value) {
    for (U32 i = 0; i < width; ++i) data[index * width + i] = (U8)(value >> (i * 8));
}

static S32 commonSSSE3SignedLane(const U8* data, U32 index, U32 width) {
    U32 value = commonSSSE3ReadLane(data, index, width);
    if (width == 1) return (value & 0x80) ? (S32)value - 0x100 : (S32)value;
    if (width == 2) return (value & 0x8000) ? (S32)value - 0x10000 : (S32)value;
    S32 result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static S32 commonSSSE3SaturateS16(S32 value) {
    return value > 32767 ? 32767 : (value < -32768 ? -32768 : value);
}

static void commonSSSE3Apply(U8* destination, const U8* source, U32 size, U8 opcode) {
    U8 a[16], b[16], result[16] = {};
    memcpy(a, destination, size);
    memcpy(b, source, size);
    const U32 byteCount = size;
    switch (opcode) {
    case 0x00: // PSHUFB
        for (U32 i = 0; i < byteCount; ++i) {
            U8 selector = b[i];
            result[i] = (selector & 0x80) ? 0 : a[selector & (byteCount - 1)];
        }
        break;
    case 0x01: // PHADDW
    case 0x05: // PHSUBW
    case 0x03: // PHADDSW
    case 0x07: // PHSUBSW
        for (U32 half = 0; half < 2; ++half) {
            const U8* input = half ? b : a;
            U32 outputBase = half * (byteCount / 2);
            for (U32 i = 0; i < byteCount / 4; ++i) {
                S32 left = commonSSSE3SignedLane(input, i * 2, 2);
                S32 right = commonSSSE3SignedLane(input, i * 2 + 1, 2);
                S32 value;
                if (opcode == 0x01) value = (S16)(commonSSSE3ReadLane(input, i * 2, 2) + commonSSSE3ReadLane(input, i * 2 + 1, 2));
                else if (opcode == 0x05) value = (S16)(commonSSSE3ReadLane(input, i * 2, 2) - commonSSSE3ReadLane(input, i * 2 + 1, 2));
                else if (opcode == 0x03) value = commonSSSE3SaturateS16(left + right);
                else value = commonSSSE3SaturateS16(left - right);
                commonSSSE3WriteLane(result + outputBase, i, 2, (U16)value);
            }
        }
        break;
    case 0x02: // PHADDD
    case 0x06: // PHSUBD
        for (U32 half = 0; half < 2; ++half) {
            const U8* input = half ? b : a;
            U32 outputBase = half * (byteCount / 2);
            for (U32 i = 0; i < byteCount / 8; ++i) {
                U32 left = commonSSSE3ReadLane(input, i * 2, 4);
                U32 right = commonSSSE3ReadLane(input, i * 2 + 1, 4);
                commonSSSE3WriteLane(result + outputBase, i, 4, opcode == 0x02 ? left + right : left - right);
            }
        }
        break;
    case 0x04: // PMADDUBSW
        for (U32 i = 0; i < byteCount / 2; ++i) {
            S32 left = (S32)a[i * 2] * commonSSSE3SignedLane(b, i * 2, 1);
            S32 right = (S32)a[i * 2 + 1] * commonSSSE3SignedLane(b, i * 2 + 1, 1);
            commonSSSE3WriteLane(result, i, 2, (U16)commonSSSE3SaturateS16(left + right));
        }
        break;
    case 0x08: case 0x09: case 0x0a: // PSIGNB/W/D
        for (U32 i = 0; i < byteCount / (opcode == 0x08 ? 1 : (opcode == 0x09 ? 2 : 4)); ++i) {
            const S32 laneWidth = opcode == 0x08 ? 1 : (opcode == 0x09 ? 2 : 4);
            U32 value = commonSSSE3ReadLane(a, i, laneWidth);
            S32 sign = commonSSSE3SignedLane(b, i, laneWidth);
            if (sign == 0) value = 0;
            else if (sign < 0) value = (U32)0 - value;
            commonSSSE3WriteLane(result, i, laneWidth, value);
        }
        break;
    case 0x0b: // PMULHRSW
        for (U32 i = 0; i < byteCount / 2; ++i) {
            S32 product = commonSSSE3SignedLane(a, i, 2) * commonSSSE3SignedLane(b, i, 2);
            S32 rounded = product + 0x4000;
            S32 value = rounded >= 0 ? rounded / 32768 : -((-rounded + 32767) / 32768);
            commonSSSE3WriteLane(result, i, 2, (U16)value);
        }
        break;
    case 0x1c: case 0x1d: case 0x1e: // PABSB/W/D
        for (U32 i = 0; i < byteCount / (opcode == 0x1c ? 1 : (opcode == 0x1d ? 2 : 4)); ++i) {
            const U32 laneWidth = opcode == 0x1c ? 1 : (opcode == 0x1d ? 2 : 4);
            U32 value = commonSSSE3ReadLane(b, i, laneWidth);
            if (commonSSSE3SignedLane(b, i, laneWidth) < 0) value = (U32)0 - value;
            commonSSSE3WriteLane(result, i, laneWidth, value);
        }
        break;
    default:
        return;
    }
    memcpy(destination, result, size);
}

static void commonSSSE3Align(U8* destination, const U8* source, U32 size, U8 shift) {
    U8 a[16], b[16], result[16] = {};
    memcpy(a, destination, size);
    memcpy(b, source, size);
    for (U32 i = 0; i < size; ++i) {
        U32 index = i + shift;
        if (index < size) result[i] = b[index];
        else if (index < size * 2) result[i] = a[index - size];
    }
    memcpy(destination, result, size);
}

void common_ssse3MmxRR(CPU* cpu, U32 reg, U32 rm, U8 opcode) {
    U64 source = cpu->fpu.getMMX((U8)rm)->q;
    MMX_reg* destination = cpu->fpu.getMMX((U8)reg);
    cpu->fpu.startMMX();
    U8 dst[8], src[8];
    memcpy(dst, &destination->q, sizeof(U64));
    memcpy(src, &source, sizeof(U64));
    commonSSSE3Apply(dst, src, sizeof(U64), opcode);
    memcpy(&destination->q, dst, sizeof(U64));
}

void common_ssse3MmxRE(CPU* cpu, U32 reg, U32 address, U8 opcode) {
    U64 source = cpu->memory->readq(address);
    MMX_reg* destination = cpu->fpu.getMMX((U8)reg);
    cpu->fpu.startMMX();
    U8 dst[8], src[8];
    memcpy(dst, &destination->q, sizeof(U64));
    memcpy(src, &source, sizeof(U64));
    commonSSSE3Apply(dst, src, sizeof(U64), opcode);
    memcpy(&destination->q, dst, sizeof(U64));
}

void common_ssse3XmmRR(CPU* cpu, U32 reg, U32 rm, U8 opcode) {
    U8 dst[16], src[16];
    memcpy(dst, &cpu->xmm[reg].pi.u64[0], sizeof(dst));
    memcpy(src, &cpu->xmm[rm].pi.u64[0], sizeof(src));
    commonSSSE3Apply(dst, src, sizeof(dst), opcode);
    memcpy(&cpu->xmm[reg].pi.u64[0], dst, sizeof(dst));
}

void common_ssse3XmmRE(CPU* cpu, U32 reg, U32 address, U8 opcode) {
    U8 dst[16], src[16];
    U64 low = cpu->memory->readq(address), high = cpu->memory->readq(address + 8);
    memcpy(src, &low, sizeof(low));
    memcpy(src + sizeof(low), &high, sizeof(high));
    memcpy(dst, &cpu->xmm[reg].pi.u64[0], sizeof(dst));
    commonSSSE3Apply(dst, src, sizeof(dst), opcode);
    memcpy(&cpu->xmm[reg].pi.u64[0], dst, sizeof(dst));
}

void common_palignrMmxRR(CPU* cpu, U32 reg, U32 rm, U8 shift) {
    U64 source = cpu->fpu.getMMX((U8)rm)->q;
    MMX_reg* destination = cpu->fpu.getMMX((U8)reg);
    cpu->fpu.startMMX();
    U8 dst[8], src[8];
    memcpy(dst, &destination->q, sizeof(U64));
    memcpy(src, &source, sizeof(U64));
    commonSSSE3Align(dst, src, sizeof(U64), shift);
    memcpy(&destination->q, dst, sizeof(U64));
}

void common_palignrMmxRE(CPU* cpu, U32 reg, U32 address, U8 shift) {
    U64 source = cpu->memory->readq(address);
    MMX_reg* destination = cpu->fpu.getMMX((U8)reg);
    cpu->fpu.startMMX();
    U8 dst[8], src[8];
    memcpy(dst, &destination->q, sizeof(U64));
    memcpy(src, &source, sizeof(U64));
    commonSSSE3Align(dst, src, sizeof(U64), shift);
    memcpy(&destination->q, dst, sizeof(U64));
}

void common_palignrXmmRR(CPU* cpu, U32 reg, U32 rm, U8 shift) {
    U8 dst[16], src[16];
    memcpy(dst, &cpu->xmm[reg].pi.u64[0], sizeof(dst));
    memcpy(src, &cpu->xmm[rm].pi.u64[0], sizeof(src));
    commonSSSE3Align(dst, src, sizeof(dst), shift);
    memcpy(&cpu->xmm[reg].pi.u64[0], dst, sizeof(dst));
}

void common_palignrXmmRE(CPU* cpu, U32 reg, U32 address, U8 shift) {
    U8 dst[16], src[16];
    U64 low = cpu->memory->readq(address), high = cpu->memory->readq(address + 8);
    memcpy(src, &low, sizeof(low));
    memcpy(src + sizeof(low), &high, sizeof(high));
    memcpy(dst, &cpu->xmm[reg].pi.u64[0], sizeof(dst));
    commonSSSE3Align(dst, src, sizeof(dst), shift);
    memcpy(&cpu->xmm[reg].pi.u64[0], dst, sizeof(dst));
}

static U32 commonSse41ReadMemory(CPU* cpu, U32 address, U8* output, U32 size) {
    memset(output, 0, 16);
    for (U32 i = 0; i < size; ++i) output[i] = cpu->memory->readb(address + i);
    return size;
}

static U32 commonSse41MemorySize38(U8 opcode) {
    switch (opcode) {
    case 0x20: case 0x23: case 0x30: case 0x33: return 8;
    case 0x21: case 0x24: case 0x31: case 0x34: return 4;
    case 0x22: case 0x32: return 2;
    case 0x25: case 0x35: return 8;
    default: return 16;
    }
}

static S64 commonSse41SignedLane(const U8* data, U32 index, U32 width) {
    U64 value = 0;
    for (U32 i = 0; i < width; ++i) value |= (U64)data[index * width + i] << (i * 8);
    if (width < 8 && (value & ((U64)1 << (width * 8 - 1)))) value |= ~(((U64)1 << (width * 8)) - 1);
    S64 result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static U64 commonSse41ReadLane(const U8* data, U32 index, U32 width) {
    U64 result = 0;
    for (U32 i = 0; i < width; ++i) result |= (U64)data[index * width + i] << (i * 8);
    return result;
}

static void commonSse41WriteLane(U8* data, U32 index, U32 width, U64 value) {
    for (U32 i = 0; i < width; ++i) data[index * width + i] = (U8)(value >> (i * 8));
}

static void commonSse41Ptest(CPU* cpu, const U8* destination, const U8* source) {
    U32 anyAnd = 0, anyNotAnd = 0;
    for (U32 i = 0; i < 16; ++i) {
        anyAnd |= destination[i] & source[i];
        anyNotAnd |= (U8)~destination[i] & source[i];
    }
    cpu->fillFlags();
    cpu->flags &= ~(AF | OF | SF | CF | PF | ZF);
    if (!anyAnd) cpu->flags |= ZF;
    if (!anyNotAnd) cpu->flags |= CF;
}

static void commonSse41Apply38(CPU* cpu, U8* destination, const U8* source, U8 opcode) {
    U8 a[16], b[16], result[16] = {};
    memcpy(a, destination, sizeof(a));
    memcpy(b, source, sizeof(b));
    switch (opcode) {
    case 0x10: { // PBLENDVB, implicit XMM0 byte mask
        U8 mask[16];
        memcpy(mask, &cpu->xmm[0].pi.u64[0], sizeof(mask));
        for (U32 i = 0; i < 16; ++i) result[i] = (mask[i] & 0x80) ? b[i] : a[i];
        break;
    }
    case 0x14: case 0x15: { // BLENDVPS / BLENDVPD, implicit XMM0
        U8 mask[16];
        memcpy(mask, &cpu->xmm[0].pi.u64[0], sizeof(mask));
        U32 width = opcode == 0x14 ? 4 : 8;
        for (U32 lane = 0; lane < 16 / width; ++lane) {
            U32 signByte = lane * width + width - 1;
            memcpy(result + lane * width, (mask[signByte] & 0x80) ? b + lane * width : a + lane * width, width);
        }
        break;
    }
    case 0x17: { // PTEST
        commonSse41Ptest(cpu, a, b);
        return;
    }
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: { // PMOVSX / PMOVZX
        bool signExtend = opcode < 0x30;
        U8 base = signExtend ? 0x20 : 0x30;
        U32 sourceWidth, destinationWidth;
        switch (opcode - base) {
        case 0: sourceWidth = 1; destinationWidth = 2; break;
        case 1: sourceWidth = 1; destinationWidth = 4; break;
        case 2: sourceWidth = 1; destinationWidth = 8; break;
        case 3: sourceWidth = 2; destinationWidth = 4; break;
        case 4: sourceWidth = 2; destinationWidth = 8; break;
        default: sourceWidth = 4; destinationWidth = 8; break;
        }
        for (U32 lane = 0; lane < 16 / destinationWidth; ++lane) {
            U64 value = signExtend ? (U64)commonSse41SignedLane(b, lane, sourceWidth) : commonSse41ReadLane(b, lane, sourceWidth);
            commonSse41WriteLane(result, lane, destinationWidth, value);
        }
        break;
    }
    case 0x28: { // PMULDQ: signed dword products in lanes 0 and 2
        for (U32 lane = 0; lane < 2; ++lane) {
            S64 product = commonSse41SignedLane(a, lane * 2, 4) * commonSse41SignedLane(b, lane * 2, 4);
            commonSse41WriteLane(result, lane, 8, (U64)product);
        }
        break;
    }
    case 0x37: // PCMPGTQ
        for (U32 lane = 0; lane < 2; ++lane)
            commonSse41WriteLane(result, lane, 8, commonSse41SignedLane(a, lane, 8) > commonSse41SignedLane(b, lane, 8) ? ~(U64)0 : 0);
        break;
    case 0x29: // PCMPEQQ
        for (U32 lane = 0; lane < 2; ++lane) commonSse41WriteLane(result, lane, 8, commonSse41ReadLane(a, lane, 8) == commonSse41ReadLane(b, lane, 8) ? ~(U64)0 : 0);
        break;
    case 0x2a: // MOVNTDQA
        memcpy(result, b, sizeof(result));
        break;
    case 0x2b: // PACKUSDW
        for (U32 lane = 0; lane < 4; ++lane) {
            S64 value = commonSse41SignedLane(a, lane, 4);
            commonSse41WriteLane(result, lane, 2, value < 0 ? 0 : (value > 65535 ? 65535 : (U64)value));
        }
        for (U32 lane = 0; lane < 4; ++lane) {
            S64 value = commonSse41SignedLane(b, lane, 4);
            commonSse41WriteLane(result, lane + 4, 2, value < 0 ? 0 : (value > 65535 ? 65535 : (U64)value));
        }
        break;
    case 0x38: case 0x39: case 0x3a: case 0x3b: // PMINSB/PMINSD/PMINUW/PMINUD
    case 0x3c: case 0x3d: case 0x3e: case 0x3f: { // PMAXSB/PMAXSD/PMAXUW/PMAXUD
        U32 width = (opcode == 0x38 || opcode == 0x3c) ? 1 : ((opcode == 0x39 || opcode == 0x3d) ? 4 : ((opcode == 0x3a || opcode == 0x3e) ? 2 : 4));
        bool signedCompare = opcode == 0x38 || opcode == 0x39 || opcode == 0x3c || opcode == 0x3d;
        bool takeMinimum = opcode <= 0x3b;
        for (U32 lane = 0; lane < 16 / width; ++lane) {
            U64 av = commonSse41ReadLane(a, lane, width), bv = commonSse41ReadLane(b, lane, width);
            bool chooseA = signedCompare ? (takeMinimum ? commonSse41SignedLane(a, lane, width) <= commonSse41SignedLane(b, lane, width) : commonSse41SignedLane(a, lane, width) >= commonSse41SignedLane(b, lane, width))
                : (takeMinimum ? av <= bv : av >= bv);
            commonSse41WriteLane(result, lane, width, chooseA ? av : bv);
        }
        break;
    }
    case 0x40: // PMULLD
        for (U32 lane = 0; lane < 4; ++lane) commonSse41WriteLane(result, lane, 4, (U32)(commonSse41ReadLane(a, lane, 4) * commonSse41ReadLane(b, lane, 4)));
        break;
    case 0x41: { // PHMINPOSUW
        U32 minimum = 0xffff, index = 0;
        for (U32 lane = 0; lane < 8; ++lane) {
            U32 value = (U32)commonSse41ReadLane(b, lane, 2);
            if (value < minimum) { minimum = value; index = lane; }
        }
        commonSse41WriteLane(result, 0, 2, minimum);
        commonSse41WriteLane(result, 1, 2, index);
        break;
    }
    default:
        return;
    }
    memcpy(destination, result, sizeof(result));
}

void common_sse41_38RR(CPU* cpu, U32 reg, U32 rm, U8 opcode) {
    U8 destination[16], source[16];
    memcpy(destination, &cpu->xmm[reg].pi.u64[0], sizeof(destination));
    memcpy(source, &cpu->xmm[rm].pi.u64[0], sizeof(source));
    commonSse41Apply38(cpu, destination, source, opcode);
    memcpy(&cpu->xmm[reg].pi.u64[0], destination, sizeof(destination));
}

void common_sse41_38RE(CPU* cpu, U32 reg, U32 address, U8 opcode) {
    U8 destination[16], source[16];
    memcpy(destination, &cpu->xmm[reg].pi.u64[0], sizeof(destination));
    commonSse41ReadMemory(cpu, address, source, commonSse41MemorySize38(opcode));
    commonSse41Apply38(cpu, destination, source, opcode);
    memcpy(&cpu->xmm[reg].pi.u64[0], destination, sizeof(destination));
}

void common_sse41ptestRR(CPU* cpu, U32 reg, U32 rm) {
    U8 destination[16], source[16];
    memcpy(destination, &cpu->xmm[reg].pi.u64[0], sizeof(destination));
    memcpy(source, &cpu->xmm[rm].pi.u64[0], sizeof(source));
    commonSse41Ptest(cpu, destination, source);
}

void common_sse41ptestRE(CPU* cpu, U32 reg, U32 address) {
    U8 destination[16], source[16];
    memcpy(destination, &cpu->xmm[reg].pi.u64[0], sizeof(destination));
    commonSse41ReadMemory(cpu, address, source, 16);
    commonSse41Ptest(cpu, destination, source);
}

static U32 commonSse41RoundF32(U32 bits, U32 mode) {
    U32 exponent = bits & 0x7f800000;
    if (exponent == 0x7f800000) return (bits & 0x007fffff) ? bits | 0x00400000 : bits;
    float input;
    memcpy(&input, &bits, sizeof(input));
    double value = input;
    double rounded;
    switch (mode) {
    case ROUND_Down: rounded = floor(value); break;
    case ROUND_Up: rounded = ceil(value); break;
    case ROUND_Chop: rounded = value < 0 ? ceil(value) : floor(value); break;
    default: {
        double lower = floor(value), fraction = value - lower;
        if (fraction < 0.5) rounded = lower;
        else if (fraction > 0.5) rounded = lower + 1.0;
        else rounded = fmod(fabs(lower), 2.0) == 0.0 ? lower : lower + 1.0;
        break;
    }
    }
    if (rounded == 0.0 && (bits & 0x80000000)) rounded = -0.0;
    float result = (float)rounded;
    U32 resultBits;
    memcpy(&resultBits, &result, sizeof(resultBits));
    if ((bits & 0x7f800000) == 0x7f800000 && (bits & 0x007fffff)) resultBits = bits | 0x00400000;
    return resultBits;
}

static U64 commonSse41RoundF64(U64 bits, U32 mode) {
    if ((bits & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL) return (bits & 0x000fffffffffffffULL) ? bits | 0x0008000000000000ULL : bits;
    double value;
    memcpy(&value, &bits, sizeof(value));
    double rounded;
    switch (mode) {
    case ROUND_Down: rounded = floor(value); break;
    case ROUND_Up: rounded = ceil(value); break;
    case ROUND_Chop: rounded = value < 0 ? ceil(value) : floor(value); break;
    default: {
        double lower = floor(value), fraction = value - lower;
        if (fraction < 0.5) rounded = lower;
        else if (fraction > 0.5) rounded = lower + 1.0;
        else rounded = fmod(fabs(lower), 2.0) == 0.0 ? lower : lower + 1.0;
        break;
    }
    }
    U64 result;
    if (rounded == 0.0 && (bits >> 63)) rounded = -0.0;
    memcpy(&result, &rounded, sizeof(result));
    return result;
}

static U32 commonSse41RoundMode(CPU* cpu, U8 imm) {
    if (imm & 4) return (cpu->mxcsr >> 13) & 3;
    return imm & 3;
}

static void commonSse41StoreMemory(CPU* cpu, U32 address, U64 value, U32 size) {
    for (U32 i = 0; i < size; ++i) cpu->memory->writeb(address + i, (U8)(value >> (i * 8)));
}

static U32 commonSse41F32Multiply(U32 aBits, U32 bBits);
static U32 commonSse41F32Add(U32 aBits, U32 bBits);
static U64 commonSse41F64Multiply(U64 aBits, U64 bBits);
static U64 commonSse41F64Add(U64 aBits, U64 bBits);

static void commonSse41Apply3a(CPU* cpu, U8* result, const U8* a, const U8* b, U8 opcode, U8 imm) {
    memcpy(result, a, 16);
    switch (opcode) {
    case 0x08: case 0x0a: { // ROUNDPS / ROUNDSS
        U32 lanes = opcode == 0x0a ? 1 : 4;
        U32 mode = commonSse41RoundMode(cpu, imm);
        for (U32 lane = 0; lane < lanes; ++lane) commonSse41WriteLane(result, lane, 4, commonSse41RoundF32((U32)commonSse41ReadLane(b, lane, 4), mode));
        break;
    }
    case 0x09: case 0x0b: { // ROUNDPD / ROUNDSD
        U32 lanes = opcode == 0x0b ? 1 : 2;
        U32 mode = commonSse41RoundMode(cpu, imm);
        for (U32 lane = 0; lane < lanes; ++lane) commonSse41WriteLane(result, lane, 8, commonSse41RoundF64(commonSse41ReadLane(b, lane, 8), mode));
        break;
    }
    case 0x0c: case 0x0d: { // BLENDPS / BLENDPD
        U32 width = opcode == 0x0c ? 4 : 8;
        for (U32 lane = 0; lane < 16 / width; ++lane)
            memcpy(result + lane * width, (imm & (1 << lane)) ? b + lane * width : a + lane * width, width);
        break;
    }
    case 0x0e: // PBLENDW
        for (U32 lane = 0; lane < 8; ++lane)
            commonSse41WriteLane(result, lane, 2, commonSse41ReadLane((imm & (1 << lane)) ? b : a, lane, 2));
        break;
    case 0x21: { // INSERTPS
        U32 sourceLane = (imm >> 6) & 3, destinationLane = (imm >> 4) & 3;
        commonSse41WriteLane(result, destinationLane, 4, commonSse41ReadLane(b, sourceLane, 4));
        for (U32 lane = 0; lane < 4; ++lane) if (imm & (1 << lane)) commonSse41WriteLane(result, lane, 4, 0);
        break;
    }
    case 0x40: { // DPPS
        U32 sum = 0;
        for (U32 lane = 0; lane < 4; ++lane) if (imm & (1 << (lane + 4))) {
            U32 abits = (U32)commonSse41ReadLane(a, lane, 4), bbits = (U32)commonSse41ReadLane(b, lane, 4);
            sum = commonSse41F32Add(sum, commonSse41F32Multiply(abits, bbits));
        }
        for (U32 lane = 0; lane < 4; ++lane) commonSse41WriteLane(result, lane, 4, (imm & (1 << lane)) ? sum : 0);
        break;
    }
    case 0x41: { // DPPD
        U64 sum = 0;
        for (U32 lane = 0; lane < 2; ++lane) if (imm & (1 << (lane + 4))) {
            U64 abits = commonSse41ReadLane(a, lane, 8), bbits = commonSse41ReadLane(b, lane, 8);
            sum = commonSse41F64Add(sum, commonSse41F64Multiply(abits, bbits));
        }
        for (U32 lane = 0; lane < 2; ++lane) commonSse41WriteLane(result, lane, 8, (imm & (1 << lane)) ? sum : 0);
        break;
    }
    case 0x42: { // MPSADBW
        U32 aBase = (imm & 4) ? 4 : 0, bBase = (imm & 3) * 4;
        for (U32 lane = 0; lane < 8; ++lane) {
            U32 sum = 0;
            for (U32 byte = 0; byte < 4; ++byte) {
                U32 av = a[aBase + lane + byte], bv = b[bBase + byte];
                sum += av > bv ? av - bv : bv - av;
            }
            commonSse41WriteLane(result, lane, 2, sum);
        }
        break;
    }
    default:
        break;
    }
}

static U32 commonSse41F32Multiply(U32 aBits, U32 bBits) {
    const U32 exponentMask = 0x7f800000, fractionMask = 0x007fffff, quietBit = 0x00400000;
    bool aNan = (aBits & exponentMask) == exponentMask && (aBits & fractionMask);
    bool bNan = (bBits & exponentMask) == exponentMask && (bBits & fractionMask);
    if (aNan) return aBits | quietBit;
    if (bNan) return bBits | quietBit;
    bool aInf = (aBits & exponentMask) == exponentMask;
    bool bInf = (bBits & exponentMask) == exponentMask;
    bool aZero = (aBits & 0x7fffffff) == 0;
    bool bZero = (bBits & 0x7fffffff) == 0;
    if ((aInf && bZero) || (bInf && aZero)) return 0xffc00000;
    float a, b, result;
    memcpy(&a, &aBits, sizeof(a)); memcpy(&b, &bBits, sizeof(b));
    result = a * b;
    U32 resultBits;
    memcpy(&resultBits, &result, sizeof(resultBits));
    return resultBits;
}

static U32 commonSse41F32Add(U32 aBits, U32 bBits) {
    const U32 exponentMask = 0x7f800000, fractionMask = 0x007fffff, quietBit = 0x00400000;
    bool aNan = (aBits & exponentMask) == exponentMask && (aBits & fractionMask);
    bool bNan = (bBits & exponentMask) == exponentMask && (bBits & fractionMask);
    if (aNan) return aBits | quietBit;
    if (bNan) return bBits | quietBit;
    if ((aBits & exponentMask) == exponentMask && (bBits & exponentMask) == exponentMask && ((aBits ^ bBits) & 0x80000000)) return 0xffc00000;
    float a, b, result;
    memcpy(&a, &aBits, sizeof(a)); memcpy(&b, &bBits, sizeof(b));
    result = a + b;
    U32 resultBits;
    memcpy(&resultBits, &result, sizeof(resultBits));
    return resultBits;
}

static U64 commonSse41F64Multiply(U64 aBits, U64 bBits) {
    const U64 exponentMask = 0x7ff0000000000000ULL, fractionMask = 0x000fffffffffffffULL, quietBit = 0x0008000000000000ULL;
    bool aNan = (aBits & exponentMask) == exponentMask && (aBits & fractionMask);
    bool bNan = (bBits & exponentMask) == exponentMask && (bBits & fractionMask);
    if (aNan) return aBits | quietBit;
    if (bNan) return bBits | quietBit;
    bool aInf = (aBits & exponentMask) == exponentMask;
    bool bInf = (bBits & exponentMask) == exponentMask;
    bool aZero = (aBits & 0x7fffffffffffffffULL) == 0;
    bool bZero = (bBits & 0x7fffffffffffffffULL) == 0;
    if ((aInf && bZero) || (bInf && aZero)) return 0xfff8000000000000ULL;
    double a, b, result;
    memcpy(&a, &aBits, sizeof(a)); memcpy(&b, &bBits, sizeof(b));
    result = a * b;
    U64 resultBits;
    memcpy(&resultBits, &result, sizeof(resultBits));
    return resultBits;
}

static U64 commonSse41F64Add(U64 aBits, U64 bBits) {
    const U64 exponentMask = 0x7ff0000000000000ULL, fractionMask = 0x000fffffffffffffULL, quietBit = 0x0008000000000000ULL;
    bool aNan = (aBits & exponentMask) == exponentMask && (aBits & fractionMask);
    bool bNan = (bBits & exponentMask) == exponentMask && (bBits & fractionMask);
    if (aNan) return aBits | quietBit;
    if (bNan) return bBits | quietBit;
    if ((aBits & exponentMask) == exponentMask && (bBits & exponentMask) == exponentMask && ((aBits ^ bBits) >> 63)) return 0xfff8000000000000ULL;
    double a, b, result;
    memcpy(&a, &aBits, sizeof(a)); memcpy(&b, &bBits, sizeof(b));
    result = a + b;
    U64 resultBits;
    memcpy(&resultBits, &result, sizeof(resultBits));
    return resultBits;
}

static U32 commonCrc32Update(U32 crc, U32 value, U32 bytes) {
    for (U32 i = 0; i < bytes; ++i) {
        crc ^= (U8)(value >> (i * 8));
        for (U32 bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0x82f63b78 & (0 - (crc & 1)));
    }
    return crc;
}

void common_crc32RR(CPU* cpu, U32 reg, U32 rm, U32 opcodeAndSize) {
    U32 size = (opcodeAndSize >> 8) & 0xff;
    U32 value = size == 1 ? *cpu->reg8[rm] : (size == 2 ? cpu->reg[rm].u16 : cpu->reg[rm].u32);
    cpu->reg[reg].u32 = commonCrc32Update(cpu->reg[reg].u32, value, size);
}

void common_crc32RE(CPU* cpu, U32 reg, U32 address, U32 opcodeAndSize) {
    U32 size = (opcodeAndSize >> 8) & 0xff;
    U32 value = size == 1 ? cpu->memory->readb(address) : (size == 2 ? cpu->memory->readw(address) : cpu->memory->readd(address));
    cpu->reg[reg].u32 = commonCrc32Update(cpu->reg[reg].u32, value, size);
}

static U32 commonPopcount32(U32 value) {
    U32 count = 0;
    while (value) {
        value &= value - 1;
        count++;
    }
    return count;
}

static void commonPopcntFlags(CPU* cpu, U32 count) {
    cpu->fillFlags();
    cpu->flags &= ~(AF | OF | SF | CF | PF | ZF);
    if (!count) cpu->flags |= ZF;
}

void common_popcntR16R16(CPU* cpu, U32 reg, U32 rm) {
    U32 count = commonPopcount32(cpu->reg[rm].u16);
    cpu->reg[reg].u16 = (U16)count;
    commonPopcntFlags(cpu, count);
}

void common_popcntR16E16(CPU* cpu, U32 reg, U32 address) {
    U32 count = commonPopcount32(cpu->memory->readw(address));
    cpu->reg[reg].u16 = (U16)count;
    commonPopcntFlags(cpu, count);
}

void common_popcntR32R32(CPU* cpu, U32 reg, U32 rm) {
    U32 count = commonPopcount32(cpu->reg[rm].u32);
    cpu->reg[reg].u32 = count;
    commonPopcntFlags(cpu, count);
}

void common_popcntR32E32(CPU* cpu, U32 reg, U32 address) {
    U32 count = commonPopcount32(cpu->memory->readd(address));
    cpu->reg[reg].u32 = count;
    commonPopcntFlags(cpu, count);
}

static S32 commonPcmpValue(const U8* data, U32 index, U32 type) {
    switch (type & 3) {
    case 0: return data[index];
    case 1: return data[index * 2] | (data[index * 2 + 1] << 8);
    case 2: return (S8)data[index];
    default: return (S16)(data[index * 2] | (data[index * 2 + 1] << 8));
    }
}

static S32 commonPcmpExplicitLength(U32 value, bool words) {
    S32 limit = words ? 8 : 16;
    S64 length = (S32)value;
    if (length > limit) length = limit;
    if (length < -limit) length = -limit;
    return length < 0 ? (S32)-length : (S32)length;
}

static S32 commonPcmpImplicitLength(const U8* data, bool words) {
    S32 limit = words ? 8 : 16;
    S32 length = 0;
    if (words) {
        while (length < limit && (data[length * 2] | (data[length * 2 + 1] << 8))) length++;
    } else {
        while (length < limit && data[length]) length++;
    }
    return length;
}

static U32 commonPcmpStr(CPU* cpu, const U8* d, const U8* s, U8 ctrl, S32 valids, S32 validd) {
    U32 upper = (ctrl & 1) ? 7 : 15;
    U32 res = 0;
    valids--;
    validd--;

    cpu->fillFlags();
    cpu->flags &= ~(AF | OF | SF | CF | PF | ZF);
    if (valids < (S32)upper) cpu->flags |= ZF;
    if (validd < (S32)upper) cpu->flags |= SF;

    switch ((ctrl >> 2) & 3) {
    case 0: // equal any
        for (S32 j = valids; j >= 0; --j) {
            res <<= 1;
            S32 v = commonPcmpValue(s, j, ctrl & 3);
            for (S32 i = validd; i >= 0; --i) res |= (v == commonPcmpValue(d, i, ctrl & 3));
        }
        break;
    case 1: // ranges
        for (S32 j = valids; j >= 0; --j) {
            res <<= 1;
            S32 v = commonPcmpValue(s, j, ctrl & 3);
            for (S32 i = ((validd - 1) | 1); i >= 0; i -= 2)
                res |= (commonPcmpValue(d, i, ctrl & 3) >= v && commonPcmpValue(d, i - 1, ctrl & 3) <= v);
        }
        break;
    case 2: { // equal each
        S32 maxv = valids > validd ? valids : validd;
        S32 minv = valids < validd ? valids : validd;
        res = (1u << (upper - maxv)) - 1;
        res <<= (maxv - minv);
        for (S32 i = minv; i >= 0; --i) {
            res <<= 1;
            res |= (commonPcmpValue(s, i, ctrl & 3) == commonPcmpValue(d, i, ctrl & 3));
        }
        break;
    }
    case 3: // equal ordered
        if (validd == -1) {
            res = (2u << upper) - 1;
            break;
        }
        for (S32 j = (valids == (S32)upper ? valids : valids - validd); j >= 0; --j) {
            res <<= 1;
            U32 v = 1;
            for (S32 i = (valids - j < validd ? valids - j : validd); i >= 0; --i)
                v &= (commonPcmpValue(s, i + j, ctrl & 3) == commonPcmpValue(d, i, ctrl & 3));
            res |= v;
        }
        break;
    }

    switch ((ctrl >> 4) & 3) {
    case 1: res ^= (2u << upper) - 1; break;
    case 3: res ^= (1u << (valids + 1)) - 1; break;
    default: break;
    }
    if (res) cpu->flags |= CF;
    if (res & 1) cpu->flags |= OF;
    return res;
}

static void commonPcmpWriteMask(CPU* cpu, U32 res, bool words) {
    U8 mask[16];
    if (words) {
        for (U32 i = 0; i < 8; ++i) {
            U16 value = (res & (1u << i)) ? 0xffff : 0;
            memcpy(mask + i * 2, &value, sizeof(value));
        }
    } else {
        for (U32 i = 0; i < 16; ++i) mask[i] = (res & (1u << i)) ? 0xff : 0;
    }
    memcpy(&cpu->xmm[0].pi.u64[0], mask, sizeof(mask));
}

static U32 commonPcmpHighestBit(U32 value) {
    U32 index = 0;
    while (value >>= 1) index++;
    return index;
}

static U32 commonPcmpLowestBit(U32 value) {
    U32 index = 0;
    while (!(value & 1)) {
        value >>= 1;
        index++;
    }
    return index;
}

static void commonSse42Pcmp(CPU* cpu, const U8* a, const U8* b, U8 opcode, U8 imm) {
    bool words = (imm & 1) != 0;
    S32 valids, validd;
    if (opcode == 0x60 || opcode == 0x61) { // PCMPESTR*
        valids = commonPcmpExplicitLength(cpu->reg[regDX].u32, words);
        validd = commonPcmpExplicitLength(cpu->reg[regAX].u32, words);
    } else { // PCMPISTR*
        valids = commonPcmpImplicitLength(b, words);
        validd = commonPcmpImplicitLength(a, words);
    }
    U32 res = commonPcmpStr(cpu, a, b, imm, valids, validd);
    if (opcode == 0x60 || opcode == 0x62) { // PCMP*STRM
        if (imm & 0x40) commonPcmpWriteMask(cpu, res, words);
        else {
            cpu->xmm[0].pi.u64[0] = res;
            cpu->xmm[0].pi.u64[1] = 0;
        }
    } else { // PCMP*STRI
        cpu->reg[regCX].u32 = res ? ((imm & 0x40) ? commonPcmpHighestBit(res) : commonPcmpLowestBit(res)) : (16u >> (imm & 1));
    }
}

void common_sse41_3aRR(CPU* cpu, U32 reg, U32 rm, U32 packedOpcodeAndImm) {
    U8 opcode = (U8)(packedOpcodeAndImm >> 8), imm = (U8)packedOpcodeAndImm;
    U8 a[16], b[16], result[16];
    memcpy(a, &cpu->xmm[reg].pi.u64[0], sizeof(a));
    memcpy(b, &cpu->xmm[rm].pi.u64[0], sizeof(b));
    memcpy(result, a, sizeof(result));
    if (opcode >= 0x60 && opcode <= 0x63) {
        commonSse42Pcmp(cpu, a, b, opcode, imm);
        return;
    }
    if ((opcode >= 0x08 && opcode <= 0x0e) || opcode == 0x21 || (opcode >= 0x40 && opcode <= 0x42)) {
        commonSse41Apply3a(cpu, result, a, b, opcode, imm);
        memcpy(&cpu->xmm[reg].pi.u64[0], result, sizeof(result));
        return;
    }
    switch (opcode) {
    case 0x14: { // PEXTRB r/m8, xmm, imm8
        U8 value = (U8)commonSse41ReadLane(a, imm & 15, 1);
        cpu->reg[rm].u32 = value;
        return;
    }
    case 0x16: case 0x17: { // PEXTRD / EXTRACTPS r/m32, xmm, imm8
        cpu->reg[rm].u32 = (U32)commonSse41ReadLane(a, imm & 3, 4);
        return;
    }
    case 0x20: { // PINSRB xmm, r/m8, imm8
        U8 value = *cpu->reg8[rm];
        commonSse41WriteLane(result, imm & 15, 1, value);
        break;
    }
    case 0x22: // PINSRD
        commonSse41WriteLane(result, imm & 3, 4, cpu->reg[rm].u32);
        break;
    default:
        return;
    }
    memcpy(&cpu->xmm[reg].pi.u64[0], result, sizeof(result));
}

void common_sse41_3aRE(CPU* cpu, U32 reg, U32 address, U32 packedOpcodeAndImm) {
    U8 opcode = (U8)(packedOpcodeAndImm >> 8), imm = (U8)packedOpcodeAndImm;
    U8 a[16], b[16], result[16];
    memcpy(a, &cpu->xmm[reg].pi.u64[0], sizeof(a));
    memcpy(result, a, sizeof(result));
    if (opcode == 0x14 || opcode == 0x15 || opcode == 0x16 || opcode == 0x17) {
        U32 width = opcode == 0x14 ? 1 : (opcode == 0x15 ? 2 : 4);
        U64 value = commonSse41ReadLane(a, imm & (opcode == 0x14 ? 15 : (opcode == 0x15 ? 7 : 3)), width);
        commonSse41StoreMemory(cpu, address, value, width);
        return;
    }
    U32 readSize = (opcode == 0x0a || opcode == 0x21) ? 4 : ((opcode == 0x0b) ? 8 : (opcode == 0x20 ? 1 : (opcode == 0x22 ? 4 : 16)));
    commonSse41ReadMemory(cpu, address, b, readSize);
    if (opcode >= 0x60 && opcode <= 0x63) {
        commonSse42Pcmp(cpu, a, b, opcode, imm);
        return;
    }
    if ((opcode >= 0x08 && opcode <= 0x0e) || opcode == 0x21 || (opcode >= 0x40 && opcode <= 0x42)) {
        commonSse41Apply3a(cpu, result, a, b, opcode, opcode == 0x21 ? (imm & 0x3f) : imm);
        memcpy(&cpu->xmm[reg].pi.u64[0], result, sizeof(result));
        return;
    }
    switch (opcode) {
    case 0x20: commonSse41WriteLane(result, imm & 15, 1, b[0]); break;
    case 0x22: commonSse41WriteLane(result, imm & 3, 4, commonSse41ReadLane(b, 0, 4)); break;
    default:
        return;
    }
    memcpy(&cpu->xmm[reg].pi.u64[0], result, sizeof(result));
}

static U32 commonSseRoundF32ToI32(U32 value, U32 mxcsr, bool truncate) {
    union {
        U32 i;
        float f;
    } f;
    f.i = value;
    return commonSseRoundToI32((double)f.f, mxcsr, truncate);
}

void common_addpsXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_add_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_addpsE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8);
    cpu->xmm[reg].ps = simde_mm_add_ps(cpu->xmm[reg].ps, value);
}

void common_addssXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_add_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_addssE32(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u32[0] = cpu->memory->readd(address);
    cpu->xmm[reg].ps = simde_mm_add_ss(cpu->xmm[reg].ps, value);
}

void common_subpsXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_sub_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_subpsE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8);
    cpu->xmm[reg].ps = simde_mm_sub_ps(cpu->xmm[reg].ps, value);
}

void common_subssXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_sub_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_subssE32(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u32[0] = cpu->memory->readd(address);
    cpu->xmm[reg].ps = simde_mm_sub_ss(cpu->xmm[reg].ps, value);
}

void common_mulpsXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_mul_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_mulpsE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8);
    cpu->xmm[reg].ps = simde_mm_mul_ps(cpu->xmm[reg].ps, value);
}

void common_mulssXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_mul_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_mulssE32(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u32[0] = cpu->memory->readd(address);
    cpu->xmm[reg].ps = simde_mm_mul_ss(cpu->xmm[reg].ps, value);
}

void common_divpsXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_div_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_divpsE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8);
    cpu->xmm[reg].ps = simde_mm_div_ps(cpu->xmm[reg].ps, value);
}

void common_divssXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_div_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_divssE32(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u32[0] = cpu->memory->readd(address);
    cpu->xmm[reg].ps = simde_mm_div_ss(cpu->xmm[reg].ps, value);
}

void common_rcppsXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_rcp_ps(cpu->xmm[r2].ps);
}

void common_rcppsE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8);
    cpu->xmm[reg].ps = simde_mm_rcp_ps(value);
}

void common_rcpssXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_rcp_ss(cpu->xmm[r2].ps);
}

void common_rcpssE32(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value = cpu->xmm[reg].ps;
    value.u32[0] = cpu->memory->readd(address);
    cpu->xmm[reg].ps = simde_mm_rcp_ss(value);
}

void common_sqrtpsXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_sqrt_ps(cpu->xmm[r2].ps);    
}

void common_sqrtpsE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8);  
    cpu->xmm[reg].ps = simde_mm_sqrt_ps(value);
}

void common_sqrtssXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_sqrt_ss(cpu->xmm[r2].ps);
}

void common_sqrtssE32(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value = cpu->xmm[reg].ps;
    value.u32[0] = cpu->memory->readd(address);
    cpu->xmm[reg].ps = simde_mm_sqrt_ss(value);
}

void common_rsqrtpsXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_rsqrt_ps(cpu->xmm[r2].ps);    
}

void common_rsqrtpsE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8);  
    cpu->xmm[reg].ps = simde_mm_rsqrt_ps(value);
}

void common_rsqrtssXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_rsqrt_ss(cpu->xmm[r2].ps);
}

void common_rsqrtssE32(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value = cpu->xmm[reg].ps;
    value.u32[0] = cpu->memory->readd(address);
    cpu->xmm[reg].ps = simde_mm_rsqrt_ss(value);
}

void common_maxpsXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_max_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_maxpsE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8);  
    cpu->xmm[reg].ps = simde_mm_max_ps(cpu->xmm[reg].ps, value);
}

void common_maxssXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_max_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_maxssE32(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u32[0] = cpu->memory->readd(address);
    cpu->xmm[reg].ps = simde_mm_max_ss(cpu->xmm[reg].ps, value);
}

void common_minpsXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_min_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_minpsE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8);  
    cpu->xmm[reg].ps = simde_mm_min_ps(cpu->xmm[reg].ps, value);
}

void common_minssXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_min_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_minssE32(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u32[0] = cpu->memory->readd(address);
    cpu->xmm[reg].ps = simde_mm_min_ss(cpu->xmm[reg].ps, value);
}

void common_pavgbMmxMmx(CPU* cpu, U32 r1, U32 r2) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(r1)->q;
    m2.u64[0] = cpu->fpu.getMMX(r2)->q;
    simde__m64 r = simde_mm_avg_pu8(m1, m2);
    cpu->fpu.getMMX(r1)->q = r.u64[0];
}

void common_pavgbMmxE64(CPU* cpu, U32 reg, U32 address) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(reg)->q;
    m2.u64[0] = cpu->memory->readq(address);
    simde__m64 r = simde_mm_avg_pu8(m1, m2);
    cpu->fpu.getMMX(reg)->q = r.u64[0];
}

void common_pavgwMmxMmx(CPU* cpu, U32 r1, U32 r2) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(r1)->q;
    m2.u64[0] = cpu->fpu.getMMX(r2)->q;
    simde__m64 r = simde_mm_avg_pu16(m1, m2);
    cpu->fpu.getMMX(r1)->q = r.u64[0];
}

void common_pavgwMmxE64(CPU* cpu, U32 reg, U32 address) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(reg)->q;
    m2.u64[0] = cpu->memory->readq(address);
    simde__m64 r = simde_mm_avg_pu16(m1, m2);
    cpu->fpu.getMMX(reg)->q = r.u64[0];
}

void common_psadbwMmxMmx(CPU* cpu, U32 r1, U32 r2) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(r1)->q;
    m2.u64[0] = cpu->fpu.getMMX(r2)->q;
    simde__m64 r = simde_mm_sad_pu8(m1, m2);
    cpu->fpu.getMMX(r1)->q = r.u64[0];
}

void common_psadbwMmxE64(CPU* cpu, U32 reg, U32 address) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(reg)->q;
    m2.u64[0] = cpu->memory->readq(address);
    simde__m64 r = simde_mm_sad_pu8(m1, m2);
    cpu->fpu.getMMX(reg)->q = r.u64[0];
}

void common_pextrwR32Mmx(CPU* cpu, U32 r1, U32 r2, U8 imm) {
    simde__m64 m = {};
    m.u64[0] = cpu->fpu.getMMX(r2)->q;
    cpu->reg[r1].u32 = simde_mm_extract_pi16(m, imm & 3);
}

void common_pextrwE16Mmx(CPU* cpu, U32 reg, U32 address, U8 imm) {
    simde__m64 m = {};
    m.u64[0] = cpu->fpu.getMMX(reg)->q;
    cpu->memory->writew(address, (U16)simde_mm_extract_pi16(m, imm & 3));
}

void common_pinsrwMmxR32(CPU* cpu, U32 r1, U32 r2, U8 imm) {
    simde__m64 m = {};
    m.u64[0] = cpu->fpu.getMMX(r1)->q;
    m = simde_mm_insert_pi16(m, cpu->reg[r2].u16, imm & 3);
    cpu->fpu.getMMX(r1)->q = m.u64[0];
}

void common_pinsrwMmxE16(CPU* cpu, U32 reg, U32 address, U8 imm) {
    simde__m64 m = {};
    m.u64[0] = cpu->fpu.getMMX(reg)->q;
    m = simde_mm_insert_pi16(m, cpu->memory->readw(address), imm & 3);
    cpu->fpu.getMMX(reg)->q = m.u64[0];
}

void common_pmaxswMmxMmx(CPU* cpu, U32 r1, U32 r2) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(r1)->q;
    m2.u64[0] = cpu->fpu.getMMX(r2)->q;
    simde__m64 r = simde_mm_max_pi16(m1, m2);
    cpu->fpu.getMMX(r1)->q = r.u64[0];
}

void common_pmaxswMmxE64(CPU* cpu, U32 reg, U32 address) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(reg)->q;
    m2.u64[0] = cpu->memory->readq(address);
    simde__m64 r = simde_mm_max_pi16(m1, m2);
    cpu->fpu.getMMX(reg)->q = r.u64[0];
}

void common_pmaxubMmxMmx(CPU* cpu, U32 r1, U32 r2) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(r1)->q;
    m2.u64[0] = cpu->fpu.getMMX(r2)->q;
    simde__m64 r = simde_mm_max_pu8(m1, m2);
    cpu->fpu.getMMX(r1)->q = r.u64[0];
}

void common_pmaxubMmxE64(CPU* cpu, U32 reg, U32 address) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(reg)->q;
    m2.u64[0] = cpu->memory->readq(address);
    simde__m64 r = simde_mm_max_pu8(m1, m2);
    cpu->fpu.getMMX(reg)->q = r.u64[0];
}

void common_pminswMmxMmx(CPU* cpu, U32 r1, U32 r2) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(r1)->q;
    m2.u64[0] = cpu->fpu.getMMX(r2)->q;
    simde__m64 r = simde_mm_min_pi16(m1, m2);
    cpu->fpu.getMMX(r1)->q = r.u64[0];
}

void common_pminswMmxE64(CPU* cpu, U32 reg, U32 address) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(reg)->q;
    m2.u64[0] = cpu->memory->readq(address);
    simde__m64 r = simde_mm_min_pi16(m1, m2);
    cpu->fpu.getMMX(reg)->q = r.u64[0];
}

void common_pminubMmxMmx(CPU* cpu, U32 r1, U32 r2) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(r1)->q;
    m2.u64[0] = cpu->fpu.getMMX(r2)->q;
    simde__m64 r = simde_mm_min_pu8(m1, m2);
    cpu->fpu.getMMX(r1)->q = r.u64[0];
}

void common_pminubMmxE64(CPU* cpu, U32 reg, U32 address) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(reg)->q;
    m2.u64[0] = cpu->memory->readq(address);
    simde__m64 r = simde_mm_min_pu8(m1, m2);
    cpu->fpu.getMMX(reg)->q = r.u64[0];
}

void common_pmovmskbR32Mmx(CPU* cpu, U32 r1, U32 r2) {
    simde__m64 m = {};
    m.u64[0] = cpu->fpu.getMMX(r2)->q;
    cpu->reg[r1].u32 = (U32)simde_mm_movemask_pi8(m);
}

void common_pmulhuwMmxMmx(CPU* cpu, U32 r1, U32 r2) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(r1)->q;
    m2.u64[0] = cpu->fpu.getMMX(r2)->q;
    simde__m64 r = simde_mm_mulhi_pu16(m1, m2);
    cpu->fpu.getMMX(r1)->q = r.u64[0];
}

void common_pmulhuwMmxE64(CPU* cpu, U32 reg, U32 address) {
    simde__m64 m1 = {};
    simde__m64 m2 = {};
    m1.u64[0] = cpu->fpu.getMMX(reg)->q;
    m2.u64[0] = cpu->memory->readq(address);
    simde__m64 r = simde_mm_mulhi_pu16(m1, m2);
    cpu->fpu.getMMX(reg)->q = r.u64[0];
}

void common_pshufwMmxMmx(CPU* cpu, U32 r1, U32 r2, U8 imm) {
    simde__m64 m2 = {};
    m2.u64[0] = cpu->fpu.getMMX(r2)->q;
    simde__m64 r = simde_mm_shuffle_pi16(m2, imm);
    cpu->fpu.getMMX(r1)->q = r.u64[0];
}

void common_pshufwMmxE64(CPU* cpu, U32 reg, U32 address, U8 imm) {
    simde__m64 m2 = {};
    m2.u64[0] = cpu->memory->readq(address);
    simde__m64 r = simde_mm_shuffle_pi16(m2, imm);
    cpu->fpu.getMMX(reg)->q = r.u64[0];
    cpu->fpu.startMMX();
}

void common_andnpsXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_andnot_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_andnpsXmmE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8); 
    cpu->xmm[reg].ps = simde_mm_andnot_ps(cpu->xmm[reg].ps, value);
}

void common_andpsXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_and_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_andpsXmmE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8); 
    cpu->xmm[reg].ps = simde_mm_and_ps(cpu->xmm[reg].ps, value);
}

void common_orpsXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_or_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_orpsXmmE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8); 
    cpu->xmm[reg].ps = simde_mm_or_ps(cpu->xmm[reg].ps, value);
}

void common_xorpsXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_xor_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_xorpsXmmE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8); 
    cpu->xmm[reg].ps = simde_mm_xor_ps(cpu->xmm[reg].ps, value);
}

void common_cvtpi2psXmmMmx(CPU* cpu, U32 r1, U32 r2) {
    simde__m64 value;
    value.u64[0] = cpu->fpu.getMMX(r2)->q;
    cpu->xmm[r1].ps = simde_mm_cvtpi32_ps(cpu->xmm[r1].ps, value);
}

void common_cvtpi2psXmmE64(CPU* cpu, U32 reg, U32 address) {
    simde__m64 value;
    value.u64[0] = cpu->memory->readq(address);
    cpu->xmm[reg].ps = simde_mm_cvtpi32_ps(cpu->xmm[reg].ps, value);
}

void common_cvtps2piMmxXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->fpu.getMMX(r1)->q = ((U64)commonSseRoundF32ToI32(cpu->xmm[r2].ps.u32[1], cpu->mxcsr, false) << 32) | commonSseRoundF32ToI32(cpu->xmm[r2].ps.u32[0], cpu->mxcsr, false);
    cpu->fpu.startMMX();
}

void common_cvtps2piMmxE64(CPU* cpu, U32 reg, U32 address) {
    U64 value = cpu->memory->readq(address);
    cpu->fpu.getMMX(reg)->q = ((U64)commonSseRoundF32ToI32((U32)(value >> 32), cpu->mxcsr, false) << 32) | commonSseRoundF32ToI32((U32)value, cpu->mxcsr, false);
    cpu->fpu.startMMX();
}

void common_cvtsi2ssXmmR32(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_cvtsi32_ss(cpu->xmm[r1].ps, (S32)cpu->reg[r2].u32);
}

void common_cvtsi2ssXmmE32(CPU* cpu, U32 reg, U32 address) {
    cpu->xmm[reg].ps = simde_mm_cvtsi32_ss(cpu->xmm[reg].ps, (S32)cpu->memory->readd(address));
}

void common_cvtss2siR32Xmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->reg[r1].u32 = commonSseRoundF32ToI32(cpu->xmm[r2].ps.u32[0], cpu->mxcsr, false);
}

void common_cvtss2siR32E32(CPU* cpu, U32 reg, U32 address) {
    cpu->reg[reg].u32 = commonSseRoundF32ToI32(cpu->memory->readd(address), cpu->mxcsr, false);
}

void common_cvttps2piMmxXmm(CPU* cpu, U32 r1, U32 r2) {
    simde__m64 r = simde_mm_cvttps_pi32(cpu->xmm[r2].ps);
    cpu->fpu.getMMX(r1)->q = r.u64[0];
    cpu->fpu.startMMX();
}

void common_cvttps2piMmxE64(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    simde__m64 r = simde_mm_cvttps_pi32(value);
    cpu->fpu.getMMX(reg)->q = r.u64[0];
    cpu->fpu.startMMX();
}

void common_cvttss2siR32Xmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->reg[r1].u32 = simde_mm_cvttss_si32(cpu->xmm[r2].ps);
}

void common_cvttss2siR32E32(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u32[0] = cpu->memory->readd(address);
    cpu->reg[reg].u32 = simde_mm_cvttss_si32(value);
}

void common_movapsXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = cpu->xmm[r2].ps;
}

void common_movapsXmmE128(CPU* cpu, U32 reg, U32 address) {
    cpu->xmm[reg].ps.u64[0] = cpu->memory->readq(address);
    cpu->xmm[reg].ps.u64[1] = cpu->memory->readq(address+8);
}

void common_movapsE128Xmm(CPU* cpu, U32 reg, U32 address) {
    cpu->memory->writeq(address, cpu->xmm[reg].ps.u64[0]);
    cpu->memory->writeq(address+8, cpu->xmm[reg].ps.u64[1]);
}

// Snapshot both operands before writing: Chromium uses HADDPD xmm7,xmm7.
void common_haddpdXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    const simde__m128d dst = cpu->xmm[r1].pd;
    const simde__m128d src = cpu->xmm[r2].pd;
    cpu->xmm[r1].pi.u64[0] = commonSseAddSubF64(dst.u64[0], dst.u64[1], false);
    cpu->xmm[r1].pi.u64[1] = commonSseAddSubF64(src.u64[0], src.u64[1], false);
}
void common_haddpdXmmE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128d src;
    src.u64[0] = cpu->memory->readq(address);
    src.u64[1] = cpu->memory->readq(address + 8);
    const simde__m128d dst = cpu->xmm[reg].pd;
    cpu->xmm[reg].pi.u64[0] = commonSseAddSubF64(dst.u64[0], dst.u64[1], false);
    cpu->xmm[reg].pi.u64[1] = commonSseAddSubF64(src.u64[0], src.u64[1], false);
}
void common_haddpsXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    const simde__m128 dst = cpu->xmm[r1].ps;
    const simde__m128 src = cpu->xmm[r2].ps;
    cpu->xmm[r1].pi.u32[0] = commonSseAddSubF32(dst.u32[0], dst.u32[1], false);
    cpu->xmm[r1].pi.u32[1] = commonSseAddSubF32(dst.u32[2], dst.u32[3], false);
    cpu->xmm[r1].pi.u32[2] = commonSseAddSubF32(src.u32[0], src.u32[1], false);
    cpu->xmm[r1].pi.u32[3] = commonSseAddSubF32(src.u32[2], src.u32[3], false);
}
void common_haddpsXmmE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 src;
    src.u64[0] = cpu->memory->readq(address);
    src.u64[1] = cpu->memory->readq(address + 8);
    const simde__m128 dst = cpu->xmm[reg].ps;
    cpu->xmm[reg].pi.u32[0] = commonSseAddSubF32(dst.u32[0], dst.u32[1], false);
    cpu->xmm[reg].pi.u32[1] = commonSseAddSubF32(dst.u32[2], dst.u32[3], false);
    cpu->xmm[reg].pi.u32[2] = commonSseAddSubF32(src.u32[0], src.u32[1], false);
    cpu->xmm[reg].pi.u32[3] = commonSseAddSubF32(src.u32[2], src.u32[3], false);
}
void common_hsubpdXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    const simde__m128d dst = cpu->xmm[r1].pd;
    const simde__m128d src = cpu->xmm[r2].pd;
    cpu->xmm[r1].pi.u64[0] = commonSseAddSubF64(dst.u64[0], dst.u64[1], true);
    cpu->xmm[r1].pi.u64[1] = commonSseAddSubF64(src.u64[0], src.u64[1], true);
}
void common_hsubpdXmmE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128d src;
    src.u64[0] = cpu->memory->readq(address);
    src.u64[1] = cpu->memory->readq(address + 8);
    const simde__m128d dst = cpu->xmm[reg].pd;
    cpu->xmm[reg].pi.u64[0] = commonSseAddSubF64(dst.u64[0], dst.u64[1], true);
    cpu->xmm[reg].pi.u64[1] = commonSseAddSubF64(src.u64[0], src.u64[1], true);
}
void common_hsubpsXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    const simde__m128 dst = cpu->xmm[r1].ps;
    const simde__m128 src = cpu->xmm[r2].ps;
    cpu->xmm[r1].pi.u32[0] = commonSseAddSubF32(dst.u32[0], dst.u32[1], true);
    cpu->xmm[r1].pi.u32[1] = commonSseAddSubF32(dst.u32[2], dst.u32[3], true);
    cpu->xmm[r1].pi.u32[2] = commonSseAddSubF32(src.u32[0], src.u32[1], true);
    cpu->xmm[r1].pi.u32[3] = commonSseAddSubF32(src.u32[2], src.u32[3], true);
}
void common_hsubpsXmmE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 src;
    src.u64[0] = cpu->memory->readq(address);
    src.u64[1] = cpu->memory->readq(address + 8);
    const simde__m128 dst = cpu->xmm[reg].ps;
    cpu->xmm[reg].pi.u32[0] = commonSseAddSubF32(dst.u32[0], dst.u32[1], true);
    cpu->xmm[reg].pi.u32[1] = commonSseAddSubF32(dst.u32[2], dst.u32[3], true);
    cpu->xmm[reg].pi.u32[2] = commonSseAddSubF32(src.u32[0], src.u32[1], true);
    cpu->xmm[reg].pi.u32[3] = commonSseAddSubF32(src.u32[2], src.u32[3], true);
}
void common_addsubpdXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    const simde__m128d dst = cpu->xmm[r1].pd;
    const simde__m128d src = cpu->xmm[r2].pd;
    cpu->xmm[r1].pi.u64[0] = commonSseAddSubF64(dst.u64[0], src.u64[0], true);
    cpu->xmm[r1].pi.u64[1] = commonSseAddSubF64(dst.u64[1], src.u64[1], false);
}
void common_addsubpdXmmE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128d src;
    src.u64[0] = cpu->memory->readq(address);
    src.u64[1] = cpu->memory->readq(address + 8);
    const simde__m128d dst = cpu->xmm[reg].pd;
    cpu->xmm[reg].pi.u64[0] = commonSseAddSubF64(dst.u64[0], src.u64[0], true);
    cpu->xmm[reg].pi.u64[1] = commonSseAddSubF64(dst.u64[1], src.u64[1], false);
}
void common_addsubpsXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    const simde__m128 dst = cpu->xmm[r1].ps;
    const simde__m128 src = cpu->xmm[r2].ps;
    for (int i = 0; i < 4; ++i)
        cpu->xmm[r1].pi.u32[i] = commonSseAddSubF32(dst.u32[i], src.u32[i], !(i & 1));
}
void common_addsubpsXmmE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 src;
    src.u64[0] = cpu->memory->readq(address);
    src.u64[1] = cpu->memory->readq(address + 8);
    const simde__m128 dst = cpu->xmm[reg].ps;
    for (int i = 0; i < 4; ++i)
        cpu->xmm[reg].pi.u32[i] = commonSseAddSubF32(dst.u32[i], src.u32[i], !(i & 1));
}
void common_lddquXmmE128(CPU* cpu, U32 reg, U32 address) {
    cpu->xmm[reg].pi.u64[0] = cpu->memory->readq(address);
    cpu->xmm[reg].pi.u64[1] = cpu->memory->readq(address + 8);
}

// SSE3 duplicate moves preserve bits, including NaN payloads and signed zero.
void common_movddupXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    U64 value = cpu->xmm[r2].pi.u64[0];
    cpu->xmm[r1].pi.u64[0] = value;
    cpu->xmm[r1].pi.u64[1] = value;
}
void common_movddupXmmE64(CPU* cpu, U32 reg, U32 address) {
    U64 value = cpu->memory->readq(address);
    cpu->xmm[reg].pi.u64[0] = value;
    cpu->xmm[reg].pi.u64[1] = value;
}
void common_movsldupXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    U32 low = cpu->xmm[r2].pi.u32[0], high = cpu->xmm[r2].pi.u32[2];
    cpu->xmm[r1].pi.u32[0] = cpu->xmm[r1].pi.u32[1] = low;
    cpu->xmm[r1].pi.u32[2] = cpu->xmm[r1].pi.u32[3] = high;
}
void common_movsldupXmmE128(CPU* cpu, U32 reg, U32 address) {
    U64 first = cpu->memory->readq(address), second = cpu->memory->readq(address + 8);
    U32 low = (U32)first, high = (U32)second;
    cpu->xmm[reg].pi.u32[0] = cpu->xmm[reg].pi.u32[1] = low;
    cpu->xmm[reg].pi.u32[2] = cpu->xmm[reg].pi.u32[3] = high;
}
void common_movshdupXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    U32 low = cpu->xmm[r2].pi.u32[1], high = cpu->xmm[r2].pi.u32[3];
    cpu->xmm[r1].pi.u32[0] = cpu->xmm[r1].pi.u32[1] = low;
    cpu->xmm[r1].pi.u32[2] = cpu->xmm[r1].pi.u32[3] = high;
}
void common_movshdupXmmE128(CPU* cpu, U32 reg, U32 address) {
    U64 first = cpu->memory->readq(address), second = cpu->memory->readq(address + 8);
    U32 low = (U32)(first >> 32), high = (U32)(second >> 32);
    cpu->xmm[reg].pi.u32[0] = cpu->xmm[reg].pi.u32[1] = low;
    cpu->xmm[reg].pi.u32[2] = cpu->xmm[reg].pi.u32[3] = high;
}

void common_movhlpsXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps.u64[0] = cpu->xmm[r2].ps.u64[1];
}

void common_movlhpsXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps.u64[1] = cpu->xmm[r2].ps.u64[0];
}

void common_movhpsXmmE64(CPU* cpu, U32 reg, U32 address) {
    cpu->xmm[reg].ps.u64[1] = cpu->memory->readq(address);
}

void common_movhpsE64Xmm(CPU* cpu, U32 reg, U32 address) {
    cpu->memory->writeq(address, cpu->xmm[reg].ps.u64[1]);
}

void common_movlpsXmmE64(CPU* cpu, U32 reg, U32 address) {
    cpu->xmm[reg].ps.u64[0] = cpu->memory->readq(address);
}

void common_movlpsE64Xmm(CPU* cpu, U32 reg, U32 address) {
    cpu->memory->writeq(address, cpu->xmm[reg].ps.u64[0]);
}

void common_movmskpsR32Xmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->reg[r1].u32 = (U32)simde_mm_movemask_ps(cpu->xmm[r2].ps);
}

void common_movssXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps.u32[0] = cpu->xmm[r2].ps.u32[0];
}

void common_movssXmmE32(CPU* cpu, U32 reg, U32 address) {
    cpu->xmm[reg].ps.u32[0] = cpu->memory->readd(address);
    cpu->xmm[reg].ps.u32[1] = 0;
    cpu->xmm[reg].ps.u32[2] = 0;
    cpu->xmm[reg].ps.u32[3] = 0;
}

void common_movssE32Xmm(CPU* cpu, U32 reg, U32 address) {
    cpu->memory->writed(address, cpu->xmm[reg].ps.u32[0]);
}

void common_movupsXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = cpu->xmm[r2].ps;
}

void common_movupsXmmE128(CPU* cpu, U32 reg, U32 address) {
    cpu->xmm[reg].ps.u64[0] = cpu->memory->readq(address);
    cpu->xmm[reg].ps.u64[1] = cpu->memory->readq(address+8);
}

void common_movupsE128Xmm(CPU* cpu, U32 reg, U32 address) {
    cpu->memory->writeq(address, cpu->xmm[reg].ps.u64[0]);
    cpu->memory->writeq(address+8, cpu->xmm[reg].ps.u64[1]);
}

void common_maskmovqEDIMmxMmx(CPU* cpu, U32 r1, U32 r2, U32 address) {
    simde__m64 a = {};
    simde__m64 mask = {};
    U8 result[8] = {};
    a.u64[0] = cpu->fpu.getMMX(r1)->q;
    mask.u64[0] = cpu->fpu.getMMX(r2)->q;

    cpu->memory->memcpy(result, address, 8);
    simde_mm_maskmove_si64(a, mask, (int8_t*)result);
    cpu->memory->memcpy(address, result, 8);
}

#define G(rm) ((rm >> 3) & 7)
#define E(rm) (rm & 7)

void common_maskmovqEDIMmxMmxRM(CPU* cpu, U32 rm, U32 base, U32 bigAddress) {
    simde__m64 a = {};
    simde__m64 mask = {};
    U32 address = (bigAddress ? EDI : DI) + cpu->seg[base].address;
    U8 result[8] = {};
    a.u64[0] = cpu->fpu.getMMX(G(rm))->q;
    mask.u64[0] = cpu->fpu.getMMX(E(rm))->q;

    cpu->memory->memcpy(result, address, 8);
    simde_mm_maskmove_si64(a, mask, (int8_t*)result);
    cpu->memory->memcpy(address, result, 8);
}

void common_movntpsE128Xmm(CPU* cpu, U32 reg, U32 address) {
    cpu->memory->writeq(address, cpu->xmm[reg].ps.u64[0]);
    cpu->memory->writeq(address+8, cpu->xmm[reg].ps.u64[1]);
}

void common_movntqE64Mmx(CPU* cpu, U32 reg, U32 address) {
    cpu->memory->writeq(address, cpu->fpu.getMMX(reg)->q);
}

void common_shufpsXmmXmm(CPU* cpu, U32 r1, U32 r2, U8 imm) {
    cpu->xmm[r1].ps = simde_mm_shuffle_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps, imm);
}

void common_shufpsXmmE128(CPU* cpu, U32 reg, U32 address, U8 imm) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8);  
    cpu->xmm[reg].ps = simde_mm_shuffle_ps(cpu->xmm[reg].ps, value, imm);
}

void common_unpckhpsXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_unpackhi_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_unpckhpsXmmE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8);    
    cpu->xmm[reg].ps = simde_mm_unpackhi_ps(cpu->xmm[reg].ps, value);
}

void common_unpcklpsXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->xmm[r1].ps = simde_mm_unpacklo_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps);
}

void common_unpcklpsXmmE128(CPU* cpu, U32 reg, U32 address) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8);    
    cpu->xmm[reg].ps = simde_mm_unpacklo_ps(cpu->xmm[reg].ps, value);
}

void common_prefetchT0(CPU* cpu) {
}

void common_prefetchT1(CPU* cpu) {
}

void common_prefetchT2(CPU* cpu) {
}

void common_prefetchNTA(CPU* cpu) {
}

void common_cmppsXmmXmm(CPU* cpu, U32 r1, U32 r2, U8 imm) {
    int which = imm & 7;
    switch (which) {
    case 0: cpu->xmm[r1].ps = simde_mm_cmpeq_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 1: cpu->xmm[r1].ps = simde_mm_cmplt_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 2: cpu->xmm[r1].ps = simde_mm_cmple_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 3: cpu->xmm[r1].ps = simde_mm_cmpunord_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 4: cpu->xmm[r1].ps = simde_mm_cmpneq_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 5: cpu->xmm[r1].ps = simde_mm_cmpnlt_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 6: cpu->xmm[r1].ps = simde_mm_cmpnle_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 7: cpu->xmm[r1].ps = simde_mm_cmpord_ps(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    }
}

void common_cmppsXmmE128(CPU* cpu, U32 reg, U32 address, U8 imm) {
    simde__m128 value;
    value.u64[0] = cpu->memory->readq(address);
    value.u64[1] = cpu->memory->readq(address+8);
    int which = imm & 7;
    switch (which) {
    case 0: cpu->xmm[reg].ps = simde_mm_cmpeq_ps(cpu->xmm[reg].ps, value); break;
    case 1: cpu->xmm[reg].ps = simde_mm_cmplt_ps(cpu->xmm[reg].ps, value); break;
    case 2: cpu->xmm[reg].ps = simde_mm_cmple_ps(cpu->xmm[reg].ps, value); break;
    case 3: cpu->xmm[reg].ps = simde_mm_cmpunord_ps(cpu->xmm[reg].ps, value); break;
    case 4: cpu->xmm[reg].ps = simde_mm_cmpneq_ps(cpu->xmm[reg].ps, value); break;
    case 5: cpu->xmm[reg].ps = simde_mm_cmpnlt_ps(cpu->xmm[reg].ps, value); break;
    case 6: cpu->xmm[reg].ps = simde_mm_cmpnle_ps(cpu->xmm[reg].ps, value); break;
    case 7: cpu->xmm[reg].ps = simde_mm_cmpord_ps(cpu->xmm[reg].ps, value); break;
    }
}

void common_cmpssXmmXmm(CPU* cpu, U32 r1, U32 r2, U8 imm) {
    int which = imm & 7;
    switch (which) {
    case 0: cpu->xmm[r1].ps = simde_mm_cmpeq_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 1: cpu->xmm[r1].ps = simde_mm_cmplt_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 2: cpu->xmm[r1].ps = simde_mm_cmple_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 3: cpu->xmm[r1].ps = simde_mm_cmpunord_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 4: cpu->xmm[r1].ps = simde_mm_cmpneq_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 5: cpu->xmm[r1].ps = simde_mm_cmpnlt_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 6: cpu->xmm[r1].ps = simde_mm_cmpnle_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    case 7: cpu->xmm[r1].ps = simde_mm_cmpord_ss(cpu->xmm[r1].ps, cpu->xmm[r2].ps); break;
    }
}

void common_cmpssXmmE32(CPU* cpu, U32 reg, U32 address, U8 imm) {
    simde__m128 value;
    value.u32[0] = cpu->memory->readd(address);
    int which = imm & 7;
    switch (which) {
    case 0: cpu->xmm[reg].ps = simde_mm_cmpeq_ss(cpu->xmm[reg].ps, value); break;
    case 1: cpu->xmm[reg].ps = simde_mm_cmplt_ss(cpu->xmm[reg].ps, value); break;
    case 2: cpu->xmm[reg].ps = simde_mm_cmple_ss(cpu->xmm[reg].ps, value); break;
    case 3: cpu->xmm[reg].ps = simde_mm_cmpunord_ss(cpu->xmm[reg].ps, value); break;
    case 4: cpu->xmm[reg].ps = simde_mm_cmpneq_ss(cpu->xmm[reg].ps, value); break;
    case 5: cpu->xmm[reg].ps = simde_mm_cmpnlt_ss(cpu->xmm[reg].ps, value); break;
    case 6: cpu->xmm[reg].ps = simde_mm_cmpnle_ss(cpu->xmm[reg].ps, value); break;
    case 7: cpu->xmm[reg].ps = simde_mm_cmpord_ss(cpu->xmm[reg].ps, value); break;
    }
}

// :TODO: not sure about exceptions
void common_comissXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    cpu->fillFlags();
    cpu->flags&=~(AF|OF|SF|CF|PF|ZF);
    const simde__m128& a = cpu->xmm[r1].ps;
    const simde__m128& b = cpu->xmm[r2].ps;
    if (isnan(a.f32[0]) || isnan(b.f32[0])) {
        cpu->flags|=CF|ZF|PF;
    } else if (a.f32[0] == b.f32[0]) {
        cpu->flags|=ZF;
    } else if (a.f32[0] < b.f32[0]) {
        cpu->flags|=CF;
    }
}

void common_comissXmmE32(CPU* cpu, U32 reg, U32 address) {
    cpu->fillFlags();
    cpu->flags&=~(AF|OF|SF|CF|PF|ZF);
    const simde__m128& a = cpu->xmm[reg].ps;
    simde__m128 b;
    b.u32[0] = cpu->memory->readd(address);
    if (isnan(a.f32[0]) || isnan(b.f32[0])) {
        cpu->flags|=CF|ZF|PF;
    } else if (a.f32[0] == b.f32[0]) {
        cpu->flags|=ZF;
    } else if (a.f32[0] < b.f32[0]) {
        cpu->flags|=CF;
    }
}

void common_ucomissXmmXmm(CPU* cpu, U32 r1, U32 r2) {
    common_comissXmmXmm(cpu, r1, r2);
}

void common_ucomissXmmE32(CPU* cpu, U32 reg, U32 address) {
    common_comissXmmE32(cpu, reg, address);
}

void common_stmxcsr(CPU* cpu, U32 reg, U32 address) {
    cpu->memory->writed(address, cpu->mxcsr);
}

bool common_ldmxcsr(CPU* cpu, U32 reg, U32 address) {
    U32 value = cpu->memory->readd(address);
    if (value & 0xffff0000) {
        cpu->prepareException(EXCEPTION_GP, 0);
        return false;
    }
    cpu->mxcsr = value;
    return true;
}

void common_sfence(CPU* cpu) {
    std::atomic_thread_fence(std::memory_order_seq_cst);
}
