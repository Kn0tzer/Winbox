// web/tests/suite/unit/x11-animated-cursor.test.mjs — animated Xcursor
// images are no longer truncated to their first frame: the loader copies
// every frame (pixels + delay) into the XCursor, the SDL screen builds one
// SDL cursor per frame and cycles them on a timer while the cursor is
// current, and the X cursor shape table covers the shapes that used to be
// :TODO: approximations with the closest SDL system cursor named.
//
// The scheduling helpers and the shape table are compiled out of the real
// source and run; the loader and setCursor wiring are pinned by pattern.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const x11 = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const cursorHeader = readFileSync(new URL('../../../../source/x11/xcursor.h', import.meta.url), 'utf8');
const screenSource = readFileSync(new URL('../../../../platform/sdl/knativescreenSDL.cpp', import.meta.url), 'utf8');

test('the loader copies every frame with its delay instead of truncating to frame 0', () => {
  assert.doesNotMatch(x11, /animated cursors not supported/);
  const load = extractSourceSlice(x11, 'void x11_CursorImagesLoadCursor(CPU* cpu) {', '// Cursor XcursorLibraryLoadCursor', { sourceName: 'source/x11/x11common.cpp' });
  assert.match(load, /anim\.delay = frame\.delay/);
  assert.match(load, /memory->memcpy\(anim\.pixels\.data\(\), frame\.pixelsAddress/);
  assert.match(load, /cursor->animFrames\.push_back/);
  assert.match(cursorHeader, /struct XCursorAnimFrame \{[\s\S]*?U32 delay = 0;[\s\S]*?std::vector<U32> pixels;/);
  assert.match(cursorHeader, /std::vector<XCursorAnimFrame> animFrames;/);
});

test('setCursor starts the animation for multi-frame cursors and stops it for static ones', () => {
  const setCursor = extractSourceSlice(screenSource, 'void KNativeScreenSDL::setCursor(', '// Map an X core cursor-font glyph', { sourceName: 'platform/sdl/knativescreenSDL.cpp' });
  assert.match(setCursor, /cursor->animFrames\.size\(\) > 1\) \{\s*startCursorAnimation\(cursor\);/);
  assert.match(setCursor, /stopCursorAnimation\(\);/);
  assert.doesNotMatch(screenSource, /:TODO:/);
});

test('frame scheduling wraps and clamps sub-10ms delays', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-anim-cursor-'));
  try {
    const schedule = extractSourceSlice(screenSource, 'static size_t cursorAnimNextFrame(', 'static Uint32 cursorAnimTimerCb(', { sourceName: 'platform/sdl/knativescreenSDL.cpp' });
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstddef>
#include <cstdint>
typedef uint32_t U32;
typedef uint32_t Uint32;
${schedule}
int main() {
    assert(cursorAnimNextFrame(0, 3) == 1);
    assert(cursorAnimNextFrame(1, 3) == 2);
    assert(cursorAnimNextFrame(2, 3) == 0);
    assert(cursorAnimNextFrame(0, 0) == 0);
    assert(cursorAnimDelayMs(0) == 10);
    assert(cursorAnimDelayMs(9) == 10);
    assert(cursorAnimDelayMs(10) == 10);
    assert(cursorAnimDelayMs(250) == 250);
    return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('every X shape maps to its named SDL system cursor, including the former TODO shapes', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-cursor-shape-'));
  try {
    const table = extractSourceSlice(screenSource, 'static SDL_SystemCursor x11ShapeToSdlSystemCursor(', 'void KNativeScreenSDL::setCursorByX11Shape(', { sourceName: 'platform/sdl/knativescreenSDL.cpp' });
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
enum SDL_SystemCursor {
    SDL_SYSTEM_CURSOR_ARROW, SDL_SYSTEM_CURSOR_IBEAM, SDL_SYSTEM_CURSOR_WAIT,
    SDL_SYSTEM_CURSOR_CROSSHAIR, SDL_SYSTEM_CURSOR_WAITARROW, SDL_SYSTEM_CURSOR_SIZENWSE,
    SDL_SYSTEM_CURSOR_SIZENESW, SDL_SYSTEM_CURSOR_SIZEWE, SDL_SYSTEM_CURSOR_SIZENS,
    SDL_SYSTEM_CURSOR_SIZEALL, SDL_SYSTEM_CURSOR_NO, SDL_SYSTEM_CURSOR_HAND
};
${table}
int main() {
    assert(x11ShapeToSdlSystemCursor(52) == SDL_SYSTEM_CURSOR_SIZEALL);   // XC_fleur
    assert(x11ShapeToSdlSystemCursor(60) == SDL_SYSTEM_CURSOR_HAND);      // XC_hand2
    assert(x11ShapeToSdlSystemCursor(68) == SDL_SYSTEM_CURSOR_ARROW);     // XC_left_ptr
    assert(x11ShapeToSdlSystemCursor(88) == SDL_SYSTEM_CURSOR_NO);        // XC_pirate
    assert(x11ShapeToSdlSystemCursor(108) == SDL_SYSTEM_CURSOR_SIZEWE);   // XC_sb_h_double_arrow
    assert(x11ShapeToSdlSystemCursor(116) == SDL_SYSTEM_CURSOR_SIZENS);   // XC_sb_v_double_arrow
    assert(x11ShapeToSdlSystemCursor(130) == SDL_SYSTEM_CURSOR_CROSSHAIR); // XC_tcross
    assert(x11ShapeToSdlSystemCursor(150) == SDL_SYSTEM_CURSOR_WAITARROW); // XC_watch
    assert(x11ShapeToSdlSystemCursor(152) == SDL_SYSTEM_CURSOR_IBEAM);    // XC_xterm
    // the three shapes that were :TODO: approximations, now deliberate
    assert(x11ShapeToSdlSystemCursor(22) == SDL_SYSTEM_CURSOR_ARROW);     // XC_center_ptr
    assert(x11ShapeToSdlSystemCursor(64) == SDL_SYSTEM_CURSOR_WAIT);      // XC_icon
    assert(x11ShapeToSdlSystemCursor(92) == SDL_SYSTEM_CURSOR_ARROW);     // XC_question_arrow
    assert(x11ShapeToSdlSystemCursor(1000) == SDL_SYSTEM_CURSOR_WAITARROW); // left_ptr_watch
    assert(x11ShapeToSdlSystemCursor(9999) == SDL_SYSTEM_CURSOR_ARROW);   // unknown shapes fall back
    return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
