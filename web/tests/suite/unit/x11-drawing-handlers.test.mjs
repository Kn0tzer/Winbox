// web/tests/suite/unit/x11-drawing-handlers.test.mjs — the legacy X11 drawing entry
// points XClearArea, XDrawLines, XFillRectangles, XDrawPoint and XFillPolygon must
// draw, not abort the emulator. The guest point and rectangle arrays are read by the
// real reader helpers, and XWindow::clearArea is compiled against stubs and run, so the
// signed and unsigned field handling and the background rules are checked, not only the text.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const x11common = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const xwindow = readFileSync(new URL('../../../../source/x11/xwindow.cpp', import.meta.url), 'utf8');

const clearAreaHandler = extractSourceSlice(x11common, 'static void x11_ClearArea(CPU* cpu) {', '// int XSync(Display* display, Bool discard)', { sourceName: 'source/x11/x11common.cpp' });
const readers = extractSourceSlice(x11common, '// Reads count XPoint records', '// int XDrawLines(', { sourceName: 'source/x11/x11common.cpp' });
const drawLinesHandler = extractSourceSlice(x11common, 'static void x11_DrawLines(CPU* cpu) {', 'static void x11_SetArcMode(CPU* cpu) {', { sourceName: 'source/x11/x11common.cpp' });
const fillHandlers = extractSourceSlice(x11common, '// int XFillRectangles(', '// int XChangeGC(', { sourceName: 'source/x11/x11common.cpp' });
const clearArea = extractSourceSlice(xwindow, 'int XWindow::clearArea(', 'void XWindow::exposeNofity(', { sourceName: 'source/x11/xwindow.cpp' });

