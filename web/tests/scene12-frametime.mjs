// Minimal frame-time measurement for D3D12 scene12.
// Reuses the frametime-harness timing-hook approach: patch renderer.render
// for page-GPU time, hook tier.chunk for guest frame completions.
import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';

const ROOT = '/home/ubuntu/boxedwine64';
const tag = process.env.TAG || 'scene12-ft1';
const dir = `test-results/frametime-${tag}`;
await mkdir(dir, { recursive: true });

const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);
const server = await serve(`${ROOT}/dist`);
const browser = await chromium.launch({
  executablePath: '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
const logs = [];
page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
page.on('pageerror', e => logs.push(`pageerror: ${e}`));
const timeout = 1800000;

const assets = [
  ['scene12.exe', 'tools/vkd3d/probe/scene12.exe'],
  ['d3d12.dll', 'tools/vkd3d/build.w64/libs/d3d12/d3d12.dll'],
  ['d3d12core.dll', 'tools/vkd3d/build.w64/libs/d3d12core/d3d12core.dll'],
  ['dxgi.dll', 'tools/dxvk/build.w64/src/dxgi/dxgi.dll'],
  ['libvulkan.so.1', 'tools/rootfs64/libvk64/libvulkan.so.1'],
  ['bw64vulkan.dll', 'tools/vkd3d/probe/bw64vulkan.dll'],
  ['vulkan-1.dll', 'tools/vkd3d/probe/bw64vulkan.dll'],
];
const spawnEnv = 'BW64_VKTRACE=0\nBW64_VKFRAME=1\nWINEDLLOVERRIDES=d3d12=n;d3d12core=n;dxgi=n;vulkan-1=n;winevulkan=n\nLD_LIBRARY_PATH=/home/username';

try {
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('SESSION READY');
  for (const [name, rel] of assets) {
    const bytes = await readFile(rel.startsWith('/') ? rel : `${ROOT}/${rel}`);
    const b64 = bytes.toString('base64');
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/' + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + name]);
    }, { name, b64 });
  }
  console.log('ASSETS STAGED');
  const until = Date.now() + timeout;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > until) throw new Error('bootstrap timeout');
    await page.waitForTimeout(500);
  }
  console.log('BOOTSTRAP COMPLETE');
  await page.evaluate(() => {
    window.bwFt = { guest: [], page: [] };
    let r2 = null;
    window.__ftPatchRender = (renderer) => {
      if (!renderer || renderer.__ftPatched) return;
      renderer.__ftPatched = true;
      const _render = renderer.render.bind(renderer);
      let n = 0;
      renderer.render = async (f) => {
        const t0 = performance.now();
        const res = await _render(f);
        const t1 = performance.now();
        n++;
        window.bwFt.page.push({ n, t0, t1, dt: t1 - t0, rendered: !!(res && res.rendered) });
        return res;
      };
    };
    Object.defineProperty(window, 'bwVkRenderer2', {
      configurable: true,
      get() { return r2; },
      set(v) { r2 = v; if (v) window.__ftPatchRender(v); },
    });
  });
  await page.evaluate(async () => {
    const { installVkPageTier } = await import('../vkwebgpu.mjs');
    window.bwVkTier = installVkPageTier();
  });
  await page.evaluate(() => {
    const tier = window.bwVkTier;
    const _chunk = tier.chunk.bind(tier);
    let g = 0;
    tier.chunk = (bytes, flags) => {
      const r = _chunk(bytes, flags);
      let more = 0;
      try { if (bytes && bytes.length >= 8) more = (bytes[6] | (bytes[7] << 8)) & 1; } catch {}
      if (!more) { g++; window.bwFt.guest.push({ n: g, t: performance.now() }); }
      return r;
    };
  });
  console.log('TIMING HOOKS INSTALLED');
  await page.evaluate(([env]) =>
    window.bwRuntime.call('bw64_spawn', ['string', 'string'],
      ['/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\scene12.exe', env]), [spawnEnv]);
  console.log('SPAWNED');
  // Wait for RESULT 0/1 or 20 guest frames
  const done = Date.now() + 1200000;
  for (;;) {
    const st = await page.evaluate(() => ({
      g: window.bwFt.guest.length,
      done: document.title.includes('done'),
    }));
    if (logs.some(l => /scene12: RESULT [01]/.test(l))) break;
    if (Date.now() > done) throw new Error('scene12 timeout');
    await page.waitForTimeout(2000);
  }
  await page.waitForTimeout(3000);
  const data = await page.evaluate(() => ({ ft: window.bwFt }));
  await writeFile(`${dir}/frametimes.json`, JSON.stringify(data.ft));
  await writeFile(`${dir}/console.log`, logs.join('\n'));
  const gs = data.ft.guest.map(g => g.t);
  const dts = gs.slice(1).map((t, i) => t - gs[i]);
  const mean = dts.reduce((a, b) => a + b, 0) / dts.length;
  const sorted = [...dts].sort((a, b) => a - b);
  console.log(`GUEST FRAMES n=${gs.length} mean=${mean.toFixed(1)}ms median=${sorted[Math.floor(sorted.length/2)].toFixed(1)}ms p95=${sorted[Math.floor(sorted.length*0.95)].toFixed(1)}ms`);
  const ps = data.ft.page.filter(p => p.rendered).map(p => p.dt);
  if (ps.length) {
    const pm = ps.reduce((a, b) => a + b, 0) / ps.length;
    console.log(`PAGE RENDER n=${ps.length} mean=${pm.toFixed(1)}ms`);
  }
  console.log('DONE');
} finally {
  await writeFile(`${dir}/console.log`, logs.join('\n')).catch(() => {});
  await browser.close().catch(() => {});
  await server.close();
  await slot.release();
}
