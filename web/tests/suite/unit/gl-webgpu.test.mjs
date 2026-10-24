import { test } from 'node:test';
import assert from 'node:assert/strict';
import { blendFactor, convertIndices, packUniforms, stateKey, uniformLayout } from '../../../gl-webgpu-utils.mjs';
import { WebGPUGL, rewriteGlslForWebGPU } from '../../../gl-webgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

test('uniform layout and packing honor std140 vector, matrix, and array strides', () => {
  const layout = uniformLayout([
    { name: 'tint', type: 'vec3' },
    { name: 'enabled', type: 'bool' },
    { name: 'transform', type: 'mat3' },
    { name: 'weights', type: 'float', arrayLength: 2 },
  ]);
  assert.deepEqual(layout.fields.map(field => field.offset), [0, 12, 16, 64]);
  assert.equal(layout.size, 96);
  const bytes = packUniforms(layout, {
    tint: [0.25, 0.5, 0.75], enabled: true,
    transform: [1, 2, 3, 4, 5, 6, 7, 8, 9], weights: [0.125, 0.875],
  });
  const view = new DataView(bytes.buffer);
  assert.equal(view.getFloat32(0, true), 0.25);
  assert.equal(view.getInt32(12, true), 1);
  assert.equal(view.getFloat32(16, true), 1);
  assert.equal(view.getFloat32(24, true), 3);
  assert.equal(view.getFloat32(32, true), 4);
  assert.equal(view.getFloat32(64, true), 0.125);
  assert.equal(view.getFloat32(80, true), 0.875);
});

test('validateProgram exposes linked validation status through getProgramParameter', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    const program = { linked: true };
    gl.validateProgram(program);
    assert.equal(gl.getProgramParameter(program, 0x8b83), true);
    program.linked = false;
    gl.validateProgram(program);
    assert.equal(gl.getProgramParameter(program, 0x8b83), false);
  } finally { gpu.restore(); }
});

test('GL query objects return zero results and become available only after end', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    const query = gl.createQuery();
    gl.beginQuery(0x8c2f, query);
    assert.equal(gl.getQuery(0x8c2f, 0x8865), query);
    assert.equal(gl.getQueryParameter(query, 0x8867), false);
    gl.endQuery(0x8c2f);
    assert.equal(gl.getQueryParameter(query, 0x8867), true);
    assert.equal(gl.getQueryParameter(query, 0x8866), 0);
    assert.equal(gl.getQuery(0x8c2f, 0x8865), null);

    const primitives = gl.createQuery();
    gl.beginQuery(0x8c87, primitives);
    assert.equal(gl.getQueryParameter(primitives, 0x8867), false);
    gl.endQuery(0x8c87);
    assert.equal(gl.getQueryParameter(primitives, 0x8867), true);
    assert.equal(gl.getQueryParameter(primitives, 0x8866), 0);

    const timestamp = gl.createQuery();
    gl.queryCounter(timestamp, 0x8e28);
    assert.equal(gl.getQueryParameter(timestamp, 0x8867), true);
    assert.equal(gl.getQueryParameter(timestamp, 0x8866), 0);
    gl.deleteQuery(timestamp);
    assert.equal(gl.isQuery(timestamp), false);
  } finally { gpu.restore(); }
});

test('transform feedback operations reject once with INVALID_OPERATION', async () => {
  const gpu = createValidationDevice(), logs = [];
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    gl.beginTransformFeedback(0x0004);
    assert.equal(gl.getError(), 0x0502);
    gl.endTransformFeedback();
    assert.equal(gl.getError(), 0x0502);
    assert.equal(logs.filter(line => line.includes('transform feedback')).length, 1);
  } finally { gpu.restore(); }
});

