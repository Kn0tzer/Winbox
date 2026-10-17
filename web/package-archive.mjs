import { ZipWriter, BlobReader, BlobWriter } from './vendor/zip/index-native.js';
import { checkEntries, folderLimits, limits, safePath } from './files.mjs';

const directoryName = 'wine-launcher-archives';
const archiveKey = pkg => {
  if (!/^[a-z0-9_-]{1,100}$/i.test(pkg.id)) throw Error('Invalid local folder identifier');
  return pkg.id + '-' + String(pkg.patched || pkg.created || '').replace(/[^a-z0-9]/gi, '') + '.zip';
};
/** @param {any} pkg @param {{onProgress?:(received:number,total:number)=>void, signal?:AbortSignal, prefix?:string}} [options] */
export async function prepareArchive(pkg, { onProgress = (received, total) => {}, signal, prefix = 'home/username/apps/' } = {}) {
  checkEntries(pkg.files, folderLimits);
  if (prefix) for (const file of pkg.files) {
    if (new TextEncoder().encode(prefix + safePath(file.path)).length >= 255) throw Error('A file path is too long for streamed loading: ' + file.path);
  }
  const total = pkg.files.reduce((n, f) => n + f.size, 0);
  const key = archiveKey(pkg);
  let directory, handle, output, writer, completed = false;
  try {
    if (navigator.storage?.getDirectory) {
      directory = await (await navigator.storage.getDirectory()).getDirectoryHandle(directoryName, { create: true });
      try {
        const cached = await (await directory.getFileHandle(key)).getFile();
        if (cached.size >= total + 22) return lease(cached);
      } catch { /* first launch */ }
      handle = await directory.getFileHandle(key, { create: true });
      output = await handle.createWritable();
    } else {
      if (total > limits.total) throw Error('This folder needs streaming storage. Open it in a current Chrome, Edge or Firefox browser.');
      output = new BlobWriter('application/zip');
    }
    writer = new ZipWriter(output, { useWebWorkers: false, level: 0 });
    let written = 0;
    for (const file of pkg.files) {
      signal?.throwIfAborted();
      await writer.add(prefix + safePath(file.path), new BlobReader(file.blob), {
        level: 0, signal, onprogress: loaded => onProgress(written + loaded, total),
      });
      written += file.size;
    }
    const blob = await writer.close();
    completed = true;
    onProgress(total, total);
    return lease(handle ? await handle.getFile() : blob);
  } finally {
    if (!completed && writer) await writer.close().catch(() => {});
    if (!completed && handle) await directory.removeEntry(key).catch(() => {});
  }
}
function lease(blob) {
  const url = URL.createObjectURL(blob);
  return { url, bytes: blob.size, release: () => URL.revokeObjectURL(url) };
}
export async function removeArchive(pkg) {
  if (!navigator.storage?.getDirectory) return;
  const directory = await (await navigator.storage.getDirectory()).getDirectoryHandle(directoryName).catch(() => null);
  if (!directory) return;
  for await (const [name] of /** @type {any} */ (directory).entries()) if (name.startsWith(pkg.id + '-')) await directory.removeEntry(name);
}
