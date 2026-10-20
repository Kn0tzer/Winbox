import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

test('WineBus SDL registry is added to the exported 64-bit prefix idempotently', () => {
  const root = mkdtempSync(join(tmpdir(), 'boxedwine-winebus-'));
  const runtime = join(root, 'runtime');
  mkdirSync(runtime);
  const archive = join(runtime, 'prefix64.zip');
  const registryPath = 'home/username/.wine/system.reg';
  try {
    execFileSync('python3', ['-c', String.raw`
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w') as z:
    z.comment = b'keep archive comment'
    z.writestr(sys.argv[2], '[System\\\\CurrentControlSet\\\\Services\\\\MountMgr]\n"Start"=dword:00000002\n')
    z.writestr('keep.txt', 'keep me')
`, archive, registryPath], { stdio: 'pipe' });

    const script = resolve('tools/browser/stage-winebus.py');
    execFileSync('python3', [script, runtime], { stdio: 'pipe' });
    const once = readFileSync(archive);
    execFileSync('python3', [script, runtime], { stdio: 'pipe' });
    const twice = readFileSync(archive);
    assert.deepEqual(twice, once, 'a second export pass must leave the archive unchanged');

    execFileSync('python3', ['-c', String.raw`
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    assert z.comment == b'keep archive comment'
    assert z.namelist().count(sys.argv[2]) == 1
    assert z.read('keep.txt') == b'keep me'
    reg = z.read(sys.argv[2]).decode()
    assert '[System\\\\CurrentControlSet\\\\Enum\\\\ROOT\\\\WINE\\\\WINEBUS]' in reg
    assert '[System\\\\CurrentControlSet\\\\Services\\\\winebus]' in reg
    assert '"ImagePath"="C:\\\\windows\\\\system32\\\\drivers\\\\winebus.sys"' in reg
    assert '"InfPath"="winebus.inf"' in reg
    assert not any(line.endswith('\\\\') for line in reg.splitlines()), 'registry continuations must be single backslashes'
`, archive, registryPath], { stdio: 'pipe' });
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
