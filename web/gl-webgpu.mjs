// Small first-pass GLctx replacement for the WebGPU backend. The complete GL
// API is being added in stages; this slice keeps context creation, clear, swap,
// and synchronous clear-color readback working while the rest of the bridge is
// still on the WebGL2 fallback path.

import { toWGSL } from './shader.mjs';
import { liftForNaga } from './naga-rt.mjs';
import { uniformLayout, packUniforms, convertIndices, blendFactor, stateKey } from './gl-webgpu-utils.mjs';

const GL = Object.freeze({
  ARRAY_BUFFER: 0x8892, ARRAY_BUFFER_BINDING: 0x8894,
  ELEMENT_ARRAY_BUFFER: 0x8893, ELEMENT_ARRAY_BUFFER_BINDING: 0x8895,
  STATIC_DRAW: 0x88e4, DYNAMIC_DRAW: 0x88e8, STREAM_DRAW: 0x88e0,
  FRAMEBUFFER: 0x8d40, READ_FRAMEBUFFER: 0x8ca8, DRAW_FRAMEBUFFER: 0x8ca9,
  FRAMEBUFFER_BINDING: 0x8ca6, DRAW_FRAMEBUFFER_BINDING: 0x8ca6,
  READ_FRAMEBUFFER_BINDING: 0x8caa, FRAMEBUFFER_COMPLETE: 0x8cd5,
  RENDERBUFFER: 0x8d41, RENDERBUFFER_BINDING: 0x8ca7,
  TEXTURE_2D: 0x0de1, TEXTURE_BINDING_2D: 0x8069,
  COLOR_BUFFER_BIT: 0x4000, DEPTH_BUFFER_BIT: 0x0100, STENCIL_BUFFER_BIT: 0x0400,
  VIEWPORT: 0x0ba2, SCISSOR_BOX: 0x0c10, SCISSOR_TEST: 0x0c11,
  PACK_ALIGNMENT: 0x0d05, PACK_ROW_LENGTH: 0x0d02,
  PACK_SKIP_ROWS: 0x0d03, PACK_SKIP_PIXELS: 0x0d04,
  PIXEL_PACK_BUFFER_BINDING: 0x88ed, PIXEL_UNPACK_BUFFER_BINDING: 0x88ef,
  CURRENT_PROGRAM: 0x8b8d, VERSION: 0x1f02, VENDOR: 0x1f00,
  RENDERER: 0x1f01, SHADING_LANGUAGE_VERSION: 0x8b8c,
  MAX_TEXTURE_IMAGE_UNITS: 0x8872, MAX_COMBINED_TEXTURE_IMAGE_UNITS: 0x8b4d,
  MAX_VERTEX_ATTRIBS: 0x8869,
  NO_ERROR: 0, INVALID_ENUM: 0x0500, INVALID_VALUE: 0x0501,
  INVALID_OPERATION: 0x0502, OUT_OF_MEMORY: 0x0505,
  COMPILE_STATUS: 0x8b81, LINK_STATUS: 0x8b82, INFO_LOG_LENGTH: 0x8b84,
  VERTEX_SHADER: 0x8b31, FRAGMENT_SHADER: 0x8b30,
});

const clamp01 = value => Math.min(1, Math.max(0, Number(value) || 0));

const TYPE_ENUM = new Map([
  ['float', 0x1406], ['int', 0x1404], ['uint', 0x1405], ['bool', 0x8b56],
  ['vec2', 0x8b50], ['vec3', 0x8b51], ['vec4', 0x8b52],
  ['ivec2', 0x8b53], ['ivec3', 0x8b54], ['ivec4', 0x8b55],
  ['bvec2', 0x8b57], ['bvec3', 0x8b58], ['bvec4', 0x8b59],
  ['uvec2', 0x8dc6], ['uvec3', 0x8dc7], ['uvec4', 0x8dc8],
  ['mat2', 0x8b5a], ['mat3', 0x8b5b], ['mat4', 0x8b5c],
  ['mat2x3', 0x8b65], ['mat2x4', 0x8b66], ['mat3x2', 0x8b67],
  ['mat3x4', 0x8b68], ['mat4x2', 0x8b69], ['mat4x3', 0x8b6a],
  ['sampler2D', 0x8b5e],
]);

const SAMPLER_VERTEX_BASE = 2;
const SAMPLER_FRAGMENT_BASE = 18;

function componentCount(type) {
  const vector = /^(?:b|i|u)?vec([234])$/.exec(type);
  if (vector) return Number(vector[1]);
  const matrix = /^mat([234])(?:x([234]))?$/.exec(type);
  return matrix ? Number(matrix[1]) * Number(matrix[2] || matrix[1]) : 1;
}

