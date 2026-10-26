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
// One download per archive at a time. A second request for the same game
// aborts the in-flight one, so a double click cannot leave two range loops
// racing for the same 100+ MB stream (or the first one reporting stale
// progress over the newer card).
const inFlight = new Map();
function superseded(title) {
  return new Error(`${title}: download cancelled by a newer request`);
}
/**
 * Downloads a built-in archive, preferring byte ranges and falling back to one
 * streaming GET. onProgress(receivedBytes, totalBytes) drives the card meter.
 * The result is a Blob, exactly what the ZIP file input would have produced.
 * @param {{id?: string, archive: string, size?: number, title: string}} game
 * @param {(received: number, total: number) => void} [onProgress]
 */
export async function fetchGameArchive(game, onProgress = () => {}) {
  const key = game.archive || String(game.id || game.title);
  const previous = inFlight.get(key);
  if (previous) previous.abort();
  const controller = new AbortController();
  inFlight.set(key, controller);
  const { signal } = controller;
  try {
    return await stream(game, onProgress, signal);
  } catch (e) {
    if (signal.aborted) throw superseded(game.title);
    throw e;
  } finally {
    if (inFlight.get(key) === controller) inFlight.delete(key);
  }
}
// Stall watchdog for one request (same shape as the lazy-fetch fix in
// source/io/fszipurl.cpp): an AbortController whose timer is a STALL
// deadline, not a total deadline — progress() restarts the clock on every
// chunk of arriving bytes, so a slow-but-progressing download is never
// aborted, while a connection that goes silent for the whole window is
// aborted and reported loudly instead of hanging the download forever.
// The watchdog signal also fires when the parent (supersede) signal
// aborts, so the existing cancellation behaviour is preserved.
// BW64_FETCH_STALL_MS on the global object overrides the window
// (unit-test seam).
function stallWatchdog(parentSignal, label) {
  const stallMs = Number(globalThis.BW64_FETCH_STALL_MS) > 0
    ? Number(globalThis.BW64_FETCH_STALL_MS) : 60_000;
  const controller = new AbortController();
  let timer = null;
  let stalled = false;
  const onParentAbort = () => controller.abort();
  if (parentSignal) {
    if (parentSignal.aborted) controller.abort();
    else parentSignal.addEventListener('abort', onParentAbort, { once: true });
  }
  const arm = () => {
    if (timer) clearTimeout(timer);
    timer = setTimeout(() => {
      stalled = true;
      console.error(`games: fetch STALL ${label} (no progress for ${stallMs}ms)`);
      controller.abort();
    }, stallMs);
  };
  arm();
  return {
    signal: controller.signal,
    get stalled() { return stalled; },
    progress() { if (!stalled) arm(); },
    dispose() {
      if (timer) clearTimeout(timer);
      if (parentSignal) parentSignal.removeEventListener('abort', onParentAbort);
    },
    throwIfStalled(title) {
      if (stalled) throw new Error(`${title}: download stalled (no progress for ${stallMs}ms)`);
    },
  };
}
// Reads a response body to completion, restarting the watchdog's stall
// clock on every chunk. Falls back to arrayBuffer() (deadline still
// armed, no progress signal) when no readable stream is available.
async function readBodyWithWatchdog(res, watchdog, onChunk) {
  if (!res.body || typeof res.body.getReader !== 'function') {
    const buffer = await res.arrayBuffer();
    onChunk(buffer.byteLength);
    return [new Uint8Array(buffer)];
  }
  const reader = res.body.getReader();
  const parts = [];
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    watchdog.progress();
    parts.push(value);
    onChunk(value.byteLength);
  }
  return parts;
}
async function stream(game, onProgress, signal) {
  const url = new URL(game.archive, location.href).href;
  const probeWatchdog = stallWatchdog(signal, url);
  let probe;
  try {
    probe = await fetch(url, { method: 'HEAD', signal: probeWatchdog.signal });
  } catch (e) {
    probeWatchdog.throwIfStalled(game.title);
    throw e;
  } finally {
    probeWatchdog.dispose();
  }
  if (!probe.ok) throw new Error(`${game.title}: archive unavailable (HTTP ${probe.status})`);
  const total = contentLength(probe) || game.size || 0;
  const parts = [];
  let received = 0;
  if (probe.headers.get('Accept-Ranges') === 'bytes' && total) {
    for (let start = 0; start < total; start += CHUNK) {
      const end = Math.min(start + CHUNK, total) - 1;
      const watchdog = stallWatchdog(signal, `${url} bytes=${start}-${end}`);
      try {
        const res = await fetch(url, { headers: { Range: `bytes=${start}-${end}` }, signal: watchdog.signal });
        if (res.status !== 206) throw new Error('Archive host ignored the range request');
        let chunkBytes = 0;
        const chunkParts = await readBodyWithWatchdog(res, watchdog, (n) => { chunkBytes += n; });
        if (chunkBytes !== end - start + 1) throw new Error('Short range response');
        parts.push(...chunkParts);
        received += chunkBytes;
        onProgress(received, total);
      } catch (e) {
        watchdog.throwIfStalled(game.title);
        throw e;
      } finally {
        watchdog.dispose();
      }
    }
  } else {
    const watchdog = stallWatchdog(signal, url);
    try {
      const res = await fetch(url, { signal: watchdog.signal });
      if (!res.ok) throw new Error(`${game.title}: archive unavailable (HTTP ${res.status})`);
      if (!res.body) { const blob = await res.blob(); onProgress(blob.size, blob.size); return blob; }
      let reported = 0;
      await readBodyWithWatchdog(res, watchdog, (n) => {
        received += n;
        if (received - reported >= 1024 ** 2) { reported = received; onProgress(received, total || received); }
      }).then((streamed) => { parts.push(...streamed); });
      onProgress(received, total || received);
    } catch (e) {
      watchdog.throwIfStalled(game.title);
      throw e;
    } finally {
      watchdog.dispose();
    }
  }
  if (total && received !== total) throw new Error(`Truncated archive: ${received} of ${total} bytes`);
  return new Blob(parts, { type: 'application/zip' });
}
