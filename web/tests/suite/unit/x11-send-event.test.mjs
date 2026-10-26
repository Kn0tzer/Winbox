import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

test('XSendEvent resolves special windows and propagates to the first selecting ancestor', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const handler = src.match(/static void x11_SendEvent\(CPU\* cpu\) \{[\s\S]*?\n}/)?.[0];
  assert.ok(handler);
  assert.doesNotMatch(handler, /kpanic/);
  assert.match(handler, /windowId == PointerWindow\) window = server->pointerWindow/);
  assert.match(handler, /windowId == InputFocus\) window = server->inputFocus/);
  assert.match(handler, /for \(XWindowPtr recipient = window; recipient; recipient = recipient->getParent\(\)\)/);
  assert.match(handler, /if \(server->iterateEventMask\(recipient->id, event_mask[\s\S]*?\)\) break/);
  const server = readFileSync(new URL('../../../../source/x11/xserver.cpp', import.meta.url), 'utf8');
  assert.match(server, /bool XServer::iterateEventMask[\s\S]*?return matched;/);
});

test('XPutBackEvent reads the event from the second argument and resolves the display by address', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const handler = src.match(/static void x11_PutBackEvent\(CPU\* cpu\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(handler);
  assert.doesNotMatch(handler, /kpanic/);
  // tools/x11/X11.c passes (display, event) via CALL_2_R: the event is ARG2,
  // and ARG1 is a guest Display* address, not an internal display id.
  assert.match(handler, /getDisplayDataByAddressOfDisplay\(memory, ARG1\)/);
  assert.doesNotMatch(handler, /getDisplayDataById\(ARG1\)/);
  assert.match(handler, /memory->memcpy\(&event, ARG2, sizeof\(XEvent\)\)/);
  assert.match(handler, /putEvent\(event, true\)/);
});

test('event marshalling copies guest bytes through KMemory in both event handlers', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  for (const name of ['x11_PutBackEvent', 'x11_SendEvent']) {
    const handler = src.match(new RegExp(`static void ${name}\\(CPU\\* cpu\\) \\{[\\s\\S]*?\\n\\}`))?.[0];
    assert.ok(handler, name);
    assert.doesNotMatch(handler, /oops/);
    assert.doesNotMatch(handler, /UNALIGNED_MEMORY/);
    assert.match(handler, /memory->memcpy\(&event, ARG[0-9], sizeof\(XEvent\)\)/);
  }
});
