import { test } from 'node:test';
import assert from 'node:assert/strict';
import { WebGPUGL, rewriteGlslForWebGPU } from '../../../gl-webgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

test('anisotropic filtering extension maps texture state to WebGPU samplers', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    assert.ok(gl.getSupportedExtensions().includes('EXT_texture_filter_anisotropic'));
    assert.equal(gl.getExtension('EXT_texture_filter_anisotropic').TEXTURE_MAX_ANISOTROPY_EXT, 0x84fe);
    const texture = gl.createTexture();
    gl.bindTexture(0x0de1, texture);
    gl.texParameterf(0x0de1, 0x84fe, 8);
    assert.equal(gl.getError(), 0);
    assert.equal(gl._samplerFor(texture).maxAnisotropy, 8);
    gl.texParameterf(0x0de1, 0x84fe, 0);
    assert.equal(gl.getError(), 0x0501);
    assert.equal(gl._samplerFor(texture).maxAnisotropy, 8, 'invalid values do not replace existing sampler state');
  } finally { gpu.restore(); }
});

test('legacy alpha, luminance, luminance-alpha, red, and RG textures expand channels correctly', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: () => {} });
    await gl.ready;
    const cases = [
      [0x1906, new Uint8Array([64]), [255, 255, 255, 64]], // ALPHA
      [0x1909, new Uint8Array([64]), [64, 64, 64, 255]], // LUMINANCE
      [0x190a, new Uint8Array([64, 128]), [64, 64, 64, 128]], // LUMINANCE_ALPHA
      [0x1903, new Uint8Array([64]), [64, 0, 0, 255]], // RED
      [0x8227, new Uint8Array([64, 128]), [64, 128, 0, 255]], // RG
    ];
    for (const [format, data, expected] of cases) {
      const texture = gl.createTexture();
      gl.bindTexture(0x0de1, texture);
      gl.texImage2D(0x0de1, 0, format, 1, 1, 0, format, 0x1401, data);
      assert.deepEqual([...texture.pixels], expected, `format 0x${format.toString(16)}`);
    }
  } finally { gpu.restore(); }
});

test('EXT_texture_rg is advertised and R8/RG8 sampling formats preserve red and green channels', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    const extension = gl.getExtension('EXT_texture_rg');
    assert.ok(extension);
    assert.ok(gl.getSupportedExtensions().includes('EXT_texture_rg'));
    assert.equal(extension.RED_EXT, 0x1903);
    assert.equal(extension.RG_EXT, 0x8227);
    const texture = gl.createTexture();
    gl.bindTexture(0x0de1, texture);
    gl.texImage2D(0x0de1, 0, extension.R8_EXT, 1, 1, 0, extension.RED_EXT, 0x1401, new Uint8Array([71]));
    assert.deepEqual([...texture.pixels], [71, 0, 0, 255]);
    gl.texImage2D(0x0de1, 0, extension.RG8_EXT, 1, 1, 0, extension.RG_EXT, 0x1401, new Uint8Array([71, 143]));
    assert.deepEqual([...texture.pixels], [71, 143, 0, 255]);
  } finally { gpu.restore(); }
});

test('texSubImage2D applies the same legacy single-channel expansion as texImage2D', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: () => {} });
    await gl.ready;
    const texture = gl.createTexture();
    gl.bindTexture(0x0de1, texture);
    gl.texImage2D(0x0de1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, new Uint8Array([0, 0, 0, 0]));
    gl.texSubImage2D(0x0de1, 0, 0, 0, 1, 1, 0x1909, 0x1401, new Uint8Array([96]));
    assert.deepEqual([...texture.pixels], [96, 96, 96, 255]);
  } finally { gpu.restore(); }
});

test('sRGB texture formats retain sRGB GPU sampling formats for regular and S3TC uploads', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: () => {} });
    await gl.ready;
    const texture = gl.createTexture();
    gl.bindTexture(0x0de1, texture);
    gl.texImage2D(0x0de1, 0, 0x8c43, 1, 1, 0, 0x1908, 0x1401, new Uint8Array([128, 64, 32, 255]));
    await gl._ops;
    assert.equal(texture.format, 'rgba8unorm-srgb');
    assert.equal(texture.gpu.format, 'rgba8unorm-srgb');

    const compressed = gl.createTexture();
    gl.bindTexture(0x0de1, compressed);
    gl.compressedTexImage2D(0x0de1, 0, 0x8c4f, 4, 4, 0, 16, new Uint8Array([255, 255, 255, 255, 255, 255, 255, 255, 0x00, 0xf8, 0x00, 0xf8, 0, 0, 0, 0]));
    await gl._ops;
    assert.equal(compressed.format, 'rgba8unorm-srgb');
    assert.equal(compressed.gpu.format, 'rgba8unorm-srgb');
  } finally { gpu.restore(); }
});

