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
