import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const probe = readFileSync(fileURLToPath(new URL('../../../../tools/bench/gl-game-probe.mjs', import.meta.url)), 'utf8');

test('Q3 probe disables the ARB vertex-program VBO path and starts q3dm1', () => {
  const profile = probe.match(/q3:\s*\{([\s\S]*?)\n\s*\},\n\s*mindustry:/)?.[1];
  assert.ok(profile, 'Q3 probe profile must exist');
  assert.match(profile, /glBackend:\s*'webgpu'/, 'Q3 visual probe must request the GL WebGPU backend');
  const argsText = profile.match(/args:\s*\[([\s\S]*?)\]/)?.[1];
  assert.ok(argsText, 'Q3 probe arguments must exist');
  const args = [...argsText.matchAll(/'([^']*)'/g)].map((match) => match[1]);
  assert.ok(args.some((arg, index) => arg === '+set' && args[index + 1] === 'r_vbo' && args[index + 2] === '0'),
    'Q3 must avoid the unsupported VBO/ARB program path');
  assert.ok(args.some((arg, index) => arg === '+map' && args[index + 1] === 'q3dm1'),
    'Q3 probe must start the in-game scene');
});

test('GL game probe launches the runtime entry page', () => {
  assert.match(probe, /result\.launchUrl = `\$\{base\}\/dist\/runtime\/index\.html\?\$\{query\}`/);
  assert.match(probe, /BW64_GL_BACKEND.*webgpu/);
});
