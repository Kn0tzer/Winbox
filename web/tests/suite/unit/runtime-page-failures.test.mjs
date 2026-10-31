import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

// web/runtime.html is the host page inside the player frame. Its top-level
// awaits used to reject into a lone unhandledrejection log line while the
// visitor watched an endless "Getting things ready" spinner, and its vk
// tier application swallowed every per-frame rejection. These guards pin
// the loud-failure wiring so a cleanup cannot quietly remove it.
const source = readFileSync(new URL('../../../runtime.html', import.meta.url), 'utf8');

test('runtime page has no bare swallowed promise rejections', () => {
  assert.ok(!source.includes('.catch(() => {})'), 'a .catch(() => {}) swallows a failure with no trace');
  assert.ok(!source.includes('.catch(function () {})'), 'a .catch(function () {}) swallows a failure with no trace');
});

test('runtime page turns required load failures into a visitor-facing error', () => {
  assert.match(source, /send\('error', 'Your saved folder could not be loaded from browser storage/);
  assert.match(source, /bwDependencySpec = await loadDependencies[\s\S]{0,700}?send\('error'/);
});

test('runtime page reports degraded and per-frame failures instead of dropping them', () => {
  assert.ok(source.includes("'runtime.network-load'"), 'network module load failure must be reported');
  assert.ok(source.includes("'runtime.adapter-probe'"), 'adapter capability probe failure must be reported');
  assert.ok(source.includes("'runtime.vk-apply'"), 'vk tier frame/chunk failures must be reported');
});
