// D22 — Mindustry re-baseline: measure the bundled JVM's heap ergonomics
// inside the guest under the advertised machine definition (4 GiB RAM,
// 2 vCPUs). Three runs of the SAME bundled java.exe that ships in the
// Mindustry archive (its jre subtree, extracted byte-for-byte), launched
// with the SAME vmArgs Mindustry.json gives the game:
//   flags:    -XX:+PrintFlagsFinal -version  (the ergonomics table itself)
//   settings: -XshowSettings:all -version    (human-readable cross-check)
//   runtime:  HeapProbe — Runtime.maxMemory()/availableProcessors() plus
//             a live-allocation exercise of maxMemory/4
// Evidence: test-results/d22-mindustry-20261106/console-<run>.log + summary.json
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { mkdir, writeFile, copyFile, stat } from 'node:fs/promises';
import { acquireProbeSlot } from './probe-slots.mjs';

const OUT = 'test-results/d22-mindustry-20261106';
const ZIP_SRC = '/tmp/d22-probe.zip';
const VM_ARGS = ['-Dhttps.protocols=TLSv1.2,TLSv1.1,TLSv1', '-XX:+ShowCodeDetailsInExceptionMessages', '-XX:+UseCompactObjectHeaders', '--enable-native-access=ALL-UNNAMED'];
const RUNS = [
  { name: 'flags', args: [...VM_ARGS, '-XX:+PrintFlagsFinal', '-version'], marker: /openjdk version/i, need: /MaxHeapSize/ },
  { name: 'settings', args: [...VM_ARGS, '-XshowSettings:all', '-version'], marker: /VM settings:/, need: /Max\. Heap Size \(Estimated\)/ },
  { name: 'runtime', args: [...VM_ARGS, '-cp', 'Z:\\home\\username\\apps', 'HeapProbe'], marker: /HEAPPROBE DONE/, need: /HEAPPROBE maxMemory=/ },
];
const RUN_TIMEOUT_MS = 900_000;

await mkdir(OUT, { recursive: true });
await copyFile(ZIP_SRC, 'dist/d22-probe.zip');
const zipSize = (await stat(ZIP_SRC)).size;

const summary = { zipBytes: zipSize, runs: {} };
let slot, server, browser;
try {
  slot = await acquireProbeSlot();
  server = await serve('dist');
  const port = server.address().port;
  browser = await chromium.launch({ executablePath: '/usr/local/bin/chromium', headless: true, args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan'] });
  for (const run of RUNS) {
    const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
    const logs = [];
    const t0 = Date.now();
    const stamp = () => ((Date.now() - t0) / 1000).toFixed(1) + 's ';
    page.on('console', m => logs.push(stamp() + m.type() + ': ' + m.text()));
    page.on('pageerror', e => logs.push(stamp() + 'pageerror: ' + e.message));
    const args = encodeURIComponent(JSON.stringify(run.args));
    const gz = encodeURIComponent('http://127.0.0.1:' + port + '/d22-probe.zip');
    const url = 'http://127.0.0.1:' + port + '/runtime/index.html?installation=demo-mindustry&arch=x64&session=0&persist=0&gpu=0&gltrace=0&winedbg=-all&exe=' + encodeURIComponent('jre/bin/java.exe') + '&args=' + args + '&gamezip=' + gz + '&gamezipsize=' + zipSize;
    logs.push('URL: ' + url);
    let hit = false, needHit = false;
    try {
      await page.goto(url);
      const deadline = Date.now() + RUN_TIMEOUT_MS;
      while (Date.now() < deadline) {
        const text = logs.join('\n');
        needHit = run.need.test(text);
        if (needHit && run.marker.test(text)) { hit = true; break; }
        await page.waitForTimeout(2000);
      }
      if (hit) await page.waitForTimeout(15000);
    } catch (e) {
      logs.push('RUN ERROR: ' + e.message);
    }
    summary.runs[run.name] = { markerHit: hit, needHit, seconds: Math.round((Date.now() - t0) / 1000), lines: logs.length };
    await writeFile(OUT + '/console-' + run.name + '.log', logs.join('\n'));
    await page.close().catch(() => {});
    console.log('RUN', run.name, JSON.stringify(summary.runs[run.name]));
  }
} finally {
  await writeFile(OUT + '/summary.json', JSON.stringify(summary, null, 2)).catch(() => {});
  try { await browser?.close(); } catch {}
  try { server?.close(); } catch {}
  try { await slot?.release(); } catch {}
}
console.log('SUMMARY', JSON.stringify(summary.runs));
