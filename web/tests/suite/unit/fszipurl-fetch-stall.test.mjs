// web/tests/suite/unit/fszipurl-fetch-stall.test.mjs — lazy-fetch stall fix
// (runtime.md "Lazy archive fetch stall" row, Celeste 113MB-then-535s-silence).
//
// Before the fix, a never-settling fetch() hung the guest forever and
// silently: bw64StartFetch had no AbortController/timeout, the launcher
// prefetch wrapper's pending map could wedge on a stalled job, same-block
// waiters waited on fetchCv with no deadline, and web/games.mjs shared the
// no-timeout shape. These tests prove the stall path now terminates loudly
// at every layer, and that slow-but-progressing fetches are NOT aborted
// (the watchdog is a stall deadline reset by progress, not a total cap).
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {fetchGameArchive} from '../../../games.mjs';

const cpp = readFileSync('source/io/fszipurl.cpp', 'utf8');
const launcher = readFileSync('project/emscripten/wine64-launcher.js', 'utf8');
const gamesSource = readFileSync('web/games.mjs', 'utf8');

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function waitFor(cond, timeoutMs = 5000) {
  const t0 = Date.now();
  while (!cond()) {
    if (Date.now() - t0 > timeoutMs) throw new Error('waitFor timed out');
    await sleep(5);
  }
}
function captureErrors() {
  const lines = [];
  const real = console.error;
  console.error = (...args) => { lines.push(args.map(String).join(' ')); };
  return {lines, restore() { console.error = real; }};
}

// ---------------------------------------------------------------------------
// Source-level pins for the C++ side (the EM_JS/C++ cannot run under node).
// ---------------------------------------------------------------------------
test('fszipurl C++: stall watchdog, waiter deadline and bounded retry are present', () => {
  assert.match(cpp, /AbortController/, 'bw64StartFetch must wire an AbortController');
  assert.match(cpp, /BW64_FETCH_STALL_MS/, 'stall window must have the test-seam override');
  assert.match(cpp, /fszipurl: fetch STALL url=/, 'stall log must name url+offset');
  assert.match(cpp, /bytesSoFar=/, 'stall log must carry bytes so far');
  assert.match(cpp, /BW64_FETCH_STALLED = -2/, 'stall result code mirrored in C++');
  assert.match(cpp, /range fetch STALLED/, 'fetchRaw must log stalls distinctly');
  assert.match(cpp, /range fetch retry %d\/3/, 'fetchRaw must retry transient failures (bounded)');
  assert.match(cpp, /fetchCv\.wait_until/, 'same-block waiters must wait with a deadline');
  assert.match(cpp, /FETCH_WAIT_DEADLINE\{240\}/, 'waiter deadline constant');
  assert.match(cpp, /STALL waiting for in-flight block/, 'waiter stall must log loudly');
});

test('games.mjs + launcher: stall watchdogs are present', () => {
  assert.match(gamesSource, /stallWatchdog/, 'games.mjs must route requests through a stall watchdog');
  assert.match(gamesSource, /games: fetch STALL/, 'games.mjs stall must log loudly');
  assert.match(gamesSource, /download stalled/, 'games.mjs stall must surface as a loud error');
  assert.match(launcher, /BW64_PREFETCH_STALL_MS/, 'launcher watchdog must have the test-seam override');
  assert.match(launcher, /bw64-prefetch: fetch STALL/, 'launcher stall must log loudly');
  assert.match(launcher, /init\.signal = ctrl\.signal/, 'launcher watchdog must abort the native fetch');
});

// ---------------------------------------------------------------------------
// bw64StartFetch (EM_JS body extracted from fszipurl.cpp and run for real).
// ---------------------------------------------------------------------------
function extractStartFetch() {
  const sig = cpp.indexOf('EM_JS(void, bw64StartFetch,');
  assert.ok(sig >= 0, 'bw64StartFetch EM_JS found');
  const open = cpp.indexOf('), {', sig);
  const close = cpp.indexOf('\n});', open);
  assert.ok(open >= 0 && close > open, 'EM_JS body bounds');
  const body = cpp.slice(open + '), {'.length, close);
  return new Function('ctxAddr', 'jobAddr', 'url', 'start', 'endIncl', 'dstAddr', 'want', body);
}
function startFetchHarness(fetchImpl, stallMs) {
  const done = [];
  const heapWrites = [];
  const saved = {
    fetch: globalThis.fetch,
    UTF8ToString: globalThis.UTF8ToString,
    _bw64FetchDone: globalThis._bw64FetchDone,
    HEAPU8: globalThis.HEAPU8,
    BW64_FETCH_STALL_MS: globalThis.BW64_FETCH_STALL_MS,
  };
  globalThis.UTF8ToString = (s) => s;
  globalThis._bw64FetchDone = (ctx, job, result) => { done.push(result); };
  globalThis.HEAPU8 = {set(arr, addr) { heapWrites.push([addr, Uint8Array.from(arr)]); }};
  globalThis.BW64_FETCH_STALL_MS = stallMs;
  globalThis.fetch = fetchImpl;
  return {
    done, heapWrites,
    restore() { Object.assign(globalThis, saved); },
  };
}

