// web/tests/suite/unit/x11-alloccolor-write.test.mjs — XAllocColor sharing a
// read/write colormap cell must keep the cell writable.
//
// source/x11/x11common.cpp:x11_AllocColor matched an exact RGB on a cell that
// XAllocColorCells had allocated read/write, returned it, and overwrote its
// flags with COLOR_ALLOCATED alone, silently revoking the owner's write
// permission; every scan past any writable cell also warned as if allocation
// had failed. The match now shares the cell without touching its flags.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/x11/x11common.cpp';
const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const allocColor = extractSourceSlice(source, '// Status XAllocColor(Display* display, Colormap colormap, XColor* screen_in_out)', '// Status XAllocColorCells', { sourceName });
const allocCells = extractSourceSlice(source, '// Status XAllocColorCells', '// XVisualInfo* XGetVisualInfo', { sourceName });

test('XAllocColor shares a writable cell without revoking COLOR_WRITE', () => {
  assert.doesNotMatch(allocColor, /COLOR_WRITE not implemented/);
  assert.doesNotMatch(allocColor, /c\.flags = COLOR_ALLOCATED;/);
  assert.match(allocColor, /COLOR_WRITE is kept/);
  assert.match(allocColor, /if \(\(c\.flags & COLOR_ALLOCATED\) && c\.r == r && c\.g == g && c\.b == b\)/);
});

test('XAllocColorCells still allocates read/write cells for the share to find', () => {
  assert.match(allocCells, /COLOR_ALLOCATED \| COLOR_WRITE/);
});
