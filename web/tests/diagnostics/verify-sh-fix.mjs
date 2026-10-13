import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

// Verify /bin/sh fix
import { chromium } from '@playwright/test';
import { serve } from '../../../tools/browser/serve.mjs';
import { writeFile, mkdir } from 'node:fs/promises';
import { acquireProbeSlot } from '../probe-slots.mjs';
const TAG = process.env.PROBE_TAG || '20261007-shfix-verify1';
const dir = `test-results/${TAG}`;
await mkdir(dir, { recursive: true });
const slot = await acquireProbeSlot();
console.log('Got probe slot', slot.index);
try {
  const server = await serve('dist');
  const browser = await chromium.launch({
    executablePath: '/usr/local/bin/chromium', headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage'],
  });
  const page = await browser.newPage();
  const logs = [];
  page.on('console', m => logs.push(m.type() + ': ' + m.text()));
  const flush = () => writeFile(dir + '/console.log', logs.join('\n'));
  await page.goto('http://127.0.0.1:' + server.address().port + '/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&arch=x86&session=1&novideo=1');
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout: 300000 });
  console.log('SESSION READY');
  await page.waitForTimeout(120000);
  await flush();
  const logText = logs.join('\n');
  const summary = {
    tag: TAG,
    interpNotFound: (logText.match(/Interpreter not found/g) || []).length,
    createWinstation: (logText.match(/create_winstation/g) || []).length,
    winebootFailed: (logText.match(/run_wineboot failed/g) || []).length,
    vkboot: /vkboot/.test(logText),
  };
  await writeFile(dir + '/summary.json', JSON.stringify(summary, null, 2));
  console.log('SUMMARY ' + JSON.stringify(summary));
  await browser.close();
  server.close();
} finally {
  await slot.release();
}
