import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

test('MIT-SHM is not advertised and direct calls fail safely for XPutImage fallback', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const query = src.match(/static void x11_QueryExtension\(CPU\* cpu\) \{[\s\S]*?\n}/)?.[0];
  assert.ok(query);
  assert.match(query, /name == "MIT-SHM"[\s\S]*?EAX = False/);
  for (const name of ['x11_ShmAttach', 'x11_ShmCreateImage', 'x11_ShmDetach', 'x11_ShmPutImage']) {
    const handler = src.match(new RegExp(`static void ${name}\\(CPU\\* cpu\\) \\{[\\s\\S]*?\\n\\}`))?.[0];
    assert.ok(handler, name);
    assert.doesNotMatch(handler, /kpanic/);
    assert.match(handler, /x11LogShmUnavailable\(\)/);
  }
  assert.match(src, /X11 MIT-SHM unavailable: shared mappings are unavailable; use XPutImage/);
});
