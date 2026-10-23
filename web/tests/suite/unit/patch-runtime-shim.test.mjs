// web/tests/suite/unit/patch-runtime-shim.test.mjs — tools/rootfs64/patch-runtime-shim.py.
//
// Builds a miniature runtime export (support archive + dependencies.json) and checks that the
// script swaps both Vulkan shims, renames the archive to its new content hash, refreshes
// dependencies.json / winbox-runtime.json, leaves other entries alone, and is idempotent.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const sha = data => createHash('sha256').update(data).digest('hex');

test('patch-runtime-shim swaps shims and refreshes the content-addressed manifest', () => {
  const dir = mkdtempSync(join(tmpdir(), 'patch-shim-'));
  try {
    execFileSync('python3', ['-I', '-c', `import zipfile,json,hashlib,sys
d=sys.argv[1]
name='support-'+'0'*64+'.zip'
with zipfile.ZipFile(d+'/'+name,'w',zipfile.ZIP_DEFLATED) as z:
 for p in ['lib/i386-linux-gnu','usr/lib/i386-linux-gnu','lib/x86_64-linux-gnu','usr/lib/x86_64-linux-gnu']:
  z.writestr(p+'/libvulkan.so.1',b'old shim')
 z.writestr('usr/lib/x86_64-linux-gnu/libGL.so.1',b'untouched')
 z.writestr('winbox-runtime.json',json.dumps({'vulkan':{'i386':'x','x86_64':'y'},'openGL':{}}))
json.dump({'version':1,'archive':name,'bytes':1,'sha256':'0'*64,'vulkan':{'i386':'x','x86_64':'y'}},open(d+'/dependencies.json','w'))
`, dir]);
    const run = () => execFileSync('python3', [join(repo, 'tools/rootfs64/patch-runtime-shim.py'), dir], { encoding: 'utf8' });
    run();
    const manifest = JSON.parse(readFileSync(join(dir, 'dependencies.json'), 'utf8'));
    const files = readdirSync(dir).filter(f => f.startsWith('support-'));
    assert.deepEqual(files, [manifest.archive], 'old archive removed, new one present');
    assert.equal(manifest.archive, `support-${manifest.sha256}.zip`);
    const archive = readFileSync(join(dir, manifest.archive));
    assert.equal(sha(archive), manifest.sha256);
    assert.equal(archive.length, manifest.bytes);
    const shim64 = sha(readFileSync(join(repo, 'tools/rootfs64/libvk64/libvulkan.so.1')));
    const shim32 = sha(readFileSync(join(repo, 'tools/rootfs64/libvk32/libvulkan.so.1')));
    assert.deepEqual(manifest.vulkan, { i386: shim32, x86_64: shim64 });
    const out = execFileSync('python3', ['-I', '-c', `import zipfile,sys,json,hashlib
z=zipfile.ZipFile(sys.argv[1])
h=lambda n:hashlib.sha256(z.read(n)).hexdigest()
print(json.dumps({'a':h('lib/x86_64-linux-gnu/libvulkan.so.1'),'b':h('usr/lib/x86_64-linux-gnu/libvulkan.so.1'),
 'c':h('lib/i386-linux-gnu/libvulkan.so.1'),'d':h('usr/lib/i386-linux-gnu/libvulkan.so.1'),
 'gl':z.read('usr/lib/x86_64-linux-gnu/libGL.so.1').decode(),'rt':json.loads(z.read('winbox-runtime.json'))['vulkan']}))`,
      join(dir, manifest.archive)], { encoding: 'utf8' });
    const got = JSON.parse(out);
    assert.deepEqual([got.a, got.b, got.c, got.d], [shim64, shim64, shim32, shim32]);
    assert.equal(got.gl, 'untouched');
    assert.deepEqual(got.rt, manifest.vulkan);
    assert.match(run(), /nothing to do/);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
