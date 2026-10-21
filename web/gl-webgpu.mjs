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

function identityMatrix() { return new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]); }
function mulMat4(matrix, vector) {
  const out = new Array(4);
  for (let row = 0; row < 4; row++) out[row] = matrix[row] * vector[0] + matrix[4 + row] * vector[1] + matrix[8 + row] * vector[2] + matrix[12 + row] * vector[3];
  return out;
}

const GL_TOPOLOGY = new Map([[0x0000, 'point-list'], [0x0001, 'line-list'], [0x0002, 'line-strip'], [0x0004, 'triangle-list'], [0x0005, 'triangle-strip']]);
const GL_COMPARE = new Map([[0x0200, 'never'], [0x0201, 'less'], [0x0202, 'equal'], [0x0203, 'less-equal'], [0x0204, 'greater'], [0x0205, 'not-equal'], [0x0206, 'greater-equal'], [0x0207, 'always']]);
const GL_BLEND_OP = new Map([[0x8006, 'add'], [0x800a, 'subtract'], [0x800b, 'reverse-subtract'], [0x8007, 'min'], [0x8008, 'max']]);
const signedType = type => [0x1400, 0x1402, 0x1404].includes(type);
function halfToFloat(value) {
  const sign = (value & 0x8000) ? -1 : 1, exponent = (value >> 10) & 0x1f, fraction = value & 0x3ff;
  if (exponent === 0) return sign * 2 ** -14 * (fraction / 1024);
  if (exponent === 31) return fraction ? NaN : sign * Infinity;
  return sign * 2 ** (exponent - 15) * (1 + fraction / 1024);
}

