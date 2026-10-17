import { checkEntries, limits, safePath } from './files.mjs';
const persistenceDb = id => 'bw64persist-wine-' + id;
function open(id) {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(persistenceDb(id), 1);
    request.onupgradeneeded = () => request.result.createObjectStore('files');
    request.onerror = () => reject(request.error);
    request.onsuccess = () => resolve(request.result);
  });
}
export function validateSaves(entries) {
  const files = entries.map(entry => {
    if (!entry.path.startsWith('/root/home/username/')) throw Error('Invalid save path');
    return { path: safePath(entry.path.slice('/root/home/username/'.length)), size: entry.data.length };
  });
  if (files.length) checkEntries(files, limits);
}
export async function readSaveBlobs(files, prefix) {
  if (files.length) checkEntries(files, limits); // Bound saves before reading any Blob.
  const entries = [];
  for (const file of files) entries.push({ path: prefix + safePath(file.path), data: new Uint8Array(await file.blob.arrayBuffer()) });
  validateSaves(entries);
  return entries;
}
export async function deleteStored(id) {
  return new Promise((resolve, reject) => {
    const request = indexedDB.deleteDatabase(persistenceDb(id));
    request.onsuccess = resolve;
    request.onerror = () => reject(request.error);
    request.onblocked = () => reject(Error('Close other tabs running this folder, then try again.'));
  });
}
export async function storedFiles(id) {
  const db = await open(id);
  try {
    return await new Promise((resolve, reject) => {
      const transaction = db.transaction('files', 'readonly'), records = [];
      let bytes = 0, failure;
      const cursor = transaction.objectStore('files').openCursor();
      cursor.onsuccess = () => {
        const record = cursor.result;
        if (!record) return;
        const entry = { path: String(record.key), data: record.value.data };
        try {
          validateSaves([entry]); bytes += entry.data.length;
          if (records.length >= limits.count || bytes > limits.total) throw Error('Save tree exceeds backup limits');
          records.push(entry); record.continue();
        } catch (error) { failure = error; transaction.abort(); }
      };
      transaction.oncomplete = () => resolve(records);
      transaction.onerror = transaction.onabort = () => reject(failure || transaction.error);
    });
  } finally { db.close(); }
}
export async function writeStored(id, entries, replace = false) {
  validateSaves(entries);
  const db = await open(id);
  try {
    await new Promise((resolve, reject) => {
      const transaction = db.transaction('files', 'readwrite'), store = transaction.objectStore('files');
      if (replace) store.clear();
      for (const entry of entries) store.put({ data: entry.data, sig: Date.now() + ':' + entry.data.length }, entry.path);
      transaction.oncomplete = () => resolve();
      transaction.onerror = transaction.onabort = () => reject(transaction.error);
    });
  } finally { db.close(); }
}
export async function snapshotRuntime(iframe) {
  if (!iframe) return;
  const win = iframe.contentWindow, FS = win.bwRuntime?.fs();
  if (!FS || win.bwFilesystemReady !== true) return;
  const files = []; let size = 0;
  function walk(dir) {
    for (const name of FS.readdir(dir)) {
      if (name === '.' || name === '..' || name.startsWith('.bw64clip')) continue;
      const path = dir + '/' + name, stat = FS.lstat(path);
      if (FS.isDir(stat.mode)) walk(path);
      else if (FS.isFile(stat.mode)) {
        size += stat.size;
        if (stat.size > limits.file || size > limits.total || files.length >= limits.count) throw Error('Save tree exceeds backup limits');
        files.push({ path, data: FS.readFile(path) });
      }
    }
  }
  walk('/root/home/username');
  await writeStored(iframe.dataset.installation, files, true);
}
