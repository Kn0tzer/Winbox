import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

// Simple WSCOUNT hex dump probe - captures wineserver reply wire format
import { chromium } from '@playwright/test';
import { serve } from '../../../tools/browser/serve.mjs';
import { writeFile, mkdir } from 'node:fs/promises';

const TAG = process.env.PROBE_TAG || 'wscount-hex-32';
const dir = 'test-results/' + TAG;
await mkdir(dir, { recursive: true });

const server = await serve(process.env.WINE_DIST_DIR || 'dist');
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [];
page.on('console', m => logs.push(m.type() + ': ' + m.text()));
page.on('pageerror', e => logs.push('PAGEERROR: ' + e.message));

const ARCH = process.env.PROBE_ARCH || 'x86';
console.log('Starting ' + ARCH + ' probe with wscount=1...');
const port = server.address().port;
const NL = String.fromCharCode(10);
await page.goto('http://127.0.0.1:' + port + '/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&arch=' + ARCH + '&session=1&novideo=1&persist=0&gltrace=0&wscount=1');

for (let i = 0; i < 75; i++) {
  await page.waitForTimeout(2000);
  await writeFile(dir + '/console.log', logs.join(NL));
  const wscount = logs.filter(l => l.includes('WSCOUNT')).length;
  const reply = logs.filter(l => l.includes('REPLY')).length;
  if (i % 10 === 0) console.log('  ' + (i*2) + 's: ' + wscount + ' WSCOUNT, ' + reply + ' REPLY');
  if (reply >= 2) {
    console.log('Got reply data, waiting 10s more...');
    await page.waitForTimeout(10000);
    break;
  }
}

await writeFile(dir + '/console.log', logs.join(NL));
const wscountLines = logs.filter(l => l.includes('WSCOUNT'));
await writeFile(dir + '/wscount.txt', wscountLines.join(NL));
console.log('Done. ' + wscountLines.length + ' WSCOUNT lines.');
await browser.close();
server.close();
