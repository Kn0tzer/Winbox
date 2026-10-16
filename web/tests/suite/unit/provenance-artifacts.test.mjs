// web/tests/suite/unit/provenance-artifacts.test.mjs — Build provenance guards (P2).
//
// Every probe/test must assert build provenance before trusting its result.
// Past failures: stale dist/runtime/boxedwine64.js + new .wasm →
// __emscripten_receive_on_main_thread_js: func is not a function (Oct 5/8/9);
// fix staged in web/ but probes serve from dist/ (blank canvas); dist/ vs
// web/runtime/ wine32.zip drifting (binary-patched vs pristine); probe
// attributing failure to source when testing stale binary (D3D9 black
// triangle from pre-88cdd449 wasm; D3D12 "regression" was stale .so).
//
// tools/bw64-build.sh + BUILD_INFO.json exist for exactly this.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync, statSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const sha256File = (p) => createHash('sha256').update(readFileSync(p)).digest('hex');

test('BUILD_INFO.json exists in both dist/runtime and web/runtime', () => {
  for (const d of ['dist/runtime/BUILD_INFO.json', 'web/runtime/BUILD_INFO.json']) {
    const p = join(repoRoot, d);
    assert.ok(existsSync(p), `${d} must exist (bw64-build.sh writes provenance)`);
    const j = JSON.parse(readFileSync(p, 'utf8'));
    assert.ok(j.git_sha && j.git_sha.length >= 7, `${d}: git_sha must be recorded`);
    assert.ok(j.wasm_sha256 && j.wasm_sha256.length === 64, `${d}: wasm_sha256 must be recorded`);
    assert.ok(j.js_sha256 && j.js_sha256.length === 64, `${d}: js_sha256 must be recorded`);
    assert.ok(j.wasm_bytes > 1000000, `${d}: wasm_bytes must be plausible (>1MB)`);
  }
});

test('wasm/js pair hashes match BUILD_INFO.json (no torn builds)', () => {
  for (const d of ['dist/runtime', 'web/runtime']) {
    const info = JSON.parse(readFileSync(join(repoRoot, d, 'BUILD_INFO.json'), 'utf8'));
    const wasmHash = sha256File(join(repoRoot, d, 'boxedwine64.wasm'));
    const jsHash = sha256File(join(repoRoot, d, 'boxedwine64.js'));
    assert.equal(wasmHash, info.wasm_sha256, `${d}: boxedwine64.wasm hash must match BUILD_INFO.json (torn build?)`);
    assert.equal(jsHash, info.js_sha256, `${d}: boxedwine64.js hash must match BUILD_INFO.json (JS/WASM pair drift → func is not a function)`);
  }
});

test('dist/runtime and web/runtime wasm/js pairs are in sync', () => {
  const distWasm = sha256File(join(repoRoot, 'dist/runtime/boxedwine64.wasm'));
  const webWasm = sha256File(join(repoRoot, 'web/runtime/boxedwine64.wasm'));
  assert.equal(distWasm, webWasm, 'dist vs web boxedwine64.wasm must match (d01c6f5a: fix staged in web/ but probes serve dist/ → blank canvas)');
  const distJs = sha256File(join(repoRoot, 'dist/runtime/boxedwine64.js'));
  const webJs = sha256File(join(repoRoot, 'web/runtime/boxedwine64.js'));
  assert.equal(distJs, webJs, 'dist vs web boxedwine64.js must match (stale JS + new WASM → __emscripten_receive_on_main_thread_js crash)');
});