test('glClipControl changes upper-left origin and zero-to-one depth rewriting', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    gl.clipControl(0x8ca2, 0x935f);
    assert.deepEqual(gl.getClipControl(), [0x8ca2, 0x935f]);
    assert.equal(gl.getParameter(0x935c), 0x8ca2);
    assert.equal(gl.getParameter(0x935d), 0x935f);
    const rewritten = rewriteGlslForWebGPU('#version 300 es\nvoid main(){ gl_Position=vec4(0.0); }', 'vertex', new Map(), new Map(), gl.getClipControl());
    assert.match(rewritten.code, /gl_Position\.y = -gl_Position\.y;/);
    assert.doesNotMatch(rewritten.code, /gl_Position\.z = \(gl_Position\.z/);
    const fixed = gl._fixedFunctionProgram([], false, false, 0x0207, []).stages.vertex.wgsl;
    assert.match(fixed, /vec4<f32>\(clip\.x, -clip\.y, clip\.z, clip\.w\)/, 'fixed-function shaders use the same clip control state');
    gl.clipControl(0xdead, 0x935f);
    assert.equal(gl.getError(), 0x0500);
  } finally { gpu.restore(); }
});

test('blitFramebuffer scales and flips color rectangles on CPU and GPU paths', async () => {
  const gpu = createValidationDevice(), logs = [];
  try {
    const bufferWrites = [];
    gpu.device.queue.writeBuffer = (buffer, offset, data) => bufferWrites.push(new Float32Array(data));
    const createPipeline = gpu.device.createRenderPipeline.bind(gpu.device);
    gpu.device.createRenderPipeline = descriptor => {
      const pipeline = createPipeline(descriptor);
      pipeline.getBindGroupLayout = () => ({ entries: [{ binding: 0, buffer: {} }, { binding: 1, texture: { viewDimension: '2d' } }, { binding: 2, sampler: {} }] });
      return pipeline;
    };
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    const source = gl.createTexture(), destination = gl.createTexture();
    gl.bindTexture(0x0de1, source);
    gl.texImage2D(0x0de1, 0, 0x1908, 2, 1, 0, 0x1908, 0x1401, new Uint8Array([255, 0, 0, 255, 0, 0, 255, 255]));
    gl.bindTexture(0x0de1, destination);
    gl.texImage2D(0x0de1, 0, 0x1908, 4, 1, 0, 0x1908, 0x1401, new Uint8Array(16));
    await gl._ops;
    const read = gl.createFramebuffer(), draw = gl.createFramebuffer();
    gl.bindFramebuffer(0x8d40, read); gl.framebufferTexture2D(0x8d40, 0x8ce0, 0x0de1, source, 0);
    gl.bindFramebuffer(0x8d40, draw); gl.framebufferTexture2D(0x8d40, 0x8ce0, 0x0de1, destination, 0);
    gl.bindFramebuffer(0x8ca8, read); gl.bindFramebuffer(0x8ca9, draw);
    gl.blitFramebuffer(2, 0, 0, 1, 0, 0, 4, 1, 0x4000, 0x2600);
    assert.deepEqual([...destination.pixels], [0, 0, 255, 255, 0, 0, 255, 255, 255, 0, 0, 255, 255, 0, 0, 255]);
    await gl._ops;
    assert.deepEqual([...bufferWrites.at(-1)], [1, 0, -1, 1]);
    assert.deepEqual(gpu.viewports.at(-1), [0, 0, 4, 1, 0, 1]);
    assert.equal(gl.getError(), 0);
    gl.blitFramebuffer(0, 0, 1, 1, 0, 0, 1, 1, 0x0100, 0x2600);
    assert.equal(gl.getError(), 0x0502);
    assert.ok(logs.some(line => line.includes('blitFramebuffer depth/stencil')));
  } finally { gpu.restore(); }
});

test('uint8 indices widen for WebGPU and wider index types preserve values', () => {
  assert.deepEqual([...convertIndices(new Uint8Array([0, 2, 255]), 0x1401)], [0, 2, 255]);
  assert.ok(convertIndices(new Uint8Array([1]), 0x1401) instanceof Uint16Array);
  assert.deepEqual([...convertIndices(new Uint16Array([3, 4]), 0x1403)], [3, 4]);
  assert.deepEqual([...convertIndices(new Uint32Array([5]), 0x1405)], [5]);
  assert.throws(() => convertIndices(new Uint8Array([0]), 0x1405), /Uint32Array/);
});

test('OpenGL blend factors map to WebGPU blend component names', () => {
  const cases = [
    [0x0000, 'zero'], [0x0001, 'one'], [0x0300, 'src'], [0x0301, 'one-minus-src'],
    [0x0302, 'src-alpha'], [0x0303, 'one-minus-src-alpha'], [0x0304, 'dst-alpha'],
    [0x0305, 'one-minus-dst-alpha'], [0x0306, 'dst'], [0x0307, 'one-minus-dst'],
    [0x0308, 'src-alpha-saturated'], [0x8001, 'constant'], [0x8004, 'one-minus-constant'],
  ];
  for (const [gl, gpu] of cases) assert.equal(blendFactor(gl), gpu);
  assert.throws(() => blendFactor(0xdead), /Unsupported blend factor/);
});

