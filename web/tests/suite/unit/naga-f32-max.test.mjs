import test from 'node:test';
import assert from 'node:assert/strict';
import { createShaderCache } from '../../../vkwebgpu.mjs';

test('Naga FLT_MAX spellings retain exact binary32 values without hiding overflow', async () => {
  const source = 'let a=340282350000000000000000000000000000000f; let b=-3.4028235e38f; let c=3.5e38f; let d=1.5f;';
  const cache = createShaderCache({ translate: async () => source });
  const code = await cache.get('vertex', { hash: 'max', size: 20, code: new Uint8Array(20) });
  assert.match(code, /a=bitcast<f32>\(0x7f7fffffu\)/);
  assert.match(code, /b=bitcast<f32>\(0xff7fffffu\)/);
  assert.match(code, /c=3\.5e38f/);
  assert.match(code, /d=1\.5f/);
});