function nativeVertexFormat(attribute) {
  const { type, size, normalized, integer } = attribute;
  const suffix = size === 1 ? '' : `x${size}`;
  if (type === 0x1406) return `float32${suffix}`;
  if (type === 0x1404) return `sint32${suffix}`;
  if (type === 0x1405) return `uint32${suffix}`;
  const small = {
    '5120:true': `snorm8${suffix}`, '5121:true': `unorm8${suffix}`,
    '5122:true': `snorm16${suffix}`, '5123:true': `unorm16${suffix}`,
    '5120:false': `sint8${suffix}`, '5121:false': `uint8${suffix}`,
    '5122:false': `sint16${suffix}`, '5123:false': `uint16${suffix}`,
    '5131:false': `float16${suffix}`,
  };
  const format = small[`${type}:${normalized}`];
  if (!format) return null;
  if ([0x1400, 0x1401, 0x1402, 0x1403, 0x140b].includes(type) && size !== 2 && size !== 4) return null;
  if (type === 0x8363) return null;
  if (!integer && !normalized && [0x1400, 0x1401, 0x1402, 0x1403].includes(type)) return null;
  return format;
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
    this._blend = { srcRGB: 1, dstRGB: 0, srcAlpha: 1, dstAlpha: 0, equationRGB: 0x8006, equationAlpha: 0x8006 };
    this._blendColor = [0, 0, 0, 0];
    this._depthFunc = 0x0201;
    this._depthMask = true;
    this._cullFace = 0x0405;
    this._frontFace = 0x0901;
    this._colorMask = [true, true, true, true];
    this._pipelineCache = new Map();
    this._clearPipelineCache = new Map();
    this._constantBuffers = new Map();
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

  async _clearGpu(mask, state) {
    this._ensureTargets(this._width, this._height);
    const scissor = state.enabled.has(0x0c11);
    const fullColor = (mask & GL.COLOR_BUFFER_BIT) && state.colorMask.every(Boolean) && !scissor;
    const fullDepth = (mask & GL.DEPTH_BUFFER_BIT) && state.depthMask && !scissor;
    const partialColor = (mask & GL.COLOR_BUFFER_BIT) && !fullColor && state.colorMask.some(Boolean);
    const partialDepth = (mask & GL.DEPTH_BUFFER_BIT) && !fullDepth && state.depthMask;
    if (!fullColor && !fullDepth && !partialColor && !partialDepth) return;
    const encoder = this._device.createCommandEncoder();
    const descriptor = {
      colorAttachments: [{
        view: this._colorTexture.createView(),
        clearValue: { r: state.color[0], g: state.color[1], b: state.color[2], a: state.color[3] },
        loadOp: fullColor ? 'clear' : 'load', storeOp: 'store',
      }],
      depthStencilAttachment: {
        view: this._depthTexture.createView(),
        depthClearValue: state.depth,
        depthLoadOp: fullDepth ? 'clear' : 'load',
        depthStoreOp: 'store',
      },
    };
    const pass = encoder.beginRenderPass(descriptor);
    if (partialColor || partialDepth) this._drawClearRect(pass, state, partialColor, partialDepth);
    pass.end();
    this._device.queue.submit([encoder.finish()]);
  }

  _drawClearRect(pass, state, clearColor, clearDepth) {
    const color = state.color.map(value => Number(value).toPrecision(8)).join(',');
    const depth = Number(state.depth).toPrecision(8);
    const mask = state.colorMask.reduce((bits, enabled, index) => enabled && clearColor ? bits | (1 << index) : bits, 0);
    const key = stateKey([color, depth, mask, clearColor, clearDepth, this._format]);
    let pipeline = this._clearPipelineCache.get(key);
    if (!pipeline) {
      const fields = [];
      if (clearColor) fields.push('@location(0) color: vec4<f32>,');
      if (clearDepth) fields.push('@builtin(frag_depth) depth: f32,');
      const value = `${clearColor ? `color: vec4<f32>(${color}),` : ''}${clearDepth ? `depth: ${depth},` : ''}`;
      const code = `struct Out { ${fields.join(' ')} };\n@vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4<f32>{var p=array<vec2<f32>,3>(vec2<f32>(-1.0,-1.0),vec2<f32>(3.0,-1.0),vec2<f32>(-1.0,3.0));return vec4<f32>(p[i],0.0,1.0);}\n@fragment fn fs()->Out{return Out(${value});}`;
      pipeline = this._device.createRenderPipeline({
        layout: 'auto', vertex: { module: this._device.createShaderModule({ code }), entryPoint: 'vs' },
        fragment: { module: this._device.createShaderModule({ code }), entryPoint: 'fs', targets: [{ format: this._format, writeMask: mask }] },
        primitive: { topology: 'triangle-list' },
        depthStencil: { format: 'depth24plus', depthWriteEnabled: clearDepth, depthCompare: 'always' },
      });
      this._clearPipelineCache.set(key, pipeline);
    }
    pass.setPipeline(pipeline);
    pass.setViewport(0, 0, this._width, this._height, 0, 1);
    if (state.enabled.has(0x0c11)) {
      const [sx, sy, sw, sh] = state.scissor;
      const left = Math.max(0, sx), top = Math.max(0, this._height - sy - sh);
      pass.setScissorRect(left, top, Math.max(0, Math.min(this._width - left, sw)), Math.max(0, Math.min(this._height - top, sh)));
    }
    pass.draw(3);
  }

  _clearPixels(mask, state) {
    if (!(mask & GL.COLOR_BUFFER_BIT)) return;
    const rgba = state.color.map(value => Math.round(clamp01(value) * 255));
    let left = 0, right = this._width, top = 0, bottom = this._height;
    if (state.enabled.has(0x0c11)) {
      const [x, y, width, height] = state.scissor;
      left = Math.max(0, x); right = Math.min(this._width, x + width);
      top = Math.max(0, y); bottom = Math.min(this._height, y + height);
    }
    for (let y = top; y < bottom; y++) for (let x = left; x < right; x++) {
      const i = (y * this._width + x) * 4;
      for (let channel = 0; channel < 4; channel++) if (state.colorMask[channel]) this._pixels[i + channel] = rgba[channel];
    }
  }

  clearColor(r, g, b, a) { this._clear = [clamp01(r), clamp01(g), clamp01(b), clamp01(a)]; }
  clearDepth(value) { this._clearDepth = clamp01(value); }
  clear(mask) {
    const state = { color: [...this._clear], depth: this._clearDepth, enabled: new Set(this._enabled), colorMask: [...this._colorMask], depthMask: this._depthMask, scissor: [...this._scissor] };
    this._clearPixels(mask, state);
    this._enqueue(() => this._clearGpu(mask, state));
  }
  viewport(x, y, width, height) {
    this._viewport = [x | 0, y | 0, width | 0, height | 0];
    if (!this._drawFramebuffer && (x + width > this._width || y + height > this._height))
      this._ensureTargets(Math.max(this._width, x + width), Math.max(this._height, y + height));
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
  createBuffer() { const object = { id: this._nextId++, data: new Uint8Array(), gpu: null, dirty: true, version: 0, vertexCache: new Map() }; this._buffers.add(object); return object; }
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
    buffer.usage = usage; buffer.dirty = true; buffer.version++; buffer.vertexCache.clear();
  }
  bufferSubData(target, offset, data) {
    const buffer = this._boundBuffer(target);
    if (!buffer) return;
    const bytes = bytesOf(data), next = new Uint8Array(Math.max(buffer.data.length, offset + bytes.length));
    next.set(buffer.data); next.set(bytes, offset); buffer.data = next; buffer.dirty = true; buffer.version++; buffer.vertexCache.clear();
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

  _vertexSpec(attribute) {
    const buffer = attribute.buffer;
    const sourceBytes = attribute.type === 0x1400 || attribute.type === 0x1401 ? 1
      : [0x1402, 0x1403, 0x140b].includes(attribute.type) ? 2 : 4;
    const native = nativeVertexFormat(attribute);
    if (native && buffer) return {
      format: native,
      arrayStride: attribute.stride || sourceBytes * attribute.size,
      offset: attribute.offset,
      buffer: this._syncBuffer(buffer),
      stepMode: attribute.divisor ? 'instance' : 'vertex',
    };
    if (buffer) {
      const key = stateKey([buffer.version, attribute.index, attribute.type, attribute.size, attribute.normalized, attribute.integer, attribute.stride, attribute.offset]);
      let cached = buffer.vertexCache.get(key);
      if (!cached) {
        const inView = new DataView(buffer.data.buffer, buffer.data.byteOffset, buffer.data.byteLength);
        const inStride = attribute.stride || sourceBytes * attribute.size;
        const count = Math.max(1, Math.floor(Math.max(0, buffer.data.length - attribute.offset) / inStride));
        const componentSize = 4, out = new Uint8Array(count * attribute.size * componentSize);
        const view = new DataView(out.buffer);
        const signed = attribute.type === 0x1400 || attribute.type === 0x1402 || attribute.type === 0x1404;
        for (let vertex = 0; vertex < count; vertex++) for (let component = 0; component < attribute.size; component++) {
          const offset = attribute.offset + vertex * inStride + component * sourceBytes;
          let value = 0;
          if (offset + sourceBytes <= buffer.data.length) {
            if (attribute.type === 0x1400) value = inView.getInt8(offset);
            else if (attribute.type === 0x1401) value = inView.getUint8(offset);
            else if (attribute.type === 0x1402) value = inView.getInt16(offset, true);
            else if (attribute.type === 0x1403) value = inView.getUint16(offset, true);
            else if (attribute.type === 0x1404) value = inView.getInt32(offset, true);
            else if (attribute.type === 0x1405) value = inView.getUint32(offset, true);
            else if (attribute.type === 0x1406) value = inView.getFloat32(offset, true);
            else if (attribute.type === 0x140b) value = halfToFloat(inView.getUint16(offset, true));
          }
          if (attribute.normalized) {
            if (attribute.type === 0x1400) value = Math.max(-1, value / 127);
            else if (attribute.type === 0x1401) value /= 255;
            else if (attribute.type === 0x1402) value = Math.max(-1, value / 32767);
            else if (attribute.type === 0x1403) value /= 65535;
          }
          const dst = (vertex * attribute.size + component) * 4;
          if (attribute.integer) signed ? view.setInt32(dst, value | 0, true) : view.setUint32(dst, value >>> 0, true);
          else view.setFloat32(dst, Number(value), true);
        }
        const gpu = this._device.createBuffer({ size: Math.max(4, out.byteLength), usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
        if (out.byteLength) this._device.queue.writeBuffer(gpu, 0, out);
        cached = { gpu, arrayStride: Math.max(4, attribute.size * 4) };
        buffer.vertexCache.set(key, cached);
      }
      return { format: `${attribute.integer ? (signedType(attribute.type) ? 'sint32' : 'uint32') : 'float32'}${attribute.size === 1 ? '' : `x${attribute.size}`}`,
        arrayStride: cached.arrayStride, offset: 0, buffer: cached.gpu, stepMode: attribute.divisor ? 'instance' : 'vertex' };
    }

    const key = stateKey([attribute.index, ...attribute.current]);
    let gpu = this._constantBuffers.get(key);
    if (!gpu) {
      const data = attribute.integer ? new Int32Array(attribute.current) : new Float32Array(attribute.current);
      gpu = this._device.createBuffer({ size: 16, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
      this._device.queue.writeBuffer(gpu, 0, data);
      this._constantBuffers.set(key, gpu);
    }
    return { format: attribute.integer ? 'sint32x4' : 'float32x4', arrayStride: 16, offset: 0, buffer: gpu, stepMode: 'instance' };
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

  _drawSnapshot() {
    const attributes = new Map();
    for (const [index, attribute] of this._vao.attributes) attributes.set(index, { ...attribute, current: new Float32Array(attribute.current) });
    const program = this._currentProgram;
    return {
      program, uniformValues: new Map([...(program?.uniformValues || [])].map(([name, value]) => [name, ArrayBuffer.isView(value) ? value.slice() : Array.isArray(value) ? value.slice() : value])),
      attributes, elementArrayBuffer: this._vao.elementArrayBuffer,
      viewport: [...this._viewport], scissor: [...this._scissor],
      enabled: new Set(this._enabled), blend: { ...this._blend }, blendColor: [...this._blendColor],
      depthFunc: this._depthFunc, depthMask: this._depthMask, cullFace: this._cullFace,
      frontFace: this._frontFace, colorMask: [...this._colorMask],
      drawFramebuffer: this._drawFramebuffer,
    };
  }

  drawArrays(mode, first, count) { const state = this._drawSnapshot(); this._mirrorDraw(state, mode, first, count, null); this._enqueue(() => this._draw(state, mode, first, count, null, 1)); }
  drawElements(mode, count, type, offset) { const element = { type, offset }, state = this._drawSnapshot(); this._mirrorDraw(state, mode, 0, count, element); this._enqueue(() => this._draw(state, mode, 0, count, element, 1)); }
  drawArraysInstanced(mode, first, count, instances) { const state = this._drawSnapshot(); this._mirrorDraw(state, mode, first, count, null); this._enqueue(() => this._draw(state, mode, first, count, null, instances)); }
  drawElementsInstanced(mode, count, type, offset, instances) { const element = { type, offset }, state = this._drawSnapshot(); this._mirrorDraw(state, mode, 0, count, element); this._enqueue(() => this._draw(state, mode, 0, count, element, instances)); }

  _mirrorAttribute(attribute, vertex) {
    const result = [0, 0, 0, 1];
    if (!attribute?.enabled || !attribute.buffer) return attribute?.current ? [...attribute.current] : result;
    const buffer = attribute.buffer, view = new DataView(buffer.data.buffer, buffer.data.byteOffset, buffer.data.byteLength);
    const bytes = [0x1400, 0x1401].includes(attribute.type) ? 1 : [0x1402, 0x1403, 0x140b].includes(attribute.type) ? 2 : 4;
    const stride = attribute.stride || attribute.size * bytes;
    for (let component = 0; component < Math.min(4, attribute.size); component++) {
      const offset = attribute.offset + vertex * stride + component * bytes;
      if (offset < 0 || offset + bytes > buffer.data.length) continue;
      let value = attribute.type === 0x1400 ? view.getInt8(offset)
        : attribute.type === 0x1401 ? view.getUint8(offset)
          : attribute.type === 0x1402 ? view.getInt16(offset, true)
            : attribute.type === 0x1403 ? view.getUint16(offset, true)
              : attribute.type === 0x1404 ? view.getInt32(offset, true)
                : attribute.type === 0x1405 ? view.getUint32(offset, true)
                  : attribute.type === 0x1406 ? view.getFloat32(offset, true)
                    : attribute.type === 0x140b ? halfToFloat(view.getUint16(offset, true)) : 0;
      if (attribute.normalized) value = attribute.type === 0x1400 ? Math.max(-1, value / 127)
        : attribute.type === 0x1401 ? value / 255
          : attribute.type === 0x1402 ? Math.max(-1, value / 32767)
            : attribute.type === 0x1403 ? value / 65535 : value;
      result[component] = value;
    }
    return result;
  }

  _mirrorDraw(state, mode, first, count, indexed) {
    // The GL bridge reads synchronously for its X11 software present. WebGPU
    // cannot expose queue results synchronously, so mirror the fixed-function
    // untextured vertex-color subset on the CPU while the GPU remains the real
    // renderer. Other programmable draws need direct-canvas presentation.
    if (!this._isDefaultFramebuffer(state.drawFramebuffer) || !state.program || this._textures.size) return;
    const program = state.program;
    const position = program.attributeInfo.find(item => /position/i.test(item.name));
    const color = program.attributeInfo.find(item => /color/i.test(item.name));
    if (!position || !color || program.uniformInfo.some(item => item.type === 0x8b5e)) return;
    let vertices = indexed ? this._indexValues(state.elementArrayBuffer, indexed.type, indexed.offset, count)
      : Uint32Array.from({ length: count }, (_, i) => first + i);
    if (!vertices) return;
    if (mode === 0x0006) {
      const fan = [];
      for (let i = 1; i + 1 < vertices.length; i++) fan.push(vertices[0], vertices[i], vertices[i + 1]);
      vertices = Uint32Array.from(fan);
    } else if (mode === 0x0005) {
      const strip = [];
      for (let i = 0; i + 2 < vertices.length; i++) strip.push(...(i & 1 ? [vertices[i + 1], vertices[i], vertices[i + 2]] : [vertices[i], vertices[i + 1], vertices[i + 2]]));
      vertices = Uint32Array.from(strip);
    }
    if (mode !== 0x0004 && mode !== 0x0005 && mode !== 0x0006) return;
    const posAttr = state.attributes.get(program.attributeLocations.get(position.name));
    const colorAttr = state.attributes.get(program.attributeLocations.get(color.name));
    const model = program.uniformValues.get('u_modelView') || identityMatrix();
    const projection = program.uniformValues.get('u_projection') || identityMatrix();
    const viewport = state.viewport;
    const transform = index => {
      const p = this._mirrorAttribute(posAttr, index);
      const viewPosition = mulMat4(model, p), clip = mulMat4(projection, viewPosition);
      const w = clip[3];
      if (!Number.isFinite(w) || Math.abs(w) < 1e-8) return null;
      const nx = clip[0] / w, ny = clip[1] / w;
      return { x: viewport[0] + (nx + 1) * viewport[2] * 0.5, y: viewport[1] + (ny + 1) * viewport[3] * 0.5, color: this._mirrorAttribute(colorAttr, index) };
    };
    for (let i = 0; i + 2 < vertices.length; i += 3) {
      const triangle = [transform(vertices[i]), transform(vertices[i + 1]), transform(vertices[i + 2])];
      if (triangle.some(vertex => !vertex)) continue;
      this._rasterizeMirrorTriangle(triangle, state);
    }
  }

  _isDefaultFramebuffer(framebuffer) {
    return framebuffer == null || framebuffer === 0 || framebuffer === this._defaultFramebuffer || !this._framebufferObjects.has(framebuffer);
  }

  _rasterizeMirrorTriangle(vertices, state) {
    const edge = (a, b, x, y) => (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
    const area = edge(vertices[0], vertices[1], vertices[2].x, vertices[2].y);
    if (!area || (state.enabled.has(0x0b44) && (state.cullFace === 0x0405 ? area > 0 : area < 0))) return;
    const minX = Math.max(0, Math.floor(Math.min(...vertices.map(v => v.x))));
    const maxX = Math.min(this._width - 1, Math.ceil(Math.max(...vertices.map(v => v.x))));
    const minY = Math.max(0, Math.floor(Math.min(...vertices.map(v => v.y))));
    const maxY = Math.min(this._height - 1, Math.ceil(Math.max(...vertices.map(v => v.y))));
    const scissor = state.enabled.has(0x0c11) ? state.scissor : [0, 0, this._width, this._height];
    const left = Math.max(minX, scissor[0]), right = Math.min(maxX, scissor[0] + scissor[2]);
    const bottom = Math.max(minY, scissor[1]), top = Math.min(maxY, scissor[1] + scissor[3]);
    for (let y = bottom; y <= top; y++) for (let x = left; x <= right; x++) {
      const px = x + 0.5, py = y + 0.5;
      const a = edge(vertices[1], vertices[2], px, py) / area;
      const b = edge(vertices[2], vertices[0], px, py) / area;
      const c = 1 - a - b;
      if (a < 0 || b < 0 || c < 0) continue;
      const offset = (y * this._width + x) * 4;
      for (let channel = 0; channel < 4; channel++) if (state.colorMask[channel]) {
        const value = a * vertices[0].color[channel] + b * vertices[1].color[channel] + c * vertices[2].color[channel];
        this._pixels[offset + channel] = Math.round(clamp01(value) * 255);
      }
    }
  }

  _indexValues(buffer, type, offset, count) {
    if (!buffer || ![0x1401, 0x1403, 0x1405].includes(type)) return null;
    const size = type === 0x1401 ? 1 : type === 0x1403 ? 2 : 4;
    if (offset < 0 || offset + count * size > buffer.data.length) return null;
    const view = new DataView(buffer.data.buffer, buffer.data.byteOffset, buffer.data.byteLength);
    const values = new Uint32Array(count);
    for (let i = 0; i < count; i++) values[i] = type === 0x1401 ? view.getUint8(offset + i)
      : type === 0x1403 ? view.getUint16(offset + i * 2, true) : view.getUint32(offset + i * 4, true);
    return values;
  }

  _rewriteTopology(mode, first, count, indices) {
    if (mode === 0x0006) {
      const source = indices || Uint32Array.from({ length: count }, (_, i) => first + i), out = [];
      for (let i = 1; i + 1 < source.length; i++) out.push(source[0], source[i], source[i + 1]);
      return { topology: 'triangle-list', indices: Uint32Array.from(out) };
    }
    if (mode === 0x0002) {
      const source = indices || Uint32Array.from({ length: count }, (_, i) => first + i), out = [];
      for (let i = 0; i < source.length; i++) out.push(source[i], source[(i + 1) % source.length]);
      return { topology: 'line-list', indices: Uint32Array.from(out) };
    }
    return { topology: GL_TOPOLOGY.get(mode), indices };
  }

  _blendDescriptor(state) {
    const operation = GL_BLEND_OP.get(state.blend.equationRGB), alphaOperation = GL_BLEND_OP.get(state.blend.equationAlpha);
    if (!operation || !alphaOperation) throw new Error('unsupported GL blend equation');
    return {
      color: { srcFactor: blendFactor(state.blend.srcRGB), dstFactor: blendFactor(state.blend.dstRGB), operation },
      alpha: { srcFactor: blendFactor(state.blend.srcAlpha), dstFactor: blendFactor(state.blend.dstAlpha), operation: alphaOperation },
    };
  }

  _vertexLayouts(program, attributes) {
    const locations = [...program.attributeLocations.entries()].sort((a, b) => a[1] - b[1]);
    const layouts = [], buffers = [];
    for (const [name, location] of locations) {
      const attribute = attributes.get(location) || { index: location, enabled: false, buffer: null, size: 4, type: 0x1406, normalized: false, integer: false, stride: 0, offset: 0, divisor: 0, current: new Float32Array([0, 0, 0, 1]) };
      if (attribute.enabled && attribute.buffer) {
        const spec = this._vertexSpec(attribute);
        layouts.push({ arrayStride: spec.arrayStride, stepMode: spec.stepMode, attributes: [{ shaderLocation: location, offset: 0, format: spec.format }] });
        buffers.push({ slot: layouts.length - 1, buffer: spec.buffer, offset: spec.offset });
      } else {
        const spec = this._vertexSpec(attribute);
        layouts.push({ arrayStride: spec.arrayStride, stepMode: 'instance', attributes: [{ shaderLocation: location, offset: 0, format: spec.format }] });
        buffers.push({ slot: layouts.length - 1, buffer: spec.buffer, offset: 0 });
      }
    }
    return { layouts, buffers };
  }

  _pipelineFor(program, state, vertex, topology, targetFormat = this._format, depthFormat = 'depth24plus') {
    const blendEnabled = state.enabled.has(0x0be2), depthEnabled = state.enabled.has(0x0b71);
    const cullEnabled = state.enabled.has(0x0b44);
    const cull = cullEnabled ? (state.cullFace === 0x0404 ? 'front' : state.cullFace === 0x0408 ? 'front-and-back' : 'back') : 'none';
    const blend = blendEnabled ? this._blendDescriptor(state) : undefined;
    const writeMask = state.colorMask.reduce((mask, enabled, index) => enabled ? mask | (1 << index) : mask, 0);
    const depthCompare = depthEnabled ? GL_COMPARE.get(state.depthFunc) : 'always';
    if (!depthCompare) throw new Error('unsupported GL depth comparison');
    const key = stateKey({
      program: program.id, vertex: vertex.layouts, topology, blend, depth: [depthEnabled, depthCompare, state.depthMask],
      cull, frontFace: state.frontFace, colorMask: writeMask, targetFormat, depthFormat,
    });
    if (this._pipelineCache.has(key)) return this._pipelineCache.get(key);
    const vertexModule = this._device.createShaderModule({ code: program.stages.vertex.wgsl });
    const fragmentModule = this._device.createShaderModule({ code: program.stages.fragment.wgsl });
    const descriptor = {
      layout: 'auto',
      vertex: { module: vertexModule, entryPoint: 'main', buffers: vertex.layouts },
      fragment: { module: fragmentModule, entryPoint: 'main', targets: [{ format: targetFormat, writeMask, ...(blend ? { blend } : {}) }] },
      primitive: { topology, frontFace: state.frontFace === 0x0900 ? 'cw' : 'ccw', cullMode: cull === 'front-and-back' ? 'front' : cull },
      depthStencil: { format: depthFormat, depthWriteEnabled: depthEnabled && state.depthMask, depthCompare },
    };
    const pipeline = this._device.createRenderPipeline(descriptor);
    this._pipelineCache.set(key, pipeline);
    return pipeline;
  }

  async _uniformBindGroup(program, pipeline, uniformValues) {
    const entries = [];
    for (const [stage, binding] of [['vertex', 0], ['fragment', 1]]) {
      const record = program.stages[stage];
      if (!record?.layout?.fields?.length) continue;
      let buffer = program.uniformGpu?.[stage];
      const size = Math.max(16, record.layout.size);
      if (!buffer || buffer.size !== size) {
        buffer?.destroy();
        buffer = this._device.createBuffer({ size, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        program.uniformGpu ||= {};
        program.uniformGpu[stage] = buffer;
      }
      const packed = packUniforms(record.layout, Object.fromEntries(record.layout.fields.map(field => [field.name, uniformValues.get(field.name)])));
      this._device.queue.writeBuffer(buffer, 0, packed);
      entries.push({ binding, resource: { buffer, offset: 0, size } });
    }
    if (!entries.length) return null;
    return this._device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries });
  }

  async _draw(state, mode, first, count, indexed, instances) {
    const program = state.program;
    if (!program || !program.linkPromise) return;
    if (count <= 0 || instances <= 0 || !(await program.linkPromise)) return;
    if (state.cullFace === 0x0408 && state.enabled.has(0x0b44)) return;
    const topologyData = this._rewriteTopology(mode, first, count, indexed ? this._indexValues(state.elementArrayBuffer, indexed.type, indexed.offset, count) : null);
    if (!topologyData.topology || (indexed && !topologyData.indices)) return;
    const topology = topologyData.topology;
    const vertex = this._vertexLayouts(program, state.attributes);
    const viewport = state.viewport;
    const pipeline = this._pipelineFor(program, state, vertex, topology);
    const encoder = this._device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
      colorAttachments: [{ view: this._colorTexture.createView(), loadOp: 'load', storeOp: 'store' }],
      depthStencilAttachment: { view: this._depthTexture.createView(), depthLoadOp: 'load', depthStoreOp: 'store' },
    });
    pass.setPipeline(pipeline);
    for (const item of vertex.buffers) pass.setVertexBuffer(item.slot, item.buffer, item.offset);
    const bindGroup = await this._uniformBindGroup(program, pipeline, state.uniformValues);
    if (bindGroup) pass.setBindGroup(0, bindGroup);
    const x = Math.max(0, viewport[0]), y = Math.max(0, this._height - viewport[1] - viewport[3]);
    pass.setViewport(x, y, Math.max(0, viewport[2]), Math.max(0, viewport[3]), 0, 1);
    if (state.enabled.has(0x0c11)) {
      const [sx, sy, sw, sh] = state.scissor;
      const left = Math.max(0, sx), top = Math.max(0, this._height - sy - sh);
      pass.setScissorRect(left, top, Math.max(0, Math.min(this._width - left, sw)), Math.max(0, Math.min(this._height - top, sh)));
    }
    if (state.enabled.has(0x0be2)) pass.setBlendConstant({ r: state.blendColor[0], g: state.blendColor[1], b: state.blendColor[2], a: state.blendColor[3] });
    let temporaryIndex = null;
    if (topologyData.indices) {
      const max = topologyData.indices.reduce((value, index) => Math.max(value, index), 0);
      const values = max <= 0xffff ? Uint16Array.from(topologyData.indices) : Uint32Array.from(topologyData.indices);
      temporaryIndex = this._device.createBuffer({ size: Math.max(4, values.byteLength), usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST });
      this._device.queue.writeBuffer(temporaryIndex, 0, values);
      pass.setIndexBuffer(temporaryIndex, values instanceof Uint16Array ? 'uint16' : 'uint32');
      pass.drawIndexed(values.length, instances, 0, 0, 0);
    } else pass.draw(count, instances, first, 0);
    pass.end();
    this._device.queue.submit([encoder.finish()]);
    if (temporaryIndex) this._device.queue.onSubmittedWorkDone().then(() => temporaryIndex.destroy()).catch(() => {});
  }

  blendFunc(src, dst) { this.blendFuncSeparate(src, dst, src, dst); }
  blendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha) { Object.assign(this._blend, { srcRGB, dstRGB, srcAlpha, dstAlpha }); }
  blendEquation(mode) { this.blendEquationSeparate(mode, mode); }
  blendEquationSeparate(rgb, alpha) { this._blend.equationRGB = rgb; this._blend.equationAlpha = alpha; }
  blendColor(r, g, b, a) { this._blendColor = [r, g, b, a].map(clamp01); }
  colorMask(r, g, b, a) { this._colorMask = [!!r, !!g, !!b, !!a]; }
  depthMask(enabled) { this._depthMask = !!enabled; }
  depthFunc(func) { this._depthFunc = func | 0; }
  cullFace(mode) { this._cullFace = mode | 0; }
  frontFace(mode) { this._frontFace = mode | 0; }
  scissor(x, y, width, height) { this._scissor = [x | 0, y | 0, width | 0, height | 0]; }
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