test('the drawing handlers do not abort the emulator and call the drawable primitives', () => {
  assert.doesNotMatch(clearAreaHandler, /kpanic\(/);
  assert.match(clearAreaHandler, /window->clearArea\(thread,/);
  assert.doesNotMatch(drawLinesHandler, /kpanic\(|\/\/kpanic/);
  assert.match(drawLinesHandler, /drawable->drawLines\(thread, gc, readPoints\(cpu->memory, ARG4, ARG5\), ARG6 == CoordModePrevious\)/);
  assert.doesNotMatch(fillHandlers, /kpanic\(/);
  assert.match(fillHandlers, /drawable->fillRectangles\(thread, gc, readRectangles\(cpu->memory, ARG4, ARG5\)\)/);
  assert.match(fillHandlers, /drawable->drawLine\(thread, gc, \(S32\)ARG4, \(S32\)ARG5, \(S32\)ARG4, \(S32\)ARG5\)/);
  assert.match(fillHandlers, /drawable->fillPolygon\(thread, gc, readPoints\(cpu->memory, ARG4, ARG5\), ARG7 == CoordModePrevious\)/);
});

test('the guest array readers keep x and y signed and width and height unsigned', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-drawing-readers-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <vector>
typedef int16_t S16;
typedef uint16_t U16;
typedef int32_t S32;
typedef uint32_t U32;
// a guest memory of 16-bit words, addressed in bytes
struct KMemory {
    std::vector<U16> words;
    U16 readw(U32 address) { return words[address / 2]; }
};
${readers}
int main() {
    KMemory memory;
    // two XPoint records: (-2, 7) and (300, -1)
    memory.words = {(U16)-2, 7, 300, (U16)-1};
    std::vector<S32> points = readPoints(&memory, 0, 2);
    assert(points.size() == 4 && points[0] == -2 && points[1] == 7 && points[2] == 300 && points[3] == -1);

    // one XRectangle record: x = -1, y = 2, width = 65535, height = 3
    memory.words = {(U16)-1, 2, 0xffff, 3};
    std::vector<S32> rects = readRectangles(&memory, 0, 1);
    assert(rects.size() == 4 && rects[0] == -1 && rects[1] == 2 && rects[2] == 65535 && rects[3] == 3);
    return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('XWindow::clearArea paints the pixel background, runs zero sizes to the edge, and sends Expose only when asked', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-clear-area-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
typedef uint32_t U32;
typedef int32_t S32;
typedef uint32_t Pixmap;
enum { Success = 0 };
#define None 0
#define ExposureMask (1L << 15)
#define InputOutput 1
static int warnings = 0;
template <class... A> void kwarn_fmt(const char*, A&&...) { warnings++; }
struct KThread {};
struct DisplayData { int id; };
using DisplayDataPtr = std::shared_ptr<DisplayData>;
struct XGCValues { U32 foreground = 0; };
struct XGC {
    template <class T> explicit XGC(const std::shared_ptr<T>&) {}
    XGCValues values;
};
using XGCPtr = std::shared_ptr<XGC>;
struct XSetWindowAttributes { Pixmap background_pixmap = 0; U32 background_pixel = 0; };
struct Fill { S32 x, y, w, h; U32 color; };
struct Expose { S32 x, y, w, h; };
struct XServer {
    bool subscribed = false;
    static XServer* getServer() { static XServer s; return &s; }
    void iterateEventMask(U32, U32 mask, std::function<void(const DisplayDataPtr&)> callback) {
        if (subscribed && (mask & ExposureMask)) callback(std::make_shared<DisplayData>(DisplayData{1}));
    }
};
struct XWindow : std::enable_shared_from_this<XWindow> {
    U32 id = 9;
    bool isMapped = true;
    bool hasBackgroundPixel = false;
    XSetWindowAttributes attributes;
    std::vector<Fill> fills;
    std::vector<Expose> exposes;
    U32 width() const { return 100; }
    U32 height() const { return 50; }
    int fillRectangle(KThread*, const XGCPtr& gc, S32 x, S32 y, U32 w, U32 h) {
        fills.push_back({x, y, (S32)w, (S32)h, gc->values.foreground});
        return Success;
    }
    void sendExpose(const DisplayDataPtr&, S32 x, S32 y, S32 w, S32 h, S32) { exposes.push_back({x, y, w, h}); }
    int clearArea(KThread* thread, S32 x, S32 y, U32 width, U32 height, bool exposures);
};
${clearArea}
int main() {
    KThread t;
    {
        // a pixel background fills exactly the requested area with that pixel
        auto w = std::make_shared<XWindow>();
        w->hasBackgroundPixel = true;
        w->attributes.background_pixel = 0xff;
        assert(w->clearArea(&t, 4, 3, 10, 5, false) == Success);
        assert(w->fills.size() == 1);
        assert(w->fills[0].x == 4 && w->fills[0].y == 3 && w->fills[0].w == 10 && w->fills[0].h == 5 && w->fills[0].color == 0xff);
    }
    {
        // a zero width or height runs to the edge of the window
        auto w = std::make_shared<XWindow>();
        w->hasBackgroundPixel = true;
        w->attributes.background_pixel = 0x11;
        w->clearArea(&t, 10, 5, 0, 0, false);
        assert(w->fills.size() == 1 && w->fills[0].w == 90 && w->fills[0].h == 45);
        // an area that starts on the right edge is empty
        w->fills.clear();
        w->clearArea(&t, 100, 0, 0, 0, false);
        assert(w->fills.empty());
    }
    {
        // a window with no pixel background keeps its contents; a pixmap background is logged, not drawn
        auto w = std::make_shared<XWindow>();
        w->clearArea(&t, 0, 0, 0, 0, false);
        assert(w->fills.empty() && warnings == 0);
        w->attributes.background_pixmap = 5;
        w->clearArea(&t, 0, 0, 0, 0, false);
        assert(w->fills.empty() && warnings == 1);
    }
    {
        // exposures send Expose for the cleared area only when the window is mapped and selects ExposureMask
        auto w = std::make_shared<XWindow>();
        w->hasBackgroundPixel = true;
        XServer::getServer()->subscribed = true;
        w->clearArea(&t, 1, 2, 3, 4, true);
        assert(w->exposes.size() == 1);
        assert(w->exposes[0].x == 1 && w->exposes[0].y == 2 && w->exposes[0].w == 3 && w->exposes[0].h == 4);
        w->clearArea(&t, 1, 2, 3, 4, false);
        assert(w->exposes.size() == 1);
        w->isMapped = false;
        w->clearArea(&t, 1, 2, 3, 4, true);
        assert(w->exposes.size() == 1);
        XServer::getServer()->subscribed = false;
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
