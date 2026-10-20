import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

test('WineBus drivers and HID/XInput INFs are staged into the runtime and prefixes', () => {
  const root = mkdtempSync(join(tmpdir(), 'boxedwine-winebus-'));
  const runtime = join(root, 'runtime');
  mkdirSync(runtime);
  const archive = join(runtime, 'prefix64.zip');
  const prefix32 = join(runtime, 'prefix32.zip');
  const registryPath = 'home/username/.wine/system.reg';
  try {
    execFileSync('python3', ['-c', String.raw`
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w') as z:
    z.comment = b'keep archive comment'
    z.writestr(sys.argv[2], '[System\\\\CurrentControlSet\\\\Services\\\\MountMgr]\n"Start"=dword:00000002\n')
    z.writestr('keep.txt', 'keep me')
`, archive, registryPath], { stdio: 'pipe' });
    execFileSync('python3', ['-c', String.raw`
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w') as z:
    z.comment = b'keep 32-bit prefix comment'
    z.writestr('keep.txt', 'keep 32-bit prefix')
`, prefix32], { stdio: 'pipe' });
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
    const once = [archive, prefix32, ...driverArchives.map(([name]) => join(runtime, name))]
      .map(path => readFileSync(path));
    execFileSync('python3', [script, runtime], { stdio: 'pipe' });
    const twice = [archive, prefix32, ...driverArchives.map(([name]) => join(runtime, name))]
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

    for (const [prefix, comment, keep] of [
      [archive, 'keep archive comment', 'keep me'],
      [prefix32, 'keep 32-bit prefix comment', 'keep 32-bit prefix'],
    ]) {
      execFileSync('python3', ['-c', String.raw`
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    assert z.comment.decode() == sys.argv[2]
    assert z.read('keep.txt').decode() == sys.argv[3]
    inf_dir = 'home/username/.wine/drive_c/windows/inf/'
    for name in ('input.inf', 'winebus.inf', 'winehid.inf', 'wineusb.inf', 'winexinput.inf'):
        assert z.namelist().count(inf_dir + name) == 1
    input_inf = z.read(inf_dir + 'input.inf').decode()
    winebus = z.read(inf_dir + 'winebus.inf').decode()
    hid = z.read(inf_dir + 'winehid.inf').decode()
    wineusb = z.read(inf_dir + 'wineusb.inf').decode()
    xinput = z.read(inf_dir + 'winexinput.inf').decode()
    assert 'HID\\' in input_inf
    assert 'root\\winebus' in winebus
    assert 'WINEBUS\\WINE_COMP_HID' in hid
    assert 'ServiceBinary="%12%\\winehid.sys"' in hid
    assert 'root\\wineusb' in wineusb
    assert 'WINEBUS\\WINE_COMP_XINPUT' in xinput
    assert 'ServiceBinary="%12%\\winexinput.sys"' in xinput
`, prefix, comment, keep], { stdio: 'pipe' });
    }

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
