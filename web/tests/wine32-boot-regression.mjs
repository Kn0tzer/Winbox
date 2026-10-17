import { chromium } from '@playwright/test';
import { mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import { isAbsolute, relative, resolve, sep } from 'node:path';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';

// Wine's console emits cursor-forward sequences for spaces and hides the cursor
// around characters. Normalize those sequences before matching guest output.
const terminalText = text => text
  .replace(/\x1b\[(\d*)C/g, (_, columns) => ' '.repeat(Number(columns || 1)))
  .replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, '');

const repo = '/home/ubuntu/boxedwine64';
const tag = process.env.PROBE_TAG || 'wine32-boot-regression';
const program = process.env.PROBE_PROGRAM || '--version';
const expected = process.env.PROBE_EXPECT || 'wine-10.0';
const timeout = Number(process.env.PROBE_TIMEOUT || 90000);
const directory = `${repo}/test-results/${tag}`;
await mkdir(directory, { recursive: true });
const slot = await acquireProbeSlot();
let browser, server, logFile;
const logs = [];
let failure;
let build;
try {
  await waitForBuildIdle();
  build = JSON.parse(await readFile(`${repo}/dist/runtime/BUILD_INFO.json`, 'utf8'));
  server = await serve(repo);
  browser = await chromium.launch({
    executablePath: '/usr/local/bin/chromium', headless: true,
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
      '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader',
      '--enable-features=Vulkan'],
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  logFile = createWriteStream(`${directory}/console.log`);
  const record = line => { logs.push(line); logFile.write(`${line}\n`); };
  page.on('console', message => record(`${message.type()}: ${message.text()}`));
  page.on('pageerror', error => record(`pageerror: ${error.message}`));
  const params = new URLSearchParams({ arch: 'x86', p: program, session: '0', persist: '0', fresh: '1' });
  if (process.env.PROBE_GAMEZIP) {
    const asset = resolve(process.env.PROBE_GAMEZIP);
    const assetPath = relative(repo, asset);
    if (assetPath.startsWith(`..${sep}`) || isAbsolute(assetPath)) throw new Error('PROBE_GAMEZIP must be inside the repository');
    params.set('gamezip', `http://127.0.0.1:${server.address().port}/${assetPath.split(sep).map(encodeURIComponent).join('/')}`);
    params.set('gamezipsize', String((await stat(asset)).size));
    params.set('exe', process.env.PROBE_EXE || 'wine32-boot-regression.exe');
  }
  if (process.env.PROBE_QUERY) {
    for (const [key, value] of new URLSearchParams(process.env.PROBE_QUERY)) params.set(key, value);
  }
  const url = `http://127.0.0.1:${server.address().port}/dist/runtime/index.html?${params}`;
  console.log(JSON.stringify({ tag, program, url, wasm_sha256: build.wasm_sha256 }));
  await page.goto(url, { timeout: 60000 });
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    if (logs.some(line => terminalText(line).startsWith(`log: ${expected}`))) break;
    if (process.env.PROBE_STOP && logs.some(line => line.includes(process.env.PROBE_STOP))) break;
    if (logs.some(line => /Page Fault|pageerror:|abort\(|unreachable|Failed to get op|could not load kernel32|error while loading shared libraries|range fetch FAILED|blocked by CORS policy/.test(line))) break;
    await page.waitForTimeout(500);
  }
  await page.waitForTimeout(2000);
  await page.screenshot({ path: `${directory}/screenshot.png` });
} catch (error) {
  failure = error.stack;
} finally {
  await browser?.close();
  if (server) await new Promise(resolve => server.close(resolve));
  if (logFile) await new Promise(resolve => logFile.end(resolve));
  await slot.release();
}
const output = logs.filter(line => terminalText(line).startsWith(`log: ${expected}`));
const errors = logs.filter(line => /Page Fault|pageerror:|abort\(|unreachable|Failed to get op|could not load kernel32|error while loading shared libraries|range fetch FAILED|blocked by CORS policy/.test(line));
const passed = output.length > 0 && errors.length === 0 && !failure;
const summary = { tag, program, expected, passed, output: output.map(terminalText), rawOutput: output, errors, failure, logLines: logs.length, build };
await writeFile(`${directory}/summary.json`, JSON.stringify(summary, null, 2));
console.log(JSON.stringify(summary));
process.exitCode = passed ? 0 : 1;
