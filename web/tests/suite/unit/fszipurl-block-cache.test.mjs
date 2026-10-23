import {test} from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const cpp = readFileSync('source/io/fszipurl.cpp', 'utf8');
const launcher = readFileSync('project/emscripten/wine64-launcher.js', 'utf8');

test('fszipurl uses 2MiB blocks with a 512MiB LRU', () => {
  const block = /constexpr U64 BLOCK_SIZE = (\d+) \* 1024 \* 1024;/.exec(cpp);
  const max = /MAX_CACHED_BLOCKS = (\d+); \/\/ (\d+) x 2MB = 512MB/.exec(cpp);
  assert.ok(block, 'BLOCK_SIZE constant');
  assert.ok(max, 'MAX_CACHED_BLOCKS constant');
  assert.equal(Number(block[1]), 2);
  assert.equal(Number(max[1]), 256);
  assert.equal(Number(block[1]) * Number(max[1]), 512);
  assert.match(cpp, /2MB blocks/);
  assert.match(cpp, /cache capped at 512MB/);
  assert.match(cpp, /\(evicted \* BLOCK_SIZE\) >> 20/);
});

test('launcher read-ahead handles 2MiB ranges and caches the next block', async () => {
  const start = launcher.indexOf('    (function installLazyZipPrefetch()');
  const end = launcher.indexOf('    })();', start) + '    })();'.length;
  assert.ok(start >= 0 && end > start, 'read-ahead closure');
  const total = 8 * 1024 * 1024;
  const calls = [];
  const realFetch = globalThis.fetch;
  const realFlag = globalThis.__bwLazyZipPrefetch;
  try {
    globalThis.__bwLazyZipPrefetch = false;
    globalThis.fetch = async (url, init) => {
      const match = /^bytes=(\d+)-(\d+)$/.exec(init.headers.Range);
      const s = Number(match[1]);
      const e = Number(match[2]);
      calls.push([s, e]);
      if (s >= total) return new Response(null, {status: 416, headers: {'Content-Range': `bytes */${total}`}});
      return new Response(Buffer.alloc(e - s + 1, 7), {
        status: 206,
        headers: {'Content-Range': `bytes ${s}-${e}/${total}`},
      });
    };
    vm.runInThisContext(launcher.slice(start, end));
    const first = await fetch('http://test/game.zip', {
      headers: {Range: 'bytes=0-2097151'}, cache: 'no-store',
    });
    assert.equal(first.status, 206);
    assert.equal((await first.arrayBuffer()).byteLength, 2 * 1024 * 1024);
    assert.deepEqual(calls, [[0, 2097151], [2097152, 4194303], [4194304, 6291455]]);
    await new Promise(resolve => setImmediate(resolve));
    calls.length = 0;
    const second = await fetch('http://test/game.zip', {
      headers: {Range: 'bytes=2097152-4194303'}, cache: 'no-store',
    });
    assert.equal(second.status, 206);
    assert.equal((await second.arrayBuffer()).byteLength, 2 * 1024 * 1024);
    assert.deepEqual(calls, []);
  } finally {
    globalThis.fetch = realFetch;
    globalThis.__bwLazyZipPrefetch = realFlag;
  }
});
