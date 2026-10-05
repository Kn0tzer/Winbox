// scratch-vk.mjs — P2 boundary probe: run the guest Vulkan fixture (or a real
// vkcube) through the vk64 trap inside the browser and report exactly which
// entry points crossed.
//
// Modelled on scratch-baldi.mjs: serve dist/, drive the runtime iframe, capture
// the whole console, and write artifacts to test-results/. The difference is what
// it WAITS for. baldi waits for a guest window; this waits for the guest's own
// "vkfix: RESULT 0" line (or an exit_group status), because the evidence being
// collected is a syscall boundary, not a picture.
//
// How the guest program gets launched. The launcher's ?p= path always prefixes
// /usr/lib/wine/wine64 (it exists to run a Windows PE), so a native Linux ELF
// cannot go through it. Instead we boot the runtime in persistent-session mode —
// which brings up wineserver and then pins it with `wineserver64 -p`, keeping the
// kernel alive — and then call the exported bw64_spawn bridge directly with the
// ELF's argv. That is the same C bridge window.launchApp() uses
// (source/sdl/emscripten/wine64session.cpp), so no page/frontend change is needed.
//
// Env:
//   PROG      guest argv to spawn (default "/usr/bin/vkfixture")
//   ARGS      extra argv for PROG (default "", e.g. "64 64")
//   BOOT_PROG what boots first (default "cmd /c echo vkboot")
//   TIMEOUT   ms to wait for the RESULT line (default 600000)
//   TAG       artifact tag
//   VK_FRAME  set to 1 to ask the host for full frame-manifest JSON in the log
//             (?vkframe=1 -> BW64_VKFRAME=1, which makes vk64Bridge klog the
//             manifest it would hand the page)
//   SOAK_SECS keep collecting the log this long after RESULT (default 5)
import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { writeFile, mkdir } from 'node:fs/promises';

const server = process.env.WINE_URL ? null : await serve('dist');
const base = process.env.WINE_URL || `http://127.0.0.1:${server.address().port}/`;
const TAG = process.env.TAG ? '-' + process.env.TAG : '';
const art = name => 'test-results/vk' + TAG + '-' + name;
const PROG = process.env.PROG || '/usr/bin/vkfixture';
const ARGS = (process.env.ARGS || '').split(/\s+/).filter(Boolean);
const BOOT_PROG = process.env.BOOT_PROG || 'cmd /c echo vkboot';
const TIMEOUT = Number(process.env.TIMEOUT || 600000);
const SOAK_SECS = Number(process.env.SOAK_SECS || 5);
await mkdir('test-results', { recursive: true });

const q = new URLSearchParams({
  p: BOOT_PROG,
  session: '1',            // keep the kernel alive after the boot program exits
  novideo: '1',
  persist: '0',            // do not touch IndexedDB
  gltrace: '0',
  envedump: '1',           // env dumps are the cheapest extra evidence
});
if (process.env.VK_FRAME) q.set('vkframe', '1');
const url = base.replace(/\/?$/, '/runtime/index.html?') + q.toString();
console.log('VK PROBE URL:', url);
console.log('VK PROBE PROGRAM:', [PROG, ...ARGS].join(' '));