function glslDeclarations(source, qualifier) {
  const result = [];
  const re = new RegExp(`^[ \\t]*(?:layout\\s*\\(([^)]*)\\)\\s*)?${qualifier}[ \\t]+([A-Za-z_]\\w*)[ \\t]+([A-Za-z_]\\w*)(?:[ \\t]*\\[[ \\t]*(\\d+)[ \\t]*\\])?[ \\t]*;[ \\t]*$`, 'gm');
  for (const match of source.matchAll(re)) result.push({ full: match[0], layout: match[1] || '', type: match[2], name: match[3], arrayLength: Number(match[4] || 1) });
  return result;
}

function rewriteGlslForWebGPU(source, stage, attributeBindings = new Map(), interfaceLocations = new Map()) {
  let code = String(source || '');
  const uniforms = glslDeclarations(code, 'uniform');
  const fields = [];
  const samplers = [];
  const activeUniforms = [];
  for (const uniform of uniforms) {
    if (uniform.type === 'sampler2D') {
      const base = (stage === 'vertex' ? SAMPLER_VERTEX_BASE : SAMPLER_FRAGMENT_BASE) + samplers.length * 2;
      samplers.push({ name: uniform.name, textureBinding: base, samplerBinding: base + 1, arrayLength: uniform.arrayLength });
      activeUniforms.push({ name: uniform.name, type: uniform.type, arrayLength: uniform.arrayLength, sampler: true });
    } else {
      fields.push({ name: uniform.name, type: uniform.type, arrayLength: uniform.arrayLength });
      activeUniforms.push({ name: uniform.name, type: uniform.type, arrayLength: uniform.arrayLength, sampler: false });
    }
    code = code.replace(uniform.full, '');
  }

  let layout = { fields: [], size: 0 };
  if (fields.length) {
    layout = uniformLayout(fields);
    // OpenGL loose uniforms become one std140 interface block per shader
    // stage. The field offsets below are the CPU upload contract; samplers
    // stay separate because WGSL represents each as a texture+sampler pair.
    const binding = stage === 'vertex' ? 0 : 1;
    const declarations = fields.map(field => `${field.type} ${field.name}${field.arrayLength > 1 ? `[${field.arrayLength}]` : ''};`).join('\n');
    const body = code;
    const names = fields.map(field => field.name).sort((a, b) => b.length - a.length);
    if (names.length) {
      const escaped = names.map(name => name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('|');
      code = body.replace(new RegExp(`\\b(${escaped})\\b`, 'g'), (_match, name) => `bw_globals.${name}`);
    } else code = body;
    code = `layout(std140, binding = ${binding}) uniform Globals {\n${declarations}\n} bw_globals;\n${code}`;
  }

  for (const sampler of samplers) {
    const escaped = sampler.name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    code = code.replace(new RegExp(`\\btexture2D?\\s*\\(\\s*${escaped}\\s*,`, 'g'), `texture(sampler2D(${sampler.name}_bw_t, ${sampler.name}_bw_s),`);
    const array = sampler.arrayLength > 1 ? `[${sampler.arrayLength}]` : '';
    code = `layout(binding = ${sampler.textureBinding}) uniform texture2D ${sampler.name}_bw_t${array};\nlayout(binding = ${sampler.samplerBinding}) uniform sampler ${sampler.name}_bw_s${array};\n${code}`;
  }

  if (stage === 'vertex' && attributeBindings.size) {
    code = code.replace(/^[ \t]*(?:layout\s*\(([^)]*)\)\s*)?(?:in|attribute)[ \t]+([A-Za-z_]\w*)[ \t]+([A-Za-z_]\w*)[ \t]*;[ \t]*$/gm,
      (line, layoutText, type, name) => {
        if (!attributeBindings.has(name)) return line;
        return `layout(location = ${attributeBindings.get(name)}) in ${type} ${name};`;
      });
  }

  if (stage === 'vertex') {
    // GLSL uses clip-space z in [-w,+w], while WebGPU clips z in [0,w].
    // Insert the remap at the end of main so later shader assignments cannot overwrite it.
    const main = /void\s+main\s*\([^)]*\)\s*\{/m.exec(code);
    if (main) {
      let depth = 1, cursor = main.index + main[0].length;
      for (; cursor < code.length && depth; cursor++) {
        if (code[cursor] === '{') depth++;
        else if (code[cursor] === '}') depth--;
      }
      if (!depth) code = code.slice(0, cursor - 1) + '\n  gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n' + code.slice(cursor - 1);
    }
  }
  const lifted = liftForNaga(code, stage, interfaceLocations);
  if (!lifted.ok) throw new Error(`GLSL lift failed (${lifted.reason}): ${lifted.detail || ''}`);
  return { code: lifted.code, layout, activeUniforms, samplers };
}

function parseStageInputs(source, stage) {
  const qualifier = stage === 'vertex' ? '(?:in|attribute)' : 'out';
  return glslDeclarations(source, qualifier).map((item, index) => {
    const match = /(?:^|,)\s*location\s*=\s*(\d+)/.exec(item.layout);
    return { name: item.name, type: item.type, arrayLength: item.arrayLength, location: match ? Number(match[1]) : index };
  });
}

