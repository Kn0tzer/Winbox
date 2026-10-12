import test from 'node:test';
import assert from 'node:assert/strict';
import { hasPortalMenu, summarizeRenderedFrames } from '../../portal-evidence.mjs';

test('a black/loading frame or one stray menu word cannot prove the Portal menu', () => {
  for (const text of ['', 'Running...', 'litPixels=786432', 'OPTIONS', 'New Game'])
    assert.equal(hasPortalMenu(text), false);
  assert.equal(hasPortalMenu('PORTAL\nNEW GAME\nLOAD GAME\nOPTIONS\nQUIT'), true);
  assert.equal(hasPortalMenu('New\nGame\nOptions\nQuit'), true);
});

test('FPS uses the full menu observation window and excludes boot/rejected frames', () => {
  const stats = summarizeRenderedFrames([
    {start: -500, end: -400}, {start: 1000, end: 1010},
    {start: 2000, end: 2020}, {start: 4000, end: 4030},
    {start: 59000, end: 61000}, {start: 6000, end: NaN},
  ], 0, 60000);
  assert.equal(stats.renderedFrames, 3);
  assert.equal(stats.renderedFps, 0.05);
  assert.deepEqual(stats.renderTimeMs, {mean: 20, median: 20, p95: 30});
  assert.deepEqual(stats.frameIntervalMs, {median: 1010, p95: 2010});
  assert.equal(summarizeRenderedFrames([], 0, 60000).renderedFps, 0);
  assert.throws(() => summarizeRenderedFrames([], 1000, 1000), /positive time window/);
});
