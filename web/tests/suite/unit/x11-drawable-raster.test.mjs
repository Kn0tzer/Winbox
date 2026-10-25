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
const block = extractSourceSlice(source, 'static U32 readStoredPixel(', 'int XDrawable::drawRectangle(', { sourceName });

test('the drawing block has no abort and no hard-coded colour', () => {
  assert.doesNotMatch(block, /kpanic/);
  assert.doesNotMatch(block, /0xff00/);
});

test('no raster method bails out on a non-32-bit visual', () => {
  assert.doesNotMatch(block, /bits_per_rgb != 32/);
  assert.doesNotMatch(block, /not supported|not handled/);
});

test('XSetClipMask validates a depth-one pixmap and replaces clip rectangles', () => {
  const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const handler = source.match(/static void x11_SetClipMask\(CPU\* cpu\) \{[\s\S]*?\n}/)?.[0];
  assert.ok(handler);
  assert.match(handler, /getPixmap\(ARG3\)/);
  assert.match(handler, /mask->getDepth\(\) != 1\) \{ EAX = BadMatch; return; \}/);
  assert.match(handler, /gc->clip_rects\.clear\(\)/);
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
#include <algorithm>
#include <cmath>
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
#define LineSolid 0
#define LineOnOffDash 1
#define LineDoubleDash 2
#define ArcChord 0
#define ArcPieSlice 1
#define EvenOddRule 0
#define WindingRule 1
static int warnings = 0;
#define kwarn_fmt(...) (++warnings, 0)
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) (void)0
struct XRectangle { S16 x, y; U16 width, height; };
struct KThread {};
struct XVisual { int bits_per_rgb; };
using VisualPtr = std::shared_ptr<XVisual>;
struct XGCValues {
    S32 function = GXcopy; U32 foreground = 0, background = 0; S32 fill_style = FillSolid; U32 tile = 0;
    S32 line_style = 0, dash_offset = 0; std::vector<U8> dash_list{4};
    S32 ts_x_origin = 0, ts_y_origin = 0, clip_x_origin = 0, clip_y_origin = 0;
    S32 fill_rule = EvenOddRule;
    U32 clip_mask = 0;
    S32 arc_mode = ArcPieSlice;
};
class XDrawable;
using XDrawablePtr = std::shared_ptr<XDrawable>;
class XGC {
public:
    XGCValues values;
    std::vector<XRectangle> clip_rects;
    std::vector<U8> dash_list{4};
};
using XGCPtr = std::shared_ptr<XGC>;
class XDrawable {
public:
    XDrawable(U32 width, U32 height, const VisualPtr& v, U32 d=32) : visual(v), w(width), h(height), depth(d) {
        // mirrors XDrawable::calculateBytesPerLine for the stub visual
        bytes_per_line = (w * (U32)v->bits_per_rgb / 8 + 3) / 4 * 4;
        data = new U8[bytes_per_line * h]();
    }
    ~XDrawable() { delete[] data; }
    void plotPixel(const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 color);
    int drawLine(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x1, S32 y1, S32 x2, S32 y2);
    int drawLines(KThread* thread, const std::shared_ptr<XGC>& gc, const std::vector<S32>& points, bool relative);
    int fillRectangles(KThread* thread, const std::shared_ptr<XGC>& gc, const std::vector<S32>& rects);
    int fillPolygon(KThread* thread, const std::shared_ptr<XGC>& gc, const std::vector<S32>& points, bool relative);
    int fillRectangle(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 width, U32 height);
    int fillArc(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 width, U32 height, S32 angle1, S32 angle2);
    int drawArc(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 width, U32 height, S32 angle1, S32 angle2);
    static XDrawablePtr fillTile(const std::shared_ptr<XGC>& gc);
    static U32 fillColor(const std::shared_ptr<XGC>& gc, const XDrawablePtr& tile, S32 x, S32 y);
    void setDirty() {}
    U32 pixel(U32 x, U32 y) const { U32 v; std::memcpy(&v, data + bytes_per_line * y + x * 4, 4); return v; }
    void setPixel(U32 x, U32 y, U32 v) { std::memcpy(data + bytes_per_line * y + x * 4, &v, 4); }
    U32 width() const { return w; } U32 height() const { return h; }
    U32 getDepth() const { return depth; } U32 getBytesPerLine() const { return bytes_per_line; }
    U8* getData() const { return data; }
    VisualPtr visual;
    U8* data;
    U32 bytes_per_line;
    U32 w, h, depth;
};
using XPixmapPtr = std::shared_ptr<XDrawable>;
struct XServer {
    XDrawablePtr tile;
    XPixmapPtr mask;
    static XServer* getServer(bool = false) { static XServer s; return &s; }
    XDrawablePtr getDrawable(U32) { return tile; }
    XPixmapPtr getPixmap(U32) { return mask; }
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
    // clip-mask pixmaps test one bit per drawable pixel, offset by clip origin;
    // unset bits and coordinates outside the mask leave destination pixels unchanged.
    {
        auto mask = std::make_shared<XDrawable>(2, 2, v32, 1);
        mask->setPixel(0, 0, 1); mask->setPixel(1, 0, 0);
        mask->setPixel(0, 1, 0); mask->setPixel(1, 1, 1);
        XServer::getServer()->mask = mask;
        XDrawable d(4, 4, v32);
        auto gc = std::make_shared<XGC>(); gc->values.foreground = 11;
        gc->values.clip_mask = 9; gc->values.clip_x_origin = 1;
        d.fillRectangle(&t, gc, 0, 0, 4, 4);
        assert(d.pixel(1,0)==11 && d.pixel(2,0)==0 && d.pixel(1,1)==0 && d.pixel(2,1)==11);
        assert(d.pixel(0,0)==0 && d.pixel(3,3)==0);
        XServer::getServer()->mask = nullptr;
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
    // dashed strokes alternate foreground/background pixels using the GC dash list and offset
    {
        XDrawable d(8, 2, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 5; gc->values.background = 9;
        gc->values.line_style = 2; gc->dash_list = {2, 1}; gc->values.dash_offset = 1;
        d.drawLine(&t, gc, 0, 0, 7, 0);
        const U32 expected[] = {5, 9, 5, 5, 9, 5, 5, 9};
        for (U32 x = 0; x < 8; ++x) assert(d.pixel(x, 0) == expected[x]);
        XDrawable onOff(8, 1, v32);
        gc->values.line_style = 1;
        onOff.drawLine(&t, gc, 0, 0, 7, 0);
        const U32 onOffExpected[] = {5, 0, 5, 5, 0, 5, 5, 0};
        for (U32 x = 0; x < 8; ++x) assert(onOff.pixel(x, 0) == onOffExpected[x]);
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
    // 16-bit drawables draw with packed 16-bit pixels: a line, a fill and a
    // GXxor that must stay inside the 16-bit pixel value
    {
        auto v16 = std::make_shared<XVisual>(XVisual{16});
        XDrawable d(4, 4, v16);
        assert(d.getBytesPerLine() == 8); // 4 pixels * 2 bytes, padded to 4
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 0xf800;
        d.drawLine(&t, gc, 0, 0, 3, 3);
        for (U32 i = 0; i < 4; i++) assert(readStoredPixel(d.getData(), d.getBytesPerLine(), 16, i, i) == 0xf800);
        assert(readStoredPixel(d.getData(), d.getBytesPerLine(), 16, 1, 0) == 0);
        // the packed row holds 2-byte pixels, not the low halves of 4-byte ones
        assert(d.getData()[2] == 0 && d.getData()[3] == 0);
        auto gcSet = std::make_shared<XGC>();
        gcSet->values.function = GXset;
        d.fillRectangle(&t, gcSet, 0, 0, 4, 1);
        assert(readStoredPixel(d.getData(), d.getBytesPerLine(), 16, 2, 0) == 0xffff);
        auto gcXor = std::make_shared<XGC>();
        gcXor->values.function = GXxor;
        gcXor->values.foreground = 0xffff0f0f; // bits above bit 15 must not leak in
        d.fillRectangle(&t, gcXor, 0, 0, 1, 1);
        assert(readStoredPixel(d.getData(), d.getBytesPerLine(), 16, 0, 0) == 0xf0f0); // 0xffff ^ 0x0f0f, kept inside 16 bits
    }
    // 8-bit drawables draw lines, polygons and arcs with 1-byte pixels
    {
        XDrawable d(8, 8, v8);
        const int warningsBefore = warnings;
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 0xab;
        d.drawLine(&t, gc, 0, 0, 3, 3);
        for (U32 i = 0; i < 4; i++) assert(readStoredPixel(d.getData(), d.getBytesPerLine(), 8, i, i) == 0xab);
        d.fillPolygon(&t, gc, {0, 0, 6, 0, 0, 6}, false);
        assert(readStoredPixel(d.getData(), d.getBytesPerLine(), 8, 0, 0) == 0xab);
        auto gcArc = std::make_shared<XGC>();
        gcArc->values.foreground = 0xcd;
        d.fillArc(&t, gcArc, 0, 0, 8, 8, 0, 360 * 64);
        assert(readStoredPixel(d.getData(), d.getBytesPerLine(), 8, 4, 4) == 0xcd);
        d.drawArc(&t, gcArc, 0, 0, 8, 8, 0, 360 * 64);
        assert(warnings == warningsBefore); // no depth bail-out logged
    }
    // a 16-bit tile repeats across a 16-bit fill
    {
        auto v16 = std::make_shared<XVisual>(XVisual{16});
        auto tile = std::make_shared<XDrawable>(2, 1, v16);
        writeStoredPixel(tile->getData(), tile->getBytesPerLine(), 16, 0, 0, 0x1111);
        writeStoredPixel(tile->getData(), tile->getBytesPerLine(), 16, 1, 0, 0x2222);
        XServer::getServer()->tile = tile;
        XDrawable d(4, 1, v16);
        auto gc = std::make_shared<XGC>();
        gc->values.fill_style = FillTiled;
        gc->values.tile = 7;
        d.fillRectangle(&t, gc, 0, 0, 4, 1);
        assert(readStoredPixel(d.getData(), d.getBytesPerLine(), 16, 0, 0) == 0x1111);
        assert(readStoredPixel(d.getData(), d.getBytesPerLine(), 16, 1, 0) == 0x2222);
        assert(readStoredPixel(d.getData(), d.getBytesPerLine(), 16, 2, 0) == 0x1111);
        XServer::getServer()->tile = nullptr;
    }
    // relative line list: each point is an offset from the one before, so (0,0) (+3,0) (+0,+3) is an L to (3,3)
    {
        XDrawable d(8, 8, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 5;
        d.drawLines(&t, gc, {0, 0, 3, 0, 0, 3}, true);
        assert(d.pixel(0, 0) == 5 && d.pixel(3, 0) == 5 && d.pixel(3, 3) == 5);
        assert(d.pixel(2, 3) == 0 && d.pixel(3, 4) == 0);
    }
    // absolute line list draws the same L
    {
        XDrawable d(8, 8, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 5;
        d.drawLines(&t, gc, {0, 0, 3, 0, 3, 3}, false);
        assert(d.pixel(3, 0) == 5 && d.pixel(3, 3) == 5 && d.pixel(2, 3) == 0);
    }
    // rectangle list fills each rectangle and nothing between them
    {
        XDrawable d(8, 8, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 3;
        d.fillRectangles(&t, gc, {1, 1, 2, 2, 5, 5, 1, 1});
        assert(d.pixel(1, 1) == 3 && d.pixel(2, 2) == 3 && d.pixel(5, 5) == 3);
        assert(d.pixel(3, 3) == 0 && d.pixel(4, 4) == 0 && d.pixel(6, 6) == 0);
    }
    // a triangle fills the pixels whose centres are inside it: x + y <= 4 for vertices (0,0) (6,0) (0,6)
    {
        XDrawable d(8, 8, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 4;
        d.fillPolygon(&t, gc, {0, 0, 6, 0, 0, 6}, false);
        for (U32 y = 0; y < 8; y++)
            for (U32 x = 0; x < 8; x++)
                assert(d.pixel(x, y) == (x + y <= 4 ? 4u : 0u));
    }
    // the same triangle written relative to the previous point
    {
        XDrawable d(8, 8, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 4;
        d.fillPolygon(&t, gc, {0, 0, 6, 0, -6, 6}, true);
        for (U32 y = 0; y < 8; y++)
            for (U32 x = 0; x < 8; x++)
                assert(d.pixel(x, y) == (x + y <= 4 ? 4u : 0u));
    }
    // a contour that goes round the square twice: winding fills it, even-odd leaves it empty
    {
        const std::vector<S32> twice = {0, 0, 6, 0, 6, 6, 0, 6, 0, 0, 6, 0, 6, 6, 0, 6};
        XDrawable winding(8, 8, v32);
        auto gcW = std::make_shared<XGC>();
        gcW->values.foreground = 6;
        gcW->values.fill_rule = WindingRule;
        winding.fillPolygon(&t, gcW, twice, false);
        assert(winding.pixel(0, 0) == 6 && winding.pixel(3, 3) == 6 && winding.pixel(5, 5) == 6);
        assert(winding.pixel(6, 0) == 0 && winding.pixel(3, 6) == 0);

        XDrawable evenOdd(8, 8, v32);
        auto gcE = std::make_shared<XGC>();
        gcE->values.foreground = 6;
        evenOdd.fillPolygon(&t, gcE, twice, false);
        assert(evenOdd.pixel(3, 3) == 0 && evenOdd.pixel(0, 0) == 0);
    }
    // a polygon with fewer than three vertices encloses nothing
    {
        XDrawable d(8, 8, v32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 9;
        d.fillPolygon(&t, gc, {0, 0, 6, 6}, false);
        assert(d.pixel(3, 3) == 0);
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
