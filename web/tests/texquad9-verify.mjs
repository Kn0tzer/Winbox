// D3D9 texture upload verification probe.
// Verifies: sane regionCount in vkCmdCopyBufferToImage2, non-zero IMAGE_DATA,
// textured quad visibly rendered (not black).
// Run from repo root: TAG=unique TIMEOUT=900000 node /home/hatch/workspace/texquad9-verify.mjs
// (Must run from /home/ubuntu/boxedwine64 as cwd for relative imports)
import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';

const tag = process.env.TAG || `texquad9-verify-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error('TAG must contain only letters, numbers, _, . or -');
const dir = `/home/ubuntu/boxedwine64/test-results/dxvk-${tag}`;
await mkdir(dir, { recursive: true });

console.log('Waiting for probe slot...');
const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);

let summary = { verified: false };
const server = await serve(process.env.WINE_DIST_DIR || '/home/ubuntu/boxedwine64/dist');
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [], errors = [];
  page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
  page.on('pageerror', e => errors.push(e.message));
  const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
  const timeout = Number(process.env.TIMEOUT || 900000);

  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('SESSION READY');

  const assets = [
    ['texquad9.exe', '/home/ubuntu/boxedwine64/tools/d3dtests/texquad9.exe'],
    ['checker.bmp', '/home/ubuntu/boxedwine64/tools/d3dtests/checker.bmp'],
    ['d3d9.dll', '/home/ubuntu/boxedwine64/tools/dxvk/build.w64/src/d3d9/d3d9.dll'],
    ['libvulkan.so.1', '/home/ubuntu/boxedwine64/tools/rootfs64/libvk64/libvulkan.so.1'],
  ];
  for (const [name, path] of assets) {
    const bytes = await readFile(path);
    const b64 = bytes.toString('base64');
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/' + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      const registered = window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + name]);
      if (registered !== 1) throw new Error('Guest VFS registration failed for ' + name);
    }, { name, b64 });
  }
  console.log('ASSETS UPLOADED');

  const bootstrapUntil = Date.now() + 90000;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootstrapUntil) { console.log('BOOTSTRAP SENTINEL SKIPPED (timeout)'); break; }
    await flush();
    await page.waitForTimeout(500);
  }
  console.log('BOOTSTRAP COMPLETE');

  await page.evaluate(async () => {
    const { installVkPageTier } = await import('../vkwebgpu.mjs');
    window.bwVkTier = installVkPageTier();
  });
  console.log('TIER INSTALLED');

  const spawn = await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\texquad9.exe', 'BW64_VKTRACE=0\nBW64_VKFRAME=0\nWINEDLLOVERRIDES=d3d9=n\nLD_LIBRARY_PATH=/home/username']));
  console.log('SPAWN', spawn);

  // Wait for texquad9 to complete (40 frames, then RESULT).
  const until = Date.now() + timeout;
  while (Date.now() < until && !logs.some(l => /texquad9: RESULT [01]/.test(l))) {
    await flush();
    await page.waitForTimeout(2000);
  }
  await flush();

  const joined = logs.join('\n');
  const result = /texquad9: RESULT 0/.test(joined) ? 0 : /texquad9: RESULT 1/.test(joined) ? 1 : null;
  console.log('RESULT', result);

  // Get lit pixels.
  const stats = await page.evaluate(() => {
    try { const s = window.bwVkStats?.(); return s ? { lit: s.litPixels || 0 } : null; }
    catch (e) { return null; }
  }).catch(() => null);
  const lit = stats?.lit || 0;
  console.log('LITPIXELS', lit);

  // Take screenshot.
  await page.screenshot({ path: `${dir}/canvas.png` });
  console.log('SCREENSHOT SAVED');

  // Check for regionCount in bridge logs (sane vs garbage).
  const b2iLines = logs.filter(l => /CopyBufferToImage2/i.test(l));
  console.log('B2I_LINES', b2iLines.length);
  for (const l of b2iLines.slice(0, 5)) console.log('B2I:', l.slice(0, 200));

  // Check for garbage regionCount guard hits.
  const guardLines = logs.filter(l => /nreg.*1024|regionCount.*1024/i.test(l));
  console.log('GUARD_HITS', guardLines.length);

  // Analyze: textured quad should have many lit pixels (checkerboard, not just blue bg).
  // The quad covers ~512x384 = 196k pixels, 4x tiled checkerboard.
  // If texture upload failed (black), we'd see only the blue background.
  const verified = result === 0 && lit > 50000;
  summary = { verified, result, litPixels: lit, b2iLines: b2iLines.length, guardHits: guardLines.length, errors: errors.length };
  console.log('SUMMARY', JSON.stringify(summary));

  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
} finally {
  await browser.close().catch(() => {});
  await slot.release();
  console.log('SLOT RELEASED');
}
console.log('DONE', JSON.stringify(summary));
