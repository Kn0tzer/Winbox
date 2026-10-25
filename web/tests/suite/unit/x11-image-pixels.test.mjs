// web/tests/suite/unit/x11-image-pixels.test.mjs — XSubImage returns a copy of a
// region of an XImage in guest memory, and XAddPixel adds to every pixel in place.
//
// source/x11/x11common.cpp:x11_SubImage and x11_AddPixel were kpanics, so any client
// that cut a sub-image or adjusted an image's pixels stopped the emulator. Both
// handlers work on the XImage bytes in client memory, so the test compiles the
// slices against a stub guest memory and runs them.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/x11/x11common.cpp';
const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const roundUpSlice = extractSourceSlice(source, 'static U32 roundUp(', '// XImage pixels are read', { sourceName });
const helpers = extractSourceSlice(source, 'static bool imagePixelsSupported(', 'static void x11_SubImage(CPU* cpu) {', { sourceName });
const subImageAndAddPixel = extractSourceSlice(source, 'static void x11_SubImage(CPU* cpu) {', '// Pixmap XCreatePixmap(', { sourceName });

test('x11_SubImage and x11_AddPixel do not abort the emulator', () => {
  assert.doesNotMatch(subImageAndAddPixel, /kpanic\(/);
});

test('x11_SubImage copies a region and x11_AddPixel adds in place', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-image-pixels-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
typedef int32_t S32;
typedef uint32_t U32;
typedef uint8_t U8;
#define LSBFirst 0
#define MSBFirst 1
static void klog(const char*) {}
static U8 guest[1 << 16];
static U32 bump = 0x1000;
struct KMemory {
    U32 readd(U32 a) { U32 v; std::memcpy(&v, guest + a, 4); return v; }
    U8 readb(U32 a) { return guest[a]; }
    void writed(U32 a, U32 v) { std::memcpy(guest + a, &v, 4); }
    void writeb(U32 a, U8 v) { guest[a] = v; }
};
struct KProcess {
    U32 alloc(void*, U32 n) { U32 a = bump; bump += (n + 15) & ~15u; return a; }
};
struct KThread { KProcess* process; };
struct CPU {
    KMemory* memory;
    KThread* thread;
    U32 args[11];
    U32 eax;
    U32 peek32(int i) { return args[i]; }
};
#define ARG1 cpu->peek32(1)
#define ARG2 cpu->peek32(2)
#define ARG3 cpu->peek32(3)
#define ARG4 cpu->peek32(4)
#define ARG5 cpu->peek32(5)
#define EAX (cpu->eax)
// the same field offsets as source/x11/ximage.h
struct XImage {
    S32 width, height, xoffset, format;
    U32 data;
    S32 byte_order, bitmap_unit, bitmap_bit_order, bitmap_pad, depth, bytes_per_line, bits_per_pixel;
    U32 red_mask, green_mask, blue_mask;
    static void set(KMemory* m, U32 image, S32 w, S32 h, S32 offset, S32 format, U32 data, S32 bitmapPad, S32 depth, S32 bytesPerLine, S32 bitsPerPixel, U32 r, U32 g, U32 b) {
        m->writed(image + 0, w); m->writed(image + 4, h); m->writed(image + 8, offset); m->writed(image + 12, format);
        m->writed(image + 16, data); m->writed(image + 20, LSBFirst); m->writed(image + 24, 32); m->writed(image + 28, LSBFirst);
        m->writed(image + 32, bitmapPad); m->writed(image + 36, depth); m->writed(image + 40, bytesPerLine); m->writed(image + 44, bitsPerPixel);
        m->writed(image + 48, r); m->writed(image + 52, g); m->writed(image + 56, b);
    }
    static void read(KMemory* m, U32 address, XImage* i) {
        i->width = m->readd(address); i->height = m->readd(address + 4); i->xoffset = m->readd(address + 8); i->format = m->readd(address + 12);
        i->data = m->readd(address + 16); i->byte_order = m->readd(address + 20); i->bitmap_unit = m->readd(address + 24);
        i->bitmap_bit_order = m->readd(address + 28); i->bitmap_pad = m->readd(address + 32); i->depth = m->readd(address + 36);
        i->bytes_per_line = m->readd(address + 40); i->bits_per_pixel = m->readd(address + 44);
        i->red_mask = m->readd(address + 48); i->green_mask = m->readd(address + 52); i->blue_mask = m->readd(address + 56);
    }
};
#define X11_WRITED(c, a, f, v) memory->writed((a) + offsetof(c, f), v)
static KMemory guest_memory;
${roundUpSlice}
${helpers}
${subImageAndAddPixel}
static void setImage(U32 image, U32 w, U32 h, U32 data, S32 order, S32 bpl, S32 bpp, S32 xoffset=0, S32 bitOrder=0) {
    XImage::set(&guest_memory, image, w, h, xoffset, 0, data, 32, bpp, bpl, bpp, 0, 0, 0);
    guest_memory.writed(image + 20, order);
    guest_memory.writed(image + 28, bitOrder);
}
static U32 pixelAt(U32 image, U32 x, U32 y) {
    XImage i;
    XImage::read(&guest_memory, image, &i);
    return readImagePixelAt(&guest_memory, i, x, y);
}
static void putPixel32(U32 data, U32 bpl, U32 x, U32 y, U32 v) { std::memcpy(guest + data + bpl * y + 4 * x, &v, 4); }
int main() {
    KProcess process;
    KThread thread{&process};
    CPU cpu{};
    cpu.memory = &guest_memory;
    cpu.thread = &thread;
    // a 4x3 source; pixel (x, y) is 0x100 * y + x + 1
    {
        std::memset(guest, 0, sizeof(guest));
        bump = 0x1000;
        setImage(100, 4, 3, 200, LSBFirst, 16, 32);
        for (U32 y = 0; y < 3; y++)
            for (U32 x = 0; x < 4; x++) putPixel32(200, 16, x, y, 0x100 * y + x + 1);
        // the 2x2 region at (1, 1) is copied into a new image
        cpu.args[1] = 100; cpu.args[2] = 1; cpu.args[3] = 1; cpu.args[4] = 2; cpu.args[5] = 2;
        x11_SubImage(&cpu);
        U32 result = cpu.eax;
        assert(result != 0);
        XImage r;
        XImage::read(&guest_memory, result, &r);
        assert(r.width == 2 && r.height == 2 && r.bits_per_pixel == 32 && r.byte_order == LSBFirst);
        for (U32 y = 0; y < 2; y++)
            for (U32 x = 0; x < 2; x++) assert(pixelAt(result, x, y) == 0x100 * (y + 1) + (x + 1) + 1);
        // a region that runs past the source gets 0 where the source has no pixel
        cpu.args[2] = 3; cpu.args[3] = 2; cpu.args[4] = 2; cpu.args[5] = 2;
        x11_SubImage(&cpu);
        result = cpu.eax;
        assert(result != 0);
        assert(pixelAt(result, 0, 0) == 0x204);
        assert(pixelAt(result, 1, 0) == 0 && pixelAt(result, 0, 1) == 0 && pixelAt(result, 1, 1) == 0);
    }
    // XPutPixel and XGetPixel share the helpers that XSubImage uses
    {
        std::memset(guest, 0, sizeof(guest));
        bump = 0x1000;
        setImage(100, 2, 1, 200, LSBFirst, 8, 32);
        cpu.args[1] = 100; cpu.args[2] = 1; cpu.args[3] = 0; cpu.args[4] = 0x0badf00d;
        x11_PutPixel(&cpu);
        assert(cpu.eax == 1);
        cpu.args[1] = 100; cpu.args[2] = 1; cpu.args[3] = 0;
        x11_GetPixel(&cpu);
        assert(cpu.eax == 0x0badf00d);
    }
    // a 16-bit MSBFirst source stays MSBFirst in the copy
    {
        std::memset(guest, 0, sizeof(guest));
        bump = 0x1000;
        setImage(100, 3, 1, 300, MSBFirst, 8, 16);
        guest[300] = 0x12; guest[301] = 0x34; guest[302] = 0xab; guest[303] = 0xcd; guest[304] = 0x1f; guest[305] = 0x2e;
        cpu.args[1] = 100; cpu.args[2] = 2; cpu.args[3] = 0; cpu.args[4] = 2; cpu.args[5] = 1;
        x11_SubImage(&cpu);
        U32 result = cpu.eax;
        assert(result != 0);
        XImage r;
        XImage::read(&guest_memory, result, &r);
        assert(r.byte_order == MSBFirst && r.bits_per_pixel == 16);
        assert(guest[r.data] == 0x1f && guest[r.data + 1] == 0x2e);
        assert(guest[r.data + 2] == 0 && guest[r.data + 3] == 0);
    }
    // an empty region is not copied
    {
        std::memset(guest, 0, sizeof(guest));
        bump = 0x1000;
        setImage(100, 4, 1, 200, LSBFirst, 16, 32);
        cpu.args[1] = 100; cpu.args[2] = 0; cpu.args[3] = 0; cpu.args[4] = 0; cpu.args[5] = 1;
        x11_SubImage(&cpu);
        assert(cpu.eax == 0);
    }
    // XAddPixel adds to every 8-bit pixel and wraps at the depth
    {
        std::memset(guest, 0, sizeof(guest));
        bump = 0x1000;
        setImage(100, 3, 1, 800, LSBFirst, 4, 8);
        guest[800] = 0xff; guest[801] = 0x10; guest[802] = 0x00;
        cpu.args[1] = 100; cpu.args[2] = 2;
        x11_AddPixel(&cpu);
        assert(cpu.eax == 1);
        assert(guest[800] == 0x01 && guest[801] == 0x12 && guest[802] == 0x02);
    }
    // XAddPixel wraps 32-bit and 16-bit pixels
    {
        std::memset(guest, 0, sizeof(guest));
        bump = 0x1000;
        setImage(100, 1, 1, 900, LSBFirst, 4, 32);
        putPixel32(900, 4, 0, 0, 0xfffffff8);
        cpu.args[1] = 100; cpu.args[2] = 0x10;
        x11_AddPixel(&cpu);
        assert(cpu.eax == 1);
        assert(pixelAt(100, 0, 0) == 0x8);
        setImage(200, 1, 1, 1000, MSBFirst, 2, 16);
        guest[1000] = 0xff; guest[1001] = 0xf8;
        cpu.args[1] = 200; cpu.args[2] = 0x10;
        x11_AddPixel(&cpu);
        assert(cpu.eax == 1);
        assert(guest[1000] == 0x00 && guest[1001] == 0x08);
    }
    // byte-aligned pixels honor a non-zero xoffset
    {
        std::memset(guest, 0, sizeof(guest));
        bump = 0x1000;
        setImage(100, 1, 1, 1100, LSBFirst, 8, 32, 1);
        putPixel32(1100, 8, 0, 0, 0x12345678);
        putPixel32(1100, 8, 1, 0, 5);
        cpu.args[1] = 100; cpu.args[2] = 2;
        x11_AddPixel(&cpu);
        assert(cpu.eax == 1);
        assert(pixelAt(100, 0, 0) == 7);
        U32 before; std::memcpy(&before, guest + 1100, 4); assert(before == 0x12345678);
    }
    // packed 1-bit pixels use bitmap bit order, preserve neighbors, and copy through XSubImage
    {
        std::memset(guest, 0, sizeof(guest));
        bump = 0x1000;
        setImage(100, 3, 1, 1200, LSBFirst, 4, 1, 1);
        guest[1200] = 0x81; // source pixels begin at bit one
        cpu.args[1] = 100; cpu.args[2] = 0; cpu.args[3] = 0; cpu.args[4] = 1;
        x11_PutPixel(&cpu); assert(cpu.eax == 1 && guest[1200] == 0x83);
        cpu.args[1] = 100; cpu.args[2] = 0; cpu.args[3] = 0;
        x11_GetPixel(&cpu); assert(cpu.eax == 1);
        cpu.args[1] = 100; cpu.args[2] = 0; cpu.args[3] = 0; cpu.args[4] = 3; cpu.args[5] = 1;
        x11_SubImage(&cpu);
        U32 result = cpu.eax; assert(result != 0 && pixelAt(result, 0, 0) == 1 && pixelAt(result, 1, 0) == 0 && pixelAt(result, 2, 0) == 0);
        assert(guest[1200] == 0x83); // bits outside the target pixel were preserved
    }
    // packed 1-bit pixels in MSBFirst bitmap order put the first pixel in the top bit
    {
        std::memset(guest, 0, sizeof(guest));
        bump = 0x1000;
        setImage(100, 3, 1, 1300, LSBFirst, 4, 1, 0, MSBFirst);
        guest[1300] = 0x40; // second pixel set, in MSB-first bit order
        cpu.args[1] = 100; cpu.args[2] = 1; cpu.args[3] = 0;
        x11_GetPixel(&cpu); assert(cpu.eax == 1);
        cpu.args[1] = 100; cpu.args[2] = 0; cpu.args[3] = 0; cpu.args[4] = 1;
        x11_PutPixel(&cpu); assert(cpu.eax == 1 && guest[1300] == 0xc0);
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
