// web/tests/suite/unit/host-case-symlink-probe.test.mjs — host-backed case
// and symlink semantics, probed on the real host filesystem and through
// the browser import layer that publishes host files to the guest:
//
// - On this (case-sensitive) host, two files whose names differ only by
//   case coexist with distinct contents; a symlink reads through to its
//   target's contents. Those are the host facts the guest layer sits on.
// - The guest filesystem is case-insensitive, so the import layer must
//   refuse to publish both case variants (checkEntries), which it does.
// - A picked symlink arrives from the browser as an ordinary File with
//   the link's name and the target's bytes (browsers resolve links before
//   handing files to the page); fromFiles keeps the link name and size.

import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, symlinkSync, readFileSync, lstatSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { checkEntries, fromFiles } from '../../../../web/files.mjs';

test('host facts: case variants coexist distinctly and symlinks read through', () => {
  const dir = mkdtempSync(join(tmpdir(), 'bw64-host-case-'));
  try {
    writeFileSync(join(dir, 'Game.EXE'), 'upper-content');
    writeFileSync(join(dir, 'game.exe'), 'lower-content');
    // if the host were case-insensitive the second write would have
    // replaced the first file's contents
    assert.equal(readFileSync(join(dir, 'Game.EXE'), 'utf8'), 'upper-content');
    assert.equal(readFileSync(join(dir, 'game.exe'), 'utf8'), 'lower-content');
    symlinkSync(join(dir, 'Game.EXE'), join(dir, 'link.exe'));
    assert.ok(lstatSync(join(dir, 'link.exe')).isSymbolicLink());
    assert.equal(readFileSync(join(dir, 'link.exe'), 'utf8'), 'upper-content');
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('the import layer refuses to publish both case variants into the case-insensitive guest', () => {
  assert.throws(
    () => checkEntries([{ path: 'dir/Game.EXE', size: 1 }, { path: 'dir/game.exe', size: 1 }]),
    /Windows filename collision/);
  // distinct names, and a file that is not also a directory prefix, pass
  assert.equal(checkEntries([{ path: 'dir/Game.EXE', size: 1 }, { path: 'dir/data.wad', size: 2 }]), 3);
  assert.throws(
    () => checkEntries([{ path: 'dir', size: 1 }, { path: 'dir/Game.EXE', size: 1 }]),
    /File\/directory collision/);
});

test('a picked symlink is published under the link name with the target size', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'bw64-host-link-'));
  try {
    writeFileSync(join(dir, 'real.exe'), 'target-bytes');
    symlinkSync(join(dir, 'real.exe'), join(dir, 'link.exe'));
    // browsers resolve the link before the page sees it: the File carries
    // the link's name and the target's contents
    const picked = new File([readFileSync(join(dir, 'link.exe'))], 'link.exe');
    const entries = await fromFiles([picked]);
    assert.equal(entries.length, 1);
    assert.equal(entries[0].path, 'link.exe');
    assert.equal(entries[0].size, 'target-bytes'.length);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
