import {test} from 'node:test';
import assert from 'node:assert/strict';
import {rgba16UnormToHalf,v2TextureFormat} from '../../../vkwebgpu.mjs';
test('RGBA16 normalized values retain channel widths and range in half-float textures',()=>{
 const values=[0,32768,65535,1],raw=new Uint8Array(8),v=new DataView(raw.buffer);values.forEach((n,i)=>v.setUint16(i*2,n,true));const out=rgba16UnormToHalf(raw);const f=new DataView(out.buffer);
 assert.equal(out.length,8);assert.deepEqual([0,1,2,3].map(i=>f.getUint16(i*2,true)),[0,0x3800,0x3c00,0x100]);assert.equal(v2TextureFormat(91),'rgba16float');assert.deepEqual([...raw],[0,0,0,128,255,255,1,0]);
});
