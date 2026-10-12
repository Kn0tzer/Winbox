import {test} from 'node:test';
import assert from 'node:assert/strict';
import {stripNagaFragmentDepth} from '../../../vkwebgpu.mjs';
test('fragment depth is removed only from Naga output declarations and return constructors',()=>{
 const code=`struct FragmentOutput {\n @location(0) member_0: vec4<f32>,\n @builtin(frag_depth) member_1: f32,\n}\nvar<private> depth: f32;\n@fragment fn main() -> FragmentOutput {\n let color = vec4<f32>(1.0); let d = depth;\n return FragmentOutput(color, d);\n}`;
 const out=stripNagaFragmentDepth(code);assert.ok(!out.includes('@builtin(frag_depth)'));assert.ok(out.includes('FragmentOutput(color)'));assert.ok(out.includes('let d = depth;'));assert.ok(out.includes('var<private> depth: f32'));
 assert.equal(stripNagaFragmentDepth('fn main() {}'),'fn main() {}');assert.equal(stripNagaFragmentDepth(code.replace('FragmentOutput(color, d)','FragmentOutput(vec4<f32>(1.0), d)')),code.replace('FragmentOutput(color, d)','FragmentOutput(vec4<f32>(1.0), d)'));
});
