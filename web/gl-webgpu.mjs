// WebGL2-shaped GLctx replacement backed by WebGPU for the GL64 bridge.

import { toWGSL } from './shader.mjs';
import { liftForNaga } from './naga-rt.mjs';
import { uniformLayout, packUniforms, convertIndices, blendFactor, stateKey } from './gl-webgpu-utils.mjs';
import { createRecoveringBridge, reportBridgeFailure } from './bridge-recovery.mjs';

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
  VIEWPORT: 0x0ba2, DEPTH_RANGE: 0x0b70, SCISSOR_BOX: 0x0c10, SCISSOR_TEST: 0x0c11,
  PACK_ALIGNMENT: 0x0d05, PACK_ROW_LENGTH: 0x0d02,
  PACK_SKIP_ROWS: 0x0d03, PACK_SKIP_PIXELS: 0x0d04,
  PIXEL_PACK_BUFFER_BINDING: 0x88ed, PIXEL_UNPACK_BUFFER_BINDING: 0x88ef,
  CURRENT_PROGRAM: 0x8b8d, VERSION: 0x1f02, VENDOR: 0x1f00,
  RENDERER: 0x1f01, SHADING_LANGUAGE_VERSION: 0x8b8c,
  MAX_TEXTURE_SIZE: 0x0d33,
  MAX_TEXTURE_IMAGE_UNITS: 0x8872, MAX_COMBINED_TEXTURE_IMAGE_UNITS: 0x8b4d,
  MAX_VERTEX_ATTRIBS: 0x8869,
  MAX_DRAW_BUFFERS: 0x8824, DRAW_BUFFER0: 0x8825,
  BACK: 0x0405, NONE: 0,
  NO_ERROR: 0, INVALID_ENUM: 0x0500, INVALID_VALUE: 0x0501,
  INVALID_OPERATION: 0x0502, OUT_OF_MEMORY: 0x0505, INVALID_FRAMEBUFFER_OPERATION: 0x0506,
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
let shaderFailureDumped = false;

function componentCount(type) {
  const vector = /^(?:b|i|u)?vec([234])$/.exec(type);
  if (vector) return Number(vector[1]);
  const matrix = /^mat([234])(?:x([234]))?$/.exec(type);
  return matrix ? Number(matrix[1]) * Number(matrix[2] || matrix[1]) : 1;
}

function identityMatrix() { return new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]); }
function multiplyMatrix4(a, b) {
  const out = new Float32Array(16);
  for (let column = 0; column < 4; column++) for (let row = 0; row < 4; row++) {
    let value = 0;
    for (let k = 0; k < 4; k++) value += a[k * 4 + row] * b[column * 4 + k];
    out[column * 4 + row] = value;
  }
  return out;
}
function mulMat4(matrix, vector) {
  const out = new Array(4);
  for (let row = 0; row < 4; row++) out[row] = matrix[row] * vector[0] + matrix[4 + row] * vector[1] + matrix[8 + row] * vector[2] + matrix[12 + row] * vector[3];
  return out;
}

const GL_TOPOLOGY = new Map([[0x0000, 'point-list'], [0x0001, 'line-list'], [0x0002, 'line-strip'], [0x0004, 'triangle-list'], [0x0005, 'triangle-strip']]);
const GL_COMPARE = new Map([[0x0200, 'never'], [0x0201, 'less'], [0x0202, 'equal'], [0x0203, 'less-equal'], [0x0204, 'greater'], [0x0205, 'not-equal'], [0x0206, 'greater-equal'], [0x0207, 'always']]);
const GL_STENCIL_OP = new Map([[0x1e00, 'keep'], [0, 'zero'], [0x1e01, 'replace'], [0x1e02, 'invert'], [0x1e03, 'increment-clamp'], [0x1e04, 'decrement-clamp'], [0x8507, 'increment-wrap'], [0x8508, 'decrement-wrap']]);
const GL_BLEND_OP = new Map([[0x8006, 'add'], [0x800a, 'subtract'], [0x800b, 'reverse-subtract'], [0x8007, 'min'], [0x8008, 'max']]);
const signedType = type => [0x1400, 0x1402, 0x1404].includes(type);
const GL_ENABLE_CAPABILITIES = new Set([0x0b44, 0x0b71, 0x0b90, 0x0bc0, 0x0be2, 0x0bd0, 0x0c11, 0x8037, 0x809e, 0x80a0, 0x8c89, 0x8db9]);
const S3TC_BLOCK_BYTES = new Map([[0x83f0, 8], [0x83f1, 8], [0x83f2, 16], [0x83f3, 16], [0x8c4c, 8], [0x8c4d, 8], [0x8c4e, 16], [0x8c4f, 16]]);

function rgb565(value) {
  return [((value >> 11) & 31) * 255 / 31, ((value >> 5) & 63) * 255 / 63, (value & 31) * 255 / 31].map(Math.round);
}

function decodeS3tc(format, width, height, data) {
  const blockBytes = S3TC_BLOCK_BYTES.get(format);
  if (!blockBytes) return null;
  const bytes = bytesOf(data), blocksWide = Math.ceil(width / 4), blocksHigh = Math.ceil(height / 4);
  if (bytes.length !== blocksWide * blocksHigh * blockBytes) return null;
  const output = new Uint8Array(width * height * 4);
  for (let by = 0; by < blocksHigh; by++) for (let bx = 0; bx < blocksWide; bx++) {
    const offset = (by * blocksWide + bx) * blockBytes;
    const dxt1 = blockBytes === 8, alphaMode = format === 0x83f2 || format === 0x8c4e;
    const alpha = new Uint8Array(16).fill(255);
    let colorOffset = offset;
    if (!dxt1 && alphaMode) {
      for (let i = 0; i < 8; i++) {
        const packed = bytes[offset + i];
        alpha[i * 2] = (packed & 15) * 17;
        alpha[i * 2 + 1] = (packed >> 4) * 17;
      }
      colorOffset += 8;
    } else if (!dxt1) {
      const a0 = bytes[offset], a1 = bytes[offset + 1];
      const table = new Uint8Array(8); table[0] = a0; table[1] = a1;
      if (a0 > a1) for (let i = 1; i <= 6; i++) table[i + 1] = Math.round(((7 - i) * a0 + i * a1) / 7);
      else {
        for (let i = 1; i <= 4; i++) table[i + 1] = Math.round(((5 - i) * a0 + i * a1) / 5);
        table[6] = 0; table[7] = 255;
      }
      let indices = 0n;
      for (let i = 0; i < 6; i++) indices |= BigInt(bytes[offset + 2 + i]) << BigInt(i * 8);
      for (let i = 0; i < 16; i++) alpha[i] = table[Number((indices >> BigInt(i * 3)) & 7n)];
      colorOffset += 8;
    }
    const c0 = bytes[colorOffset] | (bytes[colorOffset + 1] << 8);
    const c1 = bytes[colorOffset + 2] | (bytes[colorOffset + 3] << 8);
    const palette = [rgb565(c0), rgb565(c1), [0, 0, 0], [0, 0, 0]];
    const supportsDxt1Alpha = format === 0x83f1 || format === 0x8c4d;
    const fourColor = !dxt1 || c0 > c1 || !supportsDxt1Alpha;
    for (let c = 0; c < 3; c++) {
      palette[2][c] = fourColor ? Math.round((2 * palette[0][c] + palette[1][c]) / 3) : Math.round((palette[0][c] + palette[1][c]) / 2);
      palette[3][c] = fourColor ? Math.round((palette[0][c] + 2 * palette[1][c]) / 3) : 0;
    }
    let colorIndices = (bytes[colorOffset + 4] | (bytes[colorOffset + 5] << 8) | (bytes[colorOffset + 6] << 16) | (bytes[colorOffset + 7] << 24)) >>> 0;
    for (let py = 0; py < 4; py++) for (let px = 0; px < 4; px++) {
      const local = py * 4 + px, x = bx * 4 + px, y = by * 4 + py;
      if (x >= width || y >= height) continue;
      const colorIndex = (colorIndices >>> (local * 2)) & 3, dest = (y * width + x) * 4;
      output[dest] = palette[colorIndex][0]; output[dest + 1] = palette[colorIndex][1]; output[dest + 2] = palette[colorIndex][2];
      output[dest + 3] = dxt1 && !fourColor && colorIndex === 3 ? 0 : alpha[local];
    }
  }
  return output;
}
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

function precisionCompatibilityBody(body) {
  const lines = body.split('\n').map(line => line.trim())
    .filter(line => line && !line.startsWith('//') && !line.startsWith('/*') && !line.startsWith('*'));
  return lines.every(line =>
    /^precision[ \t]+(?:lowp|mediump|highp)[ \t]+(?:float|int)[ \t]*;[ \t]*(?:\/\/.*)?$/.test(line) ||
    /^#define[ \t]+(?:lowp|mediump|highp)(?:[ \t]+.*)?$/.test(line));
}

function stripPrecisionCompatibilityGuards(source) {
  return source.replace(/^[ \t]*#(?:ifdef|ifndef)[ \t]+GL_ES[ \t]*\n([\s\S]*?)^[ \t]*#endif[ \t]*$/gm, (whole, body) => {
    const branches = body.split(/^[ \t]*#else\b[^\n]*$/m);
    return branches.length <= 2 && branches.every(precisionCompatibilityBody) ? '' : whole;
  });
}

function lowerMatrixStageInterfacesForNaga(source) {
  const inputs = [];
  const outputs = [];
  let transformed = false;
  const code = source.replace(
    /^([ \t]*)(?:layout\s*\(([^)]*)\)\s*)?((?:(?:centroid|sample|patch|flat|smooth|noperspective|invariant|precise)[ \t]+)*)(in|out)[ \t]+(mat[234](?:x[234])?)[ \t]+([A-Za-z_]\w*)[ \t]*(?:\[(\d+)\])?[ \t]*;[ \t]*$/gm,
    (line, indent, layoutText, qualifiers, direction, matrixType, name, arrayText) => {
      const dimensions = /^mat([234])(?:x([234]))?$/.exec(matrixType);
      if (!dimensions) return line;
      const columns = Number(dimensions[1]);
      const rows = Number(dimensions[2] || dimensions[1]);
      const arrayLength = arrayText ? Number(arrayText) : 1;
      if (!Number.isSafeInteger(arrayLength) || arrayLength < 1) return line;
      transformed = true;
      const vectorType = `vec${rows}`;
      const arraySuffix = arrayText ? `[${arrayLength}]` : '';
      const explicitLocation = /\blocation\s*=\s*(\d+)/.exec(layoutText || '');
      const generated = [];
      for (let column = 0; column < columns; column++) {
        const varyingName = `bw64_varying_${name}_c${column}`;
        let layout = '';
        if (layoutText !== undefined) {
          let nextLayout = layoutText;
          if (explicitLocation) {
            const location = Number(explicitLocation[1]) + column * arrayLength;
            nextLayout = nextLayout.replace(/\blocation\s*=\s*\d+/, `location = ${location}`);
          }
          layout = `layout(${nextLayout}) `;
        }
        generated.push(`${indent}${layout}${qualifiers || ''}${direction} ${vectorType} ${varyingName}${arraySuffix};`);
      }
      generated.unshift(`${indent}${matrixType} ${name}${arraySuffix};`);
      const matrixValue = (element = null) => `mat${dimensions[1]}${dimensions[2] ? `x${dimensions[2]}` : ''}(${Array.from({ length: columns }, (_, column) => {
        const varyingName = `bw64_varying_${name}_c${column}`;
        return element === null ? varyingName : `${varyingName}[${element}]`;
      }).join(', ')})`;
      if (direction === 'in') {
        for (let element = 0; element < arrayLength; element++) {
          const target = arrayText ? `${name}[${element}]` : name;
          inputs.push(`${target} = ${matrixValue(arrayText ? element : null)};`);
        }
      } else {
        for (let element = 0; element < arrayLength; element++) {
          for (let column = 0; column < columns; column++) {
            const varyingName = `bw64_varying_${name}_c${column}`;
            const target = arrayText ? `${varyingName}[${element}]` : varyingName;
            const value = arrayText ? `${name}[${element}][${column}]` : `${name}[${column}]`;
            outputs.push(`${target} = ${value};`);
          }
        }
      }
      return generated.join('\n');
    },
  );
  if (!transformed) return code;
  const main = /void\s+main\s*\([^)]*\)\s*\{/m.exec(code);
  if (!main) throw new Error('matrix stage interface requires void main()');
  let depth = 1, cursor = main.index + main[0].length;
  for (; cursor < code.length && depth; cursor++) {
    if (code[cursor] === '{') depth++;
    else if (code[cursor] === '}') depth--;
  }
  if (depth) throw new Error('matrix stage interface has an unterminated main()');
  let result = code;
  if (inputs.length) {
    const start = main.index + main[0].length;
    result = result.slice(0, start) + `\n${inputs.join('\n')}\n` + result.slice(start);
  }
  if (outputs.length) {
    // Find main again because input initialization may have shifted its close brace.
    const updatedMain = /void\s+main\s*\([^)]*\)\s*\{/m.exec(result);
    let end = updatedMain.index + updatedMain[0].length, bodyDepth = 1;
    for (; end < result.length && bodyDepth; end++) {
      if (result[end] === '{') bodyDepth++;
      else if (result[end] === '}') bodyDepth--;
    }
    const close = end - 1;
    result = result.slice(0, close) + `\n${outputs.join('\n')}\n` + result.slice(close);
  }
  return result;
}

