import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

// Diagnostic probe for wineboot c00000e5 failure
import { chromium } from '@playwright/test';
import { serve } from '../../../tools/browser/serve.mjs';
import { writeFile, mkdir } from 'node:fs/promises';
import { acquireProbeSlot } from '../probe-slots.mjs';

const TAG = process.env.PROBE_TAG || '20261007-wbdiag4';
const dir = `${diagnosticsRepoRoot}/test-results/${TAG}`;
await mkdir(dir, { recursive: true });

const slot = await acquireProbeSlot();
try {
  const server = await serve('dist');
  const browser = await chromium.launch({
    executablePath: '/usr/local/bin/chromium', headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
      '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [];
  page.on('console', m => logs.push(m.type() + ': ' + m.text()));
  page.on('pageerror', e => logs.push('PAGEERROR: ' + e.message));
  const flush = () => writeFile(dir + '/console.log', logs.join('\n'));
  try {
    await page.goto('http://127.0.0.1:' + server.address().port + '/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&arch=x86&session=1&novideo=1&persist=0');
    await page.waitForFunction(() => window.bwRuntime && window.bwRuntime.ready && window.bwRuntime.ready() === true, null, { timeout: 300000 });
    console.log('SESSION READY');
    const stripAnsi = s => s.replace(/\x1b\[[0-9;?]*[a-zA-Z]/g, '');
    for (let i = 0; i < 45; i++) {
      await page.waitForTimeout(2000);
      await flush();
      if (logs.some(l => /^log: vkboot\s*$/.test(stripAnsi(l)))) {
        console.log('VKBOOT SEEN');
        break;
      }
    }
    const fsCheck = await page.evaluate(() => {
      try {
        const fs = window.bwRuntime.fs();
        const out = {};
        try { out.wineFixed = fs.readdir('/root/tmp/wine-fixed'); } catch(e) { out.wineFixed = 'ERR:' + e.message; }
        try {
          const entries = fs.readdir('/root/tmp/wine-fixed');
          for (const e of entries) {
            if (e.indexOf('server-') === 0) {
              try { out['serverDir_' + e] = fs.readdir('/root/tmp/wine-fixed/' + e); } catch(ex) { out['serverDir_' + e] = 'ERR:' + ex.message; }
            }
          }
        } catch(e) {}
        return out;
      } catch(e) { return {error: e.message}; }
    });
    await writeFile(dir + '/fscheck.json', JSON.stringify(fsCheck, null, 2));
    console.log('FSCHECK ' + JSON.stringify(fsCheck));
    await flush();
  } catch (e) {
    console.log('FAIL ' + String(e).slice(0, 300));
  } finally {
    await browser.close(); server.close();
  }
} finally {
  await slot.release();
}
console.log('DONE');
