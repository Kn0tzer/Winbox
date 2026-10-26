import test from 'node:test';
import assert from 'node:assert/strict';
import { scanSilentSource } from '../lib/silent-failure-scan.mjs';

const file = 'source/kernel/syscall64.cpp';
test('comments and strings cannot satisfy the silent-branch logging guard', () => {
  for (const fakeLog of ['// klog("ignored");\n', '/* klog("ignored"); */', 'const char* text = "klog(ignored)";']) {
    const result = scanSilentSource(`switch (n) { default: ${fakeLog} return -ENOSYS; }`, file);
    assert.equal(result.unloggedDefault, 1, fakeLog);
    assert.equal(result.unloggedErrno, 1, fakeLog);
  }
  assert.deepEqual(scanSilentSource('switch (n) { default: klog("unsupported"); return -EINVAL; }', file),
    { emptyCatch: 0, unloggedDefault: 0, unloggedErrno: 0 });
});

test('literal comment delimiters do not hide real empty catches or invent failures', () => {
  const result = scanSilentSource('const char* url="https://host/"; try {} catch (e) {}', file);
  assert.equal(result.emptyCatch, 1);
  assert.deepEqual(scanSilentSource('/* catch {} return -EINVAL; */ const char* s="catch {}";', file),
    { emptyCatch: 0, unloggedDefault: 0, unloggedErrno: 0 });
});

test('nested switch defaults and multiline returns are scanned', () => {
  const source = 'switch (a) { case 1: switch (b) { default: break; } break; default: return\n -ENOSYS; }';
  assert.equal(scanSilentSource(source, file).unloggedDefault, 2);
  assert.equal(scanSilentSource(source, file).unloggedErrno, 1);
});