export function rewriteGlslForWebGPU(source, stage, attributeBindings = new Map(), interfaceLocations = new Map(), clipControl = [0x8ca1, 0x935e]) {
  // ES precision hints do not change WGSL behavior; strip them before parsing
  // declarations because Naga's GLSL 450 frontend rejects variable precision qualifiers.
  let code = String(source || '').replace(/^[ \t]*#version[ \t]+(330|400|410|420|430|440|450|460)(?:[ \t]+core)?[ \t]*$/gm, '#version 150');
  code = stripPrecisionCompatibilityGuards(code.replace(/\r\n?/g, '\n').replace(/\\[ \t]*\n[ \t]*/g, ''))
    .replace(/^[ \t]*#define[ \t]+(?:lowp|mediump|highp)(?:[ \t]+[^\n]*)?[ \t]*$/gm, '')
    .replace(/^[ \t]*precision[ \t]+(?:lowp|mediump|highp)[ \t]+(?:float|int)[ \t]*;[ \t]*$/gm, '')
    .replace(/\b(?:lowp|mediump|highp)\b/g, '');
  if (/\bgl_ModelViewProjectionMatrix\b/.test(code)) {
    code = code.replace(/\bgl_ModelViewProjectionMatrix\b/g, 'bw_ModelViewProjectionMatrix');
    if (!/\buniform\s+mat4\s+bw_ModelViewProjectionMatrix\b/.test(code)) {
      code = `uniform mat4 bw_ModelViewProjectionMatrix;\n${code}`;
    }
  }
  // Uniform reflection needs literal array lengths before it moves declarations
  // into a block that may precede the guest's #define line (Mindustry's LENGTH).
  const integerDefines = new Map(
    [...code.matchAll(/^[ \t]*#define[ \t]+([A-Za-z_]\w*)[ \t]+(\d+)[ \t]*(?:\/\/[^\n]*)?$/gm)]
      .map((match) => [match[1], match[2]]),
  );
  if (integerDefines.size) code = code.replace(/\[[ \t]*([A-Za-z_]\w*)[ \t]*\]/g,
    (match, name) => integerDefines.has(name) ? `[${integerDefines.get(name)}]` : match);
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
    code = code.replace(new RegExp(`\\btexture(?:2D)?\\s*\\(\\s*${escaped}\\s*,`, 'g'), `texture(sampler2D(${sampler.name}_bw_t, ${sampler.name}_bw_s),`);
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
    // GLSL negative-one-to-one clip depth needs conversion to WebGPU's [0,w]
    // range. Upper-left clip origin also reverses the vertex y coordinate.
    const main = /void\s+main\s*\([^)]*\)\s*\{/m.exec(code);
    if (main) {
      let depth = 1, cursor = main.index + main[0].length;
      for (; cursor < code.length && depth; cursor++) {
        if (code[cursor] === '{') depth++;
        else if (code[cursor] === '}') depth--;
      }
      if (!depth) {
        const remap = [
          ...(clipControl[0] === 0x8ca2 ? ['gl_Position.y = -gl_Position.y;'] : []),
          ...(clipControl[1] === 0x935e ? ['gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;'] : []),
        ];
        if (remap.length) code = code.slice(0, cursor - 1) + `\n  ${remap.join('\n  ')}\n` + code.slice(cursor - 1);
      }
    }
  }
  // WGSL entry-point interfaces cannot carry matrices. Lower matrix stage IO
  // into one vec per column and reconstruct the GLSL matrix inside main().
  code = lowerMatrixStageInterfacesForNaga(code);
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
    this._unsupportedWarnings = new Set();
    this._logError = false;
    this._nextId = 1;
    this._clear = [0, 0, 0, 0];
    this._clearDepth = 1;
    this._clearStencil = 0;
    this._viewport = [0, 0, canvas.width || 1, canvas.height || 1];
    this._scissor = [...this._viewport];
    this._pack = { alignment: 4, rowLength: 0, skipRows: 0, skipPixels: 0 };
    this._unpack = { alignment: 4, flipY: false, premultiplyAlpha: false, rowLength: 0, skipRows: 0, skipPixels: 0 };
    this._drawFramebuffer = defaultFramebuffer;
    this._readFramebuffer = defaultFramebuffer;
    this._defaultBufferSelection = { draw: 0x0405, read: 0x0405 };
    this._bufferSelections = new WeakMap();
    this._gpuWrittenFramebuffers = new WeakSet();
    this._readbackSlots = [];
    this._readbackNext = 0;
    this._frameDirty = false;
    this._errors = new Set();
    this._arrayBuffer = null;
    this._elementArrayBuffer = null;
    this._pixelPackBuffer = null;
    this._currentProgram = null;
    this._fixedFunctionProgramCache = new Map();
    this._fixedFunctionPointers = new Map();
    this._fixedTextureEnabled = Array(16).fill(false);
    this._clientActiveTextureUnit = 0;
    this._fixedFunction = {
      matrixMode: 0x1700,
      modelview: [identityMatrix()], projection: [identityMatrix()],
      texture: Array.from({ length: 16 }, () => [identityMatrix()]),
      currentTexCoords: Array.from({ length: 16 }, () => [0, 0, 0, 1]),
      texEnv: Array(16).fill(0x2100),
      color: [1, 1, 1, 1], alphaFunc: 0x0207, alphaRef: 0,
    };
    this._enabled = new Set();
    this._blend = { srcRGB: 1, dstRGB: 0, srcAlpha: 1, dstAlpha: 0, equationRGB: 0x8006, equationAlpha: 0x8006 };
    this._blendColor = [0, 0, 0, 0];
    this._depthFunc = 0x0201;
    this._depthMask = true;
    this._depthRange = [0, 1];
    this._stencilFront = { func: 0x0207, ref: 0, readMask: 0xffffffff, writeMask: 0xffffffff, fail: 0x1e00, depthFail: 0x1e00, pass: 0x1e00 };
    this._stencilBack = { ...this._stencilFront };
    this._polygonOffset = [0, 0];
    this._clipControl = [0x8ca1, 0x935e];
    this._polygonMode = 0x1b02;
    this._lineWidth = 1;
    this._pointSize = 1;
    this._cullFace = 0x0405;
    this._frontFace = 0x0901;
    this._colorMask = [true, true, true, true];
    this._pipelineCache = new Map();
    this._clearPipelineCache = new Map();
    this._blitPipelineCache = new Map();
    this._constantBuffers = new Map();
    this._buffers = new Set();
    this._shaders = new Set();
    this._programs = new Set();
    this._queries = new Set();
    this._activeQueries = new Map();
    this._textures = new Set();
    this._textureRecords = new WeakMap();
    this._framebufferObjects = new Set();
    this._renderbuffers = new Set();
    this._textureUnits = Array.from({ length: 16 }, () => ({ [GL.TEXTURE_2D]: null }));
    this._activeTextureUnit = 0;
    this._boundRenderbuffer = null;
    this._defaultWhiteTexture = null;
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
    device.addEventListener('uncapturederror', event => this._reportFailure(event.error || new Error('uncaptured WebGPU error')));
    device.lost?.then(info => this._reportFailure(new Error(`WebGPU device lost: ${info.reason} ${info.message}`))).catch(error => this._reportFailure(error));
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
      viewFormats: [`${this._format}-srgb`],
      alphaMode: 'opaque',
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST,
    });
  }

  _ensureTargets(width, height) {
    width = Math.max(1, Math.min(8192, width | 0));
    height = Math.max(1, Math.min(8192, height | 0));
    if (this._colorTexture && width === this._width && height === this._height) return;
    const old = this._pixels;
    const oldWidth = this._width;
    const oldHeight = this._height;
    this._colorTexture?.destroy();
    this._depthTexture?.destroy();
    this._width = width;
    this._height = height;
    this.canvas.width = width;
    this.canvas.height = height;
    this._configureCanvas();
    this._colorTexture = this._device.createTexture({
      size: [width, height, 1], format: this._format, viewFormats: [`${this._format}-srgb`],
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST,
    });
    this._depthTexture = this._device.createTexture({
      size: [width, height, 1], format: 'depth24plus-stencil8',
      usage: GPUTextureUsage.RENDER_ATTACHMENT,
    });
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
    const first = reportBridgeFailure('gl-webgpu', error, this._log);
    if (first) try { this.onFailure?.(error); } catch {}
  }

  _rejectUnsupported(feature, error = GL.INVALID_OPERATION) {
    if (!this._unsupportedWarnings.has(feature)) {
      this._unsupportedWarnings.add(feature);
      try { this._log?.(`gl-webgpu: unsupported ${feature}`); } catch {}
    }
    this._recordError(error);
  }

  _framebufferForTarget(target) {
    return target === GL.READ_FRAMEBUFFER ? this._readFramebuffer : this._drawFramebuffer;
  }

  _bufferSelection(framebuffer) {
    return this._framebufferObjects.has(framebuffer)
      ? this._bufferSelections.get(framebuffer) || { draw: 0x8ce0, read: 0x8ce0 }
      : this._defaultBufferSelection;
  }

  _targetFor(framebuffer = this._drawFramebuffer, srgbEnabled = this._enabled.has(0x8db9)) {
    if (this._isDefaultFramebuffer(framebuffer)) return {
      default: true, framebuffer, colorTexture: this._colorTexture, depthTexture: this._depthTexture,
      pixels: this._pixels, width: this._width, height: this._height,
      format: srgbEnabled ? `${this._format}-srgb` : this._format, depthFormat: 'depth24plus-stencil8',
    };
    const object = framebuffer;
    const colorAttachment = object?.attachments?.get(0x8ce0);
    if (!colorAttachment) return null;
    const color = colorAttachment.type === 'texture' ? colorAttachment.object : colorAttachment.object;
    const depthAttachment = object.attachments.get(0x8d00) || object.attachments.get(0x821a);
    const depth = depthAttachment?.object || null;
    const colorTexture = colorAttachment.type === 'texture' ? color?.gpu : color?.gpu;
    const depthTexture = depth ? depth.gpu : null;
    const colorMipLevel = colorAttachment.level || 0;
    const level = color?.levels?.get(colorMipLevel);
    return {
      default: false, framebuffer: object, colorObject: color, depthObject: depth,
      colorTexture, depthTexture, colorMipLevel, depthMipLevel: depthAttachment?.level || 0,
      pixels: level?.pixels || color?.pixels || null,
      width: level?.width || color?.width || 0, height: level?.height || color?.height || 0,
      format: color?.format?.endsWith('-srgb') && !srgbEnabled ? color.format.slice(0, -5) : color?.format || 'rgba8unorm',
      depthFormat: depth?.format || null,
    };
  }

  async _clearGpu(mask, state) {
    const target = this._targetFor(state.framebuffer, state.enabled.has(0x8db9));
    if (!target?.colorTexture || !target.width || !target.height) return;
    if ((state.drawBuffer ?? this._bufferSelection(state.framebuffer).draw) === GL.NONE) mask &= ~GL.COLOR_BUFFER_BIT;
    const scissor = state.enabled.has(0x0c11);
    const fullColor = (mask & GL.COLOR_BUFFER_BIT) && state.colorMask.every(Boolean) && !scissor;
    const fullDepth = target.depthTexture && (mask & GL.DEPTH_BUFFER_BIT) && state.depthMask && !scissor;
    const hasStencil = target.depthTexture && target.depthFormat?.includes('stencil');
    const fullStencil = hasStencil && (mask & GL.STENCIL_BUFFER_BIT) && !scissor && (state.stencilFront.writeMask & 0xff) === 0xff && (state.stencilBack.writeMask & 0xff) === 0xff;
    const partialStencil = hasStencil && (mask & GL.STENCIL_BUFFER_BIT) && !fullStencil && ((state.stencilFront.writeMask | state.stencilBack.writeMask) & 0xff) !== 0;
    const partialColor = (mask & GL.COLOR_BUFFER_BIT) && !fullColor && state.colorMask.some(Boolean);
    const partialDepth = target.depthTexture && (mask & GL.DEPTH_BUFFER_BIT) && !fullDepth && state.depthMask;
    if (!fullColor && !fullDepth && !fullStencil && !partialColor && !partialDepth && !partialStencil) return;
    const encoder = this._device.createCommandEncoder();
    const descriptor = {
      colorAttachments: [{
        view: target.colorTexture.createView({ format: target.format, baseMipLevel: target.colorMipLevel || 0, mipLevelCount: 1 }),
        clearValue: { r: state.color[0], g: state.color[1], b: state.color[2], a: state.color[3] },
        loadOp: fullColor ? 'clear' : 'load', storeOp: 'store',
      }],
    };
    if (target.depthTexture) descriptor.depthStencilAttachment = {
        view: target.depthTexture.createView({ baseMipLevel: target.depthMipLevel || 0, mipLevelCount: 1 }),
        depthClearValue: state.depth,
        depthLoadOp: fullDepth ? 'clear' : 'load',
        depthStoreOp: 'store',
        ...(target.depthFormat?.includes('stencil') ? {
          stencilClearValue: state.stencil & 0xff,
          stencilLoadOp: fullStencil ? 'clear' : 'load',
          stencilStoreOp: 'store',
        } : {}),
      };
    const pass = encoder.beginRenderPass(descriptor);
    if (partialColor || partialDepth || partialStencil) await this._drawClearRect(pass, state, partialColor, partialDepth, partialStencil, target);
    pass.end();
    this._device.queue.submit([encoder.finish()]);
  }

  async _drawClearRect(pass, state, clearColor, clearDepth, clearStencil, target) {
    const color = state.color.map(value => Number(value).toPrecision(8)).join(',');
    const depth = Number(state.depth).toPrecision(8);
    const mask = state.colorMask.reduce((bits, enabled, index) => enabled && clearColor ? bits | (1 << index) : bits, 0);
    const stencilMask = state.stencilFront.writeMask & state.stencilBack.writeMask & 0xff;
    const key = stateKey([color, depth, mask, clearColor, clearDepth, clearStencil, stencilMask, state.stencil & 0xff, target.format, target.depthFormat]);
    let pipeline = this._clearPipelineCache.get(key);
    if (!this._clearPipelineCache.has(key)) {
      const fields = [];
      if (clearColor) fields.push('@location(0) color: vec4<f32>,');
      if (clearDepth) fields.push('@builtin(frag_depth) depth: f32,');
      const value = [clearColor ? `vec4<f32>(${color})` : null, clearDepth ? depth : null].filter(value => value !== null).join(',');
      const fragment = fields.length ? `struct Out { ${fields.join(' ')} };\n@fragment fn fs()->Out{return Out(${value});}` : '@fragment fn fs() {}';
      const code = `@vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4<f32>{var p=array<vec2<f32>,3>(vec2<f32>(-1.0,-1.0),vec2<f32>(3.0,-1.0),vec2<f32>(-1.0,3.0));return vec4<f32>(p[i],0.0,1.0);}\n${fragment}`;
      let scopeOpen = false;
      try {
        this._device.pushErrorScope('validation');
        scopeOpen = true;
        pipeline = this._device.createRenderPipeline({
          layout: 'auto', vertex: { module: this._device.createShaderModule({ code }), entryPoint: 'vs' },
          fragment: { module: this._device.createShaderModule({ code }), entryPoint: 'fs', targets: [{ format: target.format, writeMask: mask }] },
          primitive: { topology: 'triangle-list' },
          ...(target.depthTexture ? { depthStencil: {
            format: target.depthFormat || 'depth24plus', depthWriteEnabled: Boolean(clearDepth), depthCompare: 'always',
            ...(clearStencil ? { stencilFront: { compare: 'always', failOp: 'keep', depthFailOp: 'keep', passOp: 'replace' }, stencilBack: { compare: 'always', failOp: 'keep', depthFailOp: 'keep', passOp: 'replace' }, stencilReadMask: 0xff, stencilWriteMask: stencilMask } : {}),
          } } : {}),
        });
        const validationError = await this._device.popErrorScope();
        scopeOpen = false;
        if (validationError) throw new Error(validationError.message || 'WebGPU rejected the clear pipeline');
        this._clearPipelineCache.set(key, pipeline);
      } catch (error) {
        if (scopeOpen) try { await this._device.popErrorScope(); } catch {}
        this._clearPipelineCache.set(key, null);
        this._reportFailure(error);
        return;
      }
    }
    if (!pipeline) return;
    pass.setPipeline(pipeline);
    if (clearStencil) pass.setStencilReference(state.stencil & 0xff);
    pass.setViewport(0, 0, target.width, target.height, 0, 1);
    if (state.enabled.has(0x0c11)) {
      const [sx, sy, sw, sh] = state.scissor;
      const left = Math.max(0, sx), top = Math.max(0, target.height - sy - sh);
      pass.setScissorRect(left, top, Math.max(0, Math.min(target.width - left, sw)), Math.max(0, Math.min(target.height - top, sh)));
    }
    pass.draw(3);
  }

  _clearPixels(mask, state) {
    if (!(mask & GL.COLOR_BUFFER_BIT) || (state.drawBuffer ?? this._bufferSelection(state.framebuffer).draw) === GL.NONE) return;
    const target = this._targetFor(state.framebuffer, state.enabled.has(0x8db9)), pixels = target?.pixels;
    if (!target || !pixels) return;
    const srgb = target.format?.endsWith('-srgb');
    const rgba = state.color.map((value, channel) => {
      const linear = clamp01(value);
      const encoded = srgb && channel < 3 ? (linear <= 0.0031308 ? linear * 12.92 : 1.055 * linear ** (1 / 2.4) - 0.055) : linear;
      return Math.round(encoded * 255);
    });
    let left = 0, right = target.width, top = 0, bottom = target.height;
    if (state.enabled.has(0x0c11)) {
      const [x, y, width, height] = state.scissor;
      left = Math.max(0, x); right = Math.min(target.width, x + width);
      top = Math.max(0, y); bottom = Math.min(target.height, y + height);
    }
    for (let y = top; y < bottom; y++) for (let x = left; x < right; x++) {
      const i = (y * target.width + x) * 4;
      for (let channel = 0; channel < 4; channel++) if (state.colorMask[channel]) pixels[i + channel] = rgba[channel];
    }
  }

  clearColor(r, g, b, a) { this._clear = [clamp01(r), clamp01(g), clamp01(b), clamp01(a)]; }
  clearDepth(value) { this._clearDepth = clamp01(value); }
  clearStencil(value) { this._clearStencil = value | 0; }
  // WebGL2 removed glShadeModel; its surviving smooth interpolation matches
  // the default GL_SMOOTH behavior used by the fixed-function bridge.
  shadeModel(_mode) {}
  _fixedMatrixStack() {
    const mode = this._fixedFunction.matrixMode;
    return mode === 0x1701 ? this._fixedFunction.projection
      : mode === 0x1702 ? this._fixedFunction.texture[this._activeTextureUnit] : this._fixedFunction.modelview;
  }
  matrixMode(mode) {
    if (![0x1700, 0x1701, 0x1702].includes(mode)) { this._recordError(GL.INVALID_ENUM); return; }
    this._fixedFunction.matrixMode = mode | 0;
  }
  loadIdentity() { this._fixedMatrixStack()[this._fixedMatrixStack().length - 1] = identityMatrix(); }
  loadMatrixf(matrix) {
    if (!matrix || matrix.length < 16) return;
    this._fixedMatrixStack()[this._fixedMatrixStack().length - 1] = Float32Array.from(matrix).subarray(0, 16);
  }
  loadMatrixd(matrix) { this.loadMatrixf(matrix); }
  multMatrixf(matrix) {
    if (!matrix || matrix.length < 16) return;
    const stack = this._fixedMatrixStack(), top = stack.length - 1;
    stack[top] = multiplyMatrix4(stack[top], Float32Array.from(matrix).subarray(0, 16));
  }
  multMatrixd(matrix) { this.multMatrixf(matrix); }
  pushMatrix() { const stack = this._fixedMatrixStack(); stack.push(new Float32Array(stack[stack.length - 1])); }
  popMatrix() { const stack = this._fixedMatrixStack(); if (stack.length > 1) stack.pop(); else this._recordError(GL.INVALID_OPERATION); }
  ortho(left, right, bottom, top, near, far) {
    const m = identityMatrix();
    m[0] = 2 / (right - left); m[5] = 2 / (top - bottom); m[10] = -2 / (far - near);
    m[12] = -(right + left) / (right - left); m[13] = -(top + bottom) / (top - bottom);
    m[14] = -(far + near) / (far - near);
    this.multMatrixf(m);
  }
  frustum(left, right, bottom, top, near, far) {
    const m = new Float32Array(16);
    m[0] = 2 * near / (right - left); m[5] = 2 * near / (top - bottom);
    m[8] = (right + left) / (right - left); m[9] = (top + bottom) / (top - bottom);
    m[10] = -(far + near) / (far - near); m[11] = -1;
    m[14] = -(2 * far * near) / (far - near);
    this.multMatrixf(m);
  }
  translatef(x, y, z) {
    const m = identityMatrix(); m[12] = x; m[13] = y; m[14] = z; this.multMatrixf(m);
  }
  rotatef(angle, x, y, z) {
    const length = Math.hypot(x, y, z);
    if (!length) return;
    x /= length; y /= length; z /= length;
    const radians = angle * Math.PI / 180, c = Math.cos(radians), s = Math.sin(radians), t = 1 - c;
    this.multMatrixf(new Float32Array([
      x * x * t + c, y * x * t + z * s, z * x * t - y * s, 0,
      x * y * t - z * s, y * y * t + c, z * y * t + x * s, 0,
      x * z * t + y * s, y * z * t - x * s, z * z * t + c, 0,
      0, 0, 0, 1,
    ]));
  }
  scalef(x, y, z) { this.multMatrixf(new Float32Array([x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1])); }
  color3f(r, g, b) { this._fixedFunction.color = [r, g, b, 1].map(clamp01); }
  color4f(r, g, b, a) { this._fixedFunction.color = [r, g, b, a].map(clamp01); }
  color4ub(r, g, b, a) { this.color4f(r / 255, g / 255, b / 255, a / 255); }
  alphaFunc(func, reference) {
    if (![0x0200, 0x0201, 0x0202, 0x0203, 0x0204, 0x0205, 0x0206, 0x0207].includes(func)) { this._recordError(GL.INVALID_ENUM); return; }
    if (!Number.isFinite(reference)) { this._recordError(GL.INVALID_VALUE); return; }
    this._fixedFunction.alphaFunc = func | 0;
    this._fixedFunction.alphaRef = clamp01(reference);
  }
  texEnvi(target, pname, value) {
    if (target !== 0x2300) { this._recordError(GL.INVALID_ENUM); return; }
    if (pname !== 0x2200) { this._recordError(GL.INVALID_ENUM); return; }
    if (![0x2100, 0x1e01, 0x2101, 0x0104, 0x8570].includes(value)) { this._recordError(GL.INVALID_ENUM); return; }
    this._fixedFunction.texEnv[this._activeTextureUnit] = value | 0;
  }
  texEnvf(target, pname, value) { this.texEnvi(target, pname, value); }
  texEnvfv(target, pname, values) { if (pname === 0x2201) this._fixedFunction.texEnvColor = Array.from(values || []); }
  texEnviv(target, pname, values) { this.texEnvfv(target, pname, values); }
  clientActiveTexture(texture) {
    if (!Number.isInteger(texture) || texture < 0x84c0 || texture >= 0x84c8) { this._recordError(GL.INVALID_ENUM); return; }
    if (texture > 0x84c1) { this._rejectUnsupported('fixed-function client texture units above unit 1', GL.INVALID_OPERATION); return; }
    this._clientActiveTextureUnit = texture - 0x84c0;
  }
  multiTexCoord2f(texture, s, t) {
    const unit = Math.max(0, Math.min(1, (texture | 0) - 0x84c0));
    this._fixedFunction.currentTexCoords[unit] = [Number(s), Number(t), 0, 1];
  }
  fixedFunctionPointer(index, size, type, stride, offset) {
    this._fixedFunctionPointers.set(index | 0, {
      buffer: this._arrayBuffer, size: size | 0, type: type | 0, stride: stride | 0, offset: offset | 0,
    });
  }
  clear(mask) {
    const state = { color: [...this._clear], depth: this._clearDepth, stencil: this._clearStencil, stencilFront: { ...this._stencilFront }, stencilBack: { ...this._stencilBack }, enabled: new Set(this._enabled), colorMask: [...this._colorMask], depthMask: this._depthMask, scissor: [...this._scissor], framebuffer: this._drawFramebuffer };
    if ((mask & GL.COLOR_BUFFER_BIT) && this._bufferSelection(state.framebuffer).draw !== GL.NONE) this._frameDirty = true;
    this._clearPixels(mask, state);
    this._enqueue(() => this._clearGpu(mask, state));
  }
  viewport(x, y, width, height) {
    if (width < 0 || height < 0) { this._recordError(GL.INVALID_VALUE); return; }
    this._viewport = [x | 0, y | 0, width | 0, height | 0];
    if (this._isDefaultFramebuffer(this._drawFramebuffer) && (x + width > this._width || y + height > this._height))
      this._ensureTargets(Math.max(this._width, x + width), Math.max(this._height, y + height));
  }
  scissor(x, y, width, height) {
    if (width < 0 || height < 0) { this._recordError(GL.INVALID_VALUE); return; }
    this._scissor = [x | 0, y | 0, width | 0, height | 0];
  }
  enable(capability) {
    if (capability === GL.TEXTURE_2D) this._fixedTextureEnabled[this._activeTextureUnit] = true;
    else if (capability === 0x8861) this._rejectUnsupported('point sprites', GL.INVALID_OPERATION);
    else if (GL_ENABLE_CAPABILITIES.has(capability)) this._enabled.add(capability);
    else this._recordError(GL.INVALID_ENUM);
  }
  disable(capability) {
    if (capability === GL.TEXTURE_2D) this._fixedTextureEnabled[this._activeTextureUnit] = false;
    else if (capability === 0x8861) this._rejectUnsupported('point sprites', GL.INVALID_OPERATION);
    else if (GL_ENABLE_CAPABILITIES.has(capability)) this._enabled.delete(capability);
    else this._recordError(GL.INVALID_ENUM);
  }
  isEnabled(capability) {
    if (capability === GL.TEXTURE_2D) return !!this._fixedTextureEnabled[this._activeTextureUnit];
    if (!GL_ENABLE_CAPABILITIES.has(capability) && capability !== 0x8861) { this._recordError(GL.INVALID_ENUM); return false; }
    if (capability === 0x8861) { this._rejectUnsupported('point sprites', GL.INVALID_OPERATION); return false; }
    return this._enabled.has(capability);
  }

  getParameter(parameter) {
    switch (parameter) {
      case GL.VERSION: return 'OpenGL ES 3.0 WebGPUGL';
      case GL.VENDOR: return 'WebGPU';
      case GL.RENDERER: return 'WebGPU GL translation';
      case GL.SHADING_LANGUAGE_VERSION: return 'WebGL GLSL ES 3.00';
      case GL.VIEWPORT: return new Int32Array(this._viewport);
      case GL.DEPTH_RANGE: return new Float32Array(this._depthRange);
      case 0x935c: return this._clipControl[0];
      case 0x935d: return this._clipControl[1];
      case 0x0b21: return this._lineWidth;
      case 0x0b11: return this._pointSize;
      case GL.SCISSOR_BOX: return new Int32Array(this._scissor);
      case GL.DRAW_FRAMEBUFFER_BINDING: case GL.FRAMEBUFFER_BINDING: return this._drawFramebuffer;
      case GL.READ_FRAMEBUFFER_BINDING: return this._readFramebuffer;
      case GL.DRAW_BUFFER0: return this._bufferSelection(this._drawFramebuffer).draw;
      case GL.ARRAY_BUFFER_BINDING: return this._arrayBuffer;
      case GL.ELEMENT_ARRAY_BUFFER_BINDING: return this._elementArrayBuffer;
      case GL.PIXEL_PACK_BUFFER_BINDING: return this._pixelPackBuffer;
      case GL.TEXTURE_BINDING_2D: return this._textureUnits[this._activeTextureUnit]?.[GL.TEXTURE_2D]?.handle || null;
      case GL.RENDERBUFFER_BINDING: return this._boundRenderbuffer;
      case 0x84e0: return 0x84c0 + this._activeTextureUnit;
      case GL.CURRENT_PROGRAM: return this._currentProgram;
      case GL.PACK_ALIGNMENT: return this._pack.alignment;
      case GL.PACK_ROW_LENGTH: return this._pack.rowLength;
      case GL.PACK_SKIP_ROWS: return this._pack.skipRows;
      case GL.PACK_SKIP_PIXELS: return this._pack.skipPixels;
      case GL.MAX_TEXTURE_SIZE: return this._device?.limits?.maxTextureDimension2D || 8192;
      case 0x0cf5: return this._unpack.alignment;
      case GL.MAX_TEXTURE_IMAGE_UNITS: return 8;
      case GL.MAX_COMBINED_TEXTURE_IMAGE_UNITS: return 8;
      case GL.MAX_DRAW_BUFFERS: return 1;
      case 0x8869: return 16; // GL_MAX_VERTEX_ATTRIBS
      case GL.FRAMEBUFFER_COMPLETE: return GL.FRAMEBUFFER_COMPLETE;
      default: this._recordError(GL.INVALID_ENUM); return 0;
    }
  }

  pixelStorei(parameter, value) {
    if (![GL.PACK_ALIGNMENT, GL.PACK_ROW_LENGTH, GL.PACK_SKIP_ROWS, GL.PACK_SKIP_PIXELS, 0x0cf5, 0x0cf2, 0x0cf3, 0x0cf4].includes(parameter)) { this._recordError(GL.INVALID_ENUM); return; }
    if (!Number.isInteger(value) || value < 0 || (parameter === GL.PACK_ALIGNMENT || parameter === 0x0cf5) && ![1, 2, 4, 8].includes(value)) { this._recordError(GL.INVALID_VALUE); return; }
    if (parameter === GL.PACK_ALIGNMENT) this._pack.alignment = value;
    else if (parameter === GL.PACK_ROW_LENGTH) this._pack.rowLength = value;
    else if (parameter === GL.PACK_SKIP_ROWS) this._pack.skipRows = value;
    else if (parameter === GL.PACK_SKIP_PIXELS) this._pack.skipPixels = value;
    else if (parameter === 0x0cf5) this._unpack.alignment = value;
    else if (parameter === 0x0cf2) this._unpack.rowLength = value;
    else if (parameter === 0x0cf3) this._unpack.skipRows = value;
    else if (parameter === 0x0cf4) this._unpack.skipPixels = value;
    else if (parameter === 0x9240) this._unpack.flipY = !!value;
    else if (parameter === 0x9241) this._unpack.premultiplyAlpha = !!value;
  }

  readPixels(x, y, width, height, format, type, destination) {
    if (this._bufferSelection(this._readFramebuffer).read === GL.NONE) return;
    if (width < 0 || height < 0) { this._recordError(GL.INVALID_VALUE); return; }
    const components = format === 0x1907 ? 3 : [0x1908, 0x80e1].includes(format) ? 4 : 0;
    const byteType = type === 0x1401;
    const floatType = type === 0x1406;
    const knownFormats = [0x1903, 0x8227, 0x1907, 0x1908, 0x80e1, 0x1902, 0x84f9];
    const knownTypes = [0x1401, 0x1403, 0x1405, 0x1406, 0x84fa];
    let targetDestination = destination;
    if (!knownFormats.includes(format)) { this._rejectUnsupported(`readPixels format 0x${Number(format).toString(16)}`, GL.INVALID_ENUM); return; }
    if (!knownTypes.includes(type)) { this._rejectUnsupported(`readPixels type 0x${Number(type).toString(16)}`, GL.INVALID_ENUM); return; }
    if (!components) { this._rejectUnsupported(`readPixels format 0x${Number(format).toString(16)}`, GL.INVALID_OPERATION); return; }
    if (!byteType && !floatType) { this._rejectUnsupported(`readPixels type 0x${Number(type).toString(16)}`, GL.INVALID_OPERATION); return; }
    const pboOffset = typeof destination === 'number' ? destination : null;
    if (pboOffset !== null) {
      if (!this._pixelPackBuffer) { this._rejectUnsupported('pixel pack buffer destination without PIXEL_PACK_BUFFER', GL.INVALID_OPERATION); return; }
      if (!Number.isSafeInteger(pboOffset) || pboOffset < 0) { this._recordError(GL.INVALID_VALUE); return; }
      const data = this._pixelPackBuffer.data;
      if (pboOffset > data.byteLength || (floatType && pboOffset % 4)) { this._recordError(GL.INVALID_OPERATION); return; }
      targetDestination = floatType
        ? new Float32Array(data.buffer, data.byteOffset + pboOffset, Math.floor((data.byteLength - pboOffset) / 4))
        : new Uint8Array(data.buffer, data.byteOffset + pboOffset, data.byteLength - pboOffset);
    }
    const byteDestination = targetDestination instanceof Uint8Array || targetDestination instanceof Uint8ClampedArray;
    const floatDestination = targetDestination instanceof Float32Array;
    if (byteType && !byteDestination || floatType && !floatDestination) {
      this._rejectUnsupported('readPixels destination type', GL.INVALID_OPERATION); return;
    }
    if (!targetDestination || typeof targetDestination.length !== 'number') {
      this._rejectUnsupported('pixel pack buffer destination', GL.INVALID_OPERATION); return;
    }
    const target = this._targetFor(this._readFramebuffer);
    if (!target?.default && this._gpuWrittenFramebuffers.has(target?.framebuffer)) {
      this._rejectUnsupported('GPU-backed framebuffer readPixels', GL.INVALID_OPERATION); return;
    }
    // The default framebuffer is served from the last GPU readback once one
    // has landed; the CPU shadow only covers frames before the first present.
    const gpuFrame = target?.default && this._gpuPixels?.length === target.width * target.height * 4;
    const pixels = gpuFrame ? this._gpuPixels : target?.pixels;
    if (!target || !pixels) return;
    const rowLength = this._pack.rowLength || width;
    const rawStride = rowLength * components * (floatType ? 4 : 1);
    const stride = Math.ceil(rawStride / this._pack.alignment) * this._pack.alignment;
    const bytesPerComponent = floatType ? 4 : 1;
    const base = (this._pack.skipRows * stride + this._pack.skipPixels * components * bytesPerComponent) / bytesPerComponent;
    const required = base + Math.max(0, height - 1) * stride / bytesPerComponent + width * components;
    if (pboOffset !== null && required > targetDestination.length) { this._recordError(GL.INVALID_OPERATION); return; }
    for (let row = 0; row < height; row++) {
      const sourceY = y + row;
      if (sourceY < 0 || sourceY >= target.height) continue;
      for (let col = 0; col < width; col++) {
        const sourceX = x + col;
        if (sourceX < 0 || sourceX >= target.width) continue;
        const src = (sourceY * target.width + sourceX) * 4;
        const dst = base + row * stride / bytesPerComponent + col * components;
        if (dst + components > targetDestination.length) continue;
        const rgba = [pixels[src], pixels[src + 1], pixels[src + 2], pixels[src + 3]];
        const channels = format === 0x1907 ? rgba.slice(0, 3) : format === 0x80e1 ? [rgba[2], rgba[1], rgba[0], rgba[3]] : rgba;
        if (floatType) for (let c = 0; c < components; c++) targetDestination[dst + c] = channels[c] / 255;
        else targetDestination.set(channels, dst);
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
    if (this._frameDirty) {
      this._frameDirty = false;
      if (!this._startReadback()) this._frameDirty = true;
    }
  }

  // The bridge's X11 present reads the default framebuffer with a synchronous
  // glReadPixels, but WebGPU only exposes queue results asynchronously. Changed
  // color frames copy through one of two reusable staging buffers; when the map
  // resolves, the bottom-up RGBA frame replaces the CPU shadow that
  // readPixels serves. Programmable draws (FNA, Mindustry) reach the screen
  // only this way. The image lags the draw calls by about one swap.
  _startReadback() {
    if (!this._colorTexture) return false;
    const width = this._width, height = this._height, format = this._format;
    const bytesPerRow = Math.ceil(width * 4 / 256) * 256;
    const size = bytesPerRow * height;
    if (this._readbackSlots.length < 2) {
      while (this._readbackSlots.length < 2) this._readbackSlots.push({ buffer: null, busy: false });
    }
    let slot = null;
    for (let offset = 0; offset < this._readbackSlots.length; offset++) {
      const index = (this._readbackNext + offset) % this._readbackSlots.length;
      if (!this._readbackSlots[index].busy) {
        slot = this._readbackSlots[index];
        this._readbackNext = (index + 1) % this._readbackSlots.length;
        break;
      }
    }
    if (!slot) return false;
    if (!slot.buffer || slot.buffer.size !== size) {
      slot.buffer?.destroy();
      slot.buffer = this._device.createBuffer({ size, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    }
    const buffer = slot.buffer;
    const encoder = this._device.createCommandEncoder();
    encoder.copyTextureToBuffer({ texture: this._colorTexture }, { buffer, bytesPerRow, rowsPerImage: height }, [width, height, 1]);
    this._device.queue.submit([encoder.finish()]);
    slot.busy = true;
    buffer.mapAsync(GPUMapMode.READ).then(() => {
      const source = new Uint8Array(buffer.getMappedRange());
      const swap = format === 'bgra8unorm' || format === 'bgra8unorm-srgb';
      if (width === this._width && height === this._height) {
        if (this._gpuPixels?.length !== width * height * 4) this._gpuPixels = new Uint8Array(width * height * 4);
        const out = this._gpuPixels;
        for (let y = 0; y < height; y++) {
          const row = (height - 1 - y) * bytesPerRow, dst = y * width * 4;
          if (swap) for (let x = 0; x < width; x++) {
            const s = row + x * 4, d = dst + x * 4;
            out[d] = source[s + 2]; out[d + 1] = source[s + 1]; out[d + 2] = source[s]; out[d + 3] = source[s + 3];
          } else out.set(source.subarray(row, row + width * 4), dst);
        }
        this._gpuFrames = (this._gpuFrames || 0) + 1;
      }
      buffer.unmap();
    }).catch(error => this._reportFailure(error)).finally(() => { slot.busy = false; });
    return true;
  }
  _recordError(error) { if (error !== GL.NO_ERROR) this._errors.add(error); }
  getError() {
    const error = this._errors.values().next().value ?? GL.NO_ERROR;
    if (error !== GL.NO_ERROR) this._errors.delete(error);
    return error;
  }
  isContextLost() { return false; }
  getContextAttributes() { return { alpha: true, depth: true, stencil: true, antialias: false, premultipliedAlpha: true, preserveDrawingBuffer: true }; }
  getSupportedExtensions() { return ['WEBGL_compressed_texture_s3tc', 'ANGLE_instanced_arrays', 'OES_vertex_array_object', 'EXT_read_format_bgra', 'EXT_texture_filter_anisotropic', 'EXT_sRGB']; }
  getExtension(name) {
    if (name === 'WEBGL_compressed_texture_s3tc' || name === 'WEBKIT_WEBGL_compressed_texture_s3tc') return {
      COMPRESSED_RGB_S3TC_DXT1_EXT: 0x83f0, COMPRESSED_RGBA_S3TC_DXT1_EXT: 0x83f1,
      COMPRESSED_RGBA_S3TC_DXT3_EXT: 0x83f2, COMPRESSED_RGBA_S3TC_DXT5_EXT: 0x83f3,
    };
    if (name === 'ANGLE_instanced_arrays') return {
      vertexAttribDivisorANGLE: this.vertexAttribDivisor.bind(this),
      drawArraysInstancedANGLE: this.drawArraysInstanced.bind(this),
      drawElementsInstancedANGLE: this.drawElementsInstanced.bind(this),
    };
    if (name === 'OES_vertex_array_object') return {
      createVertexArrayOES: this.createVertexArray.bind(this), deleteVertexArrayOES: this.deleteVertexArray.bind(this),
      isVertexArrayOES: this.isVertexArray.bind(this), bindVertexArrayOES: this.bindVertexArray.bind(this),
    };
    if (name === 'EXT_read_format_bgra') return { BGRA_EXT: 0x80e1 };
    if (name === 'EXT_texture_filter_anisotropic') return { TEXTURE_MAX_ANISOTROPY_EXT: 0x84fe, MAX_TEXTURE_MAX_ANISOTROPY_EXT: 16 };
    if (name === 'EXT_sRGB') return { SRGB_EXT: 0x8c40, SRGB_ALPHA_EXT: 0x8c42, SRGB8_ALPHA8_EXT: 0x8c43, FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING_EXT: 0x8210, LINEAR_EXT: 0x2601 };
    return null;
  }

  // S2a keeps the original client bytes because WebGPU buffers cannot be read
  // synchronously. GPU copies are created lazily with every buffer role the GL
  // bridge may assign, then refreshed from this shadow before a draw.
  createBuffer() { const object = { id: this._nextId++, data: new Uint8Array(), gpu: null, dirty: true, version: 0, vertexCache: new Map() }; this._buffers.add(object); return object; }
  deleteBuffer(buffer) {
    if (!buffer) return;
    buffer.gpu?.destroy(); buffer.gpu = null; this._buffers.delete(buffer);
    if (this._arrayBuffer === buffer) this._arrayBuffer = null;
    if (this._elementArrayBuffer === buffer) this._elementArrayBuffer = null;
    if (this._pixelPackBuffer === buffer) this._pixelPackBuffer = null;
    for (const vao of [this._defaultVao, ...this._vaos]) {
      if (vao.elementArrayBuffer === buffer) vao.elementArrayBuffer = null;
      for (const attribute of vao.attributes.values()) if (attribute.buffer === buffer) attribute.buffer = null;
    }
  }
  isBuffer(buffer) { return this._buffers.has(buffer); }
  _validateBufferTarget(target) {
    if (target === GL.ARRAY_BUFFER || target === GL.ELEMENT_ARRAY_BUFFER || target === 0x88eb) return true;
    if ([0x8f36, 0x8f37, 0x88eb, 0x88ec, 0x8a11, 0x8c8e, 0x8f3f].includes(target)) this._rejectUnsupported(`buffer target 0x${target.toString(16)}`);
    else this._recordError(GL.INVALID_ENUM);
    return false;
  }
  bindBuffer(target, buffer) {
    const value = buffer || null;
    if (target === GL.ARRAY_BUFFER) this._arrayBuffer = value;
    else if (target === GL.ELEMENT_ARRAY_BUFFER) { this._elementArrayBuffer = value; this._vao.elementArrayBuffer = value; }
    else if (target === 0x88eb) this._pixelPackBuffer = value;
    else this._validateBufferTarget(target);
  }
  _boundBuffer(target) { return target === GL.ARRAY_BUFFER ? this._arrayBuffer : target === GL.ELEMENT_ARRAY_BUFFER ? this._elementArrayBuffer : target === 0x88eb ? this._pixelPackBuffer : null; }
  bufferData(target, data, usage = GL.STATIC_DRAW) {
    if (!this._validateBufferTarget(target)) return;
    const buffer = this._boundBuffer(target);
    if (!buffer) { this._recordError(GL.INVALID_OPERATION); return; }
    if (typeof data === 'number' && data < 0) { this._recordError(GL.INVALID_VALUE); return; }
    if (![0x88e0, 0x88e1, 0x88e2, 0x88e4, 0x88e5, 0x88e6, 0x88e8, 0x88e9, 0x88ea].includes(usage)) { this._recordError(GL.INVALID_ENUM); return; }
    const length = typeof data === 'number' ? data | 0 : bytesOf(data).length;
    buffer.data = typeof data === 'number' ? new Uint8Array(length) : bytesOf(data);
    buffer.usage = usage; buffer.dirty = true; buffer.version++; buffer.vertexCache.clear();
  }
  bufferSubData(target, offset, data) {
    if (!this._validateBufferTarget(target)) return;
    const buffer = this._boundBuffer(target);
    if (!buffer) { this._recordError(GL.INVALID_OPERATION); return; }
    if (offset < 0) { this._recordError(GL.INVALID_VALUE); return; }
    const bytes = bytesOf(data);
    if (offset + bytes.length > buffer.data.length) { this._recordError(GL.INVALID_VALUE); return; }
    buffer.data.set(bytes, offset); buffer.dirty = true; buffer.version++; buffer.vertexCache.clear();
  }
  getBufferParameter(target, parameter) {
    if (!this._validateBufferTarget(target)) return 0;
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
    let failingShader = null;
    let failingStage = 'unknown';
    let failingPreparedCode = '';
    program.linkPromise = this.ready.then(async () => {
      for (const [shader, stage] of [[vertex, 'vertex'], [fragment, 'fragment']]) {
        failingShader = shader;
        failingStage = stage;
        failingPreparedCode = '';
        const prepared = rewriteGlslForWebGPU(shader.source, stage, program.attributes, locationRegistry, this._clipControl);
        failingPreparedCode = prepared.code;
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
      if (failingShader && !shaderFailureDumped) {
        shaderFailureDumped = true;
        this._log(`gl-webgpu: ${failingStage} shader preparation/translation failed: ${program.infoLog}` +
          `\n--- Guest GLSL ---\n${failingShader.source}\n--- GLSL after WebGPU pre-pass ---\n${failingPreparedCode}`);
      }
      this._reportFailure(new Error(`program ${program.id} link failed: ${program.infoLog}`));
      return false;
    });
  }
  validateProgram(program) { if (program) program.validateStatus = !!program.linked; }
  useProgram(program) { this._currentProgram = program || null; }
  getProgramParameter(program, parameter) {
    if (parameter === GL.LINK_STATUS) return !!program?.linked;
    if (parameter === 0x8b83) return !!program?.validateStatus;
    if (parameter === 0x8b85) return program?.shaders?.length || 0;
    if (parameter === 0x8b86) return program?.uniformInfo?.length || 0;
    if (parameter === 0x8b89) return program?.attributeInfo?.length || 0;
    return 0;
  }
  getProgramInfoLog(program) { return program?.infoLog || ''; }
  createQuery() { const query = { id: this._nextId++, target: 0, active: false, available: false, result: 0 }; this._queries.add(query); return query; }
  deleteQuery(query) {
    if (!query || !this._queries.has(query)) return;
    if (query.active) this._activeQueries.delete(query.target);
    query.active = false;
    this._queries.delete(query);
  }
  isQuery(query) { return this._queries.has(query); }
  beginQuery(target, query) {
    if (![0x8914, 0x8c2f, 0x8d6a, 0x8c87, 0x8c88, 0x88bf].includes(target)) { this._recordError(GL.INVALID_ENUM); return; }
    if (!this._queries.has(query) || query.active || this._activeQueries.has(target) || query.target && query.target !== target) { this._recordError(GL.INVALID_OPERATION); return; }
    query.target = target; query.active = true; query.available = false; query.result = 0;
    this._activeQueries.set(target, query);
  }
  endQuery(target) {
    if (![0x8914, 0x8c2f, 0x8d6a, 0x8c87, 0x8c88, 0x88bf].includes(target)) { this._recordError(GL.INVALID_ENUM); return; }
    const query = this._activeQueries.get(target);
    if (!query) { this._recordError(GL.INVALID_OPERATION); return; }
    query.active = false; query.available = true; this._activeQueries.delete(target);
  }
  queryCounter(query, target) {
    if (target !== 0x8e28) { this._recordError(GL.INVALID_ENUM); return; }
    if (!this._queries.has(query) || query.active) { this._recordError(GL.INVALID_OPERATION); return; }
    query.target = target; query.result = 0; query.available = true;
  }
  getQuery(target, parameter) {
    if (![0x8914, 0x8c2f, 0x8d6a, 0x8c87, 0x8c88, 0x88bf].includes(target)) { this._recordError(GL.INVALID_ENUM); return null; }
    if (parameter === 0x8864) return 0;
    if (parameter !== 0x8865) { this._recordError(GL.INVALID_ENUM); return null; }
    return this._activeQueries.get(target) || null;
  }
  getQueryParameter(query, parameter) {
    if (!this._queries.has(query)) { this._recordError(GL.INVALID_OPERATION); return null; }
    if (parameter === 0x8867) return query.available;
    if (parameter === 0x8866) return query.result;
    this._recordError(GL.INVALID_ENUM); return null;
  }
  createTransformFeedback() { this._rejectUnsupported('transform feedback', GL.INVALID_OPERATION); return null; }
  deleteTransformFeedback() { this._rejectUnsupported('transform feedback', GL.INVALID_OPERATION); }
  isTransformFeedback() { this._rejectUnsupported('transform feedback', GL.INVALID_OPERATION); return false; }
  bindTransformFeedback() { this._rejectUnsupported('transform feedback', GL.INVALID_OPERATION); }
  beginTransformFeedback() { this._rejectUnsupported('transform feedback', GL.INVALID_OPERATION); }
  endTransformFeedback() { this._rejectUnsupported('transform feedback', GL.INVALID_OPERATION); }
  pauseTransformFeedback() { this._rejectUnsupported('transform feedback', GL.INVALID_OPERATION); }
  resumeTransformFeedback() { this._rejectUnsupported('transform feedback', GL.INVALID_OPERATION); }
  transformFeedbackVaryings() { this._rejectUnsupported('transform feedback', GL.INVALID_OPERATION); }
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
      viewport: [...this._viewport], scissor: [...this._scissor], depthRange: [...this._depthRange],
      enabled: new Set(this._enabled), blend: { ...this._blend }, blendColor: [...this._blendColor],
      depthFunc: this._depthFunc, depthMask: this._depthMask, cullFace: this._cullFace,
      stencilFront: { ...this._stencilFront }, stencilBack: { ...this._stencilBack },
      polygonOffset: [...this._polygonOffset],
      frontFace: this._frontFace, colorMask: [...this._colorMask],
      drawFramebuffer: this._drawFramebuffer, drawBuffer: this._bufferSelection(this._drawFramebuffer).draw,
      textureUnits: this._textureUnits.map(unit => ({ ...unit })),
    };
  }

  _fixedFunctionProgram(textureUnits, colorArray, alphaEnabled, alphaFunc, texEnv) {
    const key = stateKey([textureUnits, colorArray, alphaEnabled ? alphaFunc : 0x0207,
      textureUnits.map(unit => texEnv[unit]), this._clipControl]);
    if (this._fixedFunctionProgramCache.has(key)) return this._fixedFunctionProgramCache.get(key);
    const vertexLayout = uniformLayout([{ name: 'u_mvp', type: 'mat4' },
      ...textureUnits.map(unit => ({ name: `u_texMatrix${unit}`, type: 'mat4' }))]);
    const fragmentLayout = alphaEnabled ? uniformLayout([{ name: 'u_alphaRef', type: 'float' }]) : { fields: [], size: 0 };
    const vertexInputs = textureUnits.map((unit, index) => `, @location(${index + 2}) texCoord${unit}: vec4<f32>`).join('');
    const vertexOutputs = textureUnits.map((unit, index) => `, @location(${index + 1}) texCoord${unit}: vec2<f32>`).join('');
    const vertexMatrices = textureUnits.map(unit => `, u_texMatrix${unit}: mat4x4<f32>`).join('');
    const vertexTransforms = textureUnits.map(unit => `let uv${unit} = globals.u_texMatrix${unit} * vec4<f32>(input.texCoord${unit}.xy, 0.0, 1.0); output.texCoord${unit} = uv${unit}.xy / uv${unit}.w;`).join(' ');
    const clipY = this._clipControl[0] === 0x8ca2 ? '-clip.y' : 'clip.y';
    const clipZ = this._clipControl[1] === 0x935e ? '(clip.z + clip.w) * 0.5' : 'clip.z';
    const vertex = `struct VertexGlobals { u_mvp: mat4x4<f32>${vertexMatrices} };
@group(0) @binding(0) var<uniform> globals: VertexGlobals;
struct VertexInput { @location(0) position: vec4<f32>, @location(1) color: vec4<f32>${vertexInputs} };
struct VertexOutput { @builtin(position) position: vec4<f32>, @location(0) color: vec4<f32>${vertexOutputs} };
@vertex fn main(input: VertexInput) -> VertexOutput { var output: VertexOutput; let clip = globals.u_mvp * input.position; output.position = vec4<f32>(clip.x, ${clipY}, ${clipZ}, clip.w); output.color = input.color; ${vertexTransforms} return output; }`;
    const fragmentInput = `struct FragmentInput { @location(0) color: vec4<f32>${vertexOutputs} };`;
    const fragmentGlobals = alphaEnabled
      ? 'struct FragmentGlobals { u_alphaRef: f32 };\n@group(0) @binding(1) var<uniform> fragmentGlobals: FragmentGlobals;'
      : '';
    const sampler = textureUnits.map(unit => `@group(0) @binding(${2 + unit * 2}) var fixedTexture${unit}: texture_2d<f32>;\n@group(0) @binding(${3 + unit * 2}) var fixedSampler${unit}: sampler;`).join('\n');
    const textureCombines = [];
    for (const unit of textureUnits) {
      const texel = `texel${unit}`;
      const sample = `let ${texel} = textureSample(fixedTexture${unit}, fixedSampler${unit}, input.texCoord${unit});`;
      const mode = texEnv[unit];
      const combine = mode === 0x1e01 ? `color = ${texel};`
        : mode === 0x2101 ? `color = vec4<f32>(mix(color.rgb, ${texel}.rgb, ${texel}.a), color.a);`
          : mode === 0x0104 ? `color = min(color + ${texel}, vec4<f32>(1.0));`
            : `color = color * ${texel};`;
      textureCombines.push(`${sample} ${combine}`);
    }
    const alphaCompare = new Map([
      [0x0200, 'false'], [0x0201, 'color.a < fragmentGlobals.u_alphaRef'],
      [0x0202, 'color.a == fragmentGlobals.u_alphaRef'], [0x0203, 'color.a <= fragmentGlobals.u_alphaRef'],
      [0x0204, 'color.a > fragmentGlobals.u_alphaRef'], [0x0205, 'color.a != fragmentGlobals.u_alphaRef'],
      [0x0206, 'color.a >= fragmentGlobals.u_alphaRef'], [0x0207, 'true'],
    ]).get(alphaFunc) || 'true';
    const alphaCheck = alphaEnabled ? `if (!(${alphaCompare})) { discard; }` : '';
    const fragment = `${fragmentInput}\n${fragmentGlobals}\n${sampler}\n@fragment fn main(input: FragmentInput) -> @location(0) vec4<f32> { var color = input.color; ${textureCombines.join(' ')} ${alphaCheck} return color; }`;
    const textureAttributes = textureUnits.map((unit, index) => [unit === 0 ? 'a_texCoord' : `a_texCoord${unit}`, index + 2]);
    const program = {
      id: this._nextId++, linked: true, linkPromise: Promise.resolve(true), infoLog: '',
      attributes: new Map(), attributeLocations: new Map([['a_position', 0], ['a_color', 1], ...textureAttributes]),
      attributeInfo: [{ name: 'a_position', size: 1, type: 0x8b52 }, { name: 'a_color', size: 1, type: 0x8b52 }, ...textureAttributes.map(([name]) => ({ name, size: 1, type: 0x8b52 }))],
      uniformInfo: [], uniformLocations: new Map(), uniformValues: new Map(),
      fixedFunction: { texture: textureUnits.length > 0, textureUnits: [...textureUnits], colorArray, alphaEnabled,
        alphaFunc, alphaRef: this._fixedFunction.alphaRef, texEnv: [...texEnv] },
      stages: {
        vertex: { wgsl: vertex, layout: vertexLayout, activeUniforms: [], samplers: [] },
        fragment: { wgsl: fragment, layout: fragmentLayout, activeUniforms: [],
          samplers: textureUnits.map(unit => ({ name: `u_fixedTexture${unit}`, textureBinding: 2 + unit * 2,
            samplerBinding: 3 + unit * 2, arrayLength: 1 })) },
      },
    };
    this._fixedFunctionProgramCache.set(key, program);
    return program;
  }

  _fixedArrayFloatData(data, spec, color) {
    if (!data?.byteLength || !spec?.size) return null;
    const componentBytes = spec.type === 0x1400 || spec.type === 0x1401 ? 1
      : spec.type === 0x1402 || spec.type === 0x1403 ? 2
        : spec.type === 0x140a ? 8 : 4;
    const size = Math.max(1, Math.min(4, spec.size | 0));
    const elementBytes = componentBytes * size;
    const stride = spec.stride > 0 ? spec.stride : elementBytes;
    const count = Math.max(0, Math.floor((data.byteLength - elementBytes) / stride) + 1);
    if (!count) return null;
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength), result = new Float32Array(count * size);
    const read = offset => spec.type === 0x1400 ? view.getInt8(offset)
      : spec.type === 0x1401 ? view.getUint8(offset)
        : spec.type === 0x1402 ? view.getInt16(offset, true)
          : spec.type === 0x1403 ? view.getUint16(offset, true)
            : spec.type === 0x1404 ? view.getInt32(offset, true)
              : spec.type === 0x1405 ? view.getUint32(offset, true)
                : spec.type === 0x1406 ? view.getFloat32(offset, true)
                  : spec.type === 0x140a ? view.getFloat64(offset, true) : 0;
    for (let i = 0; i < count; i++) for (let c = 0; c < size; c++) {
      let value = read(i * stride + c * componentBytes);
      if (color) {
        if (spec.type === 0x1400) value = Math.max(-1, value / 127);
        else if (spec.type === 0x1401) value /= 255;
        else if (spec.type === 0x1402) value = Math.max(-1, value / 32767);
        else if (spec.type === 0x1403) value /= 65535;
        else if (spec.type === 0x1404) value = Math.max(-1, value / 2147483647);
        else if (spec.type === 0x1405) value /= 4294967295;
      }
      result[i * size + c] = value;
    }
    return result;
  }

  _fixedArrayAttribute(index, spec, data, color, temporaryBuffers, baseVertex = 0, current = null, pointerIndex = index) {
    const attribute = { index, enabled: !!spec?.enabled, buffer: null, size: spec?.size || (color ? 4 : 4),
      type: spec?.type || 0x1406, normalized: false, stride: spec?.stride || 0, offset: 0, integer: false, divisor: 0,
      current: new Float32Array(current || (color ? this._fixedFunction.color : [0, 0, 0, 1])) };
    if (!attribute.enabled) return attribute;
    const pointer = this._fixedFunctionPointers.get(pointerIndex);
    const values = this._fixedArrayFloatData(data, spec, color);
    if (values) {
      const buffer = this.createBuffer();
      buffer.data = new Uint8Array(values.buffer.slice(0)); buffer.dirty = true; buffer.version++;
      temporaryBuffers.push(buffer);
      Object.assign(attribute, { buffer, type: 0x1406, stride: 0, offset: 0 });
    } else if (pointer?.buffer) {
      const componentBytes = [0x1400, 0x1401].includes(pointer.type) ? 1
        : [0x1402, 0x1403, 0x140b].includes(pointer.type) ? 2 : pointer.type === 0x140a ? 8 : 4;
      const stride = pointer.stride || pointer.size * componentBytes;
      Object.assign(attribute, { buffer: pointer.buffer, type: pointer.type, size: pointer.size,
        stride: pointer.stride, offset: pointer.offset + Math.max(0, baseVertex | 0) * stride,
        normalized: color && [0x1400, 0x1401, 0x1402, 0x1403].includes(pointer.type) });
    }
    return attribute;
  }

  _fixedFunctionDraw(mode, first, count, indexed, arrays) {
    if (this._currentProgram || count <= 0) return false;
    const textureUnits = [0, 1].filter(unit => this._fixedTextureEnabled[unit]);
    const colorArray = !!arrays.color?.enabled;
    const alphaTestEnabled = this._enabled.has(0x0bc0);
    const fixed = this._fixedFunction;
    const program = this._fixedFunctionProgram(textureUnits, colorArray, alphaTestEnabled, fixed.alphaFunc, fixed.texEnv);
    program.fixedFunction.alphaRef = fixed.alphaRef;
    const stackTop = stack => stack[stack.length - 1];
    const mvp = multiplyMatrix4(stackTop(fixed.projection), stackTop(fixed.modelview));
    program.uniformValues.set('u_mvp', mvp);
    for (const unit of textureUnits) {
      program.uniformValues.set(`u_texMatrix${unit}`, stackTop(fixed.texture[unit]));
      program.uniformValues.set(`u_fixedTexture${unit}`, unit);
    }
    if (textureUnits.includes(0)) program.uniformValues.set('u_fixedTexture', 0);
    if (fixed.alphaFunc !== 0x0207) program.uniformValues.set('u_alphaRef', fixed.alphaRef);

    const temporaryBuffers = [];
    const attributes = new Map();
    const baseVertex = arrays.baseVertex | 0;
    const position = this._fixedArrayAttribute(0, arrays.vertex, arrays.vertexData, false, temporaryBuffers, baseVertex);
    const color = this._fixedArrayAttribute(1, arrays.color, arrays.colorData, true, temporaryBuffers, baseVertex);
    const texCoordSpecs = [
      [arrays.texCoord || arrays.texCoord0, arrays.texCoordData || arrays.texCoordData0],
      [arrays.texCoord1, arrays.texCoordData1],
    ];
    const texCoords = textureUnits.map((unit, index) => this._fixedArrayAttribute(2 + index,
      texCoordSpecs[unit][0], texCoordSpecs[unit][1], false, temporaryBuffers, baseVertex,
      fixed.currentTexCoords[unit], 2 + unit));
    if (!position.enabled || (!position.buffer && !this._fixedFunctionPointers.get(0)?.buffer)) return false;
    attributes.set(0, position); attributes.set(1, color);
    texCoords.forEach((attribute, index) => attributes.set(2 + index, attribute));

    let elementBuffer = this._elementArrayBuffer;
    let elementOffset = 0;
    let elementType = indexed?.type;
    if (indexed?.bytes?.byteLength) {
      elementBuffer = this.createBuffer(); elementBuffer.data = bytesOf(indexed.bytes); elementBuffer.dirty = true; elementBuffer.version++;
      temporaryBuffers.push(elementBuffer);
    } else if (indexed) elementOffset = indexed.offset || 0;

    const previousVao = this._vao, previousProgram = this._currentProgram;
    this._vao = { id: -1, elementArrayBuffer: elementBuffer, attributes };
    this._elementArrayBuffer = elementBuffer;
    this._currentProgram = program;
    const state = this._drawSnapshot();
    state.fixedTemporaryBuffers = temporaryBuffers;
    this._vao = previousVao; this._elementArrayBuffer = previousVao.elementArrayBuffer; this._currentProgram = previousProgram;
    const element = indexed ? { type: elementType, offset: elementOffset } : null;
    this._mirrorDraw(state, mode, first, count, element);
    this._enqueue(() => this._draw(state, mode, first, count, element, 1));
    return true;
  }

  fixedFunctionImmediateDraw(mode, commands) {
    const vertices = [], colors = [], texCoords = [[], []];
    const current = this._fixedFunction.currentTexCoords.map(value => [...value]);
    for (let i = 0; i + 4 < commands.length; i += 5) {
      const values = Array.from(commands.subarray(i + 1, i + 5));
      if (commands[i] === 0) this.color4f(...values);
      else if (commands[i] === 2) {
        const rawUnit = values[0] >= 0x84c0 ? values[0] - 0x84c0 : values[0];
        const unit = Math.max(0, Math.min(1, rawUnit | 0));
        current[unit] = [values[1], values[2], 0, 1];
        this._fixedFunction.currentTexCoords[unit] = [...current[unit]];
      } else if (commands[i] === 1) {
        vertices.push(...values); colors.push(...this._fixedFunction.color);
        for (let unit = 0; unit < 2; unit++) texCoords[unit].push(current[unit][0], current[unit][1]);
      }
    }
    const count = vertices.length / 4;
    if (!count) return true;
    const arrays = {
      vertex: { enabled: true, size: 4, type: 0x1406, stride: 0 },
      color: { enabled: true, size: 4, type: 0x1406, stride: 0 },
      texCoord: { enabled: this._fixedTextureEnabled[0], size: 2, type: 0x1406, stride: 0 },
      texCoord1: { enabled: this._fixedTextureEnabled[1], size: 2, type: 0x1406, stride: 0 },
      vertexData: new Float32Array(vertices), colorData: new Float32Array(colors),
      texCoordData: new Float32Array(texCoords[0]), texCoordData1: new Float32Array(texCoords[1]),
    };
    if (mode === 0x0007) {
      const indices = [];
      for (let i = 0; i + 3 < count; i += 4) indices.push(i, i + 1, i + 2, i, i + 2, i + 3);
      return this.fixedFunctionDrawElements(4, indices.length, 0x1405, 0, new Uint32Array(indices), arrays);
    }
    return this.fixedFunctionDrawArrays(mode === 0x0009 ? 6 : mode, 0, count, arrays);
  }

  fixedFunctionDrawArrays(mode, first, count, arrays) {
    return this._fixedFunctionDraw(mode, first, count, null, arrays);
  }
  fixedFunctionDrawElements(mode, count, type, offset, indexBytes, arrays) {
    return this._fixedFunctionDraw(mode, 0, count, { type, offset, bytes: indexBytes }, arrays);
  }

  drawArrays(mode, first, count) {
    if (this._bufferSelection(this._drawFramebuffer).draw === GL.NONE) return;
    if (![0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007].includes(mode)) { this._recordError(GL.INVALID_ENUM); return; }
    if (first < 0 || count < 0) { this._recordError(GL.INVALID_VALUE); return; }
    if (count > 0) this._frameDirty = true;
    if (!this._currentProgram && this._fixedFunctionDraw(mode, first, count, null, this._fixedFunctionLegacyArrays || {})) return;
    const state = this._drawSnapshot(); this._mirrorDraw(state, mode, first, count, null); this._enqueue(() => this._draw(state, mode, first, count, null, 1));
  }
  drawElements(mode, count, type, offset) {
    if (this._bufferSelection(this._drawFramebuffer).draw === GL.NONE) return;
    if (![0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007].includes(mode)) { this._recordError(GL.INVALID_ENUM); return; }
    if (count < 0 || offset < 0) { this._recordError(GL.INVALID_VALUE); return; }
    if (![0x1401, 0x1403, 0x1405].includes(type)) { this._recordError(GL.INVALID_ENUM); return; }
    if (count > 0) this._frameDirty = true;
    if (!this._currentProgram && this._fixedFunctionDraw(mode, 0, count, { type, offset }, this._fixedFunctionLegacyArrays || {})) return;
    const element = { type, offset }, state = this._drawSnapshot(); this._mirrorDraw(state, mode, 0, count, element); this._enqueue(() => this._draw(state, mode, 0, count, element, 1));
  }
  drawArraysInstanced(mode, first, count, instances) { const state = this._drawSnapshot(); if (state.drawBuffer === GL.NONE) return; if (count > 0 && instances > 0) this._frameDirty = true; this._mirrorDraw(state, mode, first, count, null); this._enqueue(() => this._draw(state, mode, first, count, null, instances)); }
  drawElementsInstanced(mode, count, type, offset, instances) { const element = { type, offset }, state = this._drawSnapshot(); if (state.drawBuffer === GL.NONE) return; if (count > 0 && instances > 0) this._frameDirty = true; this._mirrorDraw(state, mode, 0, count, element); this._enqueue(() => this._draw(state, mode, 0, count, element, instances)); }

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
    if (!this._isDefaultFramebuffer(state.drawFramebuffer) || !state.program) return;
    const program = state.program;
    const fixed = program.fixedFunction;
    const position = program.attributeInfo.find(item => /position/i.test(item.name));
    const color = program.attributeInfo.find(item => /color/i.test(item.name));
    if (!position || !color || (!fixed && program.uniformInfo.some(item => item.type === 0x8b5e))) return;
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
    const textureCoordinates = (fixed?.textureUnits || []).map(unit => {
      const name = unit === 0 ? 'a_texCoord' : `a_texCoord${unit}`;
      return {
        unit,
        attribute: state.attributes.get(program.attributeLocations.get(name)),
        matrix: program.uniformValues.get(`u_texMatrix${unit}`) || identityMatrix(),
      };
    });
    const fixedMvp = fixed ? program.uniformValues.get('u_mvp') || identityMatrix() : null;
    const model = program.uniformValues.get('u_modelView') || identityMatrix();
    const projection = program.uniformValues.get('u_projection') || identityMatrix();
    const viewport = state.viewport;
    const transform = index => {
      const p = this._mirrorAttribute(posAttr, index);
      const viewPosition = fixed ? null : mulMat4(model, p);
      const clip = fixed ? mulMat4(fixedMvp, p) : mulMat4(projection, viewPosition);
      if (clip.some(value => !Number.isFinite(value))) return null;
      const vertex = { clip, color: this._mirrorAttribute(colorAttr, index) };
      if (fixed?.texture) {
        vertex.texCoords = new Map();
        for (const { unit, attribute, matrix } of textureCoordinates) {
          const coordinates = this._mirrorAttribute(attribute, index);
          const transformed = mulMat4(matrix, [coordinates[0], coordinates[1], 0, 1]);
          vertex.texCoords.set(unit, [transformed[0] / (transformed[3] || 1), transformed[1] / (transformed[3] || 1)]);
        }
      }
      return vertex;
    };
    const clipPlanes = [
      clip => clip[0] + clip[3], clip => clip[3] - clip[0],
      clip => clip[1] + clip[3], clip => clip[3] - clip[1],
      clip => clip[2] + clip[3], clip => clip[3] - clip[2],
    ];
    const interpolate = (a, b, t) => {
      const vertex = {
        clip: a.clip.map((value, i) => value + (b.clip[i] - value) * t),
        color: a.color.map((value, i) => value + (b.color[i] - value) * t),
      };
      if (a.texCoords || b.texCoords) {
        vertex.texCoords = new Map();
        for (const unit of new Set([...(a.texCoords?.keys() || []), ...(b.texCoords?.keys() || [])])) {
          const aUV = a.texCoords?.get(unit) || [0, 0], bUV = b.texCoords?.get(unit) || [0, 0];
          vertex.texCoords.set(unit, aUV.map((value, i) => value + (bUV[i] - value) * t));
        }
      }
      return vertex;
    };
    const clipTriangle = vertices => {
      let polygon = vertices;
      for (const distanceToPlane of clipPlanes) {
        const clipped = [];
        let previous = polygon[polygon.length - 1];
        let previousDistance = distanceToPlane(previous.clip);
        for (const current of polygon) {
          const currentDistance = distanceToPlane(current.clip);
          const previousInside = previousDistance >= 0;
          const currentInside = currentDistance >= 0;
          if (previousInside !== currentInside) {
            const t = previousDistance / (previousDistance - currentDistance);
            clipped.push(interpolate(previous, current, t));
          }
          if (currentInside) clipped.push(current);
          previous = current;
          previousDistance = currentDistance;
        }
        polygon = clipped;
        if (polygon.length < 3) return [];
      }
      return polygon.map(vertex => {
        const w = vertex.clip[3];
        if (!(w > 1e-8)) return null;
        const invW = 1 / w, nx = vertex.clip[0] * invW, ny = vertex.clip[1] * invW;
        return {
          x: viewport[0] + (nx + 1) * viewport[2] * 0.5,
          y: viewport[1] + (ny + 1) * viewport[3] * 0.5,
          z: clamp01((vertex.clip[2] * invW + 1) * 0.5), invW,
          color: vertex.color, texCoords: vertex.texCoords,
        };
      });
    };
    for (let i = 0; i + 2 < vertices.length; i += 3) {
      const transformed = [transform(vertices[i]), transform(vertices[i + 1]), transform(vertices[i + 2])];
      if (transformed.some(vertex => !vertex)) continue;
      const polygon = clipTriangle(transformed);
      if (polygon.length < 3 || polygon.some(vertex => !vertex)) continue;
      for (let j = 1; j + 1 < polygon.length; j++)
        this._rasterizeMirrorTriangle([polygon[0], polygon[j], polygon[j + 1]], state);
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
    const fixed = state.program?.fixedFunction;
    const textureUnits = fixed?.textureUnits || [];
    const textures = new Map(textureUnits.map(unit => [unit,
      this._textureUnits[unit]?.[GL.TEXTURE_2D] || this._whiteTexture()]));
    const sampleTexture = (texture, u, v) => {
      if (!texture?.pixels || !texture.width || !texture.height) return [1, 1, 1, 1];
      const wrap = (value, mode) => {
        if (mode === 0x2901) return value - Math.floor(value);
        if (mode === 0x8370) { const mirrored = ((value % 2) + 2) % 2; return mirrored <= 1 ? mirrored : 2 - mirrored; }
        return clamp01(value);
      };
      u = wrap(u, texture.params.get(0x2802) ?? 0x812f);
      v = wrap(v, texture.params.get(0x2803) ?? 0x812f);
      const tx = Math.min(texture.width - 1, Math.floor(u * texture.width));
      const ty = Math.min(texture.height - 1, Math.floor(v * texture.height));
      const offset = (ty * texture.width + tx) * 4;
      const rgba = Array.from(texture.pixels.subarray(offset, offset + 4), value => value / 255);
      if (texture.format?.endsWith('-srgb')) for (let channel = 0; channel < 3; channel++)
        rgba[channel] = rgba[channel] <= 0.04045 ? rgba[channel] / 12.92 : ((rgba[channel] + 0.055) / 1.055) ** 2.4;
      return rgba;
    };
    const compareAlpha = (func, alpha, reference) => {
      switch (func) {
        case 0x0200: return false; case 0x0201: return alpha < reference;
        case 0x0202: return alpha === reference; case 0x0203: return alpha <= reference;
        case 0x0204: return alpha > reference; case 0x0205: return alpha !== reference;
        case 0x0206: return alpha >= reference; default: return true;
      }
    };
    const factor = (mode, source, destination, channel) => {
      switch (mode) {
        case 0x0000: return 0; case 0x0001: return 1;
        case 0x0300: return source[channel]; case 0x0301: return 1 - source[channel];
        case 0x0302: return source[3]; case 0x0303: return 1 - source[3];
        case 0x0304: return destination[3]; case 0x0305: return 1 - destination[3];
        case 0x0306: return destination[channel]; case 0x0307: return 1 - destination[channel];
        case 0x8001: return state.blendColor[channel]; case 0x8004: return 1 - state.blendColor[channel];
        default: return 1;
      }
    };
    const blend = (source, destination, sourceMode, destinationMode, operation, channel) => {
      if (operation === 0x8007) return Math.min(source[channel], destination[channel]);
      if (operation === 0x8008) return Math.max(source[channel], destination[channel]);
      const s = source[channel] * factor(sourceMode, source, destination, channel);
      const d = destination[channel] * factor(destinationMode, source, destination, channel);
      if (operation === 0x800a) return s - d;
      if (operation === 0x800b) return d - s;
      return s + d;
    };
    for (let y = bottom; y <= top; y++) for (let x = left; x <= right; x++) {
      const px = x + 0.5, py = y + 0.5;
      const a = edge(vertices[1], vertices[2], px, py) / area;
      const b = edge(vertices[2], vertices[0], px, py) / area;
      const c = 1 - a - b;
      if (a < 0 || b < 0 || c < 0) continue;
      const reciprocalW = a * vertices[0].invW + b * vertices[1].invW + c * vertices[2].invW;
      if (!Number.isFinite(reciprocalW) || Math.abs(reciprocalW) < 1e-12) continue;
      const wa = a * vertices[0].invW / reciprocalW;
      const wb = b * vertices[1].invW / reciprocalW;
      const wc = c * vertices[2].invW / reciprocalW;
      const offset = (y * this._width + x) * 4;
      const source = [0, 1, 2, 3].map(channel =>
        wa * vertices[0].color[channel] + wb * vertices[1].color[channel] + wc * vertices[2].color[channel]);
      if (fixed?.texture) {
        for (const unit of textureUnits) {
          const coordinates = vertices.map(vertex => vertex.texCoords?.get(unit) || [0, 0]);
          const u = wa * coordinates[0][0] + wb * coordinates[1][0] + wc * coordinates[2][0];
          const v = wa * coordinates[0][1] + wb * coordinates[1][1] + wc * coordinates[2][1];
          const texel = sampleTexture(textures.get(unit), u, v);
          const texEnv = fixed.texEnv[unit];
          if (texEnv === 0x1e01) source.splice(0, 4, ...texel);
          else if (texEnv === 0x2101) for (let channel = 0; channel < 3; channel++) source[channel] = source[channel] * (1 - texel[3]) + texel[channel] * texel[3];
          else if (texEnv === 0x0104) for (let channel = 0; channel < 4; channel++) source[channel] = Math.min(1, source[channel] + texel[channel]);
          else for (let channel = 0; channel < 4; channel++) source[channel] *= texel[channel];
        }
      }
      if (fixed?.alphaEnabled && !compareAlpha(fixed.alphaFunc, source[3], fixed.alphaRef)) continue;
      const destination = Array.from(this._pixels.subarray(offset, offset + 4), value => value / 255);
      for (let channel = 0; channel < 4; channel++) if (state.colorMask[channel]) {
        const value = state.enabled.has(0x0be2)
          ? blend(source, destination, channel === 3 ? state.blend.srcAlpha : state.blend.srcRGB,
            channel === 3 ? state.blend.dstAlpha : state.blend.dstRGB,
            channel === 3 ? state.blend.equationAlpha : state.blend.equationRGB, channel)
          : source[channel];
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
    const source = indices || Uint32Array.from({ length: count }, (_, i) => first + i);
    if (mode === 0x0006) {
      const out = [];
      for (let i = 1; i + 1 < source.length; i++) out.push(source[0], source[i], source[i + 1]);
      indices = Uint32Array.from(out);
    }
    if (mode === 0x0002) {
      const out = [];
      for (let i = 0; i < source.length; i++) out.push(source[i], source[(i + 1) % source.length]);
      return { topology: 'line-list', indices: Uint32Array.from(out) };
    }
    if ([0x0004, 0x0005, 0x0006].includes(mode) && this._polygonMode === 0x1b01) {
      const triangles = [];
      if (mode === 0x0004) for (let i = 0; i + 2 < source.length; i += 3) triangles.push(source[i], source[i + 1], source[i + 2]);
      else if (mode === 0x0005) for (let i = 0; i + 2 < source.length; i++) triangles.push(...(i & 1 ? [source[i + 1], source[i], source[i + 2]] : [source[i], source[i + 1], source[i + 2]]));
      else for (let i = 1; i + 1 < source.length; i++) triangles.push(source[0], source[i], source[i + 1]);
      const edges = [];
      for (let i = 0; i + 2 < triangles.length; i += 3) edges.push(triangles[i], triangles[i + 1], triangles[i + 1], triangles[i + 2], triangles[i + 2], triangles[i]);
      return { topology: 'line-list', indices: Uint32Array.from(edges) };
    }
    if ([0x0004, 0x0005, 0x0006].includes(mode) && this._polygonMode === 0x1b00) {
      const points = [];
      if (mode === 0x0004) points.push(...source);
      else if (mode === 0x0005) for (let i = 0; i + 2 < source.length; i++) points.push(source[i], source[i + 1], source[i + 2]);
      else for (let i = 1; i + 1 < source.length; i++) points.push(source[0], source[i], source[i + 1]);
      return { topology: 'point-list', indices: Uint32Array.from(points) };
    }
    if (mode === 0x0006) return { topology: 'triangle-list', indices };
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

  async _pipelineFor(program, state, vertex, topology, targetFormat = this._format, depthFormat = 'depth24plus') {
    const blendEnabled = state.enabled.has(0x0be2), depthEnabled = state.enabled.has(0x0b71);
    const cullEnabled = state.enabled.has(0x0b44);
    const cull = cullEnabled ? (state.cullFace === 0x0404 ? 'front' : state.cullFace === 0x0408 ? 'front-and-back' : 'back') : 'none';
    const blend = blendEnabled ? this._blendDescriptor(state) : undefined;
    const writeMask = state.colorMask.reduce((mask, enabled, index) => enabled ? mask | (1 << index) : mask, 0);
    const depthCompare = depthEnabled ? GL_COMPARE.get(state.depthFunc) : 'always';
    if (!depthCompare) throw new Error('unsupported GL depth comparison');
    const key = stateKey({
      program: program.id, vertex: vertex.layouts, topology, blend, depth: [depthEnabled, depthCompare, state.depthMask],
      stencil: [state.enabled.has(0x0b90), state.stencilFront, state.stencilBack],
      polygonOffset: state.enabled.has(0x8037) ? state.polygonOffset : null,
      cull, frontFace: state.frontFace, colorMask: writeMask, targetFormat, depthFormat,
    });
    if (this._pipelineCache.has(key)) return this._pipelineCache.get(key);
    let scopeOpen = false;
    try {
      this._device.pushErrorScope('validation');
      scopeOpen = true;
      const vertexModule = this._device.createShaderModule({ code: program.stages.vertex.wgsl });
      const fragmentModule = this._device.createShaderModule({ code: program.stages.fragment.wgsl });
      const stencilState = depthFormat?.includes('stencil') ? {
        stencilFront: { compare: GL_COMPARE.get(state.stencilFront.func), failOp: GL_STENCIL_OP.get(state.stencilFront.fail), depthFailOp: GL_STENCIL_OP.get(state.stencilFront.depthFail), passOp: GL_STENCIL_OP.get(state.stencilFront.pass) },
        stencilBack: { compare: GL_COMPARE.get(state.stencilBack.func), failOp: GL_STENCIL_OP.get(state.stencilBack.fail), depthFailOp: GL_STENCIL_OP.get(state.stencilBack.depthFail), passOp: GL_STENCIL_OP.get(state.stencilBack.pass) },
        stencilReadMask: state.stencilFront.readMask & state.stencilBack.readMask,
        stencilWriteMask: state.stencilFront.writeMask & state.stencilBack.writeMask,
      } : {};
      const descriptor = {
        layout: 'auto',
        vertex: { module: vertexModule, entryPoint: 'main', buffers: vertex.layouts },
        fragment: { module: fragmentModule, entryPoint: 'main', targets: [{ format: targetFormat, writeMask, ...(blend ? { blend } : {}) }] },
        primitive: { topology, frontFace: state.frontFace === 0x0900 ? 'cw' : 'ccw', cullMode: cull === 'front-and-back' ? 'front' : cull,
          ...(state.enabled.has(0x8037) ? { depthBias: Math.trunc(state.polygonOffset[1]), depthBiasSlopeScale: state.polygonOffset[0], depthBiasClamp: 0 } : {}) },
        ...(depthFormat ? { depthStencil: { format: depthFormat, depthWriteEnabled: depthEnabled && state.depthMask, depthCompare, ...stencilState } } : {}),
      };
      const pipeline = this._device.createRenderPipeline(descriptor);
      const validationError = await this._device.popErrorScope();
      scopeOpen = false;
      if (validationError) throw new Error(validationError.message || 'WebGPU rejected the render pipeline');
      this._pipelineCache.set(key, pipeline);
      return pipeline;
    } catch (error) {
      if (scopeOpen) try { await this._device.popErrorScope(); } catch {}
      this._pipelineCache.set(key, null);
      this._reportFailure(error);
      return null;
    }
  }

  async _uniformBindGroup(program, pipeline, uniformValues, textureUnits = this._textureUnits) {
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
    for (const stage of ['vertex', 'fragment']) {
      const record = program.stages[stage];
      for (const sampler of record?.samplers || []) {
        const unitValue = uniformValues.get(sampler.name);
        const unit = Number(ArrayBuffer.isView(unitValue) ? unitValue[0] : unitValue ?? 0) | 0;
        const texture = textureUnits[unit]?.[GL.TEXTURE_2D] || this._whiteTexture();
        const gpu = this._ensureTextureGpu(texture);
        if (!gpu) continue;
        entries.push({ binding: sampler.textureBinding, resource: gpu.createView() });
        entries.push({ binding: sampler.samplerBinding, resource: this._samplerFor(texture) });
      }
    }
    if (!entries.length) return null;
    return this._device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries });
  }

  _whiteTexture() {
    if (this._defaultWhiteTexture) return this._defaultWhiteTexture;
    const texture = this.createTexture();
    texture.width = texture.height = 1; texture.pixels = new Uint8Array([255, 255, 255, 255]);
    texture.levels.set(0, { width: 1, height: 1, pixels: texture.pixels });
    this._defaultWhiteTexture = texture;
    return texture;
  }

  async _draw(state, mode, first, count, indexed, instances) {
    const program = state.program;
    if (!program || !program.linkPromise) return;
    if (count <= 0 || instances <= 0 || !(await program.linkPromise)) return;
    if (state.cullFace === 0x0408 && state.enabled.has(0x0b44)) return;
    const topologyData = this._rewriteTopology(mode, first, count, indexed ? this._indexValues(state.elementArrayBuffer, indexed.type, indexed.offset, count) : null);
    if (!topologyData.topology || (indexed && !topologyData.indices)) return;
    const topology = topologyData.topology;
    const target = this._targetFor(state.drawFramebuffer);
    if (!target?.colorTexture) return;
    const vertex = this._vertexLayouts(program, state.attributes);
    const viewport = state.viewport;
    const pipeline = await this._pipelineFor(program, state, vertex, topology, target.format, target.depthTexture ? target.depthFormat || 'depth24plus' : null);
    if (!pipeline) return; // A failed shader/pipeline is a skipped draw.
    const encoder = this._device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
      colorAttachments: [{ view: target.colorTexture.createView({ format: target.format, baseMipLevel: target.colorMipLevel || 0, mipLevelCount: 1 }), loadOp: 'load', storeOp: 'store' }],
      ...(target.depthTexture ? { depthStencilAttachment: { view: target.depthTexture.createView({ baseMipLevel: target.depthMipLevel || 0, mipLevelCount: 1 }), depthLoadOp: 'load', depthStoreOp: 'store', ...(target.depthFormat?.includes('stencil') ? { stencilLoadOp: 'load', stencilStoreOp: 'store' } : {}) } } : {}),
    });
    pass.setPipeline(pipeline);
    if (state.enabled.has(0x0b90)) pass.setStencilReference(state.stencilFront.ref);
    for (const item of vertex.buffers) pass.setVertexBuffer(item.slot, item.buffer, item.offset);
    const bindGroup = await this._uniformBindGroup(program, pipeline, state.uniformValues, state.textureUnits);
    if (bindGroup) pass.setBindGroup(0, bindGroup);
    const x = Math.max(0, viewport[0]), y = Math.max(0, target.height - viewport[1] - viewport[3]);
    pass.setViewport(x, y, Math.max(0, viewport[2]), Math.max(0, viewport[3]), ...state.depthRange);
    if (state.enabled.has(0x0c11)) {
      const [sx, sy, sw, sh] = state.scissor;
      const left = Math.max(0, sx), top = Math.max(0, target.height - sy - sh);
      pass.setScissorRect(left, top, Math.max(0, Math.min(target.width - left, sw)), Math.max(0, Math.min(target.height - top, sh)));
    }
    if (state.enabled.has(0x0be2)) pass.setBlendConstant({ r: state.blendColor[0], g: state.blendColor[1], b: state.blendColor[2], a: state.blendColor[3] });
    let temporaryIndex = null;
    if (topologyData.indices) {
      const max = topologyData.indices.reduce((value, index) => Math.max(value, index), 0);
      const values = max <= 0xffff ? Uint16Array.from(topologyData.indices) : Uint32Array.from(topologyData.indices);
      const indexSize = Math.max(4, Math.ceil(values.byteLength / 4) * 4);
      const upload = values.byteLength % 4 ? new Uint8Array(indexSize) : values;
      if (upload !== values) upload.set(new Uint8Array(values.buffer, values.byteOffset, values.byteLength));
      temporaryIndex = this._device.createBuffer({ size: indexSize, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST });
      this._device.queue.writeBuffer(temporaryIndex, 0, upload);
      pass.setIndexBuffer(temporaryIndex, values instanceof Uint16Array ? 'uint16' : 'uint32');
      pass.drawIndexed(values.length, instances, 0, 0, 0);
    } else pass.draw(count, instances, first, 0);
    pass.end();
    this._device.queue.submit([encoder.finish()]);
    if (!target.default) this._gpuWrittenFramebuffers.add(target.framebuffer);
    if (temporaryIndex) this._device.queue.onSubmittedWorkDone().then(() => temporaryIndex.destroy()).catch(() => {});
    if (state.fixedTemporaryBuffers?.length) this._device.queue.onSubmittedWorkDone().then(() => {
      for (const buffer of state.fixedTemporaryBuffers) {
        buffer.gpu?.destroy();
        this._buffers.delete(buffer);
      }
    }).catch(() => {});
  }

  blendFunc(src, dst) { this.blendFuncSeparate(src, dst, src, dst); }
  blendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha) {
    try { for (const factor of [srcRGB, dstRGB, srcAlpha, dstAlpha]) blendFactor(factor); }
    catch { this._recordError(GL.INVALID_ENUM); return; }
    Object.assign(this._blend, { srcRGB, dstRGB, srcAlpha, dstAlpha });
  }
  blendEquation(mode) { this.blendEquationSeparate(mode, mode); }
  blendEquationSeparate(rgb, alpha) {
    if (!GL_BLEND_OP.has(rgb) || !GL_BLEND_OP.has(alpha)) { this._recordError(GL.INVALID_ENUM); return; }
    this._blend.equationRGB = rgb; this._blend.equationAlpha = alpha;
  }
  blendColor(r, g, b, a) { this._blendColor = [r, g, b, a].map(clamp01); }
  colorMask(r, g, b, a) { this._colorMask = [!!r, !!g, !!b, !!a]; }
  depthMask(enabled) { this._depthMask = !!enabled; }
  depthFunc(func) { if (!GL_COMPARE.has(func)) { this._recordError(GL.INVALID_ENUM); return; } this._depthFunc = func | 0; }
  depthRange(near, far) { this._depthRange = [clamp01(near), clamp01(far)]; }
  clipControl(origin, depth) {
    if (![0x8ca1, 0x8ca2].includes(origin) || ![0x935e, 0x935f].includes(depth)) { this._recordError(GL.INVALID_ENUM); return; }
    if (origin === this._clipControl[0] && depth === this._clipControl[1]) return;
    this._clipControl = [origin, depth];
    this._pipelineCache.clear();
    this._fixedFunctionProgramCache.clear();
    for (const program of this._programs) if (program.shaders?.length) this.linkProgram(program);
  }
  getClipControl() { return [...this._clipControl]; }
  cullFace(mode) { if (![0x0404, 0x0405, 0x0408].includes(mode)) { this._recordError(GL.INVALID_ENUM); return; } this._cullFace = mode | 0; }
  frontFace(mode) { if (![0x0900, 0x0901].includes(mode)) { this._recordError(GL.INVALID_ENUM); return; } this._frontFace = mode | 0; }
  scissor(x, y, width, height) {
    if (width < 0 || height < 0) { this._recordError(GL.INVALID_VALUE); return; }
    this._scissor = [x | 0, y | 0, width | 0, height | 0];
  }
  stencilFunc(func, ref, mask) { this.stencilFuncSeparate(0x0408, func, ref, mask); }
  stencilFuncSeparate(face, func, ref, mask) {
    if (![0x0404, 0x0405, 0x0408].includes(face)) { this._recordError(GL.INVALID_ENUM); return; }
    if (!GL_COMPARE.has(func)) { this._recordError(GL.INVALID_ENUM); return; }
    if (!Number.isFinite(ref) || !Number.isFinite(mask)) { this._recordError(GL.INVALID_VALUE); return; }
    if (face !== 0x0408) {
      const other = face === 0x0404 ? this._stencilBack : this._stencilFront;
      if ((mask >>> 0) !== other.readMask) { this._rejectUnsupported('separate front/back stencil read masks', GL.INVALID_OPERATION); return; }
    }
    for (const state of this._stencilFaces(face)) Object.assign(state, { func, ref: ref | 0, readMask: mask >>> 0 });
  }
  stencilOp(fail, depthFail, pass) { this.stencilOpSeparate(0x0408, fail, depthFail, pass); }
  stencilOpSeparate(face, fail, depthFail, pass) {
    if (![0x0404, 0x0405, 0x0408].includes(face)) { this._recordError(GL.INVALID_ENUM); return; }
    if (![fail, depthFail, pass].every(op => GL_STENCIL_OP.has(op))) { this._recordError(GL.INVALID_ENUM); return; }
    for (const state of this._stencilFaces(face)) Object.assign(state, { fail, depthFail, pass });
  }
  stencilMask(mask) { this.stencilMaskSeparate(0x0408, mask); }
  stencilMaskSeparate(face, mask) {
    if (![0x0404, 0x0405, 0x0408].includes(face)) { this._recordError(GL.INVALID_ENUM); return; }
    if (!Number.isFinite(mask)) { this._recordError(GL.INVALID_VALUE); return; }
    if (face !== 0x0408) {
      const other = face === 0x0404 ? this._stencilBack : this._stencilFront;
      if ((mask >>> 0 & 0xff) !== (other.writeMask & 0xff)) { this._rejectUnsupported('separate front/back stencil write masks', GL.INVALID_OPERATION); return; }
    }
    for (const state of this._stencilFaces(face)) state.writeMask = mask >>> 0;
  }
  _stencilFaces(face) { return face === 0x0404 ? [this._stencilFront] : face === 0x0405 ? [this._stencilBack] : [this._stencilFront, this._stencilBack]; }
  polygonOffset(factor, units) {
    if (!Number.isFinite(factor) || !Number.isFinite(units)) { this._recordError(GL.INVALID_VALUE); return; }
    this._polygonOffset = [factor, units];
  }
  polygonMode(face, mode) {
    if (![0x0404, 0x0405, 0x0408].includes(face) || ![0x1b00, 0x1b01, 0x1b02].includes(mode)) { this._recordError(GL.INVALID_ENUM); return; }
    if (face !== 0x0408) { this._rejectUnsupported('polygon mode for a single face', GL.INVALID_OPERATION); return; }
    this._polygonMode = mode;
  }
  lineWidth(width) {
    if (!Number.isFinite(width) || width <= 0) { this._recordError(GL.INVALID_VALUE); return; }
    if (width !== 1) { this._rejectUnsupported('line width other than 1', GL.INVALID_OPERATION); return; }
    this._lineWidth = width;
  }
  pointSize(size) {
    if (!Number.isFinite(size) || size <= 0) { this._recordError(GL.INVALID_VALUE); return; }
    if (size !== 1) { this._rejectUnsupported('point size other than 1', GL.INVALID_OPERATION); return; }
    this._pointSize = size;
  }
  sampleCoverage() {}

  _newTextureRecord(handle = null) {
    const texture = { id: this._nextId++, target: GL.TEXTURE_2D, width: 0, height: 0, format: 'rgba8unorm', levels: new Map(), params: new Map([[0x2800, 0x2601], [0x2801, 0x2703], [0x2802, 0x812f], [0x2803, 0x812f]]), gpu: null, gpuMipCount: 0 };
    texture.handle = handle || texture;
    if (handle && (typeof handle === 'object' || typeof handle === 'function')) this._textureRecords.set(handle, texture);
    if (texture.handle === texture) this._textureRecords.set(texture, texture);
    this._textures.add(texture); return texture;
  }
  _textureRecord(texture) {
    if (!texture) return null;
    if (this._textures.has(texture)) return texture;
    if ((typeof texture === 'object' || typeof texture === 'function') && this._textureRecords.has(texture)) return this._textureRecords.get(texture);
    return this._newTextureRecord(texture);
  }
  createTexture() { return this._newTextureRecord(); }
  deleteTexture(texture) {
    texture = this._textureRecord(texture);
    if (!texture) return;
    texture.gpu?.destroy(); texture.gpu = null; this._textures.delete(texture);
    for (const unit of this._textureUnits) if (unit[GL.TEXTURE_2D] === texture) unit[GL.TEXTURE_2D] = null;
    for (const framebuffer of this._framebufferObjects) for (const [attachment, record] of framebuffer.attachments)
      if (record.type === 'texture' && record.object === texture) framebuffer.attachments.delete(attachment);
  }
  isTexture(texture) { return !!texture && (this._textures.has(texture) || ((typeof texture === 'object' || typeof texture === 'function') && this._textureRecords.has(texture))); }
  _boundTexture(target = GL.TEXTURE_2D) { return this._textureUnits[this._activeTextureUnit]?.[target] || null; }
  _validateTextureTarget(target) {
    if (target === GL.TEXTURE_2D) return true;
    if ([0x8513, 0x8515, 0x8516, 0x8517, 0x8518, 0x8519, 0x851a, 0x806f, 0x8c1a].includes(target)) {
      this._rejectUnsupported(`texture target 0x${Number(target).toString(16)}`, GL.INVALID_OPERATION);
      return false;
    }
    this._recordError(GL.INVALID_ENUM);
    return false;
  }
  bindTexture(target, texture) {
    if (!this._validateTextureTarget(target)) return;
    this._textureUnits[this._activeTextureUnit][target] = this._textureRecord(texture);
  }
  _rgbaPixels(width, height, format, type, data) {
    const rgba = new Uint8Array(width * height * 4);
    if (!data) return rgba;
    const source = data.data && ArrayBuffer.isView(data.data) ? data.data : data;
    if (!ArrayBuffer.isView(source)) return rgba;
    const componentCount = format === 0x1907 ? 3 : [0x1903, 0x1906, 0x1909].includes(format) ? 1 : [0x190a, 0x8227].includes(format) ? 2 : 4;
    const componentBytes = type === 0x1406 ? 4 : type === 0x1403 || type === 0x1402 ? 2 : 1;
    const rowPixels = this._unpack.rowLength || width;
    const rowBytes = Math.ceil(rowPixels * componentCount * componentBytes / this._unpack.alignment) * this._unpack.alignment;
    const base = this._unpack.skipRows * rowBytes + this._unpack.skipPixels * componentCount * componentBytes;
    const bytes = new Uint8Array(source.buffer, source.byteOffset, source.byteLength);
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    for (let y = 0; y < height; y++) {
      const inputY = this._unpack.flipY ? height - 1 - y : y;
      for (let x = 0; x < width; x++) {
        const input = base + inputY * rowBytes + x * componentCount * componentBytes;
        const output = (y * width + x) * 4;
        let values = [0, 0, 0, 255];
        if (format === 0x1906) values = [255, 255, 255, 0];
        else if (format === 0x1909) values = [0, 0, 0, 255];
        for (let c = 0; c < componentCount; c++) {
          let value = 0;
          if (input + (c + 1) * componentBytes <= bytes.length) value = componentBytes === 4
            ? Math.round(clamp01(view.getFloat32(input + c * componentBytes, true)) * 255)
            : componentBytes === 2 ? Math.round(view.getUint16(input + c * componentBytes, true) / 257) : view.getUint8(input + c);
          if (format === 0x1906) values[3] = value;
          else if (format === 0x1909) values[0] = values[1] = values[2] = value;
          else if (format === 0x190a) { values[c === 0 ? 0 : 3] = value; if (c === 0) values[1] = values[2] = value; }
          else values[c] = value;
        }
        if (this._unpack.premultiplyAlpha && componentCount === 4) for (let c = 0; c < 3; c++) values[c] = Math.round(values[c] * values[3] / 255);
        rgba.set(values, output);
      }
    }
    return rgba;
  }
  _uploadRows(pixels, width, height) {
    const result = new Uint8Array(pixels.length);
    for (let y = 0; y < height; y++) result.set(pixels.subarray(y * width * 4, (y + 1) * width * 4), (height - 1 - y) * width * 4);
    return result;
  }
  _ensureTextureGpu(texture) {
    if (!texture?.width || !texture?.height) return null;
    const levels = Math.floor(Math.log2(Math.max(texture.width, texture.height))) + 1;
    if (!texture.gpu || texture.gpuMipCount !== levels) {
      texture.gpu?.destroy();
      texture.gpu = this._device.createTexture({ size: [texture.width, texture.height, 1], mipLevelCount: levels, format: texture.format,
        viewFormats: [texture.format === 'rgba8unorm-srgb' ? 'rgba8unorm' : 'rgba8unorm-srgb'],
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST | GPUTextureUsage.COPY_SRC | GPUTextureUsage.RENDER_ATTACHMENT });
      texture.gpuMipCount = levels;
    }
    for (const [level, record] of texture.levels) {
      if (level >= levels) continue;
      this._device.queue.writeTexture({ texture: texture.gpu, mipLevel: level }, this._uploadRows(record.pixels, record.width, record.height),
        { bytesPerRow: record.width * 4, rowsPerImage: record.height }, [record.width, record.height, 1]);
    }
    return texture.gpu;
  }
  texImage2D(...args) {
    const [target, level, internalFormat] = args;
    let width, height, border = 0, format, type, data;
    if (args.length >= 9) [, , , width, height, border, format, type, data] = args;
    else if (args.length >= 6) {
      [, , , format, type, data] = args;
      width = data?.width || 0; height = data?.height || 0;
    } else { this._recordError(GL.INVALID_VALUE); return; }
    if (!this._validateTextureTarget(target)) return;
    const texture = this._boundTexture(target);
    if (!texture) { this._recordError(GL.INVALID_OPERATION); return; }
    if (width < 0 || height < 0 || level < 0 || border !== 0) { this._recordError(GL.INVALID_VALUE); return; }
    if (![0x1903, 0x1906, 0x1909, 0x190a, 0x8227, 0x1907, 0x1908, 0x80e1, 0x8d94, 0x8228].includes(format)) { this._recordError(GL.INVALID_ENUM); return; }
    width |= 0; height |= 0;
    if (level === 0) {
      texture.width = width; texture.height = height;
      texture.format = [0x8c41, 0x8c43, 0x8c40, 0x8c42].includes(internalFormat) ? 'rgba8unorm-srgb' : 'rgba8unorm';
    }
    const levelWidth = Math.max(1, texture.width >> level), levelHeight = Math.max(1, texture.height >> level);
    texture.levels.set(level, { width: levelWidth, height: levelHeight, pixels: this._rgbaPixels(levelWidth, levelHeight, format, type, data) });
    if (level === 0) texture.pixels = texture.levels.get(0).pixels;
    this._enqueue(() => this._ensureTextureGpu(texture));
  }
  texSubImage2D(...args) {
    const [target, level, xoffset, yoffset, width, height, format, type, data] = args;
    if (!this._validateTextureTarget(target)) return;
    const texture = this._boundTexture(target), record = texture?.levels.get(level);
    if (!texture) { this._recordError(GL.INVALID_OPERATION); return; }
    if (!record) { this._recordError(GL.INVALID_OPERATION); return; }
    if ([xoffset, yoffset, width, height, level].some(value => !Number.isInteger(value) || value < 0) ||
        xoffset + width > record.width || yoffset + height > record.height) { this._recordError(GL.INVALID_VALUE); return; }
    const patch = this._rgbaPixels(width, height, format, type, data);
    for (let y = 0; y < height; y++) {
      const dstY = yoffset + y;
      if (dstY < 0 || dstY >= record.height) continue;
      const left = Math.max(0, xoffset), right = Math.min(record.width, xoffset + width);
      if (right > left) record.pixels.set(patch.subarray((y * width + left - xoffset) * 4, (y * width + right - xoffset) * 4), (dstY * record.width + left) * 4);
    }
    if (level === 0) texture.pixels = record.pixels;
    this._enqueue(() => {
      const gpu = this._ensureTextureGpu(texture);
      if (!gpu) return;
      const gpuRows = this._uploadRows(patch, width, height);
      this._device.queue.writeTexture({ texture: gpu, mipLevel: level, origin: [xoffset, record.height - yoffset - height, 0] }, gpuRows,
        { bytesPerRow: width * 4, rowsPerImage: height }, [width, height, 1]);
    });
  }
  compressedTexImage2D(target, level, format, width, height, border, imageSize, data) {
    if (!this._validateTextureTarget(target)) return;
    if (width < 0 || height < 0 || border !== 0 || imageSize < 0) { this._recordError(GL.INVALID_VALUE); return; }
    const blockBytes = S3TC_BLOCK_BYTES.get(format);
    if (!blockBytes) {
      const known = [0x9270, 0x9271, 0x9272, 0x9273, 0x9274, 0x9275, 0x9276, 0x9277, 0x9278, 0x9279, 0x8e8c, 0x8e8d].includes(format);
      this._rejectUnsupported(`compressed texture format 0x${Number(format).toString(16)}`, known ? GL.INVALID_OPERATION : GL.INVALID_ENUM);
      return;
    }
    const expectedSize = Math.ceil(width / 4) * Math.ceil(height / 4) * blockBytes;
    if (imageSize !== expectedSize) { this._recordError(GL.INVALID_VALUE); return; }
    const pixels = decodeS3tc(format, width, height, data);
    if (!pixels) { this._recordError(GL.INVALID_VALUE); return; }
    const texture = this._boundTexture(target);
    if (!texture) { this._recordError(GL.INVALID_OPERATION); return; }
    texture.width = width; texture.height = height; texture.internalFormat = format;
    texture.format = [0x8c4c, 0x8c4d, 0x8c4e, 0x8c4f].includes(format) ? 'rgba8unorm-srgb' : 'rgba8unorm';
    texture.levels.set(level, { width, height, pixels });
    if (level === 0) texture.pixels = pixels;
    this._enqueue(() => this._ensureTextureGpu(texture));
  }
  compressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data) {
    const texture = this._boundTexture(target), record = texture?.levels.get(level);
    if (!this._validateTextureTarget(target)) return;
    if (!record || texture.internalFormat !== format) { this._recordError(GL.INVALID_OPERATION); return; }
    if (xoffset < 0 || yoffset < 0 || width < 0 || height < 0 || xoffset + width > record.width || yoffset + height > record.height) { this._recordError(GL.INVALID_VALUE); return; }
    const blockBytes = S3TC_BLOCK_BYTES.get(format);
    if (!blockBytes) { this._rejectUnsupported(`compressed texture format 0x${Number(format).toString(16)}`, GL.INVALID_OPERATION); return; }
    if ((xoffset % 4) || (yoffset % 4) || (width % 4 && xoffset + width !== record.width) || (height % 4 && yoffset + height !== record.height)) { this._recordError(GL.INVALID_OPERATION); return; }
    const expectedSize = Math.ceil(width / 4) * Math.ceil(height / 4) * blockBytes;
    const pixels = imageSize === expectedSize ? decodeS3tc(format, width, height, data) : null;
    if (!pixels) { this._recordError(GL.INVALID_VALUE); return; }
    for (let y = 0; y < height; y++) {
      const dst = ((yoffset + y) * record.width + xoffset) * 4;
      record.pixels.set(pixels.subarray(y * width * 4, (y + 1) * width * 4), dst);
    }
    if (level === 0) texture.pixels = record.pixels;
    this._enqueue(() => this._ensureTextureGpu(texture));
  }
  texParameteri(target, parameter, value) {
    if (!this._validateTextureTarget(target)) return;
    const texture = this._boundTexture(target);
    if (!texture) { this._recordError(GL.INVALID_OPERATION); return; }
    if (parameter === 0x84fe) {
      if (!Number.isFinite(value) || value < 1) { this._recordError(GL.INVALID_VALUE); return; }
      if (value > 16) { this._rejectUnsupported('anisotropy greater than 16', GL.INVALID_VALUE); return; }
      texture.params.set(parameter, Math.floor(value));
      texture.samplerCache?.clear();
      return;
    }
    const values = parameter === 0x2802 || parameter === 0x2803 ? [0x812f, 0x2901, 0x8370]
      : parameter === 0x2800 ? [0x2600, 0x2601]
        : parameter === 0x2801 ? [0x2600, 0x2601, 0x2700, 0x2701, 0x2702, 0x2703, 0x2704, 0x2705]
          : parameter === 0x813c || parameter === 0x813d ? null : undefined;
    if (values === undefined) { this._recordError(GL.INVALID_ENUM); return; }
    if (values && !values.includes(value)) { this._recordError(GL.INVALID_ENUM); return; }
    if (values === null && (!Number.isInteger(value) || value < 0)) { this._recordError(GL.INVALID_VALUE); return; }
    texture.params.set(parameter, value);
    texture.samplerCache?.clear();
  }
  texParameterf(target, parameter, value) { this.texParameteri(target, parameter, value); }
  _samplerFor(texture) {
    texture.samplerCache ||= new Map();
    const cacheKey = stateKey([...texture.params.entries()].sort((a, b) => a[0] - b[0]));
    if (texture.samplerCache.has(cacheKey)) return texture.samplerCache.get(cacheKey);
    const address = value => value === 0x2901 ? 'repeat' : value === 0x8370 ? 'mirror-repeat' : 'clamp-to-edge';
    const filter = value => value === 0x2600 ? 'nearest' : 'linear';
    const min = texture.params.get(0x2801) ?? 0x2703, mag = texture.params.get(0x2800) ?? 0x2601;
    const maxAnisotropy = Math.max(1, Math.min(16, texture.params.get(0x84fe) || 1));
    const sampler = this._device.createSampler({ addressModeU: address(texture.params.get(0x2802)), addressModeV: address(texture.params.get(0x2803)),
      magFilter: filter(mag), minFilter: filter(min === 0x2600 || min === 0x2601 ? min : min === 0x2700 || min === 0x2701 || min === 0x2702 ? 0x2600 : 0x2601),
      mipmapFilter: min === 0x2700 || min === 0x2702 || min === 0x2704 ? 'nearest' : 'linear', lodMinClamp: 0, lodMaxClamp: min === 0x2600 || min === 0x2601 ? 0 : 32,
      maxAnisotropy: mag === 0x2601 && ![0x2600, 0x2700, 0x2702, 0x2704].includes(min) ? maxAnisotropy : 1 });
    texture.samplerCache.set(cacheKey, sampler);
    return sampler;
  }
  generateMipmap(target) {
    if (!this._validateTextureTarget(target)) return;
    const texture = this._boundTexture(target), base = texture?.levels.get(0);
    if (!texture || !base) { this._recordError(GL.INVALID_OPERATION); return; }
    let prior = base, level = 1;
    while (prior.width > 1 || prior.height > 1) {
      const width = Math.max(1, prior.width >> 1), height = Math.max(1, prior.height >> 1), pixels = new Uint8Array(width * height * 4);
      for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) for (let c = 0; c < 4; c++) {
        let total = 0, count = 0;
        for (let dy = 0; dy < 2; dy++) for (let dx = 0; dx < 2; dx++) {
          const sx = Math.min(prior.width - 1, x * 2 + dx), sy = Math.min(prior.height - 1, y * 2 + dy);
          total += prior.pixels[(sy * prior.width + sx) * 4 + c]; count++;
        }
        pixels[(y * width + x) * 4 + c] = Math.round(total / count);
      }
      const record = { width, height, pixels };
      texture.levels.set(level++, record); prior = record;
    }
    this._enqueue(() => this._ensureTextureGpu(texture));
  }
  activeTexture(texture) {
    if (!Number.isInteger(texture) || texture < 0x84c0 || texture >= 0x84c8) { this._recordError(GL.INVALID_ENUM); return; }
    this._activeTextureUnit = texture - 0x84c0;
  }
  createFramebuffer() {
    const object = { id: this._nextId++, attachments: new Map() };
    this._framebufferObjects.add(object);
    this._bufferSelections.set(object, { draw: 0x8ce0, read: 0x8ce0 });
    return object;
  }
  deleteFramebuffer(framebuffer) { this._framebufferObjects.delete(framebuffer); if (this._drawFramebuffer === framebuffer) this._drawFramebuffer = 0; if (this._readFramebuffer === framebuffer) this._readFramebuffer = 0; }
  isFramebuffer(framebuffer) { return this._framebufferObjects.has(framebuffer); }
  bindFramebuffer(target, framebuffer) {
    const value = framebuffer || 0;
    if (target === GL.READ_FRAMEBUFFER) this._readFramebuffer = value;
    else if (target === GL.DRAW_FRAMEBUFFER) this._drawFramebuffer = value;
    else if (target === GL.FRAMEBUFFER) { this._readFramebuffer = value; this._drawFramebuffer = value; }
    else this._recordError(GL.INVALID_ENUM);
  }
  _boundFramebuffer(target) { return this._framebufferForTarget(target); }
  framebufferTexture2D(target, attachment, textarget, texture, level = 0) {
    if (![GL.FRAMEBUFFER, GL.DRAW_FRAMEBUFFER].includes(target)) { this._recordError(GL.INVALID_ENUM); return; }
    if (![0x8ce0, 0x8d00, 0x8d20, 0x821a].includes(attachment) && !(attachment >= 0x8ce1 && attachment <= 0x8cff)) { this._recordError(GL.INVALID_ENUM); return; }
    if (!this._validateTextureTarget(textarget)) return;
    if (!Number.isInteger(level) || level < 0) { this._recordError(GL.INVALID_VALUE); return; }
    const framebuffer = this._boundFramebuffer(target);
    if (!framebuffer?.attachments) return;
    if (!texture) framebuffer.attachments.delete(attachment);
    else framebuffer.attachments.set(attachment, { type: 'texture', object: this._textureRecord(texture), target: textarget, level });
  }
  framebufferRenderbuffer(target, attachment, renderbufferTarget, renderbuffer) {
    if (![GL.FRAMEBUFFER, GL.DRAW_FRAMEBUFFER].includes(target)) { this._recordError(GL.INVALID_ENUM); return; }
    if (![0x8ce0, 0x8d00, 0x8d20, 0x821a].includes(attachment) && !(attachment >= 0x8ce1 && attachment <= 0x8cff)) { this._recordError(GL.INVALID_ENUM); return; }
    if (renderbufferTarget !== 0x8d41) { this._recordError(GL.INVALID_ENUM); return; }
    const framebuffer = this._boundFramebuffer(target);
    if (!framebuffer?.attachments) return;
    if (!renderbuffer) framebuffer.attachments.delete(attachment);
    else framebuffer.attachments.set(attachment, { type: 'renderbuffer', object: renderbuffer });
  }
  checkFramebufferStatus(target) {
    const framebuffer = this._boundFramebuffer(target);
    if (this._isDefaultFramebuffer(framebuffer)) return GL.FRAMEBUFFER_COMPLETE;
    const color = framebuffer?.attachments?.get(0x8ce0), depth = framebuffer?.attachments?.get(0x8d00) || framebuffer?.attachments?.get(0x821a);
    const valid = attachment => attachment?.object && attachment.object.width > 0 && attachment.object.height > 0;
    return valid(color) || valid(depth) ? GL.FRAMEBUFFER_COMPLETE : 0x8cd7;
  }
  createRenderbuffer() { const object = { id: this._nextId++, width: 0, height: 0, format: 'depth24plus', internalFormat: 0, gpu: null, pixels: null }; this._renderbuffers.add(object); return object; }
  deleteRenderbuffer(object) { if (!object) return; object.gpu?.destroy(); this._renderbuffers.delete(object); for (const framebuffer of this._framebufferObjects) for (const [key, record] of framebuffer.attachments) if (record.object === object) framebuffer.attachments.delete(key); }
  isRenderbuffer(object) { return this._renderbuffers.has(object); }
  bindRenderbuffer(target, object) {
    if (target !== 0x8d41) { this._recordError(GL.INVALID_ENUM); return; }
    this._boundRenderbuffer = object || null;
  }
  renderbufferStorage(target, internalFormat, width, height) {
    const buffer = this._boundRenderbuffer;
    if (!buffer || target !== 0x8d41) return;
    buffer.gpu?.destroy(); buffer.width = width | 0; buffer.height = height | 0; buffer.internalFormat = internalFormat;
    buffer.format = [0x81a5, 0x81a6, 0x81a7, 0x8cac].includes(internalFormat) ? (internalFormat === 0x8cac ? 'depth32float' : 'depth24plus') : 'rgba8unorm';
    buffer.pixels = new Uint8Array(Math.max(0, buffer.width * buffer.height * 4));
    this._enqueue(() => {
      if (!buffer.width || !buffer.height) return;
      buffer.gpu = this._device.createTexture({ size: [buffer.width, buffer.height, 1], format: buffer.format,
        usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING });
    });
  }
  getRenderbufferParameter(target, parameter) {
    if (!this._boundRenderbuffer) return 0;
    return parameter === 0x8d42 ? this._boundRenderbuffer.internalFormat : parameter === 0x8d43 ? this._boundRenderbuffer.width : parameter === 0x8d44 ? this._boundRenderbuffer.height : 0;
  }
  getFramebufferAttachmentParameter(target, attachment, parameter) {
    const record = this._boundFramebuffer(target)?.attachments?.get(attachment);
    if (!record) return 0;
    if (parameter === 0x8cd0) return record.type === 'texture' ? 0x1702 : 0x8d41;
    if (parameter === 0x8cd1) return record.type === 'texture' ? record.object.handle : 0;
    if (parameter === 0x8cd2) return record.level || 0;
    if (parameter === 0x8210) return record.object?.format?.endsWith('-srgb') ? 0x8c40 : 0x2601;
    this._recordError(GL.INVALID_ENUM);
    return 0;
  }
  async _blitColorFormats(source, destination, sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, filter) {
    // RGBA texture attachments and the browser's BGRA surface cannot be copied
    // byte-for-byte. A render pass performs the channel-format conversion.
    let pipeline = this._blitPipelineCache.get(destination.format);
    if (!pipeline) {
      const code = `
struct Out { @builtin(position) position: vec4<f32>, @location(0) uv: vec2<f32> };
@group(0) @binding(0) var<uniform> sourceRect: vec4<f32>;
@group(0) @binding(1) var sourceTexture: texture_2d<f32>;
@group(0) @binding(2) var sourceSampler: sampler;
@vertex fn vs(@builtin(vertex_index) i: u32) -> Out {
  let p = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
  var out: Out; out.position = vec4<f32>(p[i], 0.0, 1.0);
  out.uv = vec2<f32>(p[i].x * 0.5 + 0.5, 0.5 - p[i].y * 0.5); return out;
}
@fragment fn fs(input: Out) -> @location(0) vec4<f32> {
  return textureSampleLevel(sourceTexture, sourceSampler, sourceRect.xy + input.uv * sourceRect.zw, 0.0);
}`;
      const module = this._device.createShaderModule({ code });
      pipeline = this._device.createRenderPipeline({ layout: 'auto',
        vertex: { module, entryPoint: 'vs' },
        fragment: { module, entryPoint: 'fs', targets: [{ format: destination.format }] },
        primitive: { topology: 'triangle-list' } });
      this._blitPipelineCache.set(destination.format, pipeline);
    }
    const uniform = this._device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    const destinationLeft = Math.min(dx0, dx1), destinationRight = Math.max(dx0, dx1);
    const destinationBottom = Math.min(dy0, dy1), destinationTop = Math.max(dy0, dy1);
    const txLeft = (destinationLeft - dx0) / (dx1 - dx0), txRight = (destinationRight - dx0) / (dx1 - dx0);
    const tyTop = (destinationTop - dy0) / (dy1 - dy0), tyBottom = (destinationBottom - dy0) / (dy1 - dy0);
    const sourceXLeft = sx0 + txLeft * (sx1 - sx0), sourceXRight = sx0 + txRight * (sx1 - sx0);
    const sourceYTop = sy0 + tyTop * (sy1 - sy0), sourceYBottom = sy0 + tyBottom * (sy1 - sy0);
    this._device.queue.writeBuffer(uniform, 0, new Float32Array([
      sourceXLeft / source.width, 1 - sourceYTop / source.height,
      (sourceXRight - sourceXLeft) / source.width, (sourceYTop - sourceYBottom) / source.height,
    ]));
    const sampling = filter === 0x2600 ? 'nearest' : 'linear';
    const bindGroup = this._device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
      { binding: 0, resource: { buffer: uniform } },
      { binding: 1, resource: source.colorTexture.createView({ baseMipLevel: source.colorMipLevel || 0, mipLevelCount: 1 }) },
      { binding: 2, resource: this._device.createSampler({ minFilter: sampling, magFilter: sampling }) },
    ] });
    const encoder = this._device.createCommandEncoder();
    const pass = encoder.beginRenderPass({ colorAttachments: [{
      view: destination.colorTexture.createView({ baseMipLevel: destination.colorMipLevel || 0, mipLevelCount: 1 }),
      loadOp: 'load', storeOp: 'store',
    }] });
    pass.setPipeline(pipeline); pass.setBindGroup(0, bindGroup);
    const width = destinationRight - destinationLeft, height = destinationTop - destinationBottom;
    pass.setViewport(destinationLeft, destination.height - destinationTop, width, height, 0, 1);
    pass.draw(3); pass.end();
    this._device.queue.submit([encoder.finish()]);
    this._device.queue.onSubmittedWorkDone().then(() => uniform.destroy()).catch(() => {});
  }

  blitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter) {
    if (mask & ~(GL.COLOR_BUFFER_BIT | GL.DEPTH_BUFFER_BIT | GL.STENCIL_BUFFER_BIT)) { this._recordError(GL.INVALID_VALUE); return; }
    if ((mask & GL.COLOR_BUFFER_BIT) && ![0x2600, 0x2601].includes(filter)) { this._recordError(GL.INVALID_ENUM); return; }
    if ((mask & (GL.DEPTH_BUFFER_BIT | GL.STENCIL_BUFFER_BIT)) && filter !== 0x2600) { this._recordError(GL.INVALID_OPERATION); return; }
    if (mask & (GL.DEPTH_BUFFER_BIT | GL.STENCIL_BUFFER_BIT)) { this._rejectUnsupported('blitFramebuffer depth/stencil', GL.INVALID_OPERATION); return; }
    if (!(mask & GL.COLOR_BUFFER_BIT)) return;
    const source = this._targetFor(this._readFramebuffer), destination = this._targetFor(this._drawFramebuffer);
    if (!source?.colorTexture || !destination?.colorTexture || !source?.pixels || !destination?.pixels) { this._recordError(GL.INVALID_FRAMEBUFFER_OPERATION); return; }
    if (srcX0 === srcX1 || srcY0 === srcY1 || dstX0 === dstX1 || dstY0 === dstY1) return;
    if (destination.default) this._frameDirty = true;
    const dx = Math.min(dstX0, dstX1), dy = Math.min(dstY0, dstY1), dw = Math.abs(dstX1 - dstX0), dh = Math.abs(dstY1 - dstY0);
    const linear = filter === 0x2601;
    for (let y = 0; y < dh; y++) for (let x = 0; x < dw; x++) {
      const destX = dx + x, destY = dy + y;
      if (destX < 0 || destY < 0 || destX >= destination.width || destY >= destination.height) continue;
      const tx = (destX + 0.5 - dstX0) / (dstX1 - dstX0), ty = (destY + 0.5 - dstY0) / (dstY1 - dstY0);
      const sampleX = srcX0 + tx * (srcX1 - srcX0), sampleY = srcY0 + ty * (srcY1 - srcY0);
      const dst = (destY * destination.width + destX) * 4;
      if (!linear) {
        const srcX = Math.floor(sampleX), srcY = Math.floor(sampleY);
        if (srcX < 0 || srcY < 0 || srcX >= source.width || srcY >= source.height) continue;
        const src = (srcY * source.width + srcX) * 4;
        destination.pixels.set(source.pixels.subarray(src, src + 4), dst);
      } else {
        const fx = sampleX - 0.5, fy = sampleY - 0.5, x0 = Math.floor(fx), y0 = Math.floor(fy), wx = fx - x0, wy = fy - y0;
        for (let c = 0; c < 4; c++) {
          const at = (px, py) => source.pixels[(Math.max(0, Math.min(source.height - 1, py)) * source.width + Math.max(0, Math.min(source.width - 1, px))) * 4 + c];
          destination.pixels[dst + c] = Math.round((at(x0, y0) * (1 - wx) + at(x0 + 1, y0) * wx) * (1 - wy) + (at(x0, y0 + 1) * (1 - wx) + at(x0 + 1, y0 + 1) * wx) * wy);
        }
      }
    }
    this._enqueue(() => {
      const sameSize = Math.abs(srcX1 - srcX0) === dw && Math.abs(srcY1 - srcY0) === dh;
      const sameOrientation = (srcX1 - srcX0) * (dstX1 - dstX0) > 0 && (srcY1 - srcY0) * (dstY1 - dstY0) > 0;
      if (source.format !== destination.format || !sameSize || !sameOrientation || filter === 0x2601)
        return this._blitColorFormats(source, destination, srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, filter);
      const sx = Math.min(srcX0, srcX1), sy = Math.min(srcY0, srcY1), sw = Math.abs(srcX1 - srcX0), sh = Math.abs(srcY1 - srcY0);
      const encoder = this._device.createCommandEncoder();
      encoder.copyTextureToTexture({ texture: source.colorTexture, mipLevel: source.colorMipLevel || 0, origin: [sx, source.height - sy - sh, 0] },
        { texture: destination.colorTexture, mipLevel: destination.colorMipLevel || 0, origin: [dx, destination.height - dy - dh, 0] }, [sw, sh, 1]);
      this._device.queue.submit([encoder.finish()]);
    });
  }
  drawBuffers(buffers) {
    const values = Array.from(buffers || []);
    if (!values.length) { this._bufferSelection(this._drawFramebuffer).draw = GL.NONE; return; }
    if (values.length > 1) { this._rejectUnsupported('multiple draw buffers (MRT)', GL.INVALID_OPERATION); return; }
    const defaultFramebuffer = this._isDefaultFramebuffer(this._drawFramebuffer);
    const expected = defaultFramebuffer ? GL.BACK : 0x8ce0;
    if (values[0] !== GL.NONE && values[0] !== GL.BACK && !(values[0] >= 0x8ce0 && values[0] <= 0x8cff)) { this._recordError(GL.INVALID_ENUM); return; }
    if (values[0] !== GL.NONE && values[0] !== expected) { this._recordError(GL.INVALID_OPERATION); return; }
    this._bufferSelection(this._drawFramebuffer).draw = values[0];
  }
  readBuffer(mode) {
    const defaultFramebuffer = this._isDefaultFramebuffer(this._readFramebuffer);
    const expected = defaultFramebuffer ? GL.BACK : 0x8ce0;
    if (mode !== GL.NONE && mode !== GL.BACK && !(mode >= 0x8ce0 && mode <= 0x8cff)) { this._recordError(GL.INVALID_ENUM); return; }
    if (mode !== GL.NONE && mode !== expected) { this._recordError(GL.INVALID_OPERATION); return; }
    this._bufferSelection(this._readFramebuffer).read = mode;
  }
}

/** Create a context object for the GL64 bridge's post-create GLctx hook. */
export function createWebGPUGLContext(originalContext, defaultFramebuffer = 0, log = console.warn) {
  if (typeof OffscreenCanvas !== 'function') return null;
  if (typeof navigator === 'undefined' || !navigator.gpu) return null;
  const width = Math.max(1, originalContext?.canvas?.width | 0);
  const height = Math.max(1, originalContext?.canvas?.height | 0);
  try {
    const canvas = new OffscreenCanvas(width, height);
    return createRecoveringBridge(new WebGPUGL(canvas, { fallbackContext: originalContext, defaultFramebuffer, log }), { name: 'gl-webgpu' });
  } catch (error) {
    reportBridgeFailure('gl-webgpu.context', error, log);
    return null;
  }
}
