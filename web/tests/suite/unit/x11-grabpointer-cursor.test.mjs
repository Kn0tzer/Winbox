// web/tests/suite/unit/x11-grabpointer-cursor.test.mjs — XGrabPointer on the
// legacy X11 hostcall path must not abort the emulator, must show the grab's
// cursor while the grab is active, and must release it on ungrab.
//
// Before this change x11_GrabPointer kpanicked on owner_events, GrabModeSync and
// a non-None cursor, and it read GrabModeSync from ARG4 (the event mask) instead
// of ARG5 (the pointer mode). activeCursor() is compiled against stubs and run,
// so its behaviour is checked, not just its text.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const x11common = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const xserver = readFileSync(new URL('../../../../source/x11/xserver.cpp', import.meta.url), 'utf8');

const handler = extractSourceSlice(x11common, 'static void x11_GrabPointer(CPU* cpu) {', '// int XUngrabPointer(', { sourceName: 'source/x11/x11common.cpp' });
const grabPointer = extractSourceSlice(xserver, 'U32 XServer::grabPointer(', '// https://www.x.org/releases/X11R7.6/doc/xproto/x11protocol.html#requests:UngrabPointer', { sourceName: 'source/x11/xserver.cpp' });
const ungrabPointer = extractSourceSlice(xserver, 'U32 XServer::ungrabPointer(', 'U32 XServer::createColorMap(', { sourceName: 'source/x11/xserver.cpp' });
const activeCursor = extractSourceSlice(xserver, 'XCursorPtr XServer::activeCursor(', 'XGCPtr XServer::createGC(', { sourceName: 'source/x11/xserver.cpp' });

test('x11_GrabPointer does not abort on owner_events, GrabModeSync or a cursor', () => {
  assert.doesNotMatch(handler, /kpanic\(/);
});

test('x11_GrabPointer reads the pointer mode from ARG5 and rejects an unknown cursor', () => {
  assert.match(handler, /ARG5 == GrabModeSync/);
  assert.doesNotMatch(handler, /ARG4 == GrabModeSync/);
  assert.match(handler, /getCursor\(ARG8\)/);
  assert.match(handler, /EAX = BadCursor;/);
  assert.match(handler, /server->grabPointer\(data, window, confined, grabCursor, ARG4, ARG9\)/);
});

test('grabPointer stores the grab cursor and refreshes the screen cursor even when the pointer is already on the grab window', () => {
  assert.match(grabPointer, /this->grabbedCursor = cursor;/);
  assert.match(grabPointer, /setCursor\(activeCursor\(pointerWindow\)\)/);
  assert.doesNotMatch(grabPointer, /updateCursor/);
});

test('ungrabPointer releases the grab cursor', () => {
  assert.match(ungrabPointer, /this->grabbedCursor = nullptr;/);
});

test('activeCursor shows the grab cursor only while a grab with a cursor is active', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-grab-cursor-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <memory>
#include <mutex>
#define BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(m) std::lock_guard<std::mutex> guard(m)
struct XCursor { int id; };
using XCursorPtr = std::shared_ptr<XCursor>;
struct XWindow { XCursorPtr cursor; XCursorPtr getCursor() { return cursor; } };
using XWindowPtr = std::shared_ptr<XWindow>;
struct XServer {
    std::mutex grabbedMutex;
    bool isGrabbed = false;
    XCursorPtr grabbedCursor;
    XCursorPtr activeCursor(const XWindowPtr& wnd);
};
${activeCursor}
int main() {
    XServer s;
    auto win = std::make_shared<XWindow>();
    win->cursor = std::make_shared<XCursor>(XCursor{1});
    auto grabCursor = std::make_shared<XCursor>(XCursor{2});

    assert(s.activeCursor(win)->id == 1);            // no grab: the window's cursor

    s.isGrabbed = true;
    s.grabbedCursor = grabCursor;
    assert(s.activeCursor(win)->id == 2);            // grab with a cursor: the grab's cursor

    s.grabbedCursor = nullptr;
    assert(s.activeCursor(win)->id == 1);            // grab without a cursor: the window's cursor

    s.isGrabbed = false;
    s.grabbedCursor = grabCursor;
    assert(s.activeCursor(win)->id == 1);            // stale grab cursor is ignored once released
    return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
