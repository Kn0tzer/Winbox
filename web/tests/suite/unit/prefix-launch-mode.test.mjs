import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

const source = readFileSync('project/emscripten/wine64-launcher.js', 'utf8');
const buildArguments = source.match(/function buildArguments\(\) \{[\s\S]*?\n    \}/)?.[0];
assert.ok(buildArguments, 'launcher argument builder must exist');

function argv({ arch32, session, boot = false }) {
  const wine = arch32 ? '/opt/wine-stable/bin/wine' : '/usr/lib/wine/wine64';
  const prefix = arch32 ? 'prefix32.zip' : 'prefix64.zip';
  return runInNewContext(buildArguments + '; buildArguments()', {
    ROOT: '/root',
    GLIBC_ZIP: arch32 ? 'glibc-rootfs32.zip' : 'glibc-rootfs64.zip',
    WINE_ZIP: arch32 ? 'wine32.zip' : 'wine64.zip',
    wineZipArg: arch32 ? 'wine32.zip' : 'wine64.zip',
    PREFIX_ZIP: prefix,
    WINE64: wine,
    WINESERVER64: arch32 ? '/usr/lib/wine/wineserver32' : '/usr/lib/wine/wineserver64',
    WINEDLLPATH: arch32 ? '/opt/wine-stable/lib/wine' : '/usr/lib/x86_64-linux-gnu/wine',
    USE_PREFIX: !boot,
    SESSION: session,
    DO_BOOT: boot,
    NOVIDEO: false,
    PROG: 'notepad.exe',
    window: {},
    prefixEnv: () => ['WINEPREFIX=/home/username/.wine'],
    wineProgArgv: () => [wine, 'notepad.exe'],
  });
}

test('initialized prefixes start the target directly in both architectures and session modes', () => {
  for (const arch32 of [false, true]) for (const session of [false, true]) {
    const args = Array.from(argv({ arch32, session }));
    assert.ok(args.includes(arch32 ? 'prefix32.zip' : 'prefix64.zip'));
    assert.deepEqual(args.slice(-2), [
      arch32 ? '/opt/wine-stable/bin/wine' : '/usr/lib/wine/wine64',
      'notepad.exe',
    ]);
    assert.ok(!args.includes('wineboot'), 'normal prefix launch must not explicitly run wineboot');
    assert.ok(!args.includes('--init'));
  }
});

test('explicit boot probes remain the only launcher path that runs wineboot --init', () => {
  const args = Array.from(argv({ arch32: false, session: false, boot: true }));
  assert.deepEqual(args.slice(-3), ['/usr/lib/wine/wine64', 'wineboot', '--init']);
});
