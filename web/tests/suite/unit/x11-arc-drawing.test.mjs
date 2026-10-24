// web/tests/suite/unit/x11-arc-drawing.test.mjs — XFillArc and XDrawArc draw
// the ellipse, pie slice, chord or outline instead of aborting the emulator.
//
// source/x11/x11common.cpp had kpanics for both calls, and the drawing core
// had no arc code. The arc functions and the tile helpers they share with
// fillRectangle are compiled against a stub drawable that records each pixel it
// is asked to plot, and the test checks which pixels a pie, a chord, a full
// ellipse and an outline draw.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/x11/xdrawable.cpp';
const source = readFileSync(new URL('../../../../source/x11/xdrawable.cpp', import.meta.url), 'utf8');
const tileHelpers = extractSourceSlice(source, '// The tile pixmap for a FillTiled GC', 'int XDrawable::fillRectangle(', { sourceName });
const arcBlock = extractSourceSlice(source, 'namespace {\nconst double kPi', 'int XDrawable::drawRectangle(', { sourceName });

test('the arc block has no abort and no silent skip', () => {
  assert.doesNotMatch(arcBlock, /kpanic/);
  assert.match(arcBlock, /int XDrawable::fillArc\(/);
  assert.match(arcBlock, /int XDrawable::drawArc\(/);
});

test('fill and outline draw the shapes the arc angles describe', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-arc-drawing-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <vector>
typedef int32_t S32;
typedef uint32_t U32;
typedef uint8_t U8;
enum { Success = 0, FillTiled = 1, ArcChord = 0, ArcPieSlice = 1 };
struct KThread {};
template <typename... A> static void kwarn_fmt(const char*, A...) {}
struct Visual { U32 bits_per_rgb; };
#define VisualPtr std::shared_ptr<Visual>
class XGC;
class XDrawable;
#define XDrawablePtr std::shared_ptr<XDrawable>
struct XGCValues { S32 foreground = 0; U32 tile = 0; S32 fill_style = 0; S32 arc_mode = 0; S32 ts_x_origin = 0; S32 ts_y_origin = 0; };
class XGC { public: XGCValues values; };
struct DrawnPixel { S32 x, y; U32 color; };
struct XServer {
    std::map<U32, XDrawablePtr> drawables;
    static XServer* getServer() { static XServer s; return &s; }
    XDrawablePtr getDrawable(U32 id) { auto it = drawables.find(id); return it == drawables.end() ? nullptr : it->second; }
};
class XDrawable {
public:
    XDrawable(U32 width, U32 height, U32 bits) : visual(std::make_shared<Visual>(Visual{bits})), w(width), h(height), bytes_per_line(width * 4), data(new U8[width * height * 4]()) {}
    ~XDrawable() { delete[] data; }
    VisualPtr visual;
    U32 w, h;
    U32 bytes_per_line;
    U8* data;
    std::vector<DrawnPixel> drawn;
    void plotPixel(const std::shared_ptr<XGC>&, S32 x, S32 y, U32 color) {
        if (x < 0 || y < 0 || x >= (S32)w || y >= (S32)h) return;
        drawn.push_back({x, y, color});
    }
    void setDirty() {}
    int fillArc(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 width, U32 height, S32 angle1, S32 angle2);
    int drawArc(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 width, U32 height, S32 angle1, S32 angle2);
    static XDrawablePtr fillTile(const std::shared_ptr<XGC>& gc);
    static U32 fillColor(const std::shared_ptr<XGC>& gc, const XDrawablePtr& tile, S32 x, S32 y);
};
${tileHelpers}
${arcBlock}
static bool drawnAt(const XDrawable& d, S32 x, S32 y) {
    for (const auto& p : d.drawn) if (p.x == x && p.y == y) return true;
    return false;
}
static bool uniquePixels(const XDrawable& d) {
    for (size_t i = 0; i < d.drawn.size(); i++)
        for (size_t j = i + 1; j < d.drawn.size(); j++)
            if (d.drawn[i].x == d.drawn[j].x && d.drawn[i].y == d.drawn[j].y) return false;
    return true;
}
int main() {
    const S32 full = 360 * 64;
    const S32 quarter = 90 * 64;
    // a filled ellipse in a 10x10 box
    {
        XDrawable d(10, 10, 32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 0xff; gc->values.arc_mode = ArcPieSlice;
        assert(d.fillArc(nullptr, gc, 0, 0, 10, 10, 0, full) == Success);
        assert(!drawnAt(d, 0, 0));
        assert(drawnAt(d, 4, 4) && drawnAt(d, 9, 4) && drawnAt(d, 5, 0));
        assert(d.drawn[0].color == 0xff);
    }
    // a pie slice from 3 o'clock to 12 o'clock is the upper-right quarter
    {
        XDrawable d(10, 10, 32);
        auto gc = std::make_shared<XGC>();
        gc->values.arc_mode = ArcPieSlice;
        d.fillArc(nullptr, gc, 0, 0, 10, 10, 0, quarter);
        assert(drawnAt(d, 8, 1) && drawnAt(d, 8, 4));
        assert(!drawnAt(d, 1, 1) && !drawnAt(d, 1, 8) && !drawnAt(d, 8, 8));
    }
    // a chord from 3 o'clock to 12 o'clock is the cap between the arc and its chord
    {
        XDrawable d(10, 10, 32);
        auto gc = std::make_shared<XGC>();
        gc->values.arc_mode = ArcChord;
        d.fillArc(nullptr, gc, 0, 0, 10, 10, 0, quarter);
        assert(drawnAt(d, 8, 1));
        assert(!drawnAt(d, 8, 4) && !drawnAt(d, 1, 8) && !drawnAt(d, 5, 5));
    }
    // a clockwise sweep from 12 o'clock to 3 o'clock is the same upper-right quarter
    {
        XDrawable d(10, 10, 32);
        auto gc = std::make_shared<XGC>();
        gc->values.arc_mode = ArcPieSlice;
        d.fillArc(nullptr, gc, 0, 0, 10, 10, quarter, -quarter);
        assert(drawnAt(d, 8, 1) && !drawnAt(d, 1, 8));
    }
    // an outline is one pixel thick, drawn once per pixel, and stays on the curve
    {
        XDrawable d(10, 10, 32);
        auto gc = std::make_shared<XGC>();
        gc->values.foreground = 0x7;
        assert(d.drawArc(nullptr, gc, 0, 0, 10, 10, 0, full) == Success);
        assert(uniquePixels(d));
        assert(drawnAt(d, 0, 4) && drawnAt(d, 9, 4) && drawnAt(d, 4, 0) && drawnAt(d, 4, 9));
        assert(!drawnAt(d, 4, 4) && !drawnAt(d, 5, 5));
        for (const auto& p : d.drawn) assert(p.x >= 0 && p.x < 10 && p.y >= 0 && p.y < 10);
    }
    // an outline arc only draws its own quarter
    {
        XDrawable d(10, 10, 32);
        auto gc = std::make_shared<XGC>();
        d.drawArc(nullptr, gc, 0, 0, 10, 10, 0, quarter);
        assert(drawnAt(d, 8, 1) && !drawnAt(d, 1, 8) && !drawnAt(d, 1, 1));
    }
    // an empty box and a depth this path does not draw leave the drawable alone
    {
        XDrawable d(10, 10, 32);
        auto gc = std::make_shared<XGC>();
        d.fillArc(nullptr, gc, 0, 0, 0, 10, 0, full);
        d.drawArc(nullptr, gc, 0, 0, 10, 0, 0, full);
        assert(d.drawn.empty());
        XDrawable eight(10, 10, 8);
        assert(eight.fillArc(nullptr, gc, 0, 0, 10, 10, 0, full) == Success && eight.drawn.empty());
    }
    // a tiled fill takes its colour from the tile, anchored at the tile origin
    {
        auto tile = std::make_shared<XDrawable>(2, 2, 32);
        const U32 colors[4] = {0x11, 0x22, 0x33, 0x44};
        for (int i = 0; i < 4; i++) std::memcpy(tile->data + i * 4, &colors[i], 4);
        XServer::getServer()->drawables[5] = tile;
        auto gc = std::make_shared<XGC>();
        gc->values.fill_style = FillTiled; gc->values.tile = 5;
        XDrawablePtr t = XDrawable::fillTile(gc);
        assert(t != nullptr);
        assert(XDrawable::fillColor(gc, t, 3, 2) == 0x22);
        assert(XDrawable::fillColor(gc, t, -1, -1) == 0x44);
        gc->values.fill_style = 0;
        assert(XDrawable::fillTile(gc) == nullptr);
        assert(XDrawable::fillColor(gc, nullptr, 3, 2) == 0);
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
