// web/tests/suite/unit/xwire-keyboard-map.test.mjs -- XWire keyboard tables
// must be COMPLETE and CONSISTENT (kbprobe lane, 2026-10-26).
//
// Empirical finding (guest probe, native bw64_xwire_key battery): every key
// whose SDL scancode had no case in sdlScancodeToX11, OR whose X keycode had
// no keysym in keysymForKeycode, reached the guest as NOTHING -- no
// WM_KEYDOWN, no raw input, no async state. winex11 silently drops NoSymbol
// keys. Dead on the pre-fix tree: F1-F12, PageUp/PageDown, Insert,
// PrintScreen, ScrollLock, Pause, CapsLock, NumLock, keypad, RControl,
// RAlt, GUI, Menu. This test pins both tables against the canonical evdev
// (X keycode = kernel code + 8) / X keysym assignments and the invariant
// that every emitted keycode carries a nonzero keysym.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = process.env.KB_MAP_ROOT || join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const sdlSrc = readFileSync(join(root, 'source/x11wire/xwirepresentSDL.cpp'), 'utf8');
const connSrc = readFileSync(join(root, 'source/x11wire/xwireconnection.cpp'), 'utf8');

function sliceFn(src, startMarker, endMarker) {
  const a = src.indexOf(startMarker);
  assert.ok(a >= 0, `marker not found: ${startMarker}`);
  const b = src.indexOf(endMarker, a);
  assert.ok(b > a, `end marker not found after: ${startMarker}`);
  return src.slice(a, b);
}

// SDL scancode name -> X keycode, from sdlScancodeToX11's switch.
const sdlBody = sliceFn(sdlSrc, 'uint32_t sdlScancodeToX11', 'default: return 0;');
const sdlMap = new Map();
for (const m of sdlBody.matchAll(/case SDL_SCANCODE_(\w+):\s*return (\d+);/g)) sdlMap.set(m[1], Number(m[2]));

