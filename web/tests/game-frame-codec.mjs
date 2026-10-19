// Lossless JSON for a captured replay frame (resource maps and typed arrays).
export function encodeFrame(frame) {
  return JSON.stringify(frame, (_key, value) => {
    if (value instanceof Map) return { $map: [...value] };
    if (ArrayBuffer.isView(value)) {
      const bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
      let binary = '';
      for (let offset = 0; offset < bytes.length; offset += 8192)
        binary += String.fromCharCode(...bytes.subarray(offset, offset + 8192));
      return { $type: value.constructor.name, $bytes: btoa(binary) };
    }
    if (typeof value === 'bigint') return { $bigint: value.toString() };
    return value;
  });
}

export function decodeFrame(json) {
  const types = { Uint8Array, Uint16Array, Uint32Array, Int8Array, Int16Array, Int32Array, Float32Array, Float64Array };
  return JSON.parse(json, (_key, value) => {
    if (!value || typeof value !== 'object') return value;
    if (value.$map) return new Map(value.$map);
    if (value.$bigint) return BigInt(value.$bigint);
    if (value.$bytes !== undefined) {
      const Type = types[value.$type];
      if (!Type) throw Error(`Unsupported frame array: ${value.$type}`);
      const binary = atob(value.$bytes);
      const bytes = new Uint8Array(binary.length);
      for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
      return new Type(bytes.buffer);
    }
    return value;
  });
}
