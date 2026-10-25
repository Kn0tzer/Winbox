import {test} from 'node:test';
import assert from 'node:assert/strict';
import {repackScaledVb} from '../../../vkwebgpu.mjs';
test('Vulkan scaled vertex attributes become exact float values with source stride retained',()=>{
 const bytes=new Uint8Array(24),v=new DataView(bytes.buffer);
 for(let i=0;i<2;i++){v.setFloat32(i*12,1.5+i,true);bytes.set([0,1,128,255],i*12+4);v.setInt16(i*12+8,i?-1:-32768,true);v.setInt16(i*12+10,32767,true);}
 const out=repackScaledVb(bytes,12,36,[{srcOffset:4,dstOffset:12,components:4,bits:8,signed:false},{srcOffset:8,dstOffset:28,components:2,bits:16,signed:true}]);
 assert.equal(out.length,72);const f=new DataView(out.buffer);
 for(let i=0;i<2;i++){assert.deepEqual(out.slice(i*36,i*36+12),bytes.slice(i*12,i*12+12));assert.deepEqual([0,1,2,3].map(j=>f.getFloat32(i*36+12+4*j,true)),[0,1,128,255]);assert.equal(f.getFloat32(i*36+28,true),i?-1:-32768);assert.equal(f.getFloat32(i*36+32,true),32767);}
 assert.equal(repackScaledVb(bytes.subarray(0,23),12,36,[]).length,36);
});

test('Vulkan vertex strides without four-byte alignment are padded for WebGPU', async()=>{
 const bytes=Uint8Array.from([1,2,3,4,5,6]);
 const out=repackScaledVb(bytes,3,4,[]);
 assert.deepEqual([...out],[1,2,3,0,4,5,6,0]);
 const {readFile}=await import('node:fs/promises');
 const source=await readFile(new URL('../../../vkwebgpu.mjs',import.meta.url),'utf8');
 assert.match(source,/const paddedStride = stride > 0 && \(stride % 4 !== 0 \|\| minStride > stride\)/);
 assert.match(source,/const repacked = scaled \|\| walk\?\.paddedStride/);
 assert.match(source,/repackScaledVb\(vbytes\.subarray\(vb\.offset\), walk\.stride, walk\.outputStride, walk\.scaled, walk\.rawAttributes\)/);
 assert.doesNotMatch(source,/noteUnsupported\('overlapping-vertex-format'/);
});

test('Vulkan attributes extending past their binding stride retain overlapping fetch semantics',()=>{
 const bytes=Uint8Array.from([1,2,3,4,5,6,7,8,9,10]);
 const out=repackScaledVb(bytes,4,8,[],[{srcOffset:2,dstOffset:2,size:4}]);
 assert.equal(out.length,16);
 assert.deepEqual([...out.slice(2,6)],[3,4,5,6]);
 assert.deepEqual([...out.slice(10,14)],[7,8,9,10]);
});