// X keycode -> unshifted keysym (number), from keysymForKeycode's switch.
const symBody = sliceFn(connSrc, 'keysymForKeycode(int kc)', 'default: return {0, 0};');
const symMap = new Map();
for (const m of symBody.matchAll(/case (\d+):\s*return \{(0x[0-9A-Fa-f]+|'(?:\\.|.)')\s*,/g)) {
  const raw = m[2];
  const value = raw.startsWith('0x') ? parseInt(raw, 16) : raw.slice(1, -1).replace(/\\(.)/g, '$1').codePointAt(0);
  symMap.set(Number(m[1]), value);
}

// [SDL name, X keycode, X keysym] -- the canonical PC set (evdev + X11 keysyms).
const REQUIRED = [
  ['ESCAPE', 9, 0xFF1B], ['1', 10, 0x31], ['2', 11, 0x32], ['3', 12, 0x33], ['4', 13, 0x34],
  ['5', 14, 0x35], ['6', 15, 0x36], ['7', 16, 0x37], ['8', 17, 0x38], ['9', 18, 0x39], ['0', 19, 0x30],
  ['MINUS', 20, 0x2D], ['EQUALS', 21, 0x3D], ['BACKSPACE', 22, 0xFF08], ['TAB', 23, 0xFF09],
  ['Q', 24, 0x71], ['W', 25, 0x77], ['E', 26, 0x65], ['R', 27, 0x72], ['T', 28, 0x74],
  ['Y', 29, 0x79], ['U', 30, 0x75], ['I', 31, 0x69], ['O', 32, 0x6F], ['P', 33, 0x70],
  ['LEFTBRACKET', 34, 0x5B], ['RIGHTBRACKET', 35, 0x5D], ['RETURN', 36, 0xFF0D], ['LCTRL', 37, 0xFFE3],
  ['A', 38, 0x61], ['S', 39, 0x73], ['D', 40, 0x64], ['F', 41, 0x66], ['G', 42, 0x67],
  ['H', 43, 0x68], ['J', 44, 0x6A], ['K', 45, 0x6B], ['L', 46, 0x6C], ['SEMICOLON', 47, 0x3B],
  ['APOSTROPHE', 48, 0x27], ['GRAVE', 49, 0x60], ['LSHIFT', 50, 0xFFE1], ['BACKSLASH', 51, 0x5C],
  ['Z', 52, 0x7A], ['X', 53, 0x78], ['C', 54, 0x63], ['V', 55, 0x76], ['B', 56, 0x62],
  ['N', 57, 0x6E], ['M', 58, 0x6D], ['COMMA', 59, 0x2C], ['PERIOD', 60, 0x2E], ['SLASH', 61, 0x2F],
  ['RSHIFT', 62, 0xFFE2], ['KP_MULTIPLY', 63, 0xFFAA], ['LALT', 64, 0xFFE9], ['SPACE', 65, 0x20],
  ['CAPSLOCK', 66, 0xFFE5],
  ['F1', 67, 0xFFBE], ['F2', 68, 0xFFBF], ['F3', 69, 0xFFC0], ['F4', 70, 0xFFC1], ['F5', 71, 0xFFC2],
  ['F6', 72, 0xFFC3], ['F7', 73, 0xFFC4], ['F8', 74, 0xFFC5], ['F9', 75, 0xFFC6], ['F10', 76, 0xFFC7],
  ['NUMLOCKCLEAR', 77, 0xFF7F], ['SCROLLLOCK', 78, 0xFF14],
  ['KP_7', 79, 0xFFB7], ['KP_8', 80, 0xFFB8], ['KP_9', 81, 0xFFB9], ['KP_MINUS', 82, 0xFFAD],
  ['KP_4', 83, 0xFFB4], ['KP_5', 84, 0xFFB5], ['KP_6', 85, 0xFFB6], ['KP_PLUS', 86, 0xFFAB],
  ['KP_1', 87, 0xFFB1], ['KP_2', 88, 0xFFB2], ['KP_3', 89, 0xFFB3], ['KP_0', 90, 0xFFB0],
  ['KP_PERIOD', 91, 0xFFAE], ['F11', 95, 0xFFC8], ['F12', 96, 0xFFC9],
  ['KP_ENTER', 104, 0xFF8D], ['RCTRL', 105, 0xFFE4], ['KP_DIVIDE', 106, 0xFFAF],
  ['PRINTSCREEN', 107, 0xFF61], ['RALT', 108, 0xFFEA],
  ['HOME', 110, 0xFF50], ['UP', 111, 0xFF52], ['PAGEUP', 112, 0xFF55], ['LEFT', 113, 0xFF51],
  ['RIGHT', 114, 0xFF53], ['END', 115, 0xFF57], ['DOWN', 116, 0xFF54], ['PAGEDOWN', 117, 0xFF56],
  ['INSERT', 118, 0xFF63], ['DELETE', 119, 0xFFFF], ['PAUSE', 127, 0xFF13],
  ['LGUI', 133, 0xFFEB], ['RGUI', 134, 0xFFEC], ['APPLICATION', 147, 0xFF67],
];

test('every required SDL scancode maps to its canonical evdev keycode', () => {
  const missing = REQUIRED.filter(([name]) => !sdlMap.has(name)).map(([n]) => n);
  assert.deepEqual(missing, [], `sdlScancodeToX11 is missing: ${missing.join(', ')}`);
  const wrong = REQUIRED.filter(([name, kc]) => sdlMap.get(name) !== kc)
    .map(([name, kc]) => `${name}: got ${sdlMap.get(name)}, want ${kc}`);
  assert.deepEqual(wrong, [], `wrong keycodes: ${wrong.join('; ')}`);
});

test('every required keycode carries its canonical keysym', () => {
  const missing = REQUIRED.filter(([, kc]) => !symMap.has(kc)).map(([n, kc]) => `${n}(${kc})`);
  assert.deepEqual(missing, [], `keysymForKeycode is missing: ${missing.join(', ')}`);
  const wrong = REQUIRED.filter(([, kc, sym]) => symMap.get(kc) !== sym)
    .map(([name, kc, sym]) => `${name}(${kc}): got 0x${(symMap.get(kc) || 0).toString(16)}, want 0x${sym.toString(16)}`);
  assert.deepEqual(wrong, [], `wrong keysyms: ${wrong.join('; ')}`);
});

test('invariant: no emitted keycode is left keysym-less (the silent-drop class)', () => {
  const orphans = [...sdlMap.entries()].filter(([, kc]) => kc !== 0 && !symMap.get(kc)).map(([n, kc]) => `${n}->${kc}`);
  assert.deepEqual(orphans, [], `keycodes with NoSymbol (winex11 drops these): ${orphans.join(', ')}`);
});
