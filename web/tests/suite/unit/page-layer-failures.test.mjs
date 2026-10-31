import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

// The page/launcher layer (app shell, file import, archive cache,
// installation replacement) used to swallow its cleanup and persistence
// failures with bare .catch(() => {}) / catch {}: failed rollback
// deletions leaked browser storage invisibly, a refused storage
// persistence request was never mentioned, and a failed settings write
// vanished. These guards pin the reporting so a cleanup cannot quietly
// reintroduce the swallows.
const read = name => readFileSync(new URL('../../../' + name, import.meta.url), 'utf8');
const app = read('app.mjs');
const files = read('files.mjs');
const archive = read('package-archive.mjs');
const installation = read('installation.mjs');

test('page-layer files contain no bare swallowed rejections or empty catches', () => {
  for (const [name, source] of [['app.mjs', app], ['files.mjs', files], ['package-archive.mjs', archive], ['installation.mjs', installation]]) {
    assert.ok(!source.includes('.catch(() => {})'), `${name}: a .catch(() => {}) swallows a failure with no trace`);
    assert.ok(!source.includes('} catch {}'), `${name}: an empty catch {} swallows a failure with no trace`);
  }
});

test('settings and storage-persistence failures are reported', () => {
  assert.ok(app.includes('BW64-FAIL settings-load'), 'corrupt saved settings must be reported');
  assert.ok(app.includes('BW64-FAIL settings-persist'), 'a failed settings write must be reported');
  assert.ok(app.includes('requestPersistentStorage'), 'persistence requests go through the reporting helper');
  assert.ok(app.includes('Storage persistence was not granted'), 'a refused persistence request must reach the visitor');
});

test('cleanup failures are reported with what could not be removed', () => {
  assert.ok(app.includes('forgetCleanup'), 'app teardown/rollback deletions report through forgetCleanup');
  assert.ok(files.includes('BW64-FAIL files-cleanup'), 'import rollback cleanup must be reported');
  assert.ok(archive.includes('BW64-FAIL archive-cache-read'), 'archive cache read failures must be reported');
  assert.ok(archive.includes('BW64-FAIL archive-cleanup'), 'partial-archive cleanup failures must be reported');
  assert.ok(archive.includes('BW64-FAIL archive-remove'), 'archive removal failures must be reported');
  assert.ok(installation.includes('BW64-FAIL installation-cleanup'), 'save-database cleanup failures must be reported');
});

test('graphics tier setup failure during teardown is reported', () => {
  assert.ok(app.includes("log('Graphics tier setup failed: '"), 'a rejected vk tier promise must not be swallowed during closeRuntime');
});

test('a launcher that cannot load its own module says so on the page', () => {
  const index = read('index.html');
  assert.ok(index.includes('__bwBootWatchdogFired'), 'index.html carries the boot watchdog');
  assert.ok(index.includes('The launcher is taking a long time to load'), 'the watchdog leaves a visitor-visible message');
  assert.ok(app.includes('window.__bwAppBooted = true'), 'app.mjs marks a successful boot');
  assert.ok(app.includes('__bwBootWatchdogFired'), 'app.mjs clears the watchdog notice when it boots late');
});
