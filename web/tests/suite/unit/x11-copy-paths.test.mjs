// web/tests/suite/unit/x11-copy-paths.test.mjs — the legacy X11 copy paths
// (XPutImage and XCopyArea) honour the GC: a destination that starts before
// the drawable is cut off instead of written before its buffer, the GC clip
// rectangles limit what is written, the GC logical function is applied, and a
// copy inside one drawable reads each source row before a later row overwrites it.
//
// source/x11/xdrawable.cpp used to ignore the clip in copyImageData and copy,
// dropped every logical function but GXcopy and GXxor, and used a memcpy that
// corrupted an overlapping copy. The copy functions and the pixel writer are
// compiled against a small stub of the drawable, GC, memory and server, and run.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/x11/xdrawable.cpp';
const source = readFileSync(new URL('../../../../source/x11/xdrawable.cpp', import.meta.url), 'utf8');
const copyBlock = extractSourceSlice(source, 'static void logClipMaskOnce(const XGC* gc);', 'static U32 combinePixel(', { sourceName });
const pixelBlock = extractSourceSlice(source, 'static U32 combinePixel(', 'int XDrawable::drawLine(', { sourceName });

test('the copy block has no abort and no silent clip', () => {
  assert.doesNotMatch(copyBlock, /kpanic/);
  assert.doesNotMatch(copyBlock, /\/\/klog\("XDrawable::copy/);
});

test('copyImageData and copy honour the GC clip, function and bounds', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-copy-paths-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <algorithm>
typedef int32_t S32;
typedef uint32_t U32;
typedef uint8_t U8;
typedef int16_t S16;
typedef uint16_t U16;
enum { Success = 0, BadValue = 2, BadMatch = 8 };
#define GXclear 0x0
#define GXand 0x1
#define GXandReverse 0x2
#define GXcopy 0x3
#define GXandInverted 0x4
#define GXnoop 0x5
#define GXxor 0x6
#define GXor 0x7
#define GXnor 0x8
#define GXequiv 0x9
#define GXinvert 0xa
#define GXorReverse 0xb
#define GXcopyInverted 0xc
#define GXorInverted 0xd
#define GXnand 0xe
#define GXset 0xf
#define FillSolid 0
#define FillTiled 1
#define EvenOddRule 0
#define WindingRule 1
static int warnings = 0;
#define kwarn_fmt(...) (++warnings, 0)
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) (void)0
struct XRectangle { S16 x, y; U16 width, height; };
// guest memory is a flat byte array; a guest address is an offset into it
static U8 guest[1024];
struct KMemory {
    bool canRead(U32 a, U32 n) { return (size_t)a + n <= sizeof(guest); }
    void memcpy(U8* dst, U32 src, U32 n) { std::memcpy(dst, guest + src, n); }
    U32 readd(U32 a) { U32 v; std::memcpy(&v, guest + a, 4); return v; }
};
struct KThread { KMemory* memory; };
struct XVisual { int bits_per_rgb; };
using VisualPtr = std::shared_ptr<XVisual>;
struct XGCValues {
    S32 function = GXcopy; U32 foreground = 0; S32 fill_style = FillSolid; U32 tile = 0;
    S32 ts_x_origin = 0, ts_y_origin = 0, clip_x_origin = 0, clip_y_origin = 0;
    S32 fill_rule = EvenOddRule;
    U32 clip_mask = 0;
};
class XDrawable;
class XGC {
public:
    XGCValues values;
    std::vector<XRectangle> clip_rects;
};
class XDrawable {
public:
    XDrawable(U32 width, U32 height, const VisualPtr& v) : visual(v), w(width), h(height) {
        bytes_per_line = w * 4;
        data = new U8[bytes_per_line * h]();
    }
    ~XDrawable() { delete[] data; }
    void plotPixel(const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 color);
    int copyImageData(KThread* thread, const std::shared_ptr<XGC>& gc, U32 data, U32 bytes_per_line, S32 bits_per_pixel, S32 src_x, S32 src_y, S32 dst_x, S32 dst_y, U32 width, U32 height);
    int copy(KThread* thread, const std::shared_ptr<XGC>& gc, const std::shared_ptr<XDrawable>& srcDrawable, S32 srcX, S32 srcY, U32 width, U32 height, S32 dstX, S32 dstY);
    void setDirty() {}
    U32 pixel(U32 x, U32 y) const { U32 v; std::memcpy(&v, data + bytes_per_line * y + x * 4, 4); return v; }
    void setPixel(U32 x, U32 y, U32 v) { std::memcpy(data + bytes_per_line * y + x * 4, &v, 4); }
    VisualPtr visual;
    U8* data;
    U32 bytes_per_line;
    U32 w, h;
};
using XDrawablePtr = std::shared_ptr<XDrawable>;
using XGCPtr = std::shared_ptr<XGC>;
${pixelBlock}
${copyBlock}
static VisualPtr v32 = std::make_shared<XVisual>(XVisual{32});
// the image pixel at image (x, y) in guest memory, rows 16 bytes apart
static U32 img(U32 x, U32 y) { return 0x100 * y + x + 1; }
static void putImage4x4() {
    for (U32 y = 0; y < 4; y++) for (U32 x = 0; x < 4; x++) {
        U32 v = img(x, y);
        std::memcpy(guest + y * 16 + x * 4, &v, 4);
    }
}
int main() {
    KMemory mem;
    KThread t{&mem};
    // a 4x4 image put at a negative destination writes only its visible part; the rest of the image is not written
    {
        putImage4x4();
        XDrawable d(4, 4, v32);
        auto gc = std::make_shared<XGC>();
        assert(d.copyImageData(&t, gc, 0, 16, 32, 0, 0, -2, -1, 4, 4) == Success);
        for (U32 y = 0; y < 4; y++)
            for (U32 x = 0; x < 4; x++) {
                if (x < 2 && y < 3) assert(d.pixel(x, y) == img(x + 2, y + 1));
                else assert(d.pixel(x, y) == 0);
            }
    }
    // the GC clip limits a put image to the clip rectangle
    {
        putImage4x4();
        XDrawable d(4, 4, v32);
        auto gc = std::make_shared<XGC>();
        gc->clip_rects.push_back(XRectangle{1, 1, 2, 2});
        d.copyImageData(&t, gc, 0, 16, 32, 0, 0, 0, 0, 4, 4);
        for (U32 y = 0; y < 4; y++)
            for (U32 x = 0; x < 4; x++) {
                bool inside = x >= 1 && x <= 2 && y >= 1 && y <= 2;
                assert(d.pixel(x, y) == (inside ? img(x, y) : 0u));
            }
    }
    // a put image with GXand combines with the drawable pixel instead of being dropped
    {
        putImage4x4();
        XDrawable d(4, 4, v32);
        for (U32 y = 0; y < 4; y++) for (U32 x = 0; x < 4; x++) d.setPixel(x, y, 0xff);
        auto gc = std::make_shared<XGC>();
        gc->values.function = GXand;
        d.copyImageData(&t, gc, 0, 16, 32, 0, 0, 0, 0, 4, 4);
        for (U32 y = 0; y < 4; y++)
            for (U32 x = 0; x < 4; x++) assert(d.pixel(x, y) == (0xffu & img(x, y)));
    }
    // a copy inside one drawable moved down reads every source row before a later row overwrites it
    {
        auto d = std::make_shared<XDrawable>(4, 4, v32);
        for (U32 y = 0; y < 4; y++) for (U32 x = 0; x < 4; x++) d->setPixel(x, y, img(x, y));
        auto gc = std::make_shared<XGC>();
        assert(d->copy(&t, gc, d, 0, 0, 4, 3, 0, 1) == Success);
        assert(d->pixel(0, 0) == img(0, 0));
        for (U32 x = 0; x < 4; x++) {
            assert(d->pixel(x, 1) == img(x, 0));
            assert(d->pixel(x, 2) == img(x, 1));
            assert(d->pixel(x, 3) == img(x, 2));
        }
    }
    // a copy between drawables writes only the GC clip rectangle
    {
        auto src = std::make_shared<XDrawable>(4, 4, v32);
        for (U32 y = 0; y < 4; y++) for (U32 x = 0; x < 4; x++) src->setPixel(x, y, img(x, y));
        XDrawable dst(4, 4, v32);
        auto gc = std::make_shared<XGC>();
        gc->clip_rects.push_back(XRectangle{0, 0, 2, 2});
        dst.copy(&t, gc, src, 0, 0, 4, 4, 0, 0);
        for (U32 y = 0; y < 4; y++)
            for (U32 x = 0; x < 4; x++) {
                bool inside = x < 2 && y < 2;
                assert(dst.pixel(x, y) == (inside ? img(x, y) : 0u));
            }
    }
    // a clip_mask pixmap is not applied, and the copy logs that once rather than dropping the clip silently
    {
        auto src = std::make_shared<XDrawable>(4, 4, v32);
        XDrawable dst(4, 4, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.clip_mask = 1;
        int before = warnings;
        dst.copy(&t, gc, src, 0, 0, 4, 4, 0, 0);
        assert(warnings == before + 1);
        dst.copy(&t, gc, src, 0, 0, 4, 4, 0, 0);
        assert(warnings == before + 1);
    }
    // a copy with no clip and GXcopy moves a sub-rectangle, and a destination past the edge is cut off
    {
        auto src = std::make_shared<XDrawable>(4, 4, v32);
        for (U32 y = 0; y < 4; y++) for (U32 x = 0; x < 4; x++) src->setPixel(x, y, img(x, y));
        XDrawable dst(4, 4, v32);
        auto gc = std::make_shared<XGC>();
        dst.copy(&t, gc, src, 1, 1, 2, 2, 2, 2);
        assert(dst.pixel(2, 2) == img(1, 1) && dst.pixel(3, 3) == img(2, 2));
        assert(dst.pixel(2, 3) == img(1, 2) && dst.pixel(3, 2) == img(2, 1));
        assert(dst.pixel(1, 1) == 0 && dst.pixel(0, 0) == 0);
        dst.copy(&t, gc, src, 0, 0, 4, 4, 3, 3);
        assert(dst.pixel(3, 3) == img(0, 0));
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