function bytesOf(data) {
  if (typeof data === 'number') return new Uint8Array(Math.max(0, data));
  if (data instanceof ArrayBuffer) return new Uint8Array(data.slice(0));
  if (ArrayBuffer.isView(data)) {
    return new Uint8Array(data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength));
  }
  return new Uint8Array();
}

/**
 * WebGL2-shaped context object used by Emscripten's GL library.
 *
 * `canvas` must be a fresh OffscreenCanvas. Emscripten already owns its
 * original canvas through a real WebGL2 context, so the WebGPU canvas cannot be
 * acquired from that same HTML canvas. The bridge still reads pixels through
 * its existing synchronous path; this initial version mirrors clears on the
 * CPU and copies the GPU target to this offscreen canvas on flush.
 */
export class WebGPUGL {
  constructor(canvas, { fallbackContext = null, defaultFramebuffer = 0, log = console.warn } = {}) {
    if (!canvas || typeof canvas.getContext !== 'function') throw new TypeError('WebGPUGL needs a canvas');
    if (typeof navigator === 'undefined' || !navigator.gpu) throw new Error('WebGPU is unavailable');
    this.canvas = canvas;
    this._fallbackContext = fallbackContext;
    this._defaultFramebuffer = defaultFramebuffer;
    this._log = log;
    this._logError = false;
    this._nextId = 1;
    this._clear = [0, 0, 0, 0];
    this._clearDepth = 1;
    this._viewport = [0, 0, canvas.width || 1, canvas.height || 1];
    this._scissor = [...this._viewport];
    this._pack = { alignment: 4, rowLength: 0, skipRows: 0, skipPixels: 0 };
    this._drawFramebuffer = defaultFramebuffer;
    this._readFramebuffer = defaultFramebuffer;
    this._arrayBuffer = null;
    this._elementArrayBuffer = null;
    this._currentProgram = null;
    this._enabled = new Set();
    this._buffers = new Set();
    this._shaders = new Set();
    this._programs = new Set();
    this._textures = new Set();
    this._framebufferObjects = new Set();
    this._renderbuffers = new Set();
    this._vaos = new Set();
    this._defaultVao = { id: 0, elementArrayBuffer: null, attributes: new Map() };
    this._vao = this._defaultVao;
    this._uniformLocations = new Map();
    this._attributes = new Map();
    this._pixels = new Uint8Array(0);
    this._width = Math.max(1, canvas.width | 0);
    this._height = Math.max(1, canvas.height | 0);
    this._colorTexture = null;
    this._depthTexture = null;
    this._context = canvas.getContext('webgpu');
    if (!this._context) throw new Error('Could not create a WebGPU canvas context');

    // Emscripten reads GL constants off GLctx, not only from its own enum table.
    for (let proto = fallbackContext && Object.getPrototypeOf(fallbackContext); proto && proto !== Object.prototype; proto = Object.getPrototypeOf(proto)) {
      for (const name of Object.getOwnPropertyNames(proto)) {
        if (/^[A-Z][A-Z0-9_]*$/.test(name) && typeof fallbackContext?.[name] === 'number') this[name] = fallbackContext[name];
      }
    }
    Object.assign(this, GL);

    this.ready = this._initialize();
    // The C++ context hook also observes this rejection to restore WebGL2.
    this.ready.catch(error => this._reportFailure(error));
    this._ops = this.ready.then(() => undefined);
  }

