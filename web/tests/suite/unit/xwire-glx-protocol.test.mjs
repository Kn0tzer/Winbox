import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

test('GLX server-string queries return length-framed strings and unsupported minors return BadRequest', () => {
  const src = readFileSync(new URL('../../../../source/x11wire/xwireconnection.cpp', import.meta.url), 'utf8');
  const glx = src.match(/case GLX_MAJOR_OPCODE: \{[\s\S]*?\n        case RANDR_MAJOR_OPCODE:/)?.[0];
  assert.ok(glx);
  assert.match(glx, /case X_GLXQueryExtensionsString:[\s\S]*?case X_GLXQueryServerString:/);
  assert.match(glx, /memcpy\(r\.data\(\) \+ 8, &textLen, 4\)/);
  assert.match(glx, /memcpy\(r\.data\(\) \+ 32, value\.data\(\), textLen\)/);
  assert.match(glx, /GLX request minor[\s\S]*?sendError\(BadRequest, sequence, minor, opcode, minor\)/);
  assert.doesNotMatch(glx, /stub-reply|empty 32-byte/);
});
