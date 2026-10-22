import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import * as naga from '../../../vendor/naga/node.js';
import { rewriteGlslForWebGPU } from '../../../gl-webgpu.mjs';

const fixtureDir = resolve('web/tests/fixtures/glsl');
const cases = [
  ['libGDX SpriteBatch vertex', 'libgdx-spritebatch.vert.glsl', 'vertex', 'libgdx'],
  ['libGDX SpriteBatch fragment', 'libgdx-spritebatch.frag.glsl', 'fragment', 'libgdx'],
  ['Mindustry vertex', 'mindustry.vert.glsl', 'vertex', 'mindustry'],
  ['Mindustry fragment', 'mindustry.frag.glsl', 'fragment', 'mindustry'],
  ['Mindustry Convolve1dFilter fragment', 'mindustry-convolve-1d.frag.glsl', 'fragment', 'mindustry'],
  ['GL_ES precision macro compatibility vertex', 'gl-es-precision-macros.vert.glsl', 'vertex', 'gl-es'],
  ['quake3e generic vertex', 'quake3e-generic.vert.glsl', 'vertex', 'quake3e'],
  ['quake3e generic fragment', 'quake3e-generic.frag.glsl', 'fragment', 'quake3e'],
];

test('engine GLSL fixtures translate through the WebGPU pre-pass and real Naga runtime', async t => {
  const interfaceLocations = new Map();
  for (const [name, file, stage, engine] of cases) {
    await t.test(name, async () => {
      const fixture = readFileSync(resolve(fixtureDir, file), 'utf8');
      const source = file.startsWith('mindustry-') ? fixture.replace(/\n/g, '\r\n') : fixture;
      interfaceLocations.set(engine, interfaceLocations.get(engine) || new Map());
      const prepared = rewriteGlslForWebGPU(source, stage, new Map(), interfaceLocations.get(engine));
      const wgsl = naga.translate({ from: 'glsl', to: 'wgsl', source: prepared.code, parse: { stage } });
      assert.ok(String(wgsl).length > 0, `${file} should produce WGSL`);
    });
  }
});

test('explicit vertex attribute bindings reserve locations before automatic inputs', () => {
  const source = readFileSync(resolve(fixtureDir, 'gl-es-precision-macros.vert.glsl'), 'utf8');
  const prepared = rewriteGlslForWebGPU(source, 'vertex', new Map([['a_position', 0]]), new Map());
  assert.match(prepared.code, /layout\(location\s*=\s*0\)\s*in\s+vec4\s+a_position/);
  assert.match(prepared.code, /layout\(location\s*=\s*1\)\s*in\s+vec2\s+a_texCoord0/);
  const wgsl = naga.translate({ from: 'glsl', to: 'wgsl', source: prepared.code, parse: { stage: 'vertex' } });
  assert.ok(String(wgsl).length > 0);
});