const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
  headless: true,
  args: ['--no-sandbox', '--disable-dev-shm-usage'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const errors = [], logs = [];
page.on('pageerror', e => errors.push(e.message));
page.on('console', m => logs.push(m.type() + ': ' + m.text()));

const flush = () => writeFile(art('console.log'), logs.join('\n').slice(-4000000)).catch(() => {});
const joined = () => logs.join('\n');
const saw = rx => logs.some(l => rx.test(l));

let guestFailure;
const failRx = /unimpl opcode|Aborted\(|RuntimeError:|unreachable|memory access out of bounds/;
const exitRx = /exit_group syscall, status=(\d+)/;
let exitStatus = null;

try {
  await page.goto(url);
  // NOTE: this probe drives /runtime/index.html DIRECTLY (no app shell), so the
  // app page's window.wineLibrary never exists here. The runtime page exposes
  // window.bwRuntime = { fs, ready, call } from wine64-launcher.js once the
  // wasm module loads; session readiness is bwRuntime.ready(). Waiting for
  // wineLibrary here (copied from scratch-baldi.mjs, which goes through the app
  // page) made every run time out at this line while the guest booted fine
  // underneath — that was the entire witness=false/trapCalls=0 story.
  await page.waitForFunction(() => window.bwRuntime, {}, { timeout: 120000 });
  console.log('runtime up; waiting for the session bridge');
  // A full wine64 prefix boot takes minutes under the interpreter, so this wait
  // gets the whole TIMEOUT budget, not the short 60s the module load gets.
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, {}, { timeout: TIMEOUT });
  console.log('SESSION READY');
  await flush();

  // Spawn the guest ELF through the exported spawn bridge (see the header note).
  const runtime = page.frames().find(f => f.url().includes('/runtime/'));
  if (!runtime) throw new Error('no runtime frame');
  const argv = [PROG, ...ARGS].join('\n');
  const env = ['BW64_VKTRACE=2', 'BW64_VKFRAME=1'].join('\n');
  const ok = await runtime.evaluate(({ argv, env }) => {
    return window.bwRuntime.call('bw64_spawn', ['string', 'string'], [argv, env]);
  }, { argv, env });
  console.log('bw64_spawn ->', ok);
  await flush();

  // Wait for the fixture's own verdict, or an exit status, or a crash.
  const deadline = Date.now() + TIMEOUT;
  while (Date.now() < deadline) {
    for (const l of logs) {
      if (failRx.test(l) && !guestFailure) guestFailure = l;
      const m = l.match(exitRx);
      if (m) exitStatus = Number(m[1]);
    }
    if (saw(/vkfix: RESULT [01]/)) break;
    if (saw(/vkfix: RESULT 0/)) break;
    await flush();
    await page.waitForTimeout(1000);
  }
  if (SOAK_SECS) await page.waitForTimeout(SOAK_SECS * 1000);
  await flush();
} catch (e) {
  process.exitCode = 1;
  console.log('FAIL', (e.message || String(e)).slice(0, 800));
} finally {
  await flush();
  const text = joined();
  // --- the gate, evaluated mechanically -------------------------------------
  const traps = [...text.matchAll(/vk64: trap (\w+)/g)].map(m => m[1]);
  const uniq = [...new Set(traps)];
  const frames = [...text.matchAll(/vk64: FRAME (\d+) built/g)].map(m => m[1]);
  const manifest = text.includes('vk64: FRAME-JSON');
  const witness = text.includes('vk64: FIRST trap');
  const presented = /vk64: vkQueuePresentKHR/.test(text);
  const result0 = /vkfix: RESULT 0/.test(text);
  const result1 = /vkfix: RESULT 1/.test(text);
  const summary = {
    witness, trapCalls: traps.length, distinctTrapCalls: uniq.length, uniq,
    framesBuilt: frames.length, manifestEmitted: manifest, presented,
    fixtureResult: result0 ? 0 : (result1 ? 1 : null),
    guestExitStatus: exitStatus, guestFailure: guestFailure || null,
    pageErrors: errors,
  };
  await writeFile(art('summary.json'), JSON.stringify(summary, null, 2));
  console.log('=== VK SUMMARY ===');
  console.log(JSON.stringify(summary, null, 2));
  if (errors.length) console.log('PAGE ERRORS', JSON.stringify(errors).slice(0, 2000));
  // The last 120 vk64 lines: the boundary evidence itself.
  const vkLines = text.split('\n').filter(l => /vk64:|vkfix:/.test(l));
  await writeFile(art('vklog.txt'), vkLines.join('\n'));
  console.log('--- vk64/vkfix log (tail) ---');
  console.log(vkLines.slice(-120).join('\n'));
  if (summary.fixtureResult !== 0 || !witness) process.exitCode = 1;
  await browser.close();
  server?.close();
}