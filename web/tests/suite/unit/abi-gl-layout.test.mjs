import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const repo = resolve(import.meta.dirname, '../../../..');
const read = path => readFileSync(resolve(repo, path), 'utf8');
const abiSource = read('source/opengl/gl64bridge_abi.h');
const guestSource = read('tools/rootfs64/libgl64/libgl64.c');
const hostSource = read('source/opengl/gl64bridge.cpp');
const jsSource = read('web/gl-webgpu.mjs');
const bootstrapSource = read('project/emscripten/gl-webgpu-bootstrap.js');

function parseIds(source, label) {
  const enumBody = source.match(/enum\s*\{([\s\S]*?)\};/);
  assert.ok(enumBody, `${label}: function id enum not found`);
  const body = enumBody[1].replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*/g, '');
  const ids = new Map();
  let next = 0;
  for (const statement of body.split(',')) {
    const match = /^\s*(GL64_fn_\w+)\s*(?:=\s*(\d+))?\s*$/.exec(statement);
    if (!match) continue;
    if (match[1] === 'GL64_fn__MAX') continue;
    const id = match[2] === undefined ? next : Number(match[2]);
    assert.ok(!ids.has(match[1]), `${label}: duplicate function id ${match[1]}`);
    assert.ok(![...ids].some(([, previous]) => previous === id), `${label}: duplicate numeric id ${id}`);
    ids.set(match[1], id);
    next = id + 1;
  }
  return ids;
}

const hostIds = parseIds(abiSource, 'host ABI');
const guestIds = parseIds(guestSource, 'guest libGL');

test('guest GL shim function names and numeric ids match the host ABI', () => {
  assert.equal(hostIds.size, 322, 'keep the full append-only GL ABI inventory');
  assert.deepEqual([...guestIds], [...hostIds], 'guest enum ids must stay byte-for-byte aligned with host ids');
});

test('every GL ABI id has a host switch handler', () => {
  const handlers = new Set([...hostSource.matchAll(/case\s+(GL64_fn_\w+)\s*:/g)].map(match => match[1]));
  const missing = [...hostIds.keys()].filter(name => !handlers.has(name));
  assert.deepEqual(missing, [], `GL ABI ids without host dispatch handlers: ${missing.join(', ')}`);
});

test('the WebGPU GL JavaScript adapter is exported and installed under the host bridge hook', () => {
  assert.match(hostSource, /globalThis\.bwGlCreateContext/);
  assert.match(bootstrapSource, /globalThis\.bwGlCreateContext\s*=\s*adapter\.createWebGPUGLContext/);
  assert.match(jsSource, /export function createWebGPUGLContext\(/);
});