test('state hashing is stable across object key order and changes with state', () => {
  assert.equal(stateKey({ depth: true, blend: { src: 1, dst: 0 } }), stateKey({ blend: { dst: 0, src: 1 }, depth: true }));
  assert.notEqual(stateKey({ depth: true }), stateKey({ depth: false }));
});

test('GL depthRange is stored and applied to WebGPU draw viewports', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    assert.equal(typeof gl.depthRange, 'function', 'Emscripten GLctx.depthRange calls must be implemented');
    gl.depthRange(0.25, 0.75);
    assert.deepEqual(gl.getParameter(0x0b70), new Float32Array([0.25, 0.75]));

    gl._currentProgram = {
      id: 1,
      linkPromise: Promise.resolve(true),
      attributeLocations: new Map(),
      attributeInfo: [],
      uniformInfo: [],
      uniformValues: new Map(),
      stages: {
        vertex: { wgsl: '@vertex fn main() -> @builtin(position) vec4<f32> { return vec4<f32>(0.0); }', layout: { fields: [] }, samplers: [] },
        fragment: { wgsl: '@fragment fn main() -> @location(0) vec4<f32> { return vec4<f32>(1.0); }', layout: { fields: [] }, samplers: [] },
      },
    };
    await gl._draw(gl._drawSnapshot(), 0x0004, 0, 3, null, 1);
    assert.deepEqual(gpu.viewports.at(-1), [0, 0, 4, 4, 0.25, 0.75]);
  } finally {
    gpu.restore();
  }
});

test('stencil state configures the default attachment, render pipeline, and clear operation', async () => {
  const gpu = createValidationDevice(), logs = [];
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    assert.equal(gl.getContextAttributes().stencil, true);
    assert.equal(gl._depthTexture.format, 'depth24plus-stencil8');
    gl.stencilFunc(0x0202, 7, 0x0f);
    gl.stencilOp(0x1e00, 0x1e01, 0x8507);
    gl.stencilMask(0xff);
    gl.enable(0x0b90);
    gl.clearStencil(19);
    gl.clear(0x0400);
    await gl._ops;
    assert.ok(gpu.passes.length, `clear pass should be submitted: ${logs.join('; ')}`);
    assert.equal(gpu.passes.at(-1).depthStencilAttachment.stencilLoadOp, 'clear');
    assert.equal(gpu.passes.at(-1).depthStencilAttachment.stencilClearValue, 19);

    gl._currentProgram = {
      id: 1, linkPromise: Promise.resolve(true), attributeLocations: new Map(), attributeInfo: [], uniformInfo: [], uniformValues: new Map(),
      stages: {
        vertex: { wgsl: '@vertex fn main() -> @builtin(position) vec4<f32> { return vec4<f32>(0.0); }', layout: { fields: [] }, samplers: [] },
        fragment: { wgsl: '@fragment fn main() -> @location(0) vec4<f32> { return vec4<f32>(1.0); }', layout: { fields: [] }, samplers: [] },
      },
    };
    await gl._draw(gl._drawSnapshot(), 0x0004, 0, 3, null, 1);
    const stencil = gpu.pipelines.at(-1).depthStencil;
    assert.equal(stencil.format, 'depth24plus-stencil8');
    assert.deepEqual(stencil.stencilFront, { compare: 'equal', failOp: 'keep', depthFailOp: 'replace', passOp: 'increment-wrap' });
    assert.equal(stencil.stencilReadMask, 0x0f);
    assert.equal(stencil.stencilWriteMask, 0xff);
    assert.equal(gpu.stencilReferences.at(-1), 7);

    gl.stencilMaskSeparate(0x0404, 1);
    assert.equal(gl.getError(), 0x0502);
    assert.ok(logs.some(line => line.includes('separate front/back stencil write masks')));
    gl.stencilFuncSeparate(0x0404, 0x0202, 7, 1);
    assert.equal(gl.getError(), 0x0502);
    assert.ok(logs.some(line => line.includes('separate front/back stencil read masks')));
    gl.stencilMask(0x0f);
    gl.clear(0x0400);
    await gl._ops;
    assert.equal(gpu.passes.at(-1).depthStencilAttachment.stencilLoadOp, 'load', 'masked stencil clear uses a draw pass to preserve untouched bits');
    assert.equal(gpu.pipelines.at(-1).depthStencil.stencilWriteMask, 0x0f);
    assert.equal(gpu.stencilReferences.at(-1), 19);
    gl.stencilFunc(0xdead, 0, 0xff);
    assert.equal(gl.getError(), 0x0500);
  } finally { gpu.restore(); }
});

