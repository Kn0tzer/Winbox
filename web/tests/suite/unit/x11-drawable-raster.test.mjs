// web/tests/suite/unit/x11-drawable-raster.test.mjs — the legacy X11 drawable
// draws what the GC asks for: lines in any direction, the GC foreground, the GC
// logical function, clip rectangles, tile fill, and nothing outside the drawable.
//
// source/x11/xdrawable.cpp used to draw only horizontal and vertical lines, used
// a hard-coded colour for horizontal lines, aborted on tile fill, and ignored
// clip rectangles. The drawing block is compiled against a small stub of the
// drawable, GC and server, and run, so the test checks the real code.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/x11/xdrawable.cpp';
const source = readFileSync(new URL('../../../../source/x11/xdrawable.cpp', import.meta.url), 'utf8');
const block = extractSourceSlice(source, 'static U32 combinePixel(', 'int XDrawable::drawRectangle(', { sourceName });

test('the drawing block has no abort and no hard-coded colour', () => {
  assert.doesNotMatch(block, /kpanic/);
  assert.doesNotMatch(block, /0xff00/);
});

test('drawable draws lines, fills and tiles with the GC', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-drawable-raster-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
typedef int32_t S32;
typedef uint32_t U32;
typedef uint8_t U8;
typedef int16_t S16;
typedef uint16_t U16;
enum { Success = 0, BadMatch = 8 };
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
static int warnings = 0;
#define kwarn_fmt(...) (++warnings, 0)
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) (void)0
struct XRectangle { S16 x, y; U16 width, height; };
struct KThread {};
struct XVisual { int bits_per_rgb; };
using VisualPtr = std::shared_ptr<XVisual>;
struct XGCValues {
    S32 function = GXcopy; U32 foreground = 0; S32 fill_style = FillSolid; U32 tile = 0;
    S32 ts_x_origin = 0, ts_y_origin = 0, clip_x_origin = 0, clip_y_origin = 0;
};
class XDrawable;
using XDrawablePtr = std::shared_ptr<XDrawable>;
class XGC {
public:
    XGCValues values;
    std::vector<XRectangle> clip_rects;
};
using XGCPtr = std::shared_ptr<XGC>;
class XDrawable {
public:
    XDrawable(U32 width, U32 height, const VisualPtr& v) : visual(v), w(width), h(height) {
        bytes_per_line = w * 4;
        data = new U8[bytes_per_line * h]();
    }
    ~XDrawable() { delete[] data; }
    void plotPixel(const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 color);
    int drawLine(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x1, S32 y1, S32 x2, S32 y2);
    int fillRectangle(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 width, U32 height);
    void setDirty() {}
    U32 pixel(U32 x, U32 y) const { U32 v; std::memcpy(&v, data + bytes_per_line * y + x * 4, 4); return v; }
    void setPixel(U32 x, U32 y, U32 v) { std::memcpy(data + bytes_per_line * y + x * 4, &v, 4); }
    VisualPtr visual;
    U8* data;
    U32 bytes_per_line;
    U32 w, h;
};
struct XServer {
    XDrawablePtr tile;
    static XServer* getServer(bool = false) { static XServer s; return &s; }
    XDrawablePtr getDrawable(U32) { return tile; }
};
${block}
static VisualPtr v32 = std::make_shared<XVisual>(XVisual{32});
static VisualPtr v8 = std::make_shared<XVisual>(XVisual{8});
int main() {
    KThread t;
    // diagonal line: Bresenham draws both endpoints and the steps between them
    {
        XDrawable d(8, 8, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 0x12345678;
        d.drawLine(&t, gc, 0, 0, 3, 3);
        for (U32 i = 0; i < 4; i++) assert(d.pixel(i, i) == 0x12345678);
        assert(d.pixel(1, 0) == 0 && d.pixel(3, 0) == 0);
    }
    // horizontal line uses the GC foreground, not a fixed colour; endpoints inclusive
    {
        XDrawable d(8, 8, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 0xaabbccdd;
        d.drawLine(&t, gc, 1, 2, 4, 2);
        for (U32 x = 1; x <= 4; x++) assert(d.pixel(x, 2) == 0xaabbccdd);
        assert(d.pixel(0, 2) == 0 && d.pixel(5, 2) == 0);
    }
    // a line that runs off the drawable writes only inside it and does not crash
    {
        XDrawable d(4, 4, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 1;
        d.drawLine(&t, gc, -5, -5, 20, 20);
        for (U32 i = 0; i < 4; i++) assert(d.pixel(i, i) == 1);
    }
    // clip rectangles: only pixels inside one rectangle are drawn, offset by the clip origin
    {
        XDrawable d(8, 8, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 7;
        gc->clip_rects.push_back(XRectangle{2, 2, 2, 2});
        gc->values.clip_x_origin = 1;   // clip rectangle now covers x 3..4, y 2..3
        d.fillRectangle(&t, gc, 0, 0, 8, 8);
        for (U32 y = 0; y < 8; y++)
            for (U32 x = 0; x < 8; x++) {
                bool inside = x >= 3 && x <= 4 && y >= 2 && y <= 3;
                assert(d.pixel(x, y) == (inside ? 7u : 0u));
            }
    }
    // GXxor combines with the drawable pixel
    {
        XDrawable d(4, 4, v32);
        for (U32 y = 0; y < 4; y++) for (U32 x = 0; x < 4; x++) d.setPixel(x, y, 0xff);
        auto gc = std::make_shared<XGC>();
        gc->values.function = GXxor;
        gc->values.foreground = 0x0f;
        d.fillRectangle(&t, gc, 0, 0, 4, 4);
        assert(d.pixel(2, 2) == 0xf0);
    }
    // a rectangle starting at a negative x fills only its visible columns
    {
        XDrawable d(4, 4, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 9;
        d.fillRectangle(&t, gc, -2, 0, 5, 1);
        for (U32 x = 0; x < 3; x++) assert(d.pixel(x, 0) == 9);  // columns -2..2 are the rectangle; 0..2 are visible
        assert(d.pixel(3, 0) == 0 && d.pixel(0, 1) == 0);
    }
    // tiled fill repeats a 2x2 tile in both directions from the tile origin
    {
        auto tile = std::make_shared<XDrawable>(2, 2, v32);
        tile->setPixel(0, 0, 1); tile->setPixel(1, 0, 2); tile->setPixel(0, 1, 3); tile->setPixel(1, 1, 4);
        XServer::getServer()->tile = tile;
        XDrawable d(4, 4, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.fill_style = FillTiled;
        gc->values.tile = 7;
        d.fillRectangle(&t, gc, 0, 0, 4, 4);
        for (U32 y = 0; y < 4; y++)
            for (U32 x = 0; x < 4; x++)
                assert(d.pixel(x, y) == 1 + (y % 2) * 2 + (x % 2));
        XServer::getServer()->tile = nullptr;
    }
    // 8-bit drawables are logged, not drawn and not aborted
    {
        XDrawable d(4, 4, v8);
        auto gc = std::make_shared<XGC>();
        d.drawLine(&t, gc, 0, 0, 3, 3);
        assert(warnings >= 1);
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
