import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

// Minimal vkboot bootstrap verification probe
import { chromium } from '@playwright/test';
import { serve } from '../../../tools/browser/serve.mjs';
import { mkdir, writeFile, unlink } from 'node:fs/promises';
import { writeFileSync } from 'node:fs';

const tag = 'vkboot-minimal-verify';
const dir = (diagnosticsRepoRoot + '/test-results/') + tag;
await mkdir(dir, { recursive: true });
writeFileSync('/tmp/bw64-probe.lock', 'vkboot-minimal');

let server, browser;
const logs = [];
const flush = () => writeFile(dir + '/console.log', logs.join('\n'));

try {
  server = await serve('dist');
  console.log('Server on port ' + server.address().port);
  browser = await chromium.launch({
    executablePath: '/usr/local/bin/chromium', headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
      '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
  });
  const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
  page.on('console', m => { logs.push(m.type() + ': ' + m.text()); });
  page.on('pageerror', e => logs.push('pageerror: ' + e.message));

  const url = 'http://127.0.0.1:' + server.address().port + '/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=0&persist=0';
  console.log('Navigating...');
  await page.goto(url);

  const start = Date.now();
  const timeout = 180000;
  let found = false;
  let lastLog = 0;
  while (Date.now() - start < timeout) {
    await page.waitForTimeout(2000);
    await flush();
    if (logs.some(l => /^log: vkboot\s*$/.test(l))) { found = true; break; }
    if (logs.some(l => l.includes('SetupDiCreateDeviceInfoList') && l.includes('aborting'))) {
      console.log('FAIL: setupapi abort detected');
      break;
    }
    if (logs.length !== lastLog) { lastLog = logs.length; console.log('Lines: ' + logs.length); }
  }

  await flush();
  const summary = {
    passed: found, vkbootFound: found, logLines: logs.length,
    elapsedMs: Date.now() - start,
    setupapiAbort: logs.some(l => l.includes('SetupDiCreateDeviceInfoList') && l.includes('aborting')),
  };
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2));
  console.log('RESULT: ' + JSON.stringify(summary));
} catch (e) {
  console.log('ERROR: ' + e.message);
  await flush();
  await writeFile(dir + '/summary.json', JSON.stringify({ passed: false, error: e.message }));
} finally {
  try { await browser.close(); } catch (e) {}
  try { server.close(); } catch (e) {}
  try { await unlink('/tmp/bw64-probe.lock'); } catch (e) {}
}