test('polygon offset is passed to WebGPU only when POLYGON_OFFSET_FILL is enabled', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    gl.polygonOffset(1.5, 4);
    gl._currentProgram = {
      id: 1, linkPromise: Promise.resolve(true), attributeLocations: new Map(), attributeInfo: [], uniformInfo: [], uniformValues: new Map(),
      stages: {
        vertex: { wgsl: '@vertex fn main() -> @builtin(position) vec4<f32> { return vec4<f32>(0.0); }', layout: { fields: [] }, samplers: [] },
        fragment: { wgsl: '@fragment fn main() -> @location(0) vec4<f32> { return vec4<f32>(1.0); }', layout: { fields: [] }, samplers: [] },
      },
    };
    await gl._draw(gl._drawSnapshot(), 0x0004, 0, 3, null, 1);
    assert.equal('depthBias' in gpu.pipelines.at(-1).primitive, false);
    gl.enable(0x8037);
    await gl._draw(gl._drawSnapshot(), 0x0004, 0, 3, null, 1);
    assert.equal(gpu.pipelines.at(-1).primitive.depthBias, 4);
    assert.equal(gpu.pipelines.at(-1).primitive.depthBiasSlopeScale, 1.5);
    gl.polygonOffset(Infinity, 0);
    assert.equal(gl.getError(), 0x0501);
  } finally { gpu.restore(); }
});

test('polygon line mode expands triangles to edges and line width rejects non-unit values loudly', async () => {
  const gpu = createValidationDevice(), logs = [];
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    gl.polygonMode(0x0408, 0x1b01);
    const expanded = gl._rewriteTopology(0x0004, 0, 3, Uint32Array.from([2, 4, 6]));
    assert.equal(expanded.topology, 'line-list');
    assert.deepEqual([...expanded.indices], [2, 4, 4, 6, 6, 2]);
    gl.polygonMode(0x0408, 0x1b02);
    assert.equal(gl._rewriteTopology(0x0004, 0, 3, Uint32Array.from([0, 1, 2])).topology, 'triangle-list');
    gl.polygonMode(0x0408, 0x1b00);
    assert.equal(gl._rewriteTopology(0x0004, 0, 3, Uint32Array.from([0, 1, 2])).topology, 'point-list');
    gl.polygonMode(0x0408, 0x1b02);
    gl.lineWidth(1);
    gl.lineWidth(2);
    assert.equal(gl.getError(), 0x0502);
    assert.ok(logs.some(line => line.includes('line width other than 1')));
    gl.pointSize(1);
    gl.pointSize(2);
    assert.equal(gl.getError(), 0x0502);
    assert.ok(logs.some(line => line.includes('point size other than 1')));
    gl.enable(0x8861);
    assert.equal(gl.getError(), 0x0502);
    assert.ok(logs.some(line => line.includes('point sprites')));
    gl.polygonMode(0x0404, 0x1b01);
    assert.equal(gl.getError(), 0x0502);
    assert.ok(logs.some(line => line.includes('polygon mode for a single face')));
  } finally { gpu.restore(); }
});

