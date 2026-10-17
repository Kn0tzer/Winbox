import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';

// Preserve the negative control that motivated Emscripten exception catching.
// These snapshots check fixture semantics; production build flags and Wine
// startup still require a current rebuild and runtime check.
for (const [name, expectedSuccess] of [['before', false], ['after', true]]) {
  test(`Wine32 exception fixture: ${name}`, () => {
    const fixture = new URL(`../../testdata/wine32-exception-regression/${name}.mjs`, import.meta.url);
    const result = spawnSync(process.execPath, [
      '--input-type=module', '--eval',
      'const { default: run } = await import(process.argv[1]); await run();',
      fixture.href,
    ], { encoding: 'utf8', timeout: 15000 });
    assert.ifError(result.error);
    assert.equal(result.signal, null, 'fixture must finish without being killed');
    assert.equal(result.status === 0, expectedSuccess, result.stderr);
    assert.equal(result.stdout.includes('BW32_CATCH_OK'), expectedSuccess,
      'only the fixture with exception catching enabled may reach the catch handler');
  });
}
