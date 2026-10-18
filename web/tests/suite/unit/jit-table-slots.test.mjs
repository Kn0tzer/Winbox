import { readFileSync } from 'node:fs';
import { Script, createContext } from 'node:vm';
import assert from 'node:assert/strict';
import { test } from 'node:test';

// Execute the real EM_JS reservation body against real, independent Wasm
// tables sharing one linear-memory counter (the pthread ownership model).
const source = readFileSync(new URL('../../../../source/util/bwjittable.cpp', import.meta.url), 'utf8');
const body = source.match(/EM_JS\(int, bw_jit_reserve_slots_impl,[\s\S]*?, \{([\s\S]*?)\n\}\);/)[1];
function worker(buffer, initial = 8, maximum = 100) {
  const table = new WebAssembly.Table({ element: 'anyfunc', initial, maximum });
  const context = createContext({ HEAPU8: new Uint8Array(buffer), wasmTable: table,
    Int32Array, Atomics, SharedArrayBuffer });
  const reserve = new Script(`(function(counter,count) {${body}\n})`).runInContext(context);
  return { table, reserve: count => reserve(0, count) };
}

test('backends on independent workers reserve disjoint table ranges', () => {
  const memory = new SharedArrayBuffer(64);
  const guest32 = worker(memory), guest64 = worker(memory);
  assert.equal(guest32.reserve(3), 8);
  assert.equal(guest64.reserve(1), 11);
  assert.equal(guest32.reserve(2), 12);
  assert.equal(guest64.reserve(2), 14);
  assert.equal(guest32.table.length, 14);
  assert.equal(guest64.table.length, 16);
});

test('a failed table grow never reuses another worker reservation', () => {
  const memory = new SharedArrayBuffer(64);
  const full = worker(memory, 8, 8), other = worker(memory);
  assert.equal(full.reserve(1), -1);
  assert.equal(other.reserve(1), 9);
  assert.equal(full.reserve(0), -1);
  assert.equal(other.reserve(2), 10);
});

test('single-threaded runtimes use the same monotonic contract', () => {
  const { reserve, table } = worker(new ArrayBuffer(64));
  assert.equal(reserve(2), 8);
  assert.equal(reserve(1), 10);
  assert.equal(table.length, 11);
});