test('draw and read buffer selection is honored and GL errors latch until read', async () => {
  const gpu = createValidationDevice(), logs = [];
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    assert.equal(gl.getParameter(0x8824), 1, 'the WebGPU backend exposes one supported draw buffer');
    gl.drawBuffers([0x0405]);
    assert.equal(gl.getParameter(0x8825), 0x0405);
    const framebuffer = gl.createFramebuffer();
    gl.bindFramebuffer(0x8d40, framebuffer);
    assert.equal(gl.getParameter(0x8825), 0x8ce0, 'framebuffers default to COLOR_ATTACHMENT0');
    gl.drawBuffers([0]);
    gl.bindFramebuffer(0x8d40, 0);
    assert.equal(gl.getParameter(0x8825), 0x0405, 'default framebuffer selection is independent');
    gl.bindFramebuffer(0x8d40, framebuffer);
    assert.equal(gl.getParameter(0x8825), 0, 'draw buffer selection is retained by its framebuffer');
    gl.bindFramebuffer(0x8d40, 0);
    gl.drawBuffers([0xdead]);
    assert.equal(gl.getError(), 0x0500, 'unknown draw buffer enum raises INVALID_ENUM');
    assert.equal(gl.getError(), 0, 'reading the GL error clears the latched error');
    gl.drawBuffers([]);
    assert.equal(gl.getParameter(0x8825), 0, 'an empty draw-buffer list disables color output');
    gl.drawBuffers([0x0405]);
    gl.drawBuffers([0x8ce0, 0x8ce1]);
    assert.equal(gl.getError(), 0x0502, 'MRT exceeds this backend’s reported draw buffer limit');
    assert.ok(logs.some(line => line.includes('multiple draw buffers (MRT)')));

    const pixel = new Uint8Array([9, 8, 7, 6]);
    gl.readBuffer(0);
    gl.readPixels(0, 0, 1, 1, 0x1908, 0x1401, pixel);
    assert.deepEqual([...pixel], [9, 8, 7, 6], 'NONE leaves the destination untouched');
    gl.readBuffer(0xdead);
    assert.equal(gl.getError(), 0x0500);
  } finally { gpu.restore(); }
});

test('common invalid GL enums and values latch spec errors without mutating buffers', async () => {
  const gpu = createValidationDevice(), logs = [];
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    gl.enable(0xdead);
    gl.drawArrays(0xdead, 0, 3);
    assert.equal(gl.getError(), 0x0500, 'only the first error remains pending');
    assert.equal(gl.getError(), 0, 'getError consumes the pending error');
    gl.viewport(0, 0, -1, 4);
    assert.equal(gl.getError(), 0x0501);

    const buffer = gl.createBuffer();
    gl.bindBuffer(0x8892, buffer);
    gl.bufferData(0x8892, 4);
    gl.bufferSubData(0x8892, 2, new Uint8Array([1, 2, 3]));
    assert.equal(gl.getError(), 0x0501, 'out-of-bounds subdata is invalid');
    assert.equal(buffer.data.length, 4, 'invalid subdata cannot grow the GL buffer');
    gl.bufferSubData(0x8892, 1, new Uint8Array([7, 8]));
    assert.deepEqual([...buffer.data], [0, 7, 8, 0]);
    assert.equal(gl.getError(), 0);
    assert.deepEqual(logs, [], 'core GL validation errors are queried, not logged as unsupported features');
    gl.bindBuffer(0xdead, null);
    assert.equal(gl.getError(), 0x0500, 'unknown buffer targets raise INVALID_ENUM');
  } finally { gpu.restore(); }
});

test('blend/depth/cull enums fail synchronously with INVALID_ENUM before async pipeline setup', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: () => {} });
    await gl.ready;
    gl.blendEquation(0xdead);
    assert.equal(gl.getError(), 0x0500);
    gl.blendFunc(0xdead, 1);
    assert.equal(gl.getError(), 0x0500);
    gl.depthFunc(0xdead);
    assert.equal(gl.getError(), 0x0500);
    gl.cullFace(0xdead);
    assert.equal(gl.getError(), 0x0500);
    gl.frontFace(0xdead);
    assert.equal(gl.getError(), 0x0500);
  } finally { gpu.restore(); }
});

test('valid but unsupported GL buffer targets reject loudly with INVALID_OPERATION', async () => {
  const gpu = createValidationDevice(), logs = [];
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    gl.bindBuffer(0x8a11, gl.createBuffer());
    assert.equal(gl.getError(), 0x0502);
    assert.ok(logs.some(line => line.includes('buffer target 0x8a11')));
  } finally { gpu.restore(); }
});

