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
