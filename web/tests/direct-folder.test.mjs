import test from 'node:test';
import assert from 'node:assert/strict';
import { ZipReader, BlobReader, BlobWriter } from '../vendor/zip/index-native.js';
import { virtualArchive, readDirectory, requestDirectoryAccess, packageRecord } from '../direct-folder.mjs';

const entry = (path, value) => ({ path, size: value.length, blob: new Blob([value]), lastModified: 1 });
test('direct folders produce valid ZIP ranges including boundaries and CRCs', async () => {
  const files = [entry('nested/hello.txt', 'hello'), entry('empty', ''), entry('unicode/é.txt', 'world')];
  const archive = await virtualArchive({ id: 'test', files });
  const all = await archive.read(0, archive.bytes - 1);
  for (const [start, end] of [[0, 9], [26, 70], [all.length - 22, all.length - 1]])
    assert.deepEqual(await archive.read(start, end), all.slice(start, end + 1));
  const reader = new ZipReader(new BlobReader(new Blob([all])), { useWebWorkers: false });
  const zipped = await reader.getEntries();
  assert.deepEqual(zipped.map(e => e.filename), files.map(f => 'home/username/apps/' + f.path));
  for (const [i, file] of zipped.entries())
    assert.equal(await (await file.getData(new BlobWriter(), { checkSignature: true })).text(), await files[i].blob.text());
  await reader.close();
  await assert.rejects(archive.read(-1, 1), /range/i);
  await assert.rejects(archive.read(0, archive.bytes), /range/i);
});
test('cached CRCs avoid scanning unchanged source data; payload is excluded from saved records', async () => {
  const files = [entry('game.exe', 'payload')];
  const archive = await virtualArchive({ id: 'test', files });
  const record = packageRecord({ mode: 'direct', files, directory: {}, id: 'test' });
  assert.equal(record.files[0].blob, undefined);
  assert.equal(typeof record.files[0].crc32, 'number');
  files[0].blob = { size: files[0].size, slice() { throw Error('Unexpected rescan'); } };
  assert.equal((await virtualArchive({ id: 'test', files })).bytes, archive.bytes);
});
test('directory permission is read-only and revocation is actionable', async () => {
  const calls = [];
  const directory = {
    queryPermission: async options => { calls.push(options); return 'prompt'; },
    requestPermission: async options => { calls.push(options); return 'granted'; },
  };
  await requestDirectoryAccess(directory);
  assert.deepEqual(calls, [{ mode: 'read' }, { mode: 'read' }]);
  directory.requestPermission = async () => 'denied';
  await assert.rejects(requestDirectoryAccess(directory), /permission|allow/i);
});
test('directory scans reject Windows collisions and preserve only unchanged CRC metadata', async () => {
  const file = new File(['hi'], 'a.exe', { lastModified: 4 });
  const directory = { async *values() { yield { kind: 'file', name: 'a.exe', getFile: async () => file }; } };
  const old = [{ path: 'a.exe', size: 2, lastModified: 4, crc32: 17 }];
  assert.equal((await readDirectory(directory, { previous: old }))[0].crc32, 17);
  assert.equal((await readDirectory(directory, { previous: [{ ...old[0], lastModified: 3 }] }))[0].crc32, undefined);
  directory.values = async function* () {
    yield { kind: 'file', name: 'a.exe', getFile: async () => file };
    yield { kind: 'file', name: 'A.exe', getFile: async () => file };
  };
  await assert.rejects(readDirectory(directory), /collision/i);
});
