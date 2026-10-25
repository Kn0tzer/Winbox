// web/tests/suite/unit/x11-copyplane.test.mjs — XCopyPlane paints the GC foreground
// where a source bit is set and the GC background where it is clear.
//
// source/x11/x11common.cpp:x11_CopyPlane and XDrawable::copyPlane were a kpanic, so
// any client that used a depth-1 mask or bitmap stopped the emulator. The drawing
// method is compiled against a small drawable stub and run, so the test checks the
// real code.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const drawableSource = readFileSync(new URL('../../../../source/x11/xdrawable.cpp', import.meta.url), 'utf8');
const copyPlane = extractSourceSlice(drawableSource, 'int XDrawable::copyPlane(', '// The 16 X11 GC logical operations', { sourceName: 'source/x11/xdrawable.cpp' });
const x11Source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const copyPlaneHandler = extractSourceSlice(x11Source, 'static void x11_CopyPlane(CPU* cpu) {', '// GC XCreateGC(', { sourceName: 'source/x11/x11common.cpp' });

test('XCopyPlane does not abort the emulator', () => {
  assert.doesNotMatch(copyPlane, /kpanic/);
  assert.doesNotMatch(copyPlaneHandler, /kpanic\(/);
});

test('XCopyPlane paints the foreground for set bits and the background for clear bits', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-copyplane-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
typedef int32_t S32;
typedef uint32_t U32;
typedef uint8_t U8;
enum { Success = 0 };
struct KThread {};
struct XGCValues { U32 foreground = 0; U32 background = 0; };
struct XGC { XGCValues values; };
class XDrawable;
class XDrawable {
public:
    XDrawable(U32 width, U32 height) : w(width), h(height), bytes_per_line(width * 4), data(new U8[bytes_per_line * height]()) {}
    ~XDrawable() { delete[] data; }
    XDrawable(const XDrawable&) = delete;
    int copyPlane(KThread* thread, const std::shared_ptr<XGC>& gc, const std::shared_ptr<XDrawable>& srcDrawable, S32 srcX, S32 srcY, U32 width, U32 height, S32 dstX, S32 dstY, U32 bitPlane);
    // the clip and GC function are checked by the drawing tests; this stub writes the colour
    void plotPixel(const std::shared_ptr<XGC>&, S32 x, S32 y, U32 color) {
        if (x < 0 || y < 0 || x >= (S32)w || y >= (S32)h) return;
        std::memcpy(data + bytes_per_line * y + x * 4, &color, 4);
    }
    void setDirty() {}
    U32 pixel(U32 x, U32 y) const { U32 v; std::memcpy(&v, data + bytes_per_line * y + x * 4, 4); return v; }
    void setPixel(U32 x, U32 y, U32 v) { std::memcpy(data + bytes_per_line * y + x * 4, &v, 4); }
    U32 w, h;
    U32 bytes_per_line;
    U8* data;
};
${copyPlane}
static const U32 FG = 0xaaaaaaaa, BG = 0x55555555;
static std::shared_ptr<XGC> gcWithColours() {
    auto gc = std::make_shared<XGC>();
    gc->values.foreground = FG;
    gc->values.background = BG;
    return gc;
}
int main() {
    KThread t;
    // bit 0 of each source pixel picks the colour: even columns are set, odd columns are clear
    {
        auto src = std::make_shared<XDrawable>(4, 2);
        auto dst = std::make_shared<XDrawable>(4, 2);
        for (U32 y = 0; y < 2; y++)
            for (U32 x = 0; x < 4; x++) src->setPixel(x, y, x % 2 == 0 ? 1 : 0);
        assert(dst->copyPlane(&t, gcWithColours(), src, 0, 0, 4, 2, 0, 0, 0) == Success);
        for (U32 y = 0; y < 2; y++)
            for (U32 x = 0; x < 4; x++) assert(dst->pixel(x, y) == (x % 2 == 0 ? FG : BG));
    }
    // the plane argument selects the bit: bit 1 is set in the first two columns only
    {
        auto src = std::make_shared<XDrawable>(4, 1);
        auto dst = std::make_shared<XDrawable>(4, 1);
        for (U32 x = 0; x < 4; x++) src->setPixel(x, 0, x < 2 ? 2 : 1);
        dst->copyPlane(&t, gcWithColours(), src, 0, 0, 4, 1, 0, 0, 1);
        assert(dst->pixel(0, 0) == FG && dst->pixel(1, 0) == FG);
        assert(dst->pixel(2, 0) == BG && dst->pixel(3, 0) == BG);
    }
    // the source and destination offsets are honoured and pixels outside the destination are left alone
    {
        auto src = std::make_shared<XDrawable>(4, 3);
        auto dst = std::make_shared<XDrawable>(4, 2);
        src->setPixel(1, 1, 1);
        src->setPixel(2, 1, 0);
        dst->setPixel(0, 0, 0x1234);
        dst->copyPlane(&t, gcWithColours(), src, 1, 1, 2, 1, 2, 0, 0);
        assert(dst->pixel(2, 0) == FG && dst->pixel(3, 0) == BG);
        assert(dst->pixel(0, 0) == 0x1234 && dst->pixel(1, 0) == 0);
        assert(dst->pixel(0, 1) == 0 && dst->pixel(3, 1) == 0);
    }
    // a copy within one drawable reads the source bits before it writes, so an overlap is correct
    {
        auto d = std::make_shared<XDrawable>(4, 1);
        d->setPixel(0, 0, 1);
        d->setPixel(1, 0, 0);
        d->copyPlane(&t, gcWithColours(), d, 0, 0, 2, 1, 1, 0, 0);
        assert(d->pixel(1, 0) == FG && d->pixel(2, 0) == BG);
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
