import { checkEntries, folderLimits, safePath } from './files.mjs';

export async function requestDirectoryAccess(directory) {
  if (!directory) throw Error('Choose this folder again using Relink folder in its settings.');
  const permission = { mode: 'read' };
  if (await directory.queryPermission(permission) !== 'granted' && await directory.requestPermission(permission) !== 'granted')
    throw Error('Allow read access to this folder, or use Relink folder in its settings. Your original files will not be changed.');
}

export async function readDirectory(directory, { previous = [], signal = undefined } = {}) {
  const old = new Map(previous.map(file => [file.path, file]));
  const files = [];
  async function visit(handle, prefix = '') {
    for await (const child of handle.values()) {
      signal?.throwIfAborted();
      const path = safePath(prefix + child.name);
      if (child.kind === 'directory') await visit(child, path + '/');
      else if (child.kind === 'file') {
        if (files.length >= folderLimits.count) throw Error('Too many files');
        const blob = await child.getFile();
        const file = { path, size: blob.size, lastModified: blob.lastModified, blob, crc32: undefined };
        const cached = old.get(path);
        if (cached?.size === file.size && cached.lastModified === file.lastModified) file.crc32 = cached.crc32;
        files.push(file);
      }
    }
  }
  try { await visit(directory); }
  catch (error) {
    if (error.name === 'NotFoundError' || error.name === 'NotAllowedError')
      throw Error('The linked folder is unavailable. Keep it on this device and use Relink folder in its settings.');
    throw error;
  }
  const merged = new Map(files.map(file => [file.path.toLowerCase(), file]));
  for (const file of previous) if (file.override) merged.set(file.path.toLowerCase(), file);
  // Validate before merging so case collisions in originals cannot be hidden.
  checkEntries(files, folderLimits);
  const result = [...merged.values()];
  checkEntries(result, folderLimits);
  return result;
}

export async function openDirectPackage(pkg, options = {}) {
  if (pkg.mode !== 'direct') return pkg;
  await requestDirectoryAccess(pkg.directory);
  return { ...pkg, files: await readDirectory(pkg.directory, { ...options, previous: pkg.files }) };
}

export function packageRecord(pkg) {
  if (pkg.mode !== 'direct') return pkg;
  return { ...pkg, files: pkg.files.map(({ blob, ...metadata }) => metadata.override ? { ...metadata, blob } : metadata) };
}

const crcTable = Uint32Array.from({ length: 256 }, (_, index) => {
  let value = index;
  for (let bit = 0; bit < 8; bit++) value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
  return value >>> 0;
});
async function checksum(file, signal, progress) {
  let crc = 0xffffffff;
  for (let offset = 0; offset < file.size; offset += 4 * 1024 ** 2) {
    signal?.throwIfAborted();
    const bytes = new Uint8Array(await file.blob.slice(offset, offset + 4 * 1024 ** 2).arrayBuffer());
    for (const value of bytes) crc = crcTable[(crc ^ value) & 255] ^ (crc >>> 8);
    progress(bytes.length);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function header(length, signature) {
  const bytes = new Uint8Array(length), view = new DataView(bytes.buffer);
  view.setUint32(0, signature, true);
  return { bytes, u16: (offset, value) => view.setUint16(offset, value, true),
    u32: (offset, value) => view.setUint32(offset, value, true),
    u64: (offset, value) => view.setBigUint64(offset, BigInt(value), true) };
}

// A valid STORE/ZIP64 archive consisting of small headers and original File
// slices. No game data is concatenated, copied to OPFS, or persisted in IDB.
export async function virtualArchive(pkg, { signal = undefined, onProgress = (done, total) => {} } = {}) {
  const total = checkEntries(pkg.files, folderLimits);
  let done = 0, offset = 0;
  const segments = [], central = [];
  function add(blob) { segments.push({ offset, blob }); offset += blob.size; }
  for (const file of pkg.files) {
    signal?.throwIfAborted();
    const name = new TextEncoder().encode('home/username/apps/' + safePath(file.path));
    if (name.length >= 255) throw Error('A file path is too long for streamed loading: ' + file.path);
    if (!Number.isInteger(file.crc32)) file.crc32 = await checksum(file, signal, count => onProgress(done += count, total));
    else onProgress(done += file.size, total);
    const localOffset = offset;
    const local = header(30 + name.length, 0x04034b50);
    local.u16(4, 20); local.u16(6, 0x800); local.u16(12, 33); // UTF-8; Jan 1 1980
    local.u32(14, file.crc32); local.u32(18, file.size); local.u32(22, file.size); local.u16(26, name.length);
    local.bytes.set(name, 30); add(new Blob([local.bytes])); add(file.blob);
    const zip64 = localOffset >= 0xffffffff;
    const c = header(46 + name.length + (zip64 ? 12 : 0), 0x02014b50);
    c.u16(4, 45); c.u16(6, zip64 ? 45 : 20); c.u16(8, 0x800); c.u16(14, 33);
    c.u32(16, file.crc32); c.u32(20, file.size); c.u32(24, file.size);
    c.u16(28, name.length); c.u16(30, zip64 ? 12 : 0); c.u32(42, zip64 ? 0xffffffff : localOffset);
    c.bytes.set(name, 46);
    if (zip64) { c.u16(46 + name.length, 1); c.u16(48 + name.length, 8); c.u64(50 + name.length, localOffset); }
    central.push(c.bytes);
  }
  const centralOffset = offset;
  add(new Blob(central));
  const centralBytes = offset - centralOffset, endOffset = offset;
  const end64 = header(56, 0x06064b50);
  end64.u64(4, 44); end64.u16(12, 45); end64.u16(14, 45);
  end64.u64(24, pkg.files.length); end64.u64(32, pkg.files.length);
  end64.u64(40, centralBytes); end64.u64(48, centralOffset); add(new Blob([end64.bytes]));
  const locator = header(20, 0x07064b50); locator.u64(8, endOffset); locator.u32(16, 1); add(new Blob([locator.bytes]));
  const end = header(22, 0x06054b50); end.u16(8, 0xffff); end.u16(10, 0xffff);
  end.u32(12, 0xffffffff); end.u32(16, 0xffffffff); add(new Blob([end.bytes]));
  onProgress(total, total);
  return {
    url: 'winbox-local:' + crypto.randomUUID(), bytes: offset,
    async read(start, end) {
      if (!Number.isSafeInteger(start) || !Number.isSafeInteger(end) || start < 0 || end < start || end >= offset || end - start >= 1024 ** 2)
        throw Error('Invalid local file range');
      const output = new Uint8Array(end - start + 1);
      let low = 0, high = segments.length;
      while (low < high) { const mid = (low + high) >>> 1; if (segments[mid].offset + segments[mid].blob.size <= start) low = mid + 1; else high = mid; }
      for (let i = low; i < segments.length && segments[i].offset <= end; i++) {
        const segment = segments[i], from = Math.max(start, segment.offset), to = Math.min(end + 1, segment.offset + segment.blob.size);
        if (to > from) output.set(new Uint8Array(await segment.blob.slice(from - segment.offset, to - segment.offset).arrayBuffer()), from - start);
      }
      return output;
    },
    release() { segments.length = 0; },
  };
}
