import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

// 32-bit desktop test with SEH tracing: does user32's DllMain throw during winstation_init?
// The __TRY/__EXCEPT_ALL in MODULE_InitDLL swallows exceptions in DllMain.
// If an x86 emulation fault occurs during winstation_init, we'd see it here.
import { chromium } from '@playwright/test';
import { serve } from '../../../tools/browser/serve.mjs';
import { readFile, writeFile, mkdir } from 'node:fs/promises';

const TAG = process.env.PROBE_TAG || '20261006-desktoptest-seh1';
const dir = `${diagnosticsRepoRoot}/test-results/${TAG}`;
await mkdir(dir, { recursive: true });

const server = await serve((diagnosticsRepoRoot + '/dist'));
const browser = await chromium.launch({
  executablePath: '/usr/local/bin/chromium', headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
    '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [];
page.on('console', m => logs.push(`${m.type()}: ${m.text()}`));
page.on('pageerror', e => logs.push(`PAGEERROR: ${e.message}`));
const flush = () => writeFile(`${dir}/console.log`, logs.join('\n'));
const timeout = 600000;
const t0 = Date.now();
const elapsed = () => ((Date.now() - t0) / 1000).toFixed(0) + 's';
let summary = { tag: TAG, arch: 'x86', boot32: false, testStaged: false, testSpawned: false, result: null };
try {
  // Boot with SEH + winstation + loaddll tracing
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&arch=x86&session=1&novideo=1&persist=0&gltrace=0&winedbg=%2Bseh%2C%2Bwinstation%2C%2Bloaddll`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log('SESSION READY in', elapsed());
  const bootUntil = Date.now() + timeout;
  const stripAnsi = s => s.replace(/\x1b\[[0-9;?]*[a-zA-Z]/g, '');
  while (!logs.some(l => /^log: vkboot\s*$/.test(stripAnsi(l)))) {
    if (Date.now() > bootUntil) throw new Error('Wine bootstrap sentinel timed out');
    await flush();
    await page.waitForTimeout(2000);
  }
  summary.boot32 = true;
  console.log('BOOT32 OK in', elapsed());

  // Stage desktoptest.exe
  const exePath = (diagnosticsRepoRoot + '/tools/desktoptest/desktoptest.exe');
  const bytes = await readFile(exePath);
  const b64 = bytes.toString('base64');
  await page.evaluate(({ b64 }) => {
    const fs = window.bwRuntime.fs();
    fs.mkdirTree('/root/home/username');
    fs.writeFile('/root/home/username/desktoptest.exe', Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
    const registered = window.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/desktoptest.exe']);
    if (registered !== 1) throw new Error('Guest VFS registration failed');
  }, { b64 });
  summary.testStaged = true;
  console.log('TEST STAGED', bytes.length, 'bytes');

  // Spawn it
  const spawn = await page.evaluate(() => window.bwRuntime.call('bw64_spawn', ['string', 'string'],
    ['/usr/lib/wine/wine\nZ:\\\\home\\\\username\\\\desktoptest.exe', 'WINEDLLPATH=/usr/lib/i386-linux-gnu/wine']));
  summary.testSpawned = true;
  console.log('SPAWN', spawn, 'in', elapsed());

  // Wait for output
  const runUntil = Date.now() + 180000;
  while (Date.now() < runUntil) {
    await flush();
    if (logs.some(l => /desktoptest: (SUCCESS|GetLastError)/.test(l))) break;
    await page.waitForTimeout(2000);
  }
  // Extract relevant lines
  const relevant = logs.filter(l => /desktoptest:|exception|winstation|user32/i.test(l));
  summary.result = relevant.slice(-30);
  console.log('RELEVANT LINES:', relevant.length);
  relevant.slice(-20).forEach(l => console.log('  ', l.slice(0, 200)));
  await flush();
} catch (e) {
  console.log('FAIL', String(e).slice(0, 300));
  summary.error = String(e).slice(0, 300);
} finally {
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  await browser.close(); server.close();
}
console.log('SUMMARY', JSON.stringify(summary).slice(0, 500));
