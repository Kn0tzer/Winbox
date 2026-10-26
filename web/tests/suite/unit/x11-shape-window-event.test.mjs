// web/tests/suite/unit/x11-shape-window-event.test.mjs — SHAPE requests and
// XWindowEvent must not abort the emulator.
//
// source/x11/x11common.cpp:x11_ShapeCombineMask/CombineRectangles/OffsetShape
// and x11_WindowEvent were kpanics, so any client using the SHAPE extension
// or waiting on a window event stopped the emulator. SHAPE has no server-side
// region store here and its Xlib calls are void, so they log once and no-op.
// XWindowEvent delivers an already-queued matching event (the mask table in
// source/x11/displaydata.cpp is compiled and run below) and otherwise logs
// once and returns False, because the blocking wait cannot run inside a
// guest callback.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const common = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const displaySource = readFileSync(new URL('../../../../source/x11/displaydata.cpp', import.meta.url), 'utf8');

test('SHAPE calls log once and no-op instead of aborting', () => {
  for (const name of ['x11_ShapeCombineMask', 'x11_ShapeCombineRectangles', 'x11_ShapeOffsetShape']) {
    const handler = common.match(new RegExp(`static void ${name}\\(CPU\\* cpu\\) \\{[\\s\\S]*?\\n\\}`))?.[0];
    assert.ok(handler, name);
    assert.doesNotMatch(handler, /kpanic/);
    assert.match(handler, /x11LogShapeIgnored\(\)/);
  }
  assert.match(common, /X11 SHAPE: shape requests are ignored; this server keeps windows rectangular/);
});

test('XWindowEvent delivers queued events and fails loudly when none are queued', () => {
  const handler = common.match(/static void x11_WindowEvent\(CPU\* cpu\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(handler);
  assert.doesNotMatch(handler, /kpanic/);
  assert.match(handler, /findAndRemoveEventByMask\(window->id, ARG3, event\)/);
  assert.match(handler, /EAX = True/);
  assert.match(handler, /EAX = False/);
  assert.match(handler, /X11 XWindowEvent: no queued event matches the mask/);
});

test('event mask bits follow the X11 event-mask table', () => {
  const slice = extractSourceSlice(displaySource, 'static U32 x11EventMaskBits(', 'bool DisplayData::findAndRemoveEventByMask', { sourceName: 'source/x11/displaydata.cpp' });
  const dir = mkdtempSync(join(tmpdir(), 'x11-event-mask-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
typedef int32_t S32;
typedef uint32_t U32;
#define KeyPressMask (1u<<0)
#define KeyReleaseMask (1u<<1)
#define ButtonPressMask (1u<<2)
#define ButtonReleaseMask (1u<<3)
#define EnterWindowMask (1u<<4)
#define LeaveWindowMask (1u<<5)
#define PointerMotionMask (1u<<6)
#define PointerMotionHintMask (1u<<7)
#define Button1MotionMask (1u<<8)
#define Button2MotionMask (1u<<9)
#define Button3MotionMask (1u<<10)
#define Button4MotionMask (1u<<11)
#define Button5MotionMask (1u<<12)
#define ButtonMotionMask (1u<<13)
#define KeymapStateMask (1u<<14)
#define ExposureMask (1u<<15)
#define VisibilityChangeMask (1u<<16)
#define StructureNotifyMask (1u<<17)
#define ResizeRedirectMask (1u<<18)
#define SubstructureNotifyMask (1u<<19)
#define SubstructureRedirectMask (1u<<20)
#define FocusChangeMask (1u<<21)
#define PropertyChangeMask (1u<<22)
#define ColormapChangeMask (1u<<23)
#define KeyPress 2
#define KeyRelease 3
#define ButtonPress 4
#define ButtonRelease 5
#define MotionNotify 6
#define EnterNotify 7
#define LeaveNotify 8
#define FocusIn 9
#define FocusOut 10
#define KeymapNotify 11
#define Expose 12
#define GraphicsExpose 13
#define NoExpose 14
#define VisibilityNotify 15
#define CreateNotify 16
#define DestroyNotify 17
#define UnmapNotify 18
#define MapNotify 19
#define MapRequest 20
#define ReparentNotify 21
#define ConfigureNotify 22
#define ConfigureRequest 23
#define GravityNotify 24
#define ResizeRequest 25
#define CirculateNotify 26
#define CirculateRequest 27
#define PropertyNotify 28
#define SelectionClear 29
#define SelectionRequest 30
#define SelectionNotify 31
#define ColormapNotify 32
#define ClientMessage 33
#define MappingNotify 34
${slice}
int main() {
    assert(x11EventMaskBits(KeyPress) == KeyPressMask);
    assert(x11EventMaskBits(ButtonRelease) == ButtonReleaseMask);
    assert((x11EventMaskBits(MotionNotify) & PointerMotionMask) != 0);
    assert((x11EventMaskBits(MotionNotify) & ButtonMotionMask) != 0);
    assert((x11EventMaskBits(MotionNotify) & ExposureMask) == 0);
    assert(x11EventMaskBits(Expose) == ExposureMask);
    assert(x11EventMaskBits(GraphicsExpose) == ExposureMask);
    assert(x11EventMaskBits(NoExpose) == ExposureMask);
    assert(x11EventMaskBits(ConfigureNotify) == (StructureNotifyMask | SubstructureNotifyMask));
    assert(x11EventMaskBits(CreateNotify) == SubstructureNotifyMask);
    assert(x11EventMaskBits(MapRequest) == SubstructureRedirectMask);
    assert(x11EventMaskBits(ResizeRequest) == ResizeRedirectMask);
    assert(x11EventMaskBits(PropertyNotify) == PropertyChangeMask);
    assert(x11EventMaskBits(ColormapNotify) == ColormapChangeMask);
    assert(x11EventMaskBits(FocusOut) == FocusChangeMask);
    // types with no mask in the X11 table match any mask
    assert(x11EventMaskBits(ClientMessage) == 0xffffffffu);
    assert(x11EventMaskBits(SelectionNotify) == 0xffffffffu);
    assert(x11EventMaskBits(MappingNotify) == 0xffffffffu);
    // a queued KeyPress is not delivered to a caller waiting for exposure
    assert((ExposureMask & x11EventMaskBits(KeyPress)) == 0);
    return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
