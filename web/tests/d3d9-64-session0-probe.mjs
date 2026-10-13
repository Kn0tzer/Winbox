// 64-bit D3D9 probe via session=0 + gamezip (mirror of d3d9-32-session0-probe).
// Stages DXVK 64-bit d3d9.dll + libvk64 + d3dconf9.exe in a zip, runs WITHOUT
// ?arch (default = 64-bit), polls bwVkStats().litPixels.
// Purpose: isolate APP vs ARCH for the missing-pipelines question.
import { chromium } from '@playwright/test';
import { serve } from '/home/ubuntu/boxedwine64/tools/browser/serve.mjs';
import { mkdir, writeFile, stat } from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '/home/ubuntu/boxedwine64/web/tests/build-lock.mjs';
import { execSync } from 'node:child_process';

const repo = '/home/ubuntu/boxedwine64';
const tag = process.env.TAG || `d3d9-64-s0-${Date.now()}`;
const dir = `${repo}/test-results/${tag}`;
await mkdir(dir, { recursive: true });

const zipPath = `${dir}/d3d9-64-test.zip`;
execSync(
  `rm -rf /tmp/d3d964 && mkdir -p /tmp/d3d964/home/username/apps` +
  ` && cp ${repo}/tools/d3dtests/d3dconf9.exe /tmp/d3d964/home/username/apps/` +
  ` && cp ${repo}/tools/dxvk/build.w64/src/d3d9/d3d9.dll /tmp/d3d964/home/username/apps/` +
  ` && cp ${repo}/tools/rootfs64/libvk64/libvulkan.so.1 /tmp/d3d964/home/username/apps/` +
  ` && (cd /tmp/d3d964 && zip -q ${zipPath} home/username/apps/d3dconf9.exe home/username/apps/d3d9.dll home/username/apps/libvulkan.so.1)` +
  ` && rm -rf /tmp/d3d964 && unzip -l ${zipPath}`,
  { stdio: 'inherit' });
const zipSize = (await stat(zipPath)).size;

await waitForBuildIdle();
const slot = await acquireProbeSlot();
console.log('SLOT', slot.index);

let browser, server;
const logs = [];
try {
  server = await serve(repo);
  const port = server.address().port;
  console.log('SERVER', port);

  browser = await chromium.launch({
    executablePath: '/usr/local/bin/chromium',
    headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
      '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader',
      '--enable-features=Vulkan'],
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logFile = createWriteStream(`${dir}/console.log`);
  page.on('console', m => { const l = `${m.type()}: ${m.text()}`; logs.push(l); logFile.write(l + '\n'); });
  page.on('pageerror', e => { const l = `pageerror: ${e.message}`; logs.push(l); logFile.write(l + '\n'); });

  const params = new URLSearchParams({
    session: '0',
    persist: '0',
    fresh: '1',
    gamezip: `http://127.0.0.1:${port}/test-results/${tag}/d3d9-64-test.zip`,
    gamezipsize: String(zipSize),
    exe: 'd3dconf9.exe',
    dlloverrides: 'd3d9=n',
    ldpath: '/home/username/apps',
  });
  const url = `http://127.0.0.1:${port}/dist/runtime/index.html?${params}`;
  console.log('URL', url.slice(0, 120) + '...');
  await page.goto(url, { timeout: 60000 });
  console.log('NAVIGATED');

  const getStats = () => page.evaluate(() => {
    try {
      const s = window.bwVkStats?.();
      return s ? { lit: s.litPixels || 0, rendered: s.v2?.rendered || 0 } : null;
    } catch (e) { return null; }
  }).catch(() => null);

  let bestLit = 0, bestRendered = 0, pipeLane = false;
  const deadline = Date.now() + 10 * 60 * 1000; // 10 min
  while (Date.now() < deadline) {
    await page.waitForTimeout(5000);
    const joined = logs.join('\n');
    if (!pipeLane && /trap vkCreate(ShaderModule|GraphicsPipelines|RenderPass|Framebuffer)/.test(joined)) {
      pipeLane = true;
      console.log('>>> PIPELINE LANE ACTIVE');
    }
    const st = await getStats();
    if (st) {
      if (st.lit > bestLit) { bestLit = st.lit; console.log('LIT', bestLit, 'RENDERED', st.rendered); }
      bestRendered = Math.max(bestRendered, st.rendered || 0);
    }
    if (bestLit > 100) break;
    if (/Page Fault|pageerror:|abort\(|unreachable|range fetch FAILED/.test(joined)) {
      console.log('FATAL ERROR DETECTED');
      break;
    }
  }

  console.log('FINAL LIT', bestLit, 'RENDERED', bestRendered, 'PIPELANE', pipeLane);
  try { await page.screenshot({ path: `${dir}/render.png` }); console.log('SCREENSHOT OK'); }
  catch (e) { console.log('SCREENSHOT FAIL', String(e.message).slice(0, 80)); }

  logFile.end();
  const joined = logs.join('\n');
  const summary = {
    tag, pass: bestLit > 100,
    litPixels: bestLit, rendered: bestRendered, pipeLane,
    d3dOps: (joined.match(/d3dconf9: OP/g) || []).length,
    d3dDone: /d3dconf9: DONE/.test(joined),
    dxvkErrors: logs.filter(l => /dxvk|vulkan.*error|vk64.*error/i.test(l)).slice(0, 10),
    wineErrors: logs.filter(l => /wine:.*err|failed to open/i.test(l)).slice(0, 10),
    logLines: logs.length,
  };
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  console.log('SUMMARY', JSON.stringify(summary));
} catch (e) {
  console.log('PROBE ERROR', String(e.stack || e).slice(0, 500));
} finally {
  try { await browser?.close(); } catch {}
  if (server) await new Promise(r => server.close(r));
  await slot.release();
  console.log('DONE');
}
