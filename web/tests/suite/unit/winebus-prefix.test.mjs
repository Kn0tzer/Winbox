import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

test('WineBus registry and driver paths are staged idempotently for both architectures', () => {
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
    const driverArchives = [
      ['wine32.zip', 'opt/wine-stable/lib/wine/i386-windows'],
      ['wine64.zip', 'usr/lib/x86_64-linux-gnu/wine/x86_64-windows'],
    ];
    execFileSync('python3', ['-c', String.raw`
import sys, zipfile
for name, directory in zip(sys.argv[1::2], sys.argv[2::2]):
    with zipfile.ZipFile(name, 'w') as z:
        z.comment = b'keep driver archive comment'
        z.writestr(directory + '/winebus.sys', b'winebus image')
        z.writestr(directory + '/mountmgr.sys', b'mountmgr image')
        z.writestr(directory + '/hidclass.sys', b'hidclass image')
        z.writestr(directory + '/other.dll', b'keep driver archive')
`, ...driverArchives.flatMap(([name, directory]) => [join(runtime, name), directory])], { stdio: 'pipe' });

    const script = resolve('tools/browser/stage-winebus.py');
    execFileSync('python3', [script, runtime], { stdio: 'pipe' });
    const once = [archive, ...driverArchives.map(([name]) => join(runtime, name))]
      .map(path => readFileSync(path));
    execFileSync('python3', [script, runtime], { stdio: 'pipe' });
    const twice = [archive, ...driverArchives.map(([name]) => join(runtime, name))]
      .map(path => readFileSync(path));
    twice.forEach((contents, index) => {
      assert.deepEqual(contents, once[index], 'a second export pass must leave archives unchanged');
    });

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

    for (const [name, directory] of driverArchives) {
      execFileSync('python3', ['-c', String.raw`
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    assert z.comment == b'keep driver archive comment'
    for driver, contents in [('winebus.sys', b'winebus image'), ('mountmgr.sys', b'mountmgr image'), ('hidclass.sys', b'hidclass image')]:
        source = sys.argv[2] + '/' + driver
        alias = sys.argv[2] + '/drivers/' + driver
        assert z.namelist().count(alias) == 1
        assert z.read(alias) == contents
        assert z.read(source) == contents
    assert z.read(sys.argv[2] + '/other.dll') == b'keep driver archive'
`, join(runtime, name), directory], { stdio: 'pipe' });
    }
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
