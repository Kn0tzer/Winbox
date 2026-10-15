import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { portalMenuText } from '../../portal-ocr.mjs';

test('slow OCR returns by its deadline while the asset server event loop keeps running', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'bw64-ocr-'));
  let timer;
  try {
    const fixture = join(dir, 'slow.mjs');
    await writeFile(fixture, 'setTimeout(() => console.log("NEW GAME"), 1000);');
    let ticked = false;
    timer = setTimeout(() => { ticked = true; }, 10);
    const result = await portalMenuText('frame.png', { timeoutMs: 100, executable: process.execPath, prefixArgs: [fixture] });
    assert.equal(result.timedOut, true, 'OCR must stop a slow child instead of waiting for it');
    assert.equal(result.text, '');
    assert.equal(ticked, true, 'HTTP callbacks must run while OCR is pending');
    assert.throws(() => process.kill(result.childPid, 0), { code: 'ESRCH' });
  } finally { clearTimeout(timer); await rm(dir, { recursive: true, force: true }); }
});

test('successful OCR preserves menu text and missing executables remain errors', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'bw64-ocr-'));
  try {
    const fixture = join(dir, 'menu.mjs');
    await writeFile(fixture, 'console.log("NEW GAME\\nOPTIONS\\nQUIT");');
    const result = await portalMenuText('frame.png', { executable: process.execPath, prefixArgs: [fixture] });
    assert.equal(result.timedOut, false);
    assert.match(result.text, /NEW GAME\nOPTIONS\nQUIT/);
    await assert.rejects(portalMenuText('frame.png', { executable: join(dir, 'absent') }), { code: 'ENOENT' });
  } finally { await rm(dir, { recursive: true, force: true }); }
});
