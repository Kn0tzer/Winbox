// web/tests/suite/unit/x11-putpixel-writes.test.mjs — XPutPixel writes the pixel
// into the XImage in client memory, byte order and depth included.
//
// source/x11/x11common.cpp:x11_PutPixel used to log once and drop every pixel.
// XPutPixel edits the image, not the server drawable, so the handler reads the
// XImage header from guest memory and writes the pixel bytes there. The slice is
// compiled against a stub guest memory and CPU and run, so the test checks the
// real body.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/x11/x11common.cpp';
const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
// the slice starts at the image helpers that x11_GetPixel and x11_PutPixel share
const putPixel = extractSourceSlice(source, 'static bool imagePixelsSupported(', 'static void x11_SubImage(CPU* cpu) {', { sourceName });

test('x11_PutPixel writes the pixel and does not drop it', () => {
  assert.doesNotMatch(putPixel, /kpanic\(/);
  assert.doesNotMatch(putPixel, /pixels are dropped/);
});

test('x11_PutPixel writes the pixel bytes into the image', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-putpixel-writes-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
typedef int32_t S32;
typedef uint32_t U32;
typedef uint8_t U8;
#define LSBFirst 0
#define MSBFirst 1
static void klog(const char*) {}
static U8 guest[1024];
struct KMemory {
    U32 readd(U32 a) { U32 v; std::memcpy(&v, guest + a, 4); return v; }
    U8 readb(U32 a) { return guest[a]; }
    void writeb(U32 a, U8 v) { guest[a] = v; }
};
struct CPU {
    KMemory* memory;
    U32 args[11];
    U32 eax;
    U32 peek32(int i) { return args[i]; }
};
#define ARG1 cpu->peek32(1)
#define ARG2 cpu->peek32(2)
#define ARG3 cpu->peek32(3)
#define ARG4 cpu->peek32(4)
#define EAX (cpu->eax)
struct XImage {
    S32 width, height, xoffset;
    U32 data;
    S32 byte_order, bitmap_bit_order, bytes_per_line, bits_per_pixel;
    static void read(KMemory* memory, U32 address, XImage* image) {
        image->width = memory->readd(address);
        image->height = memory->readd(address + 4);
        image->xoffset = memory->readd(address + 8);
        image->data = memory->readd(address + 16);
        image->byte_order = memory->readd(address + 20);
        image->bitmap_bit_order = memory->readd(address + 28);
        image->bytes_per_line = memory->readd(address + 40);
        image->bits_per_pixel = memory->readd(address + 44);
    }
};
${putPixel}
static void word(U32 a, U32 v) { std::memcpy(guest + a, &v, 4); }
// an XImage header at address with the fields x11_PutPixel reads
static void header(U32 address, U32 width, U32 height, U32 xoffset, U32 data, U32 order, U32 bpl, U32 bpp, U32 bitOrder = 0) {
    word(address, width); word(address + 4, height); word(address + 8, xoffset);
    word(address + 16, data); word(address + 20, order); word(address + 28, bitOrder); word(address + 40, bpl); word(address + 44, bpp);
}
static KMemory mem;
static void put(U32 image, U32 x, U32 y, U32 pixel, CPU* cpu) {
    cpu->args[1] = image; cpu->args[2] = x; cpu->args[3] = y; cpu->args[4] = pixel;
    x11_PutPixel(cpu);
}
int main() {
    CPU cpu{};
    cpu.memory = &mem;
    // 32-bit LSBFirst: the least significant byte is stored first at the pixel's offset
    {
        std::memset(guest, 0, sizeof(guest));
        header(100, 4, 2, 0, 200, LSBFirst, 16, 32);
        put(100, 1, 1, 0x11223344, &cpu);
        assert(cpu.eax == 1);
        assert(guest[216 + 4] == 0x44 && guest[216 + 5] == 0x33 && guest[216 + 6] == 0x22 && guest[216 + 7] == 0x11);
        assert(guest[216] == 0 && guest[216 + 8] == 0 && guest[200] == 0);
    }
    // every pixel of a 4x2 image is written at its own offset
    {
        std::memset(guest, 0, sizeof(guest));
        header(100, 4, 2, 0, 200, LSBFirst, 16, 32);
        for (U32 y = 0; y < 2; y++)
            for (U32 x = 0; x < 4; x++) put(100, x, y, 0x100 * y + x + 1, &cpu);
        for (U32 y = 0; y < 2; y++)
            for (U32 x = 0; x < 4; x++) {
                U32 v;
                std::memcpy(&v, guest + 200 + 16 * y + 4 * x, 4);
                assert(v == 0x100 * y + x + 1);
            }
    }
    // a pixel outside the image is ignored and reported as not put
    {
        std::memset(guest, 0x77, sizeof(guest));
        header(100, 4, 2, 0, 200, LSBFirst, 16, 32);
        put(100, 4, 0, 0xffffffff, &cpu);
        assert(cpu.eax == 0);
        put(100, 0, 2, 0xffffffff, &cpu);
        assert(cpu.eax == 0);
        assert(guest[200 + 16 * 1] == 0x77 && guest[200 + 16 * 2 + 0] == 0x77);
    }
    // 8-bit depth writes one byte
    {
        std::memset(guest, 0, sizeof(guest));
        header(100, 4, 2, 0, 300, LSBFirst, 4, 8);
        put(100, 2, 1, 0xab, &cpu);
        assert(cpu.eax == 1);
        assert(guest[304 + 2] == 0xab && guest[304 + 1] == 0);
    }
    // MSBFirst 16-bit stores the most significant byte first
    {
        std::memset(guest, 0, sizeof(guest));
        header(100, 4, 2, 0, 400, MSBFirst, 8, 16);
        put(100, 1, 0, 0x1234, &cpu);
        assert(guest[400 + 2] == 0x12 && guest[400 + 3] == 0x34);
    }
    // a non-zero xoffset shifts the pixel inside its scanline
    {
        std::memset(guest, 0, sizeof(guest));
        header(100, 4, 2, 1, 200, LSBFirst, 16, 32);
        put(100, 0, 0, 0xffffffff, &cpu);
        assert(cpu.eax == 1);
        U32 shifted;
        std::memcpy(&shifted, guest + 204, 4);
        assert(shifted == 0xffffffff);
        assert(guest[200] == 0 && guest[201] == 0 && guest[202] == 0 && guest[203] == 0);
    }
    // packed pixels below a byte follow bitmap_bit_order and keep their neighbours
    {
        std::memset(guest, 0, sizeof(guest));
        header(100, 8, 1, 0, 700, LSBFirst, 1, 1, MSBFirst);
        put(100, 0, 0, 1, &cpu);
        assert(cpu.eax == 1 && guest[700] == 0x80);
        put(100, 1, 0, 1, &cpu);
        assert(guest[700] == 0xc0);
        cpu.args[1] = 100; cpu.args[2] = 1; cpu.args[3] = 0;
        x11_GetPixel(&cpu);
        assert(cpu.eax == 1);
        cpu.args[2] = 2;
        x11_GetPixel(&cpu);
        assert(cpu.eax == 0);
        std::memset(guest, 0, sizeof(guest));
        header(100, 2, 1, 0, 700, LSBFirst, 1, 4, LSBFirst);
        put(100, 1, 0, 0xa, &cpu);
        assert(guest[700] == 0xa0);
    }
    // XGetPixel reads back what XPutPixel wrote, in both byte orders, and reads 0 outside the image
    {
        std::memset(guest, 0, sizeof(guest));
        header(100, 4, 2, 0, 200, LSBFirst, 16, 32);
        put(100, 3, 1, 0x11223344, &cpu);
        cpu.args[1] = 100; cpu.args[2] = 3; cpu.args[3] = 1;
        x11_GetPixel(&cpu);
        assert(cpu.eax == 0x11223344);
        cpu.args[2] = 4;
        x11_GetPixel(&cpu);
        assert(cpu.eax == 0);
        header(500, 2, 1, 0, 600, MSBFirst, 4, 16);
        put(500, 1, 0, 0x1234, &cpu);
        cpu.args[1] = 500; cpu.args[2] = 1; cpu.args[3] = 0;
        x11_GetPixel(&cpu);
        assert(cpu.eax == 0x1234);
    }
    return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