test('invalid framebuffer and renderbuffer targets raise INVALID_ENUM without changing bindings', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    const framebuffer = gl.createFramebuffer();
    gl.bindFramebuffer(0xdead, framebuffer);
    assert.equal(gl.getError(), 0x0500);
    assert.equal(gl.getParameter(0x8ca6), 0, 'invalid framebuffer target must preserve both bindings');
    gl.bindRenderbuffer(0xdead, gl.createRenderbuffer());
    assert.equal(gl.getError(), 0x0500);
    assert.equal(gl.getParameter(0x8ca7), null, 'invalid renderbuffer target must preserve the binding');
  } finally { gpu.restore(); }
});

test('getError retains each distinct pending GL error until individually read', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    gl.enable(0xdead);
    gl.viewport(0, 0, -1, 1);
    const seen = [gl.getError(), gl.getError()].sort((a, b) => a - b);
    assert.deepEqual(seen, [0x0500, 0x0501]);
    assert.equal(gl.getError(), 0);
  } finally { gpu.restore(); }
});

test('framebuffer attachment APIs reject invalid targets and attachment enums', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    const framebuffer = gl.createFramebuffer();
    gl.bindFramebuffer(0x8d40, framebuffer);
    const texture = gl.createTexture();
    gl.bindTexture(0x0de1, texture);
    gl.texImage2D(0x0de1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, new Uint8Array([1, 2, 3, 4]));
    gl.framebufferTexture2D(0xdead, 0x8ce0, 0x0de1, texture, 0);
    assert.equal(gl.getError(), 0x0500);
    assert.equal(framebuffer.attachments.size, 0);
    gl.framebufferTexture2D(0x8d40, 0xdead, 0x0de1, texture, 0);
    assert.equal(gl.getError(), 0x0500);
    assert.equal(framebuffer.attachments.size, 0);
    gl.framebufferRenderbuffer(0xdead, 0x8ce0, 0x8d41, gl.createRenderbuffer());
    assert.equal(gl.getError(), 0x0500);
    assert.equal(framebuffer.attachments.size, 0);
    await gl._ops;
  } finally { gpu.restore(); }
});

test('renderbuffer storage validates target, binding, dimensions, and formats', async () => {
  const gpu = createValidationDevice(), logs = [];
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    gl.renderbufferStorage(0x8d41, 0x8058, 4, 4);
    assert.equal(gl.getError(), 0x0502, 'storage without a bound renderbuffer is INVALID_OPERATION');
    const renderbuffer = gl.createRenderbuffer();
    gl.bindRenderbuffer(0x8d41, renderbuffer);
    gl.renderbufferStorage(0x8d41, 0x8058, 4, 3);
    assert.equal(gl.getRenderbufferParameter(0x8d41, 0x8d42), 4);
    assert.equal(gl.getRenderbufferParameter(0x8d41, 0x8d43), 3);
    assert.equal(gl.getRenderbufferParameter(0x8d41, 0x8d44), 0x8058);
    assert.equal(gl.getRenderbufferParameter(0x8d41, 0x8cab), 0);
    gl.renderbufferStorage(0xdead, 0x8058, 4, 4);
    assert.equal(gl.getError(), 0x0500);
    gl.renderbufferStorage(0x8d41, 0x8058, -1, 4);
    assert.equal(gl.getError(), 0x0501);
    gl.renderbufferStorage(0x8d41, 0xdead, 4, 4);
    assert.equal(gl.getError(), 0x0500);
    gl.renderbufferStorage(0x8d41, 0x881a, 4, 4);
    assert.equal(gl.getError(), 0x0502, 'valid RGBA16F storage is explicitly rejected until floating color targets are implemented');
    assert.ok(logs.some(line => line.includes('renderbuffer internal format 0x881a')));
    gl.renderbufferStorage(0x8d41, 0x8d7c, 4, 4);
    assert.equal(gl.getError(), 0x0502, 'valid RGBA8UI storage is explicitly rejected until integer color targets are implemented');
    assert.ok(logs.some(line => line.includes('renderbuffer internal format 0x8d7c')));
    assert.equal(renderbuffer.width, 4, 'invalid storage requests preserve the prior allocation');
  } finally { gpu.restore(); }
});

