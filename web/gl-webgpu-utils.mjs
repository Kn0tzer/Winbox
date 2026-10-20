const GL_UNSIGNED_BYTE = 0x1401;
const GL_UNSIGNED_SHORT = 0x1403;
const GL_UNSIGNED_INT = 0x1405;

const blendFactors = new Map([
  [0x0000, 'zero'], [0x0001, 'one'],
  [0x0300, 'src'], [0x0301, 'one-minus-src'],
  [0x0302, 'src-alpha'], [0x0303, 'one-minus-src-alpha'],
  [0x0304, 'dst-alpha'], [0x0305, 'one-minus-dst-alpha'],
  [0x0306, 'dst'], [0x0307, 'one-minus-dst'],
  [0x0308, 'src-alpha-saturated'],
  [0x8001, 'constant'], [0x8002, 'one-minus-constant'],
  [0x8003, 'constant'], [0x8004, 'one-minus-constant'],
]);

const alignTo = (value, alignment) => Math.ceil(value / alignment) * alignment;

function typeInfo(type) {
  if (type === 'float' || type === 'int' || type === 'uint' || type === 'bool') {
    return { kind: type, rows: 1, columns: 1, alignment: 4, size: 4 };
  }
  const vector = /^(b|i|u)?vec([234])$/.exec(type);
  if (vector) {
    const rows = Number(vector[2]);
    return { kind: vector[1] === 'i' ? 'int' : vector[1] === 'u' ? 'uint' : vector[1] === 'b' ? 'bool' : 'float',
      rows, columns: 1, alignment: rows === 2 ? 8 : 16, size: rows * 4 };
  }
  const matrix = /^mat([234])(?:x([234]))?$/.exec(type);
  if (matrix) {
    const columns = Number(matrix[1]), rows = Number(matrix[2] || matrix[1]);
    return { kind: 'float', rows, columns, alignment: 16, columnStride: 16, size: columns * 16 };
  }
  throw new TypeError(`Unsupported uniform type: ${type}`);
}

/** Compute std140-compatible offsets and strides for a flat uniform block. */
export function uniformLayout(members) {
  if (!Array.isArray(members)) throw new TypeError('Uniform members must be an array');
  let cursor = 0;
  const fields = members.map(member => {
    if (!member || typeof member.name !== 'string') throw new TypeError('Each uniform needs a name');
    const info = typeInfo(member.type);
    const arrayLength = member.arrayLength == null ? 1 : member.arrayLength;
    if (!Number.isInteger(arrayLength) || arrayLength < 1) throw new RangeError(`Invalid array length for ${member.name}`);
    const arrayStride = arrayLength > 1 ? alignTo(info.size, 16) : 0;
    const alignment = arrayLength > 1 ? 16 : info.alignment;
    cursor = alignTo(cursor, alignment);
    const field = { name: member.name, type: member.type, offset: cursor, size: arrayLength > 1 ? arrayStride * arrayLength : info.size,
      arrayLength, arrayStride, alignment, rows: info.rows, columns: info.columns,
      columnStride: info.columnStride || 0, kind: info.kind };
    cursor += field.size;
    return field;
  });
  return { fields, size: alignTo(cursor, 16) };
}

/** Pack values into the layout returned by uniformLayout(). Missing values stay zero. */
export function packUniforms(layout, values = {}) {
  const bytes = new Uint8Array(layout.size);
  const view = new DataView(bytes.buffer);
  for (const field of layout.fields) {
    const value = values[field.name];
    if (value == null) continue;
    const componentCount = field.rows * field.columns;
    for (let element = 0; element < field.arrayLength; element++) {
      let components = value;
      if (field.arrayLength > 1) {
        components = Array.isArray(value[0]) || ArrayBuffer.isView(value[0])
          ? value[element]
          : Array.prototype.slice.call(value, element * componentCount, (element + 1) * componentCount);
      }
      if (field.rows === 1 && field.columns === 1) components = [components];
      const base = field.offset + (field.arrayLength > 1 ? element * field.arrayStride : 0);
      for (let column = 0; column < field.columns; column++) {
        const columnBase = base + (field.columns > 1 ? column * field.columnStride : 0);
        for (let row = 0; row < field.rows; row++) {
          const index = field.columns > 1 ? column * field.rows + row : row;
          const component = components[index] ?? 0;
          const offset = columnBase + row * 4;
          if (field.kind === 'float') view.setFloat32(offset, Number(component), true);
          else if (field.kind === 'uint') view.setUint32(offset, Number(component) >>> 0, true);
          else view.setInt32(offset, field.kind === 'bool' ? (component ? 1 : 0) : Number(component) | 0, true);
        }
      }
    }
  }
  return bytes;
}

/** WebGPU has no uint8 index format, so widen those indices to uint16. */
export function convertIndices(data, type) {
  if (!ArrayBuffer.isView(data)) throw new TypeError('Index data must be a typed array');
  if (type === GL_UNSIGNED_BYTE) {
    if (!(data instanceof Uint8Array) && !(data instanceof Uint8ClampedArray)) throw new TypeError('UNSIGNED_BYTE indices need byte data');
    return Uint16Array.from(data);
  }
  if (type === GL_UNSIGNED_SHORT) {
    if (!(data instanceof Uint16Array)) throw new TypeError('UNSIGNED_SHORT indices need Uint16Array data');
    return data.slice();
  }
  if (type === GL_UNSIGNED_INT) {
    if (!(data instanceof Uint32Array)) throw new TypeError('UNSIGNED_INT indices need Uint32Array data');
    return data.slice();
  }
  throw new RangeError(`Unsupported index type: ${type}`);
}

export function blendFactor(glFactor) {
  const factor = blendFactors.get(glFactor);
  if (!factor) throw new RangeError(`Unsupported blend factor: ${glFactor}`);
  return factor;
}

function stable(value) {
  if (ArrayBuffer.isView(value)) return Array.from(value, stable);
  if (Array.isArray(value)) return value.map(stable);
  if (value && typeof value === 'object') {
    return Object.fromEntries(Object.keys(value).sort().map(key => [key, stable(value[key])]));
  }
  return value;
}

/** Stable 64-bit FNV-1a key for immutable pipeline/state descriptors. */
export function stateKey(value) {
  const bytes = new TextEncoder().encode(JSON.stringify(stable(value)));
  let hash = 0xcbf29ce484222325n;
  for (const byte of bytes) hash = BigInt.asUintN(64, (hash ^ BigInt(byte)) * 0x100000001b3n);
  return hash.toString(16).padStart(16, '0');
}