  async _initialize() {
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) throw new Error('No WebGPU adapter');
    const device = await adapter.requestDevice();
    this._adapter = adapter;
    this._device = device;
    this._format = navigator.gpu.getPreferredCanvasFormat();
    this._configureCanvas();
    this._ensureTargets(this._width, this._height);
    this._pixels = new Uint8Array(this._width * this._height * 4);
    return this;
  }

  _configureCanvas() {
    this._context.configure({
      device: this._device,
      format: this._format,
      alphaMode: 'opaque',
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST,
    });
  }

  _ensureTargets(width, height) {
    width = Math.max(1, Math.min(8192, width | 0));
    height = Math.max(1, Math.min(8192, height | 0));
    if (this._colorTexture && width === this._width && height === this._height) return;
    this._colorTexture?.destroy();
    this._depthTexture?.destroy();
    this._width = width;
    this._height = height;
    this.canvas.width = width;
    this.canvas.height = height;
    this._configureCanvas();
    this._colorTexture = this._device.createTexture({
      size: [width, height, 1], format: this._format,
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
    });
    this._depthTexture = this._device.createTexture({
      size: [width, height, 1], format: 'depth24plus',
      usage: GPUTextureUsage.RENDER_ATTACHMENT,
    });
    const old = this._pixels;
    const oldWidth = this._width;
    const oldHeight = this._height;
    this._pixels = new Uint8Array(width * height * 4);
    for (let y = 0; y < Math.min(height, oldHeight); y++) {
      const copyWidth = Math.min(width, oldWidth);
      this._pixels.set(old.subarray(y * oldWidth * 4, y * oldWidth * 4 + copyWidth * 4), y * width * 4);
    }
  }

  _enqueue(operation) {
    this._ops = this._ops.then(() => this.ready).then(() => operation()).catch(error => {
      this._reportFailure(error);
    });
    return this._ops;
  }

  _reportFailure(error) {
    if (this._logError) return;
    this._logError = true;
    try { this._log('gl-webgpu: ' + (error?.message || error)); } catch {}
    try { this.onFailure?.(error); } catch {}
  }

  _clearGpu(mask) {
    this._ensureTargets(this._width, this._height);
    const encoder = this._device.createCommandEncoder();
    const descriptor = {
      colorAttachments: [{
        view: this._colorTexture.createView(),
        clearValue: { r: this._clear[0], g: this._clear[1], b: this._clear[2], a: this._clear[3] },
        loadOp: mask & GL.COLOR_BUFFER_BIT ? 'clear' : 'load', storeOp: 'store',
      }],
      depthStencilAttachment: {
        view: this._depthTexture.createView(),
        depthClearValue: this._clearDepth,
        depthLoadOp: mask & GL.DEPTH_BUFFER_BIT ? 'clear' : 'load',
        depthStoreOp: 'store',
      },
    };
    const pass = encoder.beginRenderPass(descriptor);
    pass.end();
    this._device.queue.submit([encoder.finish()]);
  }

  _clearPixels(mask) {
    if (!(mask & GL.COLOR_BUFFER_BIT)) return;
    const rgba = this._clear.map(value => Math.round(clamp01(value) * 255));
    for (let i = 0; i < this._pixels.length; i += 4) {
      this._pixels[i] = rgba[0]; this._pixels[i + 1] = rgba[1];
      this._pixels[i + 2] = rgba[2]; this._pixels[i + 3] = rgba[3];
    }
  }

  clearColor(r, g, b, a) { this._clear = [clamp01(r), clamp01(g), clamp01(b), clamp01(a)]; }
  clearDepth(value) { this._clearDepth = clamp01(value); }
  clear(mask) { this._clearPixels(mask); this._enqueue(() => this._clearGpu(mask)); }
  viewport(x, y, width, height) {
    this._viewport = [x | 0, y | 0, width | 0, height | 0];
    this._ensureTargets(Math.max(1, x + width), Math.max(1, y + height));
  }
  scissor(x, y, width, height) { this._scissor = [x | 0, y | 0, width | 0, height | 0]; }
  enable(capability) { this._enabled.add(capability); }
  disable(capability) { this._enabled.delete(capability); }
  isEnabled(capability) { return this._enabled.has(capability); }

  getParameter(parameter) {
    switch (parameter) {
      case GL.VERSION: return 'OpenGL ES 3.0 WebGPUGL';
      case GL.VENDOR: return 'WebGPU';
      case GL.RENDERER: return 'WebGPU GL translation';
      case GL.SHADING_LANGUAGE_VERSION: return 'WebGL GLSL ES 3.00';
      case GL.VIEWPORT: return new Int32Array(this._viewport);
      case GL.SCISSOR_BOX: return new Int32Array(this._scissor);
      case GL.DRAW_FRAMEBUFFER_BINDING: case GL.FRAMEBUFFER_BINDING: return this._drawFramebuffer;
      case GL.READ_FRAMEBUFFER_BINDING: return this._readFramebuffer;
      case GL.ARRAY_BUFFER_BINDING: return this._arrayBuffer;
      case GL.ELEMENT_ARRAY_BUFFER_BINDING: return this._elementArrayBuffer;
      case GL.CURRENT_PROGRAM: return this._currentProgram;
      case GL.PACK_ALIGNMENT: return this._pack.alignment;
      case GL.PACK_ROW_LENGTH: return this._pack.rowLength;
      case GL.PACK_SKIP_ROWS: return this._pack.skipRows;
      case GL.PACK_SKIP_PIXELS: return this._pack.skipPixels;
      case GL.MAX_TEXTURE_IMAGE_UNITS: return 8;
      case GL.MAX_COMBINED_TEXTURE_IMAGE_UNITS: return 8;
      case 0x8869: return 16; // GL_MAX_VERTEX_ATTRIBS
      case GL.FRAMEBUFFER_COMPLETE: return GL.FRAMEBUFFER_COMPLETE;
      default: return 0;
    }
  }

  pixelStorei(parameter, value) {
    if (parameter === GL.PACK_ALIGNMENT) this._pack.alignment = Math.max(1, value | 0);
    else if (parameter === GL.PACK_ROW_LENGTH) this._pack.rowLength = Math.max(0, value | 0);
    else if (parameter === GL.PACK_SKIP_ROWS) this._pack.skipRows = Math.max(0, value | 0);
    else if (parameter === GL.PACK_SKIP_PIXELS) this._pack.skipPixels = Math.max(0, value | 0);
  }

  readPixels(x, y, width, height, format, type, destination) {
    if (!(destination instanceof Uint8Array) && !(destination instanceof Uint8ClampedArray)) return;
    if (format !== 0x1908 || type !== 0x1401) return;
    const rowLength = this._pack.rowLength || width;
    const rawStride = rowLength * 4;
    const stride = Math.ceil(rawStride / this._pack.alignment) * this._pack.alignment;
    const base = this._pack.skipRows * stride + this._pack.skipPixels * 4;
    for (let row = 0; row < height; row++) {
      const sourceY = y + row;
      if (sourceY < 0 || sourceY >= this._height) continue;
      for (let col = 0; col < width; col++) {
        const sourceX = x + col;
        if (sourceX < 0 || sourceX >= this._width) continue;
        const src = (sourceY * this._width + sourceX) * 4;
        const dst = base + row * stride + col * 4;
        if (dst + 4 <= destination.length) destination.set(this._pixels.subarray(src, src + 4), dst);
      }
    }
  }

  flush() { this._enqueue(() => this._present()); }
  finish() { this.flush(); }
  async _present() {
    const encoder = this._device.createCommandEncoder();
    const canvasTexture = this._context.getCurrentTexture();
    encoder.copyTextureToTexture({ texture: this._colorTexture }, { texture: canvasTexture }, [this._width, this._height, 1]);
    this._device.queue.submit([encoder.finish()]);
  }
  getError() { return GL.NO_ERROR; }
  isContextLost() { return false; }
  getContextAttributes() { return { alpha: true, depth: true, stencil: false, antialias: false, premultipliedAlpha: true, preserveDrawingBuffer: true }; }
  getSupportedExtensions() { return []; }
  getExtension() { return null; }

  // S2a keeps the original client bytes because WebGPU buffers cannot be read
  // synchronously. GPU copies are created lazily with every buffer role the GL
  // bridge may assign, then refreshed from this shadow before a draw.
  createBuffer() { const object = { id: this._nextId++, data: new Uint8Array(), gpu: null, dirty: true }; this._buffers.add(object); return object; }
  deleteBuffer(buffer) {
    if (!buffer) return;
    buffer.gpu?.destroy(); buffer.gpu = null; this._buffers.delete(buffer);
    if (this._arrayBuffer === buffer) this._arrayBuffer = null;
    if (this._elementArrayBuffer === buffer) this._elementArrayBuffer = null;
    for (const vao of [this._defaultVao, ...this._vaos]) {
      if (vao.elementArrayBuffer === buffer) vao.elementArrayBuffer = null;
      for (const attribute of vao.attributes.values()) if (attribute.buffer === buffer) attribute.buffer = null;
    }
  }
  isBuffer(buffer) { return this._buffers.has(buffer); }
  bindBuffer(target, buffer) {
    const value = buffer || null;
    if (target === GL.ARRAY_BUFFER) this._arrayBuffer = value;
    else if (target === GL.ELEMENT_ARRAY_BUFFER) { this._elementArrayBuffer = value; this._vao.elementArrayBuffer = value; }
  }
  _boundBuffer(target) { return target === GL.ARRAY_BUFFER ? this._arrayBuffer : target === GL.ELEMENT_ARRAY_BUFFER ? this._elementArrayBuffer : null; }
  bufferData(target, data, usage = GL.STATIC_DRAW) {
    const buffer = this._boundBuffer(target);
    if (!buffer) return;
    const length = typeof data === 'number' ? Math.max(0, data | 0) : bytesOf(data).length;
    buffer.data = typeof data === 'number' ? new Uint8Array(length) : bytesOf(data);
    buffer.usage = usage; buffer.dirty = true;
  }
  bufferSubData(target, offset, data) {
    const buffer = this._boundBuffer(target);
    if (!buffer) return;
    const bytes = bytesOf(data), next = new Uint8Array(Math.max(buffer.data.length, offset + bytes.length));
    next.set(buffer.data); next.set(bytes, offset); buffer.data = next; buffer.dirty = true;
  }
  getBufferParameter(target, parameter) {
    const buffer = this._boundBuffer(target);
    if (parameter === 0x8764) return buffer?.data?.length || 0;
    if (parameter === 0x8765) return buffer?.usage || GL.STATIC_DRAW;
    return 0;
  }
  _attribute(index) {
    index |= 0;
    let attribute = this._vao.attributes.get(index);
    if (!attribute) {
      attribute = { index, enabled: false, buffer: null, size: 4, type: 0x1406, normalized: false, stride: 0, offset: 0, integer: false, divisor: 0, current: new Float32Array([0, 0, 0, 1]) };
      this._vao.attributes.set(index, attribute);
    }
    return attribute;
  }
  createVertexArray() { const object = { id: this._nextId++, elementArrayBuffer: null, attributes: new Map() }; this._vaos.add(object); return object; }
  deleteVertexArray(vao) { this._vaos.delete(vao); if (this._vao === vao) this.bindVertexArray(null); }
  isVertexArray(vao) { return this._vaos.has(vao); }
  bindVertexArray(vao) {
    this._vao = vao || this._defaultVao;
    this._elementArrayBuffer = this._vao.elementArrayBuffer;
  }
  enableVertexAttribArray(index) { this._attribute(index).enabled = true; }
  disableVertexAttribArray(index) { this._attribute(index).enabled = false; }
  vertexAttribPointer(index, size, type, normalized, stride, offset) {
    Object.assign(this._attribute(index), { buffer: this._arrayBuffer, size: size | 0, type: type | 0, normalized: !!normalized, stride: stride | 0, offset: offset | 0, integer: false });
  }
  vertexAttribIPointer(index, size, type, stride, offset) {
    Object.assign(this._attribute(index), { buffer: this._arrayBuffer, size: size | 0, type: type | 0, normalized: false, stride: stride | 0, offset: offset | 0, integer: true });
  }
  vertexAttribDivisor(index, divisor) { this._attribute(index).divisor = divisor | 0; }
  vertexAttrib1f(index, x) { this._attribute(index).current.set([x, 0, 0, 1]); }
  vertexAttrib2f(index, x, y) { this._attribute(index).current.set([x, y, 0, 1]); }
  vertexAttrib3f(index, x, y, z) { this._attribute(index).current.set([x, y, z, 1]); }
  vertexAttrib4f(index, x, y, z, w) { this._attribute(index).current.set([x, y, z, w]); }
  vertexAttrib1fv(index, value) { this.vertexAttrib1f(index, value[0]); }
  vertexAttrib2fv(index, value) { this.vertexAttrib2f(index, value[0], value[1]); }
  vertexAttrib3fv(index, value) { this.vertexAttrib3f(index, value[0], value[1], value[2]); }
  vertexAttrib4fv(index, value) { this.vertexAttrib4f(index, value[0], value[1], value[2], value[3]); }
  vertexAttribI4i(index, x, y, z, w) { this.vertexAttrib4f(index, x, y, z, w); }
  vertexAttribI4ui(index, x, y, z, w) { this.vertexAttrib4f(index, x, y, z, w); }
  getVertexAttrib(index, parameter) {
    const a = this._attribute(index);
    switch (parameter) {
      case 0x8622: return a.enabled;
      case 0x8623: return a.size;
      case 0x8624: return a.stride;
      case 0x8625: return a.type;
      case 0x8626: return a.current;
      case 0x886a: return a.normalized;
      case 0x889f: return a.buffer;
      case 0x88fd: return a.integer;
      case 0x88fe: return a.divisor;
      default: return 0;
    }
  }
  getVertexAttribOffset(index) { return this._attribute(index).offset; }

  _syncBuffer(buffer) {
    if (!buffer) return null;
    const usage = GPUBufferUsage.COPY_DST | GPUBufferUsage.VERTEX | GPUBufferUsage.INDEX | GPUBufferUsage.UNIFORM | GPUBufferUsage.STORAGE;
    const size = Math.max(4, Math.ceil(buffer.data.length / 4) * 4);
    if (!buffer.gpu || buffer.gpu.size !== size || buffer.gpuUsage !== usage) {
      buffer.gpu?.destroy();
      buffer.gpu = this._device.createBuffer({ size, usage });
      buffer.gpuUsage = usage; buffer.dirty = true;
    }
    if (buffer.dirty && buffer.data.length) {
      const padded = buffer.data.length % 4 ? new Uint8Array(size).map((_, i) => buffer.data[i] || 0) : buffer.data;
      this._device.queue.writeBuffer(buffer.gpu, 0, padded);
    }
    buffer.dirty = false;
    return buffer.gpu;
  }

  createShader(type) { const shader = { id: this._nextId++, type, source: '', compiled: false, infoLog: '' }; this._shaders.add(shader); return shader; }
  deleteShader(shader) { this._shaders.delete(shader); }
  isShader(shader) { return this._shaders.has(shader); }
  shaderSource(shader, source) { if (shader) shader.source = String(source); }
  compileShader(shader) {
    if (!shader) return;
    // Translation happens at link time so bindAttribLocation calls made after
    // compileShader still affect the emitted WGSL locations.
    shader.compiled = true; shader.infoLog = '';
  }
  getShaderParameter(shader, parameter) { return parameter === GL.COMPILE_STATUS ? !!shader?.compiled : 0; }
  getShaderInfoLog(shader) { return shader?.infoLog || ''; }
  getShaderSource(shader) { return shader?.source || ''; }
  getShaderPrecisionFormat() { return { rangeMin: 0, rangeMax: 0, precision: 0 }; }
  createProgram() { const program = { id: this._nextId++, shaders: [], attributes: new Map(), attributeLocations: new Map(), attributeInfo: [], uniformInfo: [], uniformValues: new Map(), uniformLocations: new Map(), stages: {}, infoLog: '' }; this._programs.add(program); return program; }
  deleteProgram(program) { this._programs.delete(program); if (this._currentProgram === program) this._currentProgram = null; }
  isProgram(program) { return this._programs.has(program); }
  attachShader(program, shader) { if (program && shader) program.shaders.push(shader); }
  detachShader(program, shader) { if (program) program.shaders = program.shaders.filter(item => item !== shader); }
  bindAttribLocation(program, index, name) { if (program) program.attributes.set(String(name), index | 0); }
  linkProgram(program) {
    if (!program) return;
    const shaders = program.shaders.filter(shader => shader.compiled);
    const vertex = shaders.find(shader => shader.type === GL.VERTEX_SHADER);
    const fragment = shaders.find(shader => shader.type === GL.FRAGMENT_SHADER);
    program.linked = Boolean(vertex && fragment);
    program.infoLog = program.linked ? '' : 'A vertex and fragment shader are required';
    program.attributeLocations.clear(); program.attributeInfo = [];
    program.uniformInfo = []; program.uniformLocations.clear(); program.uniformValues.clear();
    program.stages = {};
    if (!program.linked) { program.linkPromise = Promise.resolve(false); return; }

    for (const declaration of parseStageInputs(vertex.source, 'vertex')) {
      const location = program.attributes.has(declaration.name) ? program.attributes.get(declaration.name) : declaration.location;
      program.attributeLocations.set(declaration.name, location);
      program.attributeInfo.push({ name: declaration.name, size: declaration.arrayLength, type: TYPE_ENUM.get(declaration.type) || 0x1406 });
    }
    for (const shader of [vertex, fragment]) {
      for (const declaration of glslDeclarations(shader.source, 'uniform')) {
        if (program.uniformInfo.some(info => info.name.replace(/\[0\]$/, '') === declaration.name)) continue;
        const name = declaration.arrayLength > 1 ? `${declaration.name}[0]` : declaration.name;
        const info = { name, baseName: declaration.name, size: declaration.arrayLength, type: TYPE_ENUM.get(declaration.type) || 0x1406 };
        program.uniformInfo.push(info);
        const location = { program, name: declaration.name, id: this._uniformLocations.size };
        this._uniformLocations.set(`${program.id}:${declaration.name}`, location);
        program.uniformLocations.set(declaration.name, location);
        program.uniformValues.set(declaration.name, declaration.type === 'sampler2D' ? 0 : new Float32Array(Math.max(1, declaration.arrayLength) * componentCount(declaration.type)));
      }
    }
    const locationRegistry = new Map();
    program.linkPromise = this.ready.then(async () => {
      for (const [shader, stage] of [[vertex, 'vertex'], [fragment, 'fragment']]) {
        const prepared = rewriteGlslForWebGPU(shader.source, stage, program.attributes, locationRegistry);
        const wgsl = await toWGSL('glsl', prepared.code, stage);
        if (!wgsl) throw new Error(`${stage} shader translated to empty WGSL`);
        const activeInputs = stage === 'vertex'
          ? [...prepared.code.matchAll(/layout\s*\([^)]*location\s*=\s*(\d+)[^)]*\)\s*in\s+([A-Za-z_]\w*)\s+([A-Za-z_]\w*)/g)]
          : [];
        for (const match of activeInputs) program.attributeLocations.set(match[3], Number(match[1]));
        program.stages[stage] = { wgsl, layout: prepared.layout, activeUniforms: prepared.activeUniforms, samplers: prepared.samplers };
      }
      program.linked = true;
      return true;
    }).catch(error => {
      program.linked = false;
      program.infoLog = String(error?.message || error);
      this._log(`gl-webgpu: program ${program.id} link failed: ${program.infoLog}`);
      return false;
    });
  }
  validateProgram() {}
  useProgram(program) { this._currentProgram = program || null; }
  getProgramParameter(program, parameter) {
    if (parameter === GL.LINK_STATUS) return !!program?.linked;
    if (parameter === 0x8b85) return program?.shaders?.length || 0;
    if (parameter === 0x8b86) return program?.uniformInfo?.length || 0;
    if (parameter === 0x8b89) return program?.attributeInfo?.length || 0;
    return 0;
  }
  getProgramInfoLog(program) { return program?.infoLog || ''; }
  getAttachedShaders(program) { return program?.shaders?.slice() || []; }
  getAttribLocation(program, name) {
    if (!program) return -1;
    return program.attributeLocations.has(name) ? program.attributeLocations.get(name) : program.attributes.has(name) ? program.attributes.get(name) : -1;
  }
  getUniformLocation(program, name) {
    if (!program) return null;
    const base = String(name).replace(/\[0\]$/, '');
    if (program.uniformLocations.has(base)) return program.uniformLocations.get(base);
    const info = program.uniformInfo.find(item => item.baseName === base);
    if (!info) return null;
    const key = `${program.id}:${base}`;
    if (!this._uniformLocations.has(key)) this._uniformLocations.set(key, { program, name: base, id: this._uniformLocations.size });
    const location = this._uniformLocations.get(key);
    program.uniformLocations.set(base, location);
    return location;
  }
  getActiveAttrib(program, index) { const item = program?.attributeInfo?.[index]; return item ? { ...item } : null; }
  getActiveUniform(program, index) { const item = program?.uniformInfo?.[index]; return item ? { name: item.name, size: item.size, type: item.type } : null; }
  _setUniform(location, value) {
    if (!location?.program || location.program !== this._currentProgram) return;
    location.program.uniformValues.set(location.name, ArrayBuffer.isView(value) ? value.slice() : Array.isArray(value) ? value.slice() : value);
  }
  getUniform(program, location) { return program?.uniformValues.get(location?.name) ?? 0; }
  uniform1f(location, x) { this._setUniform(location, Number(x)); }
  uniform1fv(location, value) { this._setUniform(location, value); }
  uniform1i(location, x) { this._setUniform(location, Number(x) | 0); }
  uniform1iv(location, value) { this._setUniform(location, value); }
  uniform2f(location, x, y) { this._setUniform(location, [x, y]); }
  uniform2fv(location, value) { this._setUniform(location, value); }
  uniform2i(location, x, y) { this._setUniform(location, [x | 0, y | 0]); }
  uniform2iv(location, value) { this._setUniform(location, value); }
  uniform3f(location, x, y, z) { this._setUniform(location, [x, y, z]); }
  uniform3fv(location, value) { this._setUniform(location, value); }
  uniform3i(location, x, y, z) { this._setUniform(location, [x | 0, y | 0, z | 0]); }
  uniform3iv(location, value) { this._setUniform(location, value); }
  uniform4f(location, x, y, z, w) { this._setUniform(location, [x, y, z, w]); }
  uniform4fv(location, value) { this._setUniform(location, value); }
  uniform4i(location, x, y, z, w) { this._setUniform(location, [x | 0, y | 0, z | 0, w | 0]); }
  uniform4iv(location, value) { this._setUniform(location, value); }
  uniformMatrix2fv(location, transpose, value) { this._setUniform(location, value); }
  uniformMatrix3fv(location, transpose, value) { this._setUniform(location, value); }
  uniformMatrix4fv(location, transpose, value) { this._setUniform(location, value); }
  drawArrays() {}
  drawElements() {}
  drawArraysInstanced() {}
  drawElementsInstanced() {}
  blendFunc() {}
  blendFuncSeparate() {}
  blendEquation() {}
  blendEquationSeparate() {}
  colorMask() {}
  depthMask() {}
  depthFunc() {}
  cullFace() {}
  frontFace() {}
  scissor() {}
  stencilFunc() {}
  stencilFuncSeparate() {}
  stencilOp() {}
  stencilOpSeparate() {}
  stencilMask() {}
  polygonOffset() {}
  sampleCoverage() {}

  createTexture() { const texture = { id: this._nextId++ }; this._textures.add(texture); return texture; }
  deleteTexture(texture) { this._textures.delete(texture); }
  isTexture(texture) { return this._textures.has(texture); }
  bindTexture() {}
  texImage2D() {}
  texSubImage2D() {}
  texParameteri() {}
  texParameterf() {}
  generateMipmap() {}
  activeTexture() {}
  createFramebuffer() { const object = { id: this._nextId++ }; this._framebufferObjects.add(object); return object; }
  deleteFramebuffer(framebuffer) { this._framebufferObjects.delete(framebuffer); }
  isFramebuffer(framebuffer) { return this._framebufferObjects.has(framebuffer); }
  bindFramebuffer(target, framebuffer) {
    if (target === GL.READ_FRAMEBUFFER) this._readFramebuffer = framebuffer || 0;
    else if (target === GL.DRAW_FRAMEBUFFER) this._drawFramebuffer = framebuffer || 0;
    else { this._readFramebuffer = framebuffer || 0; this._drawFramebuffer = framebuffer || 0; }
  }
  framebufferTexture2D() {}
  framebufferRenderbuffer() {}
  checkFramebufferStatus() { return GL.FRAMEBUFFER_COMPLETE; }
  createRenderbuffer() { const object = { id: this._nextId++ }; this._renderbuffers.add(object); return object; }
  deleteRenderbuffer(object) { this._renderbuffers.delete(object); }
  isRenderbuffer(object) { return this._renderbuffers.has(object); }
  bindRenderbuffer() {}
  renderbufferStorage() {}
  getRenderbufferParameter() { return 0; }
  getFramebufferAttachmentParameter() { return 0; }
  blitFramebuffer() {}
  drawBuffers() {}
  readBuffer() {}
}

/** Create a context object for the GL64 bridge's post-create GLctx hook. */
export function createWebGPUGLContext(originalContext, defaultFramebuffer = 0, log = console.warn) {
  if (typeof OffscreenCanvas !== 'function') return null;
  if (typeof navigator === 'undefined' || !navigator.gpu) return null;
  const width = Math.max(1, originalContext?.canvas?.width | 0);
  const height = Math.max(1, originalContext?.canvas?.height | 0);
  try {
    const canvas = new OffscreenCanvas(width, height);
    return new WebGPUGL(canvas, { fallbackContext: originalContext, defaultFramebuffer, log });
  } catch (error) {
    log('gl-webgpu: context setup failed — ' + (error?.message || error));
    return null;
  }
}
