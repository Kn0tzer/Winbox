import test from 'node:test';
import assert from 'node:assert/strict';
import { rawEvents, rawEventAfter } from '../lib/raw-input-evidence.mjs';

const packet = (sequence, dx = 50, dy = 0) => `log: input-witness: RAW seq=${sequence} flags=0 dx=${dx} dy=${dy}`;

test('late copies of delivered input cannot acknowledge the next movement', () => {
  const next = rawEventAfter(7, 50, 0);
  assert.equal(next.test(packet(7)), false);
  assert.equal(next.test('log: [guest fd=2] ' + packet(7)), false);
  assert.equal(next.test(packet(8, 4, 23)), false);
  assert.equal(next.test(packet(9)), true);
});

test('packet totals count each delivered sequence once across duplicate logs', () => {
  const lines = Array.from({ length: 10 }, (_, i) => packet(i + 3));
  const duplicated = lines.flatMap(line => [line, 'log: [guest fd=2] ' + line]);
  assert.equal(rawEvents(duplicated).length, 10);
  assert.equal(rawEvents(duplicated).reduce((sum, event) => sum + event.dx, 0), 500);
});

test('a burst boundary excludes late copies of earlier reset packets', () => {
  const lines = [packet(13, -500), packet(14, 500), packet(13, -500), packet(14, 500)];
  const burst = rawEvents(lines).filter(event => event.sequence > 13);
  assert.equal(burst.reduce((sum, event) => sum + event.dx, 0), 500);
});
