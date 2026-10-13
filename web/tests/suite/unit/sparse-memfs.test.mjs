import test from 'node:test';
import assert from 'node:assert/strict';
import '../../../sparse-memfs.js';

test('Wine mapping holes stay sparse and preserve data across writes and truncation', () => {
  let ordinaryCalls = 0;
  const stock = () => { ordinaryCalls++; return 123; };
  const MEMFS = { resizeFileStorage: stock, stream_ops: { read: stock, write: stock, mmap: stock },
    ops_table: { file: { stream: { read: stock, write: stock, mmap: stock } } } };
  globalThis.bwInstallSparseTmpMappings({ getPath: node => node.path }, MEMFS);
  const node = { path: '/root/tmp/.wine-1/server32-100-7cb/tmpmap-test', usedBytes: 0, contents: null };
  const stream = { node }, ops = MEMFS.ops_table.file.stream;
  MEMFS.resizeFileStorage(node, 64 * 1024 * 1024);
  assert.equal(node.contents, null);
  assert.equal(node.usedBytes, 64 * 1024 * 1024);
  const data = new Uint8Array([7, 8, 9, 10]);
  assert.equal(ops.write(stream, data, 0, 4, 4094), 4);
  const out = new Uint8Array(8).fill(255);
  assert.equal(ops.read(stream, out, 0, 8, 4092), 8);
  assert.deepEqual([...out], [0, 0, 7, 8, 9, 10, 0, 0]);
  MEMFS.resizeFileStorage(node, 4096);
  MEMFS.resizeFileStorage(node, 64 * 1024 * 1024);
  ops.read(stream, out, 0, 8, 4092);
  assert.deepEqual([...out], [0, 0, 7, 8, 0, 0, 0, 0]);
  assert.equal(ordinaryCalls, 0);
  assert.equal(ops.write({ node: { path: '/root/ordinary.txt' } }, data, 0, 4, 0), 123);
  assert.equal(ordinaryCalls, 1);
});