test('bw64StartFetch: a never-settling fetch is aborted and settles STALLED, loudly, once', async () => {
  const cap = captureErrors();
  let capturedInit = null;
  const h = startFetchHarness((url, init) => {
    capturedInit = init;
    return new Promise(() => {}); // body stalls: never settles, ignores abort
  }, 50);
  try {
    const fn = extractStartFetch();
    fn(111, 222, 'http://test/game.zip', 2097152, 4194303, 4096, 2097152);
    await waitFor(() => h.done.length === 1);
    assert.equal(h.done[0], -2, 'stall must settle the job with BW64_FETCH_STALLED (-2)');
    assert.ok(capturedInit.signal, 'fetch must receive the AbortController signal');
    assert.equal(capturedInit.signal.aborted, true, 'watchdog must abort the fetch');
    const log = cap.lines.join('\n');
    assert.match(log, /fszipurl: fetch STALL url=http:\/\/test\/game\.zip off=2097152/);
    assert.match(log, /bytesSoFar=0/);
    await sleep(120);
    assert.equal(h.done.length, 1, 'job must settle exactly once');
    assert.equal(cap.lines.length, 1, 'stall must log exactly once per stall');
  } finally {
    h.restore();
    cap.restore();
  }
});

test('bw64StartFetch: slow-but-progressing fetch is NOT aborted (stall clock resets per chunk)', async () => {
  const cap = captureErrors();
  const chunks = [0, 1, 2, 3, 4, 5].map((i) => new Uint8Array([i, i, i, i, i]));
  let ci = 0;
  const reader = {
    read: async () => {
      await sleep(40); // each gap < 100ms stall window, total 240ms > window
      if (ci >= chunks.length) return {done: true};
      return {done: false, value: chunks[ci++]};
    },
  };
  const h = startFetchHarness(async () => ({status: 206, body: {getReader: () => reader}}), 100);
  try {
    const fn = extractStartFetch();
    fn(111, 222, 'http://test/game.zip', 0, 29, 8192, 30);
    await waitFor(() => h.done.length === 1);
    assert.equal(h.done[0], 30, 'all streamed bytes must be delivered');
    assert.equal(h.heapWrites.length, 1);
    assert.equal(h.heapWrites[0][0], 8192, 'bytes land at dstAddr');
    assert.deepEqual([...h.heapWrites[0][1]], chunks.flatMap((c) => [...c]));
    assert.equal(cap.lines.length, 0, 'no stall may be logged for a progressing fetch');
  } finally {
    h.restore();
    cap.restore();
  }
});

test('bw64StartFetch: settled failures keep the existing result codes', async () => {
  const cap = captureErrors();
  const h1 = startFetchHarness(async () => { throw new Error('network down'); }, 5000);
  try {
    extractStartFetch()(1, 2, 'http://test/a.zip', 0, 99, 0, 100);
    await waitFor(() => h1.done.length === 1);
    assert.equal(h1.done[0], -1, 'generic failure stays -1');
  } finally { h1.restore(); }
  const h2 = startFetchHarness(async () => ({status: 404}), 5000);
  try {
    extractStartFetch()(1, 2, 'http://test/a.zip', 0, 99, 0, 100);
    await waitFor(() => h2.done.length === 1);
    assert.equal(h2.done[0], -404, 'HTTP status stays -status (permanent-error path)');
  } finally { h2.restore(); cap.restore(); }
});

test('bw64StartFetch: arrayBuffer fallback (no readable stream) still delivers bytes', async () => {
  const h = startFetchHarness(async () => ({
    status: 206,
    arrayBuffer: async () => new Uint8Array([7, 8, 9]).buffer,
  }), 5000);
  try {
    extractStartFetch()(1, 2, 'http://test/a.zip', 0, 2, 1024, 3);
    await waitFor(() => h.done.length === 1);
    assert.equal(h.done[0], 3);
    assert.deepEqual([...h.heapWrites[0][1]], [7, 8, 9]);
  } finally { h.restore(); }
});

