import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

test('SDL2 export replaces both Wine ZIP entries and records file provenance', () => {
  const root = mkdtempSync(join(tmpdir(), 'boxedwine-sdl2-'));
  const runtime = join(root, 'runtime');
  const source = join(root, 'source');
  mkdirSync(runtime);
  const payloads = [
    ['amd64', 'x86_64-linux-gnu', 'wine64.zip', Buffer.from('amd64 SDL2 test library')],
    ['i386', 'i386-linux-gnu', 'wine32.zip', Buffer.from('i386 SDL2 test library')],
  ];
  try {
    for (const [arch, triplet, archive, bytes] of payloads) {
      const libraryDir = join(source, arch, 'usr', 'lib', triplet);
      mkdirSync(libraryDir, { recursive: true });
      writeFileSync(join(libraryDir, 'libSDL2-2.0.so.0'), bytes);
      const entries = archive === 'wine64.zip'
        ? [['usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0', Buffer.from('old SDL2')], ['keep.txt', Buffer.from('keep64')]]
        : [['keep.txt', Buffer.from('keep32')]];
      execFileSync('python3', ['-c', String.raw`
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w') as z:
    z.comment = b'keep archive comment'
    z.writestr('keep.txt', ${JSON.stringify(entries.find(([name]) => name === 'keep.txt')[1].toString())})
    ${archive === 'wine64.zip' ? "z.writestr('usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0', b'old SDL2')" : ''}
`, join(runtime, archive)], { stdio: 'pipe' });
    }
    execFileSync('python3', [resolve('tools/browser/stage-sdl2.py'), runtime], {
      env: { ...process.env, BW64_SDL2_SOURCE: source },
      stdio: 'pipe',
    });

    const expected = new Map(payloads.map(([arch, triplet, archive, bytes]) => [
      `${archive}:usr/lib/${triplet}/libSDL2-2.0.so.0`, bytes,
    ]));
    for (const [arch, triplet, archive, bytes] of payloads) {
      const path = `usr/lib/${triplet}/libSDL2-2.0.so.0`;
      execFileSync('python3', ['-c', String.raw`
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    assert z.comment == b'keep archive comment'
    assert z.namelist().count(sys.argv[2]) == 1
    assert z.read(sys.argv[2]) == sys.argv[3].encode()
    assert z.read('keep.txt') == sys.argv[4].encode()
`, join(runtime, archive), path, bytes.toString(), arch === 'amd64' ? 'keep64' : 'keep32'], { stdio: 'pipe' });
    }
    const provenance = JSON.parse(readFileSync(join(runtime, 'sdl2-provenance.json'), 'utf8'));
    assert.deepEqual(provenance.files.map(({ archive, path }) => `${archive}:${path}`).sort(), [...expected.keys()].sort());
    for (const record of provenance.files) {
      const bytes = expected.get(`${record.archive}:${record.path}`);
      assert.equal(record.bytes, bytes.length);
      assert.equal(record.sha256, createHash('sha256').update(bytes).digest('hex'));
    }
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
