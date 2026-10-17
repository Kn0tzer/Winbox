import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {rewriteResolvedDxvkPresent,scanWgslResources,rewritePushConstantsWGSL} from '../../../vkwebgpu.mjs';
const shader=readFileSync(new URL('../fixtures/dxvk-present-msaa.wgsl',import.meta.url),'utf8');
test('DXVK presentation samples the bridge-resolved image while retaining its offsets',()=>{
 const fixed=rewriteResolvedDxvkPresent('8b1e0f33622e4d9f',shader);
 assert.notEqual(fixed,shader);
 const resources=scanWgslResources(rewritePushConstantsWGSL(fixed).code);
 assert.deepEqual(resources.filter(x=>x.type.startsWith('texture_')).map(x=>[x.group,x.binding,x.type]),[[0,0,'texture_2d<f32>']]);
 assert.match(fixed,/vec2<i32>\(p.xy\) \+ unnamed.src_offset - unnamed.dst_offset/);
 assert.match(fixed,/textureLoad\(s_image, coord, 0\)/);
 assert.ok(resources.some(x=>x.group===3 && x.addr==='uniform'));
});
test('gamma-enabled and unrelated shaders retain their original sampling semantics',()=>{
 assert.equal(rewriteResolvedDxvkPresent('other',shader),shader);
 const gamma=shader.replace('const s_gamma_bound: bool = false;','const s_gamma_bound: bool = true;');
 assert.equal(rewriteResolvedDxvkPresent('8b1e0f33622e4d9f',gamma),gamma);
 assert.equal(rewriteResolvedDxvkPresent('8b1e0f33622e4d9f',null),null);
});
