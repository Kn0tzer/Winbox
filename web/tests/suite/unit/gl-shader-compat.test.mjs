import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import * as naga from '../../../vendor/naga/node.js';
import { rewriteGlslForWebGPU } from '../../../gl-webgpu.mjs';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const fixtureDir = resolve(repoRoot, 'web/tests/fixtures/glsl');
const cases = [
  ['libGDX SpriteBatch vertex', 'libgdx-spritebatch.vert.glsl', 'vertex', 'libgdx'],
  ['libGDX SpriteBatch fragment', 'libgdx-spritebatch.frag.glsl', 'fragment', 'libgdx'],
  ['Mindustry vertex', 'mindustry.vert.glsl', 'vertex', 'mindustry'],
  ['Mindustry fragment', 'mindustry.frag.glsl', 'fragment', 'mindustry'],
  ['Mindustry Convolve1dFilter fragment', 'mindustry-convolve-1d.frag.glsl', 'fragment', 'mindustry'],
  ['GL_ES precision macro compatibility vertex', 'gl-es-precision-macros.vert.glsl', 'vertex', 'gl-es'],
  ['GL_ES precision conditional fragment', 'gl-es-precision-else.frag.glsl', 'fragment', 'gl-es'],
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

test('matrix varyings lower to column vectors accepted by Naga for both stages', () => {
  const locations = new Map();
  const vertexSource = `#version 300 es
in vec4 a_position;
out vec4 v_position;
out mat4 v_model;
uniform mat4 u_model;
uniform mat4 u_projection;
void main() {
  v_position = a_position;
  v_model = u_model;
  gl_Position = u_projection * u_model * a_position;
}`;
  const fragmentSource = `#version 300 es
in vec4 v_position;
in mat4 v_model;
out vec4 bw_FragColor;
void main() {
  bw_FragColor = v_model * v_position;
}`;
  const vertex = rewriteGlslForWebGPU(vertexSource, 'vertex', new Map([['a_position', 0]]), locations);
  const fragment = rewriteGlslForWebGPU(fragmentSource, 'fragment', new Map(), locations);

  assert.doesNotThrow(() => naga.translate({ from: 'glsl', to: 'wgsl', source: vertex.code, parse: { stage: 'vertex' } }));
  assert.doesNotThrow(() => naga.translate({ from: 'glsl', to: 'wgsl', source: fragment.code, parse: { stage: 'fragment' } }));
  assert.match(vertex.code, /mat4 v_model;/);
  assert.match(fragment.code, /v_model = mat4\(bw64_varying_v_model_c0, bw64_varying_v_model_c1, bw64_varying_v_model_c2, bw64_varying_v_model_c3\);/);
  for (let column = 0; column < 4; column++) {
    const location = column + 1;
    assert.match(vertex.code, new RegExp(`layout\\(\\s*location\\s*=\\s*${location}\\) out vec4 bw64_varying_v_model_c${column}`));
    assert.match(fragment.code, new RegExp(`layout\\(\\s*location\\s*=\\s*${location}\\) in vec4 bw64_varying_v_model_c${column}`));
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
