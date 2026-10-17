import test from 'node:test';
import assert from 'node:assert/strict';
import { fitDisplay } from '../presentation.mjs';

test('canvas fills matching runtime bounds without a status-row offset', () => {
  assert.deepEqual(fitDisplay(1200, 750, 1024, 640), { left: 0, top: 0, width: 1200, height: 750 });
});
test('fullscreen centers letterboxing and preserves the guest coordinate scale', () => {
  const box = fitDisplay(1920, 1080, 800, 600);
  assert.deepEqual(box, { left: 240, top: 0, width: 1440, height: 1080 });
  assert.equal((960 - box.left) * 800 / box.width, 400);
  assert.equal((540 - box.top) * 600 / box.height, 300);
  assert.deepEqual(fitDisplay(640, 600, 1024, 640), { left: 0, top: 100, width: 640, height: 400 });
});
