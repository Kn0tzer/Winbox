// Focused real-WebGPU stencil acceptance; no guest boot required.
import assert from 'node:assert/strict';
import {chromium} from '@playwright/test';
import {resolve} from 'node:path';
import {serve} from '../../tools/browser/serve.mjs';
import {acquireProbeSlot} from './probe-slots.mjs';
import {waitForBuildIdle} from './build-lock.mjs';
const slot=await acquireProbeSlot();await waitForBuildIdle();let server,browser;
try{
 server=await serve(resolve(import.meta.dirname,'../..'));
 browser=await chromium.launch({executablePath:process.env.CHROME_PATH||'/usr/local/bin/chromium',headless:true,args:['--no-sandbox','--disable-dev-shm-usage','--enable-unsafe-webgpu','--enable-unsafe-swiftshader','--use-angle=vulkan','--use-vulkan=swiftshader','--enable-features=Vulkan']});
 const page=await browser.newPage();
 await page.route('**/stencil-test.html',route=>route.fulfill({contentType:'text/html',body:'<canvas id="c"></canvas>'}));
 await page.goto(`http://127.0.0.1:${server.address().port}/stencil-test.html`);
 const result=await page.evaluate(async()=>{
  const {createVkRenderer2}=await import('/web/vkwebgpu.mjs');
  const canvas=document.querySelector('canvas');
  const vs=z=>`@vertex fn main(@builtin(vertex_index) i:u32)->@builtin(position) vec4<f32>{let p=array<vec2<f32>,3>(vec2<f32>(-1,-1),vec2<f32>(3,-1),vec2<f32>(-1,3));return vec4<f32>(p[i],${z},1);}`;
  const r=await createVkRenderer2(canvas,{translate:async(_,code,stage)=>stage==='vertex'?vs(code[0]===2?'0.5':'0.1'):`@fragment fn main()->@location(0) vec4<f32>{return vec4<f32>(${code[0]===4?'0,1,0,1':'1,0,0,1'});}`});
  const errors=[];r.device.addEventListener('uncapturederror',e=>errors.push(e.error.message));
  let color;
  const create=r.device.createTexture.bind(r.device);r.device.createTexture=d=>{const t=create(d);if(d.label==='img:color')color=t;return t;};
  const face={failOp:0,passOp:2,depthFailOp:0,compareOp:7,compareMask:255,writeMask:255,reference:1};
  const pipe=(id,extra={})=>({pipeId:id,topology:3,cull:0,front:0,vb:[],va:[],vsHash:'near',fsHash:'red',depthTest:0,depthWrite:0,depthOp:1,blend:0,stencil:{enabled:1,dynamicMask:0,front:face,back:face},...extra});
  const draw={op:'draw',vertexCount:3,instanceCount:1,firstVertex:0,firstInstance:0};
  const frame={frameNo:1,width:4,height:4,shaders:new Map(['near','far','red','green'].map((id,i)=>[id,{code:new Uint8Array([i+1])}])),images:new Map(),views:new Map(),samplers:new Map(),buffers:new Map(),bufHashes:new Map(),readbacks:[],passes:[]};
  const pass=ops=>({w:4,h:4,clearColor:[0,0,1,1],clearDepth:.25,clearStencil:0,attachments:[{imageId:'color',format:37,isDepth:0,samples:1,loadOp:1,storeOp:0},{imageId:'depth',format:129,isDepth:1,samples:1,loadOp:1,storeOp:0,stencilLoadOp:1,stencilStoreOp:0,aspectMask:6}],ops});
  frame.passes.push(pass([{op:'bindPipe',pipe:pipe('mask',{colorBlend:[{enable:0,writeMask:0}]})},{op:'scissor',s:[1,1,2,2]},draw]));
  const equal={...face,compareOp:2,passOp:0,writeMask:0};
  const second=pass([{op:'clearAttachments',attachments:[{aspectMask:4,colorAttachment:0,colorBits:[0,0,0,0],depth:1,stencil:0}],rects:[{x:1,y:1,w:1,h:1,baseLayer:0,layers:1}]},{op:'scissor',s:[0,0,4,4]},{op:'bindPipe',pipe:pipe('view',{depthTest:1,stencil:{enabled:1,dynamicMask:0,front:equal,back:equal}})},draw,{op:'bindPipe',pipe:pipe('depth-check',{vsHash:'far',fsHash:'green',depthTest:1,stencil:{enabled:0}})},draw]);
  second.attachments.forEach(a=>{a.loadOp=0;if(a.isDepth)a.stencilLoadOp=0;});frame.passes.push(second);
  await r.render(frame,{present:false});
  const buffer=r.device.createBuffer({size:1024,usage:GPUBufferUsage.COPY_DST|GPUBufferUsage.MAP_READ});const encoder=r.device.createCommandEncoder();encoder.copyTextureToBuffer({texture:color},{buffer,bytesPerRow:256},[4,4]);r.device.queue.submit([encoder.finish()]);await buffer.mapAsync(GPUMapMode.READ);const bytes=new Uint8Array(buffer.getMappedRange());const pixels=[];for(let y=0;y<4;y++)for(let x=0;x<4;x++)pixels.push([...bytes.slice(y*256+x*4,y*256+x*4+4)]);buffer.unmap();buffer.destroy();const stats=r.snapshot();r.destroy();return{pixels,errors,stats};
 });
 assert.deepEqual(result.errors,[]);assert.equal(result.stats.unsupported,0);
 for(let y=0;y<4;y++)for(let x=0;x<4;x++)assert.deepEqual(result.pixels[y*4+x],x>=1&&x<3&&y>=1&&y<3&&!(x===1&&y===1)?[255,0,0,255]:[0,0,255,255],`pixel ${x},${y}`);
 console.log(JSON.stringify({pass:true,redPixels:3,bluePixels:13,unsupported:result.stats.unsupported}));
}finally{await browser?.close();server?.close();await slot.release();}