test('framebuffer and renderbuffer queries report invalid targets, pnames, and missing attachments', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    assert.equal(gl.checkFramebufferStatus(0xdead), 0);
    assert.equal(gl.getError(), 0x0500);
    gl.getRenderbufferParameter(0x8d41, 0x8d42);
    assert.equal(gl.getError(), 0x0502, 'query without bound renderbuffer is INVALID_OPERATION');
    gl.bindRenderbuffer(0x8d41, gl.createRenderbuffer());
    gl.getRenderbufferParameter(0x8d41, 0xdead);
    assert.equal(gl.getError(), 0x0500);
    const framebuffer = gl.createFramebuffer();
    gl.bindFramebuffer(0x8d40, framebuffer);
    gl.getFramebufferAttachmentParameter(0x8d40, 0x8ce0, 0x8cd0);
    assert.equal(gl.getError(), 0x0502, 'querying a missing attachment is INVALID_OPERATION');
  } finally { gpu.restore(); }
});

test('fixed-function GL_COMBINE defaults to modulating the previous color by each texture', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    gl.texEnvi(0x2300, 0x2200, 0x8570);
    assert.equal(gl.getError(), 0);
    const program = gl._fixedFunctionProgram([0, 1], true, false, 0x0207, gl._fixedFunction.texEnv);
    assert.match(program.stages.fragment.wgsl, /color = color \* texel0/);
    assert.match(program.stages.fragment.wgsl, /color = color \* texel1/);
  } finally { gpu.restore(); }
});

test('desktop GLSL core versions 330 through 460 normalize into the WebGPU frontend dialect', () => {
  for (const version of [330, 400, 410, 420, 430, 440, 450, 460]) {
    const rewritten = rewriteGlslForWebGPU(`#version ${version} core\nvoid main() { gl_Position = vec4(0.0); }`, 'vertex');
    assert.match(rewritten.code, /^#version 450\n/);
  }
});

test('unimplemented texenv combine parameters reject once with GL_INVALID_OPERATION', async () => {
  const gpu = createValidationDevice(), logs = [];
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    gl.texEnvi(0x2300, 0x8571, 0x2100);
    gl.texEnvi(0x2300, 0x8571, 0x2100);
    assert.equal(gl.getError(), 0x0502);
    assert.equal(gl.getError(), 0, 'duplicate rejection leaves one error flag');
    assert.equal(logs.filter(line => line.includes('texture environment combine parameter 0x8571')).length, 1);
    gl.texEnvi(0x2300, 0xdead, 0x2100);
    assert.equal(gl.getError(), 0x0500, 'unknown pname remains INVALID_ENUM');
  } finally { gpu.restore(); }
});

test('state and texture entry points report invalid enums, values, and operations', async () => {
  const gpu = createValidationDevice(), logs = [];
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    gl.getParameter(0xdead);
    assert.equal(gl.getError(), 0x0500);
    gl.pixelStorei(0xdead, 1);
    assert.equal(gl.getError(), 0x0500);
    gl.pixelStorei(0x0d05, 3);
    assert.equal(gl.getError(), 0x0501);
    gl.matrixMode(0xdead);
    assert.equal(gl.getError(), 0x0500);
    gl.popMatrix();
    assert.equal(gl.getError(), 0x0502, 'matrix stack underflow is INVALID_OPERATION');
    gl.isEnabled(0xdead);
    assert.equal(gl.getError(), 0x0500);
    gl.bindTexture(0xdead, gl.createTexture());
    assert.equal(gl.getError(), 0x0500);
    gl.bindTexture(0x8513, gl.createTexture());
    assert.equal(gl.getError(), 0x0502, 'valid cube-map targets are explicitly rejected');
    assert.ok(logs.some(line => line.includes('texture target 0x8513')));
    gl.texImage2D(0x0de1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, new Uint8Array(4));
    assert.equal(gl.getError(), 0x0502, 'allocating storage without a bound texture is invalid');
    const texture = gl.createTexture();
    gl.bindTexture(0x0de1, texture);
    gl.texImage2D(0x0de1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, new Uint8Array(4));
    assert.equal(gl.getError(), 0);
    gl.texParameteri(0x0de1, 0xdead, 1);
    assert.equal(gl.getError(), 0x0500);
    gl.texSubImage2D(0x0de1, 0, 1, 0, 1, 1, 0x1908, 0x1401, new Uint8Array(4));
    assert.equal(gl.getError(), 0x0501, 'texture subimage bounds are validated');
  } finally { gpu.restore(); }
});
