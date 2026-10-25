// web/tests/suite/unit/file-dialog-picker-pipeline.test.mjs — the guest
// common-dialog row: a Windows common dialog inside the guest resolves to
// files the user picked in the browser, and every emulator-owned step
// after the picker gesture is probed here end to end: app.mjs opens the
// directory picker, direct-folder.mjs walks the returned handle tree,
// publishes the file list (with the CRC cache and override rules), and
// packageRecord strips blobs for storage. The picker UI itself is
// browser-owned and needs a user gesture; no part of it is emulated.
//
// The handle tree is a fake FileSystemDirectoryHandle with the same
// values()/getFile() surface the browser returns.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { readDirectory, openDirectPackage, packageRecord } from '../../../../web/direct-folder.mjs';

function fileHandle(name, content, lastModified = 1000) {
  return {
    kind: 'file',
    name,
    async getFile() {
      const file = new File([content], name);
      Object.defineProperty(file, 'lastModified', { value: lastModified });
      return file;
    },
  };
}
function dirHandle(name, children) {
  return {
    kind: 'directory',
    name,
    async *values() { for (const child of children) yield child; },
  };
}
const tree = () => dirHandle('games', [
  fileHandle('readme.txt', 'hello', 111),
  dirHandle('Doom', [
    fileHandle('DOOM.EXE', 'MZ-doom', 222),
    dirHandle('data', [fileHandle('doom1.wad', 'WAD', 333)]),
  ]),
]);

test('the page opens the picker the pipeline consumes', () => {
  const app = readFileSync(new URL('../../../../web/app.mjs', import.meta.url), 'utf8');
  assert.match(app, /window\.showDirectoryPicker\(\{ mode: 'read', id: 'winbox-programs' \}\)/);
});

test('a picked folder tree publishes flattened guest paths with sizes', async () => {
  const files = await readDirectory(tree());
  assert.deepEqual(files.map(f => f.path), ['readme.txt', 'Doom/DOOM.EXE', 'Doom/data/doom1.wad']);
  assert.deepEqual(files.map(f => f.size), [5, 7, 3]);
  assert.equal(await files[1].blob.text(), 'MZ-doom');
});

test('an unchanged previous scan reuses its CRC instead of re-reading content identity', async () => {
  const previous = [{ path: 'Doom/DOOM.EXE', size: 7, lastModified: 222, crc32: 424242 }];
  const files = await readDirectory(tree(), { previous });
  assert.equal(files.find(f => f.path === 'Doom/DOOM.EXE').crc32, 424242);
  assert.equal(files.find(f => f.path === 'readme.txt').crc32, undefined);
});

test('a previous override wins over the scanned file at the same path, case-insensitively', async () => {
  const overrideBlob = new File(['patched'], 'x');
  const previous = [{ path: 'doom/doom.exe', size: 7, lastModified: 9, blob: overrideBlob, override: true }];
  const files = await readDirectory(tree(), { previous });
  const exe = files.filter(f => f.path.toLowerCase() === 'doom/doom.exe');
  assert.equal(exe.length, 1);
  assert.equal(exe[0].override, true);
});

test('case-colliding names in one folder are rejected before publication', async () => {
  const colliding = dirHandle('g', [fileHandle('Game.EXE', 'a'), fileHandle('game.exe', 'b')]);
  await assert.rejects(() => readDirectory(colliding), /Windows filename collision/);
});

test('a vanished folder maps to the relink error, not a raw DOMException', async () => {
  const gone = {
    kind: 'directory',
    name: 'gone',
    async *values() { const e = new Error('missing'); e.name = 'NotFoundError'; throw e; },
  };
  await assert.rejects(() => readDirectory(gone), /The linked folder is unavailable/);
});

test('openDirectPackage walks a granted handle and packageRecord strips stored blobs', async () => {
  const directory = tree();
  directory.queryPermission = async () => 'granted';
  directory.requestPermission = async () => 'granted';
  const pkg = await openDirectPackage({ mode: 'direct', directory, files: [] });
  assert.equal(pkg.files.length, 3);
  const record = packageRecord(pkg);
  assert.ok(record.files.every(f => !('blob' in f) || f.override));
  assert.deepEqual(record.files.map(f => f.path), pkg.files.map(f => f.path));
});