test('EXT_sRGB enables sRGB framebuffer views and linear/sRGB clear conversion', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    const extension = gl.getExtension('EXT_sRGB');
    assert.ok(extension);
    assert.ok(gl.getSupportedExtensions().includes('EXT_sRGB'));
    assert.equal(extension.SRGB8_ALPHA8_EXT, 0x8c43);
    assert.equal(gl._targetFor(0).format, 'rgba8unorm');
    gl.enable(0x8db9);
    assert.equal(gl.getError(), 0);
    assert.equal(gl._targetFor(0).format, 'rgba8unorm-srgb');
    gl.clearColor(0.5, 0.25, 1, 1);
    gl._clearPixels(0x4000, { framebuffer: 0, enabled: new Set([0x8db9]), color: [0.5, 0.25, 1, 1], colorMask: [true, true, true, true] });
    assert.deepEqual([...gl._pixels.slice(0, 4)], [188, 137, 255, 255]);

    const texture = gl.createTexture();
    gl.bindTexture(0x0de1, texture);
    gl.texImage2D(0x0de1, 0, extension.SRGB8_ALPHA8_EXT, 1, 1, 0, 0x1908, 0x1401, new Uint8Array([128, 128, 128, 255]));
    const framebuffer = gl.createFramebuffer();
    gl.bindFramebuffer(0x8d40, framebuffer);
    gl.framebufferTexture2D(0x8d40, 0x8ce0, 0x0de1, texture, 0);
    assert.equal(gl.getFramebufferAttachmentParameter(0x8d40, 0x8ce0, 0x8210), 0x8c40);
    assert.equal(gl._targetFor(framebuffer).format, 'rgba8unorm-srgb');
    gl.disable(0x8db9);
    assert.equal(gl._targetFor(framebuffer).format, 'rgba8unorm');
    await gl._ops;
    assert.ok(texture.gpu.viewFormats.includes('rgba8unorm'));
  } finally { gpu.restore(); }
});

test('the S3TC extension decodes DXT1, DXT3, and DXT5 blocks without GPU features', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: () => {} });
    await gl.ready;
    const extension = gl.getExtension('WEBGL_compressed_texture_s3tc');
    assert.ok(extension);
    assert.ok(gl.getSupportedExtensions().includes('WEBGL_compressed_texture_s3tc'));
    const cases = [
      [extension.COMPRESSED_RGB_S3TC_DXT1_EXT, new Uint8Array([0x00, 0xf8, 0x00, 0xf8, 0, 0, 0, 0])],
      [extension.COMPRESSED_RGBA_S3TC_DXT3_EXT, new Uint8Array([255, 255, 255, 255, 255, 255, 255, 255, 0x00, 0xf8, 0x00, 0xf8, 0, 0, 0, 0])],
      [extension.COMPRESSED_RGBA_S3TC_DXT5_EXT, new Uint8Array([255, 255, 0, 0, 0, 0, 0, 0, 0x00, 0xf8, 0x00, 0xf8, 0, 0, 0, 0])],
    ];
    for (const [format, block] of cases) {
      const texture = gl.createTexture();
      gl.bindTexture(0x0de1, texture);
      gl.compressedTexImage2D(0x0de1, 0, format, 4, 4, 0, block.length, block);
      assert.deepEqual([...texture.pixels.subarray(0, 4)], [255, 0, 0, 255]);
      assert.deepEqual([...texture.pixels.subarray(60, 64)], [255, 0, 0, 255]);
    }
    assert.equal(gl.getError(), 0);
  } finally { gpu.restore(); }
});

test('compressed texture subimages update decoded pixels and unsupported formats reject loudly', async () => {
  const gpu = createValidationDevice(), logs = [];
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    const texture = gl.createTexture(), block = new Uint8Array([0x00, 0xf8, 0x00, 0xf8, 0, 0, 0, 0]);
    gl.bindTexture(0x0de1, texture);
    gl.compressedTexImage2D(0x0de1, 0, 0x83f0, 8, 4, 0, 16, new Uint8Array([...block, ...block]));
    gl.compressedTexSubImage2D(0x0de1, 0, 4, 0, 4, 4, 0x83f0, 8, block);
    assert.deepEqual([...texture.pixels.slice(16, 20)], [255, 0, 0, 255]);
    gl.compressedTexImage2D(0x0de1, 0, 0x8e8c, 4, 4, 0, 16, new Uint8Array(16));
    assert.equal(gl.getError(), 0x0502);
    assert.ok(logs.some(line => line.includes('compressed texture format 0x8e8c')));
    gl.compressedTexImage2D(0x0de1, 0, 0x9274, 4, 4, 0, 8, block);
    assert.equal(gl.getError(), 0x0502);
    assert.ok(logs.some(line => line.includes('compressed texture format 0x9274')));
    assert.equal(gl.getExtension('WEBGL_compressed_texture_s3tc').COMPRESSED_RGBA_S3TC_DXT5_EXT, 0x83f3);
    assert.equal(gl.getExtension('EXT_read_format_bgra').BGRA_EXT, 0x80e1);
    assert.ok(gl.getSupportedExtensions().includes('EXT_read_format_bgra'));
    assert.equal(gl.getExtension('EXT_texture_filter_anisotropic').TEXTURE_MAX_ANISOTROPY_EXT, 0x84fe);
  } finally { gpu.restore(); }
});


