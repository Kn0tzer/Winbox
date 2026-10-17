import { ZipReader, ZipWriter, BlobReader, BlobWriter } from './vendor/zip/index-native.js';

export const limits = { count: 20000, file: 256 * 1024 ** 2, total: 768 * 1024 ** 2 };
export const folderLimits = { count: 100000, file: 4 * 1024 ** 3 - 1, total: 16 * 1024 ** 3 };
export function safePath(path) {
  if (typeof path !== 'string' || !path || path.length > 1024 || /[\x00-\x1f\x7f\\:]/.test(path) || path.startsWith('/')) throw new Error('Unsafe file path');
  const parts = path.split('/');
  if (parts.some(p => !p || p === '.' || p === '..' || /[. ]$/.test(p) || /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(\.|$)/i.test(p))) throw new Error(`Unsafe Windows path: ${path}`);
  return path;
}
export function checkEntries(entries, budget = limits) {
  if (!entries.length || entries.length > budget.count) throw new Error('Empty package or too many files');
  let total = 0;
  const seen = new Set();
  for (const entry of entries) {
    safePath(entry.path);
    const key = entry.path.toLowerCase();
    if (seen.has(key)) throw new Error(`Windows filename collision: ${entry.path}`);
    seen.add(key);
    if (!Number.isSafeInteger(entry.size) || entry.size < 0 || entry.size > budget.file) throw new Error(`File exceeds the package limit: ${entry.path}`);
    total += entry.size;
    if (total > budget.total) throw new Error('Package exceeds the import size limit');
  }
  // A file cannot also be the parent directory of another file.
  for (const key of seen) {
    const parts = key.split('/');
    while (parts.length > 1) { parts.pop(); if (seen.has(parts.join('/'))) throw new Error('File/directory collision'); }
  }
  return total;
}
export function stripRoot(entries) {
  const root = entries[0]?.path.split('/')[0];
  if (root && entries.every(e => e.path.startsWith(root + '/'))) return entries.map(e => ({ ...e, path: e.path.slice(root.length + 1) }));
  return entries;
}
export async function fromFiles(files) {
  const entries = stripRoot(Array.from(files, f => ({ path: f.webkitRelativePath || f.name, size: f.size, blob: f })));
  checkEntries(entries, folderLimits);
  return entries;
}
export async function fromDrop(items) {
  const roots = Array.from(items).map(item => item.webkitGetAsEntry?.()).filter(Boolean);
  const entries = [];
  async function visit(entry, path) {
    if (entries.length >= folderLimits.count) throw new Error('Too many files');
    if (entry.isFile) {
      const blob = await new Promise((ok, fail) => entry.file(ok, fail));
      entries.push({ path, size: blob.size, blob });
    } else if (entry.isDirectory) {
      const reader = entry.createReader();
      for (;;) {
        const batch = await new Promise((ok, fail) => reader.readEntries(ok, fail));
        if (!batch.length) break; // readEntries returns at most 100 entries per call.
        for (const child of batch) await visit(child, path + '/' + child.name);
      }
    }
  }
  for (const entry of roots) await visit(entry, entry.name);
  const result = stripRoot(entries);
  checkEntries(result, folderLimits);
  return result;
}
const extractedDirectories = new WeakMap();
export const sourceDirectory = entries => extractedDirectories.get(entries)?.name || null;
export const retainExtracted = entries => extractedDirectories.delete(entries);
export async function removeSource(pkg) {
  if (!/^wine-import-[a-z0-9-]+$/i.test(pkg.sourceDirectory || '')) return;
  const root = await navigator.storage.getDirectory();
  await root.removeEntry(pkg.sourceDirectory, { recursive: true });
}
/** Take ownership of native picker files; their backing paths can disappear. */
export async function ownFiles(entries, { signal = undefined, onProgress = (done, total) => {} } = {}) {
  if (extractedDirectories.has(entries)) return entries;
  const total = checkEntries(entries, folderLimits), result = [];
  if (total <= 128 * 1024 ** 2 || !navigator.storage?.getDirectory) {
    if (total > limits.total) throw Error('This folder needs streaming browser storage.');
    let done = 0;
    for (const file of entries) {
      signal?.throwIfAborted();
      result.push({ ...file, blob: new Blob([await file.blob.arrayBuffer()]) });
      onProgress(done += file.size, total);
    }
    return result;
  }
  const root = await navigator.storage.getDirectory(), name = 'wine-import-' + crypto.randomUUID();
  const directory = await root.getDirectoryHandle(name, { create: true });
  try {
    let done = 0;
    for (const [index, file] of entries.entries()) {
      signal?.throwIfAborted();
      const handle = await directory.getFileHandle(String(index), { create: true });
      await file.blob.stream().pipeTo(await handle.createWritable(), { signal });
      result.push({ ...file, blob: await handle.getFile() });
      onProgress(done += file.size, total);
    }
    extractedDirectories.set(result, { root, name, directory });
    return result;
  } catch (error) { await root.removeEntry(name, { recursive: true }).catch(() => {}); throw error; }
}
export async function cleanupExtracted(entries) {
  const directory = extractedDirectories.get(entries);
  if (directory) { await directory.root.removeEntry(directory.name, { recursive: true }); extractedDirectories.delete(entries); }
}
/** @param {Blob} blob @param {{strip?:boolean,budget?:typeof limits,stream?:boolean,signal?:AbortSignal}} options */
export async function fromZip(blob, { strip = true, budget = limits, stream = false, signal } = {}) {
  signal?.throwIfAborted();
  if (blob.size > budget.total) throw new Error('Archive too large');
  const reader = new ZipReader(new BlobReader(blob), { useWebWorkers: false });
  let temporary, successful = false;
  try {
    const archived = await reader.getEntries();
    signal?.throwIfAborted();
    if (archived.length > budget.count) throw new Error('Too many archive entries');
    /** @type {{path:string, size:number, entry?:import('@zip.js/zip.js').FileEntry, blob?:Blob}[]} */
    const entries = [];
    for (const entry of archived) {
      safePath(entry.filename.replace(/\/$/, ''));
      if (entry.encrypted || ((entry.externalFileAttributes >>> 16) & 0xf000) === 0xa000) throw new Error('Encrypted entries and symlinks are not supported');
      if (!entry.directory) entries.push({ path: entry.filename, size: entry.uncompressedSize, entry: /** @type {import('@zip.js/zip.js').FileEntry} */ (entry) });
    }
    const total = checkEntries(entries, budget); // Validate expansion before decompressing.
    if (stream && navigator.storage?.getDirectory) {
      const root = await navigator.storage.getDirectory(), name = 'wine-import-' + crypto.randomUUID();
      temporary = { root, name, directory: await root.getDirectoryHandle(name, { create: true }) };
    } else if (total > limits.total) throw Error('This archive needs streaming storage. Use a current Chrome, Edge or Firefox browser.');
    for (const [index, entry] of entries.entries()) {
      signal?.throwIfAborted();
      if (temporary) {
        const handle = await temporary.directory.getFileHandle(String(index), { create: true });
        await entry.entry.getData(await handle.createWritable(), { checkSignature: true, signal });
        entry.blob = await handle.getFile();
      } else entry.blob = await entry.entry.getData(new BlobWriter(), { checkSignature: true, signal });
      if (entry.blob.size !== entry.size) throw new Error('Archive length mismatch');
      delete entry.entry;
    }
    signal?.throwIfAborted();
    const result = strip ? stripRoot(entries) : entries;
    if (temporary) extractedDirectories.set(result, temporary);
    successful = true;
    return result;
  } finally {
    await reader.close();
    if (!successful && temporary) await temporary.root.removeEntry(temporary.name, { recursive: true }).catch(() => {});
  }
}
export async function makeZip(entries) {
  checkEntries(entries);
  const writer = new ZipWriter(new BlobWriter('application/zip'), { useWebWorkers: false });
  for (const e of entries) await writer.add(e.path, new BlobReader(e.blob), { level: 0 });
  return writer.close();
}
export async function peArchitecture(blob) {
  const head = new DataView(await blob.slice(0, 4096).arrayBuffer());
  if (head.byteLength < 64 || head.getUint16(0, true) !== 0x5a4d) throw new Error('Executable is not a Windows PE file');
  const offset = head.getUint32(60, true);
  if (offset > blob.size - 6) throw new Error('Invalid PE header offset');
  const pe = new DataView(await blob.slice(offset, offset + 6).arrayBuffer());
  if (pe.getUint32(0, true) !== 0x4550) throw new Error('Invalid PE signature');
  const machine = pe.getUint16(4, true);
  if (machine === 0x8664) return 'x64';
  if (machine === 0x14c) return 'x86';
  throw new Error(`Unsupported PE architecture: 0x${machine.toString(16)}`);
}

