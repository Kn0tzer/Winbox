import { chromium } from '@playwright/test';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
// NOTE: this rig deliberately does not waitForBuildIdle() — it stubs the
// emulator glue, so there is no guest boot to contend with a running build;
// the probe slot below is the binding coordination.
import { mkdir, writeFile } from 'node:fs/promises';
import assert from 'node:assert/strict';

// FD-77 remainder, shaped-session proof. The real launcher page and the
// real runtime launcher run from the built dist; only the wasm glue is
// replaced, by a guest shaped three ways: (1) it completes the desktop
// handshake and then its thread dumps show a process whose every
// running thread has stopped executing — the FD-77 park shape — and
// the session must end as a reported failure with the dump evidence in
// the log; (2) it boots slowly but keeps making progress (and spends
// time with a lazy fetch in flight) — no alarm may fire, and when its
// window maps and paints, startup completes normally; (3) it stalls
// before the desktop handshake at the FD-77 scale — no report may
// fire, because the pre-handshake rules demand far stronger evidence
// than a 90-second stall; (4) before the handshake, one guest thread
// loops on delivered memory faults (the FD-115 wedge shape) — the
// start must end as a reported failure once that thread's printed
// fault lines reach the loop threshold, and not before; (5) before
// the handshake, no guest process makes any progress across
// consecutive thread dumps (the FD-165 shape) — the start must end
// as a reported failure, while one progressing process anywhere in
// the dump keeps it unreported. This fixture
// proves surfacing behaviour only; guest compatibility is covered by
// the real-boot checks.