test('Wine archives match their bases and prefix differences contain only the versioned desktop helpers', () => {
  for (const z of ['wine32.zip', 'wine64.zip', 'prefix32.zip', 'prefix64.zip']) {
    const a = join(repoRoot, 'dist/runtime', z);
    const b = join(repoRoot, 'web/runtime', z);
    if (!existsSync(a) || !existsSync(b)) continue;
    if (z.startsWith('wine')) assert.equal(sha256File(a), sha256File(b), `${z}: dist vs web must match`);
    else execFileSync('python3', ['-c', `
import pathlib, sys, zipfile, json, hashlib
exported, base, root, arch = map(str, sys.argv[1:])
helper = 'deskpin32.exe' if arch == '32' else 'deskpin.exe'
paths = {'home/username/' + helper}
if arch == '64': paths.add('home/username/.wine/drive_c/' + helper)
expected = (pathlib.Path(root) / 'tools/rootfs64/gltest' / helper).read_bytes()
with zipfile.ZipFile(exported) as actual, zipfile.ZipFile(base) as original:
 assert set(actual.namelist()) == set(original.namelist()) | paths
 assert len(actual.namelist()) == len(set(actual.namelist())), 'duplicate zip members'
 for name in actual.namelist():
  assert actual.read(name) == (expected if name in paths else original.read(name)), name
provenance = json.loads((pathlib.Path(exported).parent / 'desktop-pin-provenance.json').read_text())[arch]
assert provenance['helper_sha256'] == hashlib.sha256(expected).hexdigest()
assert provenance['prefix_sha256'] == hashlib.sha256(pathlib.Path(exported).read_bytes()).hexdigest()
`, a, b, repoRoot, z.includes('32') ? '32' : '64'], { stdio: 'pipe' });
  }
});

test('wasm has valid magic (not a truncated/poisoned build)', () => {
  for (const d of ['dist/runtime/boxedwine64.wasm', 'web/runtime/boxedwine64.wasm']) {
    const buf = readFileSync(join(repoRoot, d));
    assert.equal(buf[0], 0x00, `${d}: WASM magic byte 0`);
    assert.equal(buf[1], 0x61, `${d}: WASM magic byte 1 ('a')`);
    assert.equal(buf[2], 0x73, `${d}: WASM magic byte 2 ('s')`);
    assert.equal(buf[3], 0x6d, `${d}: WASM magic byte 3 ('m')`);
  }
});

test('bw64-build.sh wrapper exists and enforces lock + provenance (P3)', () => {
  const p = join(repoRoot, 'tools/bw64-build.sh');
  assert.ok(existsSync(p), 'tools/bw64-build.sh must exist');
  const src = readFileSync(p, 'utf8');
  assert.ok(src.includes('BUILD_INFO.json'), 'build wrapper must write BUILD_INFO.json');
  assert.ok(src.includes('LOCKDIR') || src.includes('build.lock'), 'build wrapper must use a lock (P3: shared-dist poisoning)');
  assert.ok(src.includes('dist/runtime') && src.includes('web/runtime'), 'build wrapper must sync matched pair to dist/ and web/');
});

test('probe slot protocol exists (single probe at a time)', () => {
  const p = join(repoRoot, 'web/tests/probe-slots.mjs');
  assert.ok(existsSync(p), 'web/tests/probe-slots.mjs must exist (acquireProbeSlot)');
  const src = readFileSync(p, 'utf8');
  assert.ok(src.includes('acquireProbeSlot'), 'probe-slots.mjs must export acquireProbeSlot');
});

test('Wine32 export adds only the pinned architecture-independent Wine fonts', () => {
  execFileSync('python3', ['-c', `
import hashlib, json, pathlib, zipfile
root = pathlib.Path(${JSON.stringify(repoRoot)})
base = root / 'web/runtime/glibc-rootfs32.zip'
exported = root / 'dist/runtime/glibc-rootfs32.zip'
wine = root / 'web/runtime/wine64.zip'
record = json.loads((exported.parent / 'wine32-font-provenance.json').read_text())
with zipfile.ZipFile(base) as old, zipfile.ZipFile(exported) as new, zipfile.ZipFile(wine) as source:
    expected = {'opt/wine-stable/share/wine/fonts/' + pathlib.Path(n).name: source.read(n)
                for n in source.namelist() if n.startswith('usr/share/wine/fonts/') and n.endswith('.ttf')}
    assert 'opt/wine-stable/share/wine/fonts/tahoma.ttf' in expected
    assert set(new.namelist()) == set(old.namelist()) | set(expected)
    for name in old.namelist():
        assert new.read(name) == old.read(name), name
    for name, data in expected.items():
        assert new.read(name) == data, name
        assert record['fonts'][name] == hashlib.sha256(data).hexdigest(), name
for key, path in [('source_wine64_sha256', wine), ('glibc_rootfs32_sha256', exported)]:
    with path.open('rb') as f:
        assert record[key] == hashlib.file_digest(f, 'sha256').hexdigest(), key
`], { stdio: 'pipe' });
});