test('cube maps upload all face targets, generate face mipmaps, and create cube views', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    const texture = gl.createTexture();
    gl.bindTexture(0x8513, texture);
    for (let face=0x8515;face<=0x851a;face++) gl.texImage2D(face,0,0x1908,2,2,0,0x1908,0x1401,new Uint8Array(16).fill(face-0x8514));
    assert.equal(gl.getError(),0);
    gl.generateMipmap(0x8513);
    assert.equal(texture.faceLevels.get(0x8515).get(1).width,1);
    const gpuTexture=gl._ensureTextureGpu(texture);
    assert.deepEqual(gpuTexture.size,[2,2,6]);
    assert.equal(gpu.writes.filter(write=>write.d.texture===gpuTexture).length,12);
    assert.equal(gpu.writes.find(write=>write.d.texture===gpuTexture&&write.d.origin[2]===5).d.origin[2],5);
    assert.equal(gpuTexture.createView({dimension:'cube'}).dimension,'cube');
  } finally { gpu.restore(); }
});

test('3D and 2D-array storage upload layers and generate dimension-correct mipmaps', async () => {
  const gpu=createValidationDevice();
  try {
    const gl=new WebGPUGL(gpu.canvas);await gl.ready;
    const volume=gl.createTexture();gl.bindTexture(0x806f,volume);
    gl.texImage3D(0x806f,0,0x8058,2,2,2,0,0x1908,0x1401,new Uint8Array(32).map((_,i)=>i));
    gl.generateMipmap(0x806f);
    const volumeGpu=gl._ensureTextureGpu(volume);
    assert.equal(volumeGpu.dimension,'3d');assert.deepEqual(volumeGpu.size,[2,2,2]);
    assert.equal(volume.levels.get(1).layers.length,1);
    const array=gl.createTexture();gl.bindTexture(0x8c1a,array);
    gl.texStorage3D(0x8c1a,2,0x8058,2,2,3);
    gl.texSubImage3D(0x8c1a,0,0,0,1,2,2,1,0x1908,0x1401,new Uint8Array(16).fill(99));
    gl.generateMipmap(0x8c1a);
    assert.equal(array.levels.get(1).layers.length,3);
    const arrayGpu=gl._ensureTextureGpu(array);
    assert.equal(arrayGpu.dimension,'2d');assert.deepEqual(arrayGpu.size,[2,2,3]);
    assert.deepEqual([...array.levels.get(0).layers[1].slice(0,4)],[99,99,99,99]);
    assert.equal(arrayGpu.createView({dimension:'2d-array'}).dimension,'2d-array');
    assert.equal(gl.getParameter(0x8c1d),array.handle);
    assert.equal(gl.getError(),0);
  } finally {gpu.restore();}
});

test('cube, 3D, and array GLSL samplers lower to matching separate texture types', () => {
  for (const [type,target,textureType] of [['samplerCube',0x8513,'textureCube'],['sampler3D',0x806f,'texture3D'],['sampler2DArray',0x8c1a,'texture2DArray']]) {
    const source=`#version 300 es\nuniform ${type} image;\nout vec4 color;\nvoid main(){ color=texture(image, vec3(0.0)); }`;
    const result=rewriteGlslForWebGPU(source,'fragment');
    assert.equal(result.samplers[0].target,target);
    assert.match(result.code,new RegExp(`uniform ${textureType} image_bw_t`));
  }
});

test('dimensional samplers create matching WebGPU texture views in draw bind groups', async () => {
  const gpu=createValidationDevice();
  try {
    const gl=new WebGPUGL(gpu.canvas);await gl.ready;
    for(const [target,type,dimension] of [[0x8513,'samplerCube','cube'],[0x806f,'sampler3D','3d'],[0x8c1a,'sampler2DArray','2d-array']]){
      const texture=gl.createTexture();gl.bindTexture(target,texture);
      if(target===0x8513){for(let face=0x8515;face<=0x851a;face++)gl.texImage2D(face,0,0x1908,1,1,0,0x1908,0x1401,new Uint8Array(4));}
      else gl.texImage3D(target,0,0x8058,1,1,1,0,0x1908,0x1401,new Uint8Array(4));
      const sampler={name:'image',type,target,textureBinding:0,samplerBinding:1};
      const program={uniformBlocks:[],stages:{vertex:{},fragment:{layout:{fields:[],size:0},samplers:[sampler]}}};
      const textureUnits=Array.from({length:8},()=>({}));textureUnits[0][target]=texture;
      const state={uniformValues:new Map([['image',0]]),textureUnits};
      const layout={entries:[{binding:0,texture:{viewDimension:dimension}},{binding:1,sampler:{}}]};
      const group=await gl._uniformBindGroup(program,{getBindGroupLayout:()=>layout},state);
      assert.equal(group.entries[0].resource.dimension,dimension);
    }
  } finally {gpu.restore();}
});
