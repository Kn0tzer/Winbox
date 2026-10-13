import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { applySpirvFix } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../vendor/naga/node.js';

test('captured specialization eliminates an unused shadow alias while preserving dynamic rejection', () => {
  const dir = mkdtempSync(join(tmpdir(), 'spirv-alias-'));
  try {
    const shader = join(dir, 'alias.frag'), binary = join(dir, 'alias.spv');
    const source = `#version 450
layout(constant_id=12) const uint mode=0;
layout(constant_id=1) const uint mask=1;
layout(set=0,binding=0) uniform sampler2D colorImage;
layout(set=0,binding=0) uniform sampler2DShadow shadowImage;
layout(set=1,binding=0) uniform State { uint dynamicMask; } state;
layout(location=0) out vec4 color;
void main() {
  if (bitfieldExtract(mask,0,1) != 0u) color=vec4(texture(shadowImage,vec3(0.5)));
  else color=texture(colorImage,vec2(0.5));
}`;
    writeFileSync(shader, source);
    execFileSync('glslangValidator', ['-V', shader, '-o', binary]);
    const bytes = new Uint8Array(readFileSync(binary));
    const fixed = applySpirvFix(bytes, { specOverrides: [
      { constantID: 12, value: new Uint8Array([1, 0, 0, 0]) },
      { constantID: 1, value: new Uint8Array([0, 0, 0, 0]) },
    ] });
    assert.ok(fixed.applied.includes('fold-spec-constants'));
    assert.ok(fixed.applied.includes('prune-specialized-aliases'));
    const wgsl = naga.translate({ from: 'spirv', to: 'wgsl', source: fixed.bytes });
    assert.match(wgsl, /texture_2d<f32>/);
    assert.doesNotMatch(wgsl, /texture_depth_2d/);
    writeFileSync(binary, fixed.bytes);
    execFileSync('spirv-val', ['--target-env', 'vulkan1.3', binary]);
    writeFileSync(shader, source.replace('bitfieldExtract(mask,0,1)', 'bitfieldExtract(state.dynamicMask,0,1)'));
    execFileSync('glslangValidator', ['-V', shader, '-o', binary]);
    const dynamic = applySpirvFix(new Uint8Array(readFileSync(binary)));
    assert.throws(() => naga.translate({ from: 'spirv', to: 'wgsl', source: dynamic.bytes }),
      error => /conflict/.test(error.formatted));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
