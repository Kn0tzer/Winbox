import test from 'node:test';
import assert from 'node:assert/strict';
import { startupProgress } from '../startup.mjs';
test('startup describes the stage before the first frame', () => {
  const progress = startupProgress('Running...', 1000, false, 2000, 0);
  assert.match(progress.detail, /first frame/i);
  assert.equal(progress.activity, '');
});
test('reassurance requires more than fifteen seconds and recent logs', () => {
  assert.equal(startupProgress('Running...', 15000, false, 15000, 0).activity, '');
  assert.match(startupProgress('Running...', 16000, false, 16001, 0).activity, /Not frozen, still loading/);
  assert.doesNotMatch(startupProgress('Running...', 12000, false, 16001, 0).activity, /Not frozen/);
  assert.match(startupProgress('Running...', 12000, false, 16001, 0).activity, /logs.*4 seconds/i);
});
test('visible frames clear startup reassurance and downloads retain progress', () => {
  assert.equal(startupProgress('Running...', 16000, true, 16001, 0).activity, '');
  assert.equal(startupProgress('Downloading runtime: 25%', 16000, false, 16001, 0).detail, 'Downloading runtime: 25%');
  assert.match(startupProgress('Loading rootfs 70%', null, false, 5000, 0).title, /Windows files/);
});
