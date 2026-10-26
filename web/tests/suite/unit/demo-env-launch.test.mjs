import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { runtimeParameters } from '../../../../web/launch.mjs';
import { demos, demoParameters } from '../../../../web/demos.mjs';

const launcher = readFileSync(fileURLToPath(new URL('../../../../project/emscripten/wine64-launcher.js', import.meta.url)), 'utf8');

test('a demo env map reaches the launch parameters verbatim', async () => {
  const demo = {
    id: 'env-probe', title: 'Env probe', archive: 'env-probe.zip', exe: 'probe.exe', arch: 'x64', engine: 'windows',
    env: { DXVK_CONFIG: 'd3d11.maxFeatureLevel = 10_1', FNA3D_FORCE_DRIVER: 'OpenGL' },
  };
  const fetcher = async () => ({ ok: true, headers: { get: name => (name === 'content-length' ? '100' : null) } });
  const query = await demoParameters(demo, { fetcher });
  assert.deepEqual(JSON.parse(query.get('env')), demo.env,
    'the demo entry env map must be serialized into the env launch parameter');
});

test('runtimeParameters omits the env parameter when the program has none', () => {
  const query = runtimeParameters({ arch: 'x64', engine: 'windows', exe: 'a.exe', args: [] });
  assert.equal(query.get('env'), null);
});

test('runtimeParameters refuses reserved, malformed, or non-object env', () => {
  assert.throws(() => runtimeParameters({ arch: 'x64', exe: 'a.exe', env: { WINEDLLOVERRIDES: 'd3d9=n' } }), /env/,
    'env must not override the dedicated dlloverrides parameter');
  assert.throws(() => runtimeParameters({ arch: 'x64', exe: 'a.exe', env: { HOME: '/elsewhere' } }), /env/,
    'env must not override launcher-owned variables');
  assert.throws(() => runtimeParameters({ arch: 'x64', exe: 'a.exe', env: { 'BAD-NAME': 'x' } }), /env/);
  assert.throws(() => runtimeParameters({ arch: 'x64', exe: 'a.exe', env: { OK_NAME: 'a\nb' } }), /env/);
  assert.throws(() => runtimeParameters({ arch: 'x64', exe: 'a.exe', env: { OK_NAME: 42 } }), /env/);
  assert.throws(() => runtimeParameters({ arch: 'x64', exe: 'a.exe', env: ['DXVK_HUD=1'] }), /env/);
});

test('the guest launcher applies the env parameter inside prefixEnv', () => {
  const prefixEnv = launcher.match(/function prefixEnv\(\) \{([\s\S]*?)\n        return e;/);
  assert.ok(prefixEnv, 'prefixEnv must exist in wine64-launcher.js');
  assert.match(prefixEnv[1], /param\("env"\)/, 'prefixEnv must read the env URL parameter');
  assert.match(prefixEnv[1], /JSON\.parse\(envParam\)/, 'prefixEnv must parse the env JSON');
  assert.match(prefixEnv[1], /e\.push\(envKey \+ "=" \+ envValue\)/,
    'prefixEnv must add the env entries to the guest environment it returns');
});

test('no committed demo entry uses env yet (mechanism only, adoption is a coordinator decision)', () => {
  assert.ok(demos.every(demo => demo.env === undefined));
});