// ---------------------------------------------------------------------------
// Launcher prefetch wrapper: a stalled job must reject, clear pending, and
// let the next fetch of the same block start fresh.
// ---------------------------------------------------------------------------
test('launcher prefetch: stalled block fetch is aborted, logged, and does not wedge the block', async () => {
  const cap = captureErrors();
  const start = launcher.indexOf('    (function installLazyZipPrefetch()');
  const end = launcher.indexOf('    })();', start) + '    })();'.length;
  assert.ok(start >= 0 && end > start, 'read-ahead closure');
  const realFetch = globalThis.fetch;
  const realFlag = globalThis.__bwLazyZipPrefetch;
  const ranges = [];
  let firstCall = true;
  try {
    globalThis.__bwLazyZipPrefetch = false;
    globalThis.BW64_PREFETCH_STALL_MS = 50;
    globalThis.fetch = (url, init) => {
      ranges.push(init.headers.Range);
      if (firstCall) {
        firstCall = false;
        return new Promise((resolve, reject) => {
          init.signal.addEventListener('abort', () => {
            const e = new Error('The operation was aborted');
            e.name = 'AbortError';
            reject(e);
          });
        });
      }
      return Promise.resolve(new Response(Buffer.alloc(2097152, 7), {
        status: 206,
        headers: {'Content-Range': 'bytes 2097152-4194303/8388608'},
      }));
    };
    vm.runInThisContext(launcher.slice(start, end));
    await assert.rejects(
      fetch('http://test/game.zip', {headers: {Range: 'bytes=2097152-4194303'}, cache: 'no-store'}),
      /aborted/i,
      'stalled prefetch job must reject instead of hanging',
    );
    assert.match(cap.lines.join('\n'), /bw64-prefetch: fetch STALL url=http:\/\/test\/game\.zip off=2097152/);
    const second = await fetch('http://test/game.zip', {
      headers: {Range: 'bytes=2097152-4194303'}, cache: 'no-store',
    });
    assert.equal(second.status, 206);
    assert.equal((await second.arrayBuffer()).byteLength, 2097152);
    assert.equal(
      ranges.filter((r) => r === 'bytes=2097152-4194303').length, 2,
      'the retry must issue a fresh native fetch (pending was cleared), not join the dead job',
    );
  } finally {
    globalThis.fetch = realFetch;
    globalThis.__bwLazyZipPrefetch = realFlag;
    delete globalThis.BW64_PREFETCH_STALL_MS;
    cap.restore();
  }
});

// ---------------------------------------------------------------------------
// web/games.mjs upfront download: same stall shape, same loud termination.
// ---------------------------------------------------------------------------
function headersStub(map) {
  return {get: (name) => (name in map ? map[name] : null)};
}
test('games.mjs: a stalled range chunk aborts loudly instead of hanging the download', async () => {
  const cap = captureErrors();
  const realFetch = globalThis.fetch;
  const realLocation = globalThis.location;
  try {
    globalThis.location = {href: 'http://test/launcher.html'};
    globalThis.BW64_FETCH_STALL_MS = 50;
    globalThis.fetch = async (url, init) => {
      if (init && init.method === 'HEAD') {
        return {ok: true, status: 200, headers: headersStub({'Content-Length': '4', 'Accept-Ranges': 'bytes'})};
      }
      // Body stalls forever; like a real fetch Response, a pending read()
      // rejects when the request's AbortSignal fires.
      return {
        status: 206,
        body: {
          getReader: () => ({
            read: () => new Promise((resolve, reject) => {
              init.signal.addEventListener('abort', () => {
                const e = new Error('The operation was aborted');
                e.name = 'AbortError';
                reject(e);
              });
            }),
          }),
        },
      };
    };
    await assert.rejects(
      Promise.race([
        fetchGameArchive({archive: 'games/stall.zip', title: 'Stall Game', size: 4}),
        sleep(5000).then(() => { throw new Error('download hung past 5s: stall watchdog missing'); }),
      ]),
      /download stalled/,
    );
    assert.match(cap.lines.join('\n'), /games: fetch STALL/);
  } finally {
    globalThis.fetch = realFetch;
    globalThis.location = realLocation;
    delete globalThis.BW64_FETCH_STALL_MS;
    cap.restore();
  }
});

test('games.mjs: a progressing chunked download still completes', async () => {
  const realFetch = globalThis.fetch;
  const realLocation = globalThis.location;
  try {
    globalThis.location = {href: 'http://test/launcher.html'};
    globalThis.BW64_FETCH_STALL_MS = 5000;
    globalThis.fetch = async (url, init) => {
      if (init && init.method === 'HEAD') {
        return {ok: true, status: 200, headers: headersStub({'Content-Length': '4', 'Accept-Ranges': 'bytes'})};
      }
      let step = 0;
      return {
        status: 206,
        body: {
          getReader: () => ({
            read: async () => {
              step += 1;
              if (step === 1) return {done: false, value: new Uint8Array([1, 2])};
              if (step === 2) return {done: false, value: new Uint8Array([3, 4])};
              return {done: true};
            },
          }),
        },
      };
    };
    const blob = await fetchGameArchive({archive: 'games/ok.zip', title: 'OK Game', size: 4});
    assert.equal(blob.size, 4);
    assert.deepEqual([...new Uint8Array(await blob.arrayBuffer())], [1, 2, 3, 4]);
  } finally {
    globalThis.fetch = realFetch;
    globalThis.location = realLocation;
    delete globalThis.BW64_FETCH_STALL_MS;
  }
});
