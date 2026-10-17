import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

// Test if the dispatcher patch fixes 32-bit GUI loading
import { chromium } from '@playwright/test';
import { serve } from '../../../tools/browser/serve.mjs';
import { writeFile, mkdir, unlink } from 'node:fs/promises';

const TAG = process.env.PROBE_TAG || '20261007-dispatchfix1';
const dir = (diagnosticsRepoRoot + '/test-results/') + TAG;
await mkdir(dir, { recursive: true });
await writeFile('/tmp/bw64-probe-slots/slot-0', String(process.pid)).catch(() => {});

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
const timeout = 300000;
try {
  await page.goto('http://127.0.0.1:' + server.address().port + '/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&arch=x86&session=1&novideo=1&persist=0&gltrace=0');
  await page.waitForFunction(() => window.bwRuntime && window.bwRuntime.ready && window.bwRuntime.ready() === true, null, { timeout });
  console.log('SESSION READY');
  await page.waitForTimeout(180000);
  await flush();
  const logText = logs.join('\n');
  const summary = {
    tag: TAG,
    user32Loaded: /user32\.dll/i.test(logText),
    win32uLoaded: /win32u\.dll/i.test(logText),
    createWinstation: (logText.match(/create_winstation/gi) || []).length,
    createDesktop: (logText.match(/create_desktop/gi) || []).length,
    bootEventTimeout: (logText.match(/boot event wait timed out/gi) || []).length,
    winebootFailed: (logText.match(/run_wineboot failed/gi) || []).length,
    vkboot: /log: vkboot/.test(logText),
    logLines: logs.length,
  };
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2));
  console.log('SUMMARY: ' + JSON.stringify(summary, null, 2));
} catch (e) {
  console.log('PROBE ERROR: ' + e.message);
  await flush();
} finally {
  await browser.close();
  server.close();
  await unlink('/tmp/bw64-probe-slots/slot-0').catch(() => {});
}