// Bounds from Microsoft's PE/COFF image format. Architecture-only inspection
// remains available above; automatic launch additionally needs a complete image.
export async function validateExecutable(blob) {
  const arch = await peArchitecture(blob);
  const dos = new DataView(await blob.slice(0, 64).arrayBuffer());
  const offset = dos.getUint32(60, true);
  const coff = new DataView(await blob.slice(offset, offset + 24).arrayBuffer());
  if (coff.byteLength < 24) throw Error('Truncated PE file header');
  const sections = coff.getUint16(6, true), optionalBytes = coff.getUint16(20, true), flags = coff.getUint16(22, true);
  const minimum = arch === 'x64' ? 112 : 96;
  if (!sections || sections > 96 || optionalBytes < minimum || !(flags & 2) || (flags & 0x2000)) throw Error('Not a complete Windows executable image');
  const bytes = new DataView(await blob.slice(offset + 24, offset + 24 + optionalBytes + sections * 40).arrayBuffer());
  if (bytes.byteLength !== optionalBytes + sections * 40 || bytes.getUint16(0, true) !== (arch === 'x64' ? 0x20b : 0x10b)) throw Error('Truncated or invalid PE image headers');
  for (let index = 0; index < sections; index++) {
    const at = optionalBytes + index * 40, size = bytes.getUint32(at + 16, true), start = bytes.getUint32(at + 20, true);
    if (size && (start > blob.size || size > blob.size - start)) throw Error('Truncated PE section data');
  }
  return arch;
}
