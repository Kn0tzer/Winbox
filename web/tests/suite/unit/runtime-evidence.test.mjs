import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, writeFile, rm } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { runtimeCodeHash } from '../lib/runtime-evidence.mjs';
test('runtime evidence changes with renderer or compiler code and survives relocation', async () => {
  const a = await mkdtemp(join(tmpdir(), 'bw64-code-a-'));
  const b = await mkdtemp(join(tmpdir(), 'bw64-code-b-'));
  try {
    for (const root of [a,b]) {
      await mkdir(join(root,'vendor'));
      await writeFile(join(root,'renderer.mjs'),'export const version = 1;');
      await writeFile(join(root,'vendor/compiler.wasm'),new Uint8Array([0,97,115,109]));
    }
    const original = await runtimeCodeHash(a);
    assert.equal(await runtimeCodeHash(b),original);
    await writeFile(join(a,'renderer.mjs'),'export const version = 2;');
    assert.notEqual(await runtimeCodeHash(a),original);
    await writeFile(join(b,'vendor/compiler.wasm'),new Uint8Array([0,97,115,109,1]));
    assert.notEqual(await runtimeCodeHash(b),original);
    const beforeDesktopChange = await runtimeCodeHash(a);
    await writeFile(join(a, 'desktop-pin-provenance.json'), '{"helper_sha256":"changed"}');
    assert.notEqual(await runtimeCodeHash(a), beforeDesktopChange);
  } finally { await Promise.all([rm(a,{recursive:true,force:true}),rm(b,{recursive:true,force:true})]); }
});