const dir = 'test-results/fd77-stall-surfacing';
await mkdir(dir, { recursive: true });
const slot = await acquireProbeSlot();
let browser, server;
const errors = [], checks = [];
const GLUE = `(function () {
  var M = window.Module;
  window.__bwShape = { pid: 42, running: 9, min: 90000, inflight: 0 };
  window.__bwDumpCalls = 0;
  function dumpLines(s) {
    var lines = [
      'BW_THREAD_DUMP begin maxTid=120',
      'BW_THREAD_DUMP pid=' + s.pid + ' tid=100 state=running syscall=-(#0) args=0x0,0x0,0x0,0x0,0x0,0x0 rip=0x7f0 wait=- eax=0x0 noprogress_ms=' + s.min + ' fstage=mem-lock fstage_ms=' + s.min + ' fpage=0x8dd3',
      'BW_THREAD_DUMP pid=' + s.pid + ' tid=101 state=blocked syscall=read(#0) args=0xc,0x0,0x0,0x0,0x0,0x0 rip=0x7f1 wait=KUnixSocketObject::lockCond eax=0x0 noprogress_ms=0 fstage=none fstage_ms=0 fpage=0x0',
      'BW_THREAD_DUMP proc pid=' + s.pid + ' running=' + s.running + ' min_noprogress_ms=' + s.min + ' memmutex_owner_tid=100 memmutex_owner_ms=' + s.min
    ];
    (window.__bwExtraProcs || []).forEach(function (p) {
      lines.push('BW_THREAD_DUMP proc pid=' + p.pid + ' running=' + p.running + ' min_noprogress_ms=' + p.min + ' memmutex_owner_tid=0 memmutex_owner_ms=0');
    });
    lines.push('BW_THREAD_DUMP proxyfetch inflight=' + s.inflight + ' oldest_tid=0 oldest_ms=0');
    lines.push('BW_THREAD_DUMP ramalloc owner_tid=0 owner_ms=0');
    lines.push('BW_THREAD_DUMP end dumped=70 skipped-lock=0');
    return lines;
  }
  M.ccall = function (name) {
    if (name === 'bw64_dump_guest_threads') {
      window.__bwDumpCalls++;
      dumpLines(window.__bwShape).forEach(function (l) { M.print(l); });
      return 1;
    }
    return 0;
  };
  window.__bwEmit = function (lines) { lines.forEach(function (l) { M.print(l); }); };
  window.__bwPaint = function () {
    var c = document.getElementById('canvas');
    var x = c.getContext('2d');
    x.fillStyle = 'white'; x.fillRect(0, 0, c.width, c.height);
  };
  if (M.onRuntimeInitialized) M.onRuntimeInitialized();
  window.__bwReady = true;
})();`;
const HANDSHAKE = [
  'deskpin: READY hwnd=0x10048',
  ...Array(5).fill('XWireServer: accepted X11 client connection'),
  "OPENED '/root/home/username/.wine/drive_c/windows/explorer.exe' -> fd 9",
];
try {
  server = await serve(process.env.BW_TEST_DIST || 'dist');
  browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', args: ['--no-sandbox', '--disable-dev-shm-usage'] });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  page.on('pageerror', error => errors.push(error.message));
  await page.route(url => url.pathname.endsWith('/boxedwine64.js'), route => route.fulfill({ contentType: 'text/javascript', body: GLUE }));
  // All archives serve for real from dist: the app shell validates the
  // support archive with a HEAD request (content-length + accept-ranges)
  // before any launch is possible, so stubbing zips breaks startup.
  // The cadence overrides ride in on a 302: route.continue({url}) did
  // not change the frame's final URL in this rig (the launched frame
  // showed no stallwatch params and the watchdog stayed on its default
  // 90 s cadence), while a redirect makes the browser itself navigate
  // to the parameterised URL. Requests already carrying the params
  // pass through untouched.
  await page.route(url => url.pathname.endsWith('/runtime/index.html'), async route => {
    const url = new URL(route.request().url());
    if (url.searchParams.get('stallwatchdelay')) return route.continue();
    url.searchParams.set('stallwatchdelay', '300');
    url.searchParams.set('stallwatchinterval', '300');
    return route.fulfill({ status: 302, headers: { location: url.toString() }, body: '' });
  });
  page.setDefaultTimeout(120000);
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.waitForFunction(() => window.wineGames && !document.getElementById('library').hasAttribute('aria-busy'), null, { timeout: 120000 });

  const runtimeFrame = async () => {
    const handle = await page.locator('#display iframe').elementHandle();
    const frame = await handle.contentFrame();
    await frame.waitForFunction(() => window.__bwReady === true, null, { timeout: 60000 });
    return frame;
  };
  const launch = async () => {
    await page.locator('[data-game="notepad"]').click();
    await page.locator('#display iframe').waitFor();
    return runtimeFrame();
  };
  const stop = async () => {
    await page.click('#stop');
    await page.locator('#display iframe').waitFor({ state: 'detached' });
    await page.waitForFunction(() => !document.getElementById('folderButton').disabled);
  };
  const noFailure = async label => {
    assert.notEqual(await page.locator('#loadingTitle').textContent(), 'The program stopped', label);
    assert.ok(!(await page.locator('#status').textContent()).includes('reported as failed'), label);
  };

  // Scenario 1: booted, no window, no progress — reported failure.
  {
    const frame = await launch();
    assert.ok(frame.url().includes('stallwatchdelay=300'), 'cadence override reached the runtime frame: ' + frame.url());
    await frame.evaluate(lines => window.__bwEmit(lines), HANDSHAKE);
    await page.waitForFunction(() => document.getElementById('loadingTitle').textContent === 'The program stopped', null, { timeout: 20000 });
    assert.match(await page.locator('#status').textContent(), /never opened a window and stopped making progress/);
    assert.match(await page.locator('#status').textContent(), /reported as failed/);
    const log = await page.locator('#log').textContent();
    assert.ok(log.includes('BW64-FAIL component=launcher.session reason=booted-no-window-no-progress'), 'FAIL line in the session log');
    assert.ok(log.includes('min_noprogress_ms=90000'), 'FAIL line carries the dump signal');
    assert.ok(log.includes('BW_THREAD_DUMP pid=42 tid=100'), 'held dump evidence flushed into the session log');
    assert.ok(log.includes('BW_THREAD_DUMP proc pid=42'), 'dump summary lines in the session log');
    assert.ok(await frame.evaluate(() => window.__bwDumpCalls) >= 2, 'conviction took at least two dumps');
    await page.screenshot({ path: dir + '/reported-failure.png' });
    checks.push('Booted, windowless, no-progress session is reported as failed; the FAIL line and the held dump evidence are preserved in the session log');
    await stop();
  }

  // Scenario 2: slow but alive — progress, a fetch in flight, one
  // stalled dump — never an alarm; mapping and painting complete it.
  {
    const frame = await launch();
    await frame.evaluate(lines => window.__bwEmit(lines), HANDSHAKE);
    await frame.evaluate(shape => { window.__bwShape = shape; }, { pid: 42, running: 9, min: 4000, inflight: 0 });
    const before = await frame.evaluate(() => window.__bwDumpCalls);
    await frame.waitForFunction(n => window.__bwDumpCalls >= n + 3, before, { timeout: 10000 });
    await noFailure('progressing dumps raised an alarm');
    await frame.evaluate(shape => { window.__bwShape = shape; }, { pid: 42, running: 9, min: 90000, inflight: 3 });
    const fetchBefore = await frame.evaluate(() => window.__bwDumpCalls);
    await frame.waitForFunction(n => window.__bwDumpCalls >= n + 2, fetchBefore, { timeout: 10000 });
    await noFailure('a lazy fetch in flight raised an alarm');
    await frame.evaluate(shape => { window.__bwShape = shape; }, { pid: 42, running: 9, min: 90000, inflight: 0 });
    const stallBefore = await frame.evaluate(() => window.__bwDumpCalls);
    await frame.waitForFunction(n => window.__bwDumpCalls >= n + 1, stallBefore, { timeout: 10000 });
    await noFailure('a single stalled dump raised an alarm');
    await frame.evaluate(() => {
      window.__bwEmit(['XWire: first window mapped (present sink=headless)']);
      window.__bwPaint();
    });
    await page.waitForFunction(() => document.getElementById('loading').hidden, null, { timeout: 10000 });
    await noFailure('a mapped, painted session was reported');
    checks.push('Slow-but-alive start raises no alarm: progressing dumps, a fetch in flight, and a single stalled dump all pass; mapping and painting complete startup normally');
    await stop();
  }

  // Scenario 3: stalled before the desktop handshake — no report.
  {
    const frame = await launch();
    const before = await frame.evaluate(() => window.__bwDumpCalls);
    await frame.waitForFunction(n => window.__bwDumpCalls >= n + 3, before, { timeout: 10000 });
    await noFailure('a pre-handshake stall was reported');
    checks.push('A stall before the desktop handshake at the FD-77 scale is not reported — the pre-handshake rules demand far stronger evidence');
    await stop();
  }

  // Scenario 4: a pre-handshake delivered-fault loop (FD-115) is a
  // reported failed start, not an endless wait.
  {
    const frame = await launch();
    const fault = (tid, count) => 'BW64-FAIL guest-memfault32 pid=71 tid=' + tid +
      ' addr=0xfffffd08 eip=0x10584384 err=2 delivered=1 count=' + count;
    await frame.evaluate(lines => window.__bwEmit(lines),
      Array.from({ length: 31 }, (_, i) => fault(76, i + 1)));
    await noFailure('a burst of delivered faults below the loop threshold was reported');
    await frame.evaluate(lines => window.__bwEmit(lines),
      [77, 78, 79].flatMap(tid => Array.from({ length: 16 }, (_, i) => fault(tid, i + 1))));
    await noFailure('faults spread across other threads were reported as one thread looping');
    await frame.evaluate(line => window.__bwEmit([line]), fault(76, 32));
    await page.waitForFunction(() => document.getElementById('loadingTitle').textContent === 'The program stopped', null, { timeout: 20000 });
    assert.match(await page.locator('#status').textContent(), /did not finish starting/);
    assert.match(await page.locator('#status').textContent(), /reported as failed/);
    const log = await page.locator('#log').textContent();
    assert.ok(log.includes('BW64-FAIL component=launcher.session reason=prehandshake-fault-loop'), 'FAIL line in the session log');
    assert.ok(log.includes('pid=71') && log.includes('tid=76'), 'FAIL line names the looping thread');
    await page.screenshot({ path: dir + '/fault-loop-failure.png' });
    checks.push('A pre-handshake delivered-fault loop from one guest thread ends the start as a reported failure at the threshold; bursts below it and faults spread across threads raise no alarm');
    await stop();
  }

  // Scenario 5: a pre-handshake start where no guest process makes
  // any progress (FD-165) is a reported failed start; one progressing
  // process anywhere in the dump keeps it unreported.
  {
    const frame = await launch();
    await frame.evaluate(shape => { window.__bwShape = shape; }, { pid: 10, running: 1, min: 780078, inflight: 0 });
    await frame.evaluate(procs => { window.__bwExtraProcs = procs; }, [
      { pid: 12, running: 1, min: 780078 },
      { pid: 14, running: 1, min: 4000 },
    ]);
    const aliveBefore = await frame.evaluate(() => window.__bwDumpCalls);
    await frame.waitForFunction(n => window.__bwDumpCalls >= n + 3, aliveBefore, { timeout: 10000 });
    await noFailure('a pre-handshake start with one progressing process was reported');
    await frame.evaluate(procs => { window.__bwExtraProcs = procs; }, [
      { pid: 12, running: 1, min: 780078 },
      { pid: 14, running: 1, min: 780078 },
    ]);
    await page.waitForFunction(() => document.getElementById('loadingTitle').textContent === 'The program stopped', null, { timeout: 20000 });
    assert.match(await page.locator('#status').textContent(), /did not finish starting/);
    assert.match(await page.locator('#status').textContent(), /stopped making progress/);
    assert.match(await page.locator('#status').textContent(), /reported as failed/);
    const log = await page.locator('#log').textContent();
    assert.ok(log.includes('BW64-FAIL component=launcher.session reason=prehandshake-no-progress'), 'FAIL line in the session log');
    assert.ok(log.includes('min_noprogress_ms=780078'), 'FAIL line carries the dump signal');
    await page.screenshot({ path: dir + '/prehandshake-no-progress-failure.png' });
    checks.push('A pre-handshake start where no guest process makes progress ends as a reported failure; one progressing process anywhere in the dump raises no alarm');
    await stop();
  }

  assert.deepEqual(errors.filter(e => /stallwatch/i.test(e)), []);
  await writeFile(dir + '/summary.json', JSON.stringify({ pass: true, checks, errors }, null, 2));
  console.log(JSON.stringify({ pass: true, checks }));
} catch (error) {
  await writeFile(dir + '/summary.json', JSON.stringify({ pass: false, error: error.stack, checks, errors }, null, 2));
  throw error;
} finally {
  await browser?.close();
  server?.close();
  await slot.release();
}
