// Built-in game catalog. Archives are staged next to the launcher export under
// /games and fetched with the same range-request pattern the runtime uses for
// the Wine rootfs (see project/emscripten/wine64-launcher.js bw64url specs).
// The downloaded Blob is handed to the ordinary ZIP import path in files.mjs,
// so entry validation, root stripping and IndexedDB persistence are identical
// to a dropped archive.
export const builtinGames = [
  {
    id: 'baldi',
    title: "Baldi's Basics",
    archive: 'games/baldi.zip',
    size: 138518151,
    engine: 'Unity 2020.3',
    note: '~131 MB ZIP · ~471 MB installed · x64',
    exe: 'BALDI.exe',
    args: ['-screen-width', '1280', '-screen-height', '720', '-screen-fullscreen', '0'],
  },
];
const CHUNK = 8 * 1024 * 1024;
export function formatBytes(bytes) {
  const n = Number(bytes) || 0;
  if (n >= 1024 ** 3) return (n / 1024 ** 3).toFixed(1) + ' GB';
  if (n >= 1024 ** 2) return Math.round(n / 1024 ** 2) + ' MB';
  if (n >= 1024) return Math.round(n / 1024) + ' kB';
  return n + ' B';
}
function contentLength(res) {
  const n = Number(res.headers.get('Content-Length'));
  return Number.isSafeInteger(n) && n > 0 ? n : 0;
}
/**
 * Downloads a built-in archive, preferring byte ranges and falling back to one
 * streaming GET. onProgress(receivedBytes, totalBytes) drives the card meter.
 * The result is a Blob, exactly what the ZIP file input would have produced.
 * @param {{archive: string, size?: number, title: string}} game
 * @param {(received: number, total: number) => void} [onProgress]
 */
export async function fetchGameArchive(game, onProgress = () => {}) {
  const url = new URL(game.archive, location.href).href;
  const probe = await fetch(url, { method: 'HEAD' });
  if (!probe.ok) throw new Error(`${game.title}: archive unavailable (HTTP ${probe.status})`);
  const total = contentLength(probe) || game.size || 0;
  const parts = [];
  let received = 0;
  if (probe.headers.get('Accept-Ranges') === 'bytes' && total) {
    for (let start = 0; start < total; start += CHUNK) {
      const end = Math.min(start + CHUNK, total) - 1;
      const res = await fetch(url, { headers: { Range: `bytes=${start}-${end}` } });
      if (res.status !== 206) throw new Error('Archive host ignored the range request');
      const buffer = await res.arrayBuffer();
      if (buffer.byteLength !== end - start + 1) throw new Error('Short range response');
      parts.push(buffer);
      received += buffer.byteLength;
      onProgress(received, total);
    }
  } else {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`${game.title}: archive unavailable (HTTP ${res.status})`);
    if (!res.body) { const blob = await res.blob(); onProgress(blob.size, blob.size); return blob; }
    const reader = res.body.getReader();
    let reported = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      parts.push(value);
      received += value.byteLength;
      if (received - reported >= 1024 ** 2) { reported = received; onProgress(received, total || received); }
    }
    onProgress(received, total || received);
  }
  if (total && received !== total) throw new Error(`Truncated archive: ${received} of ${total} bytes`);
  return new Blob(parts, { type: 'application/zip' });
}
