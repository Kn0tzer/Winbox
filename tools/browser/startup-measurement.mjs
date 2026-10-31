#!/usr/bin/env node
// tools/browser/startup-measurement.mjs — first-attempt session-start
// measurement with dual readiness classification (FD-43 / FD-44).
//
// Why this exists. Until 2026-10-31 every start measurement in this
// project certified a session from one console line — the desktop
// pin's "deskpin: READY hwnd=0x..." — accepting any hex value. On
// builds whose win32u.dll lacks the Wine builtin signature (FD-40)
// that line carries the NTSTATUS error 0xC000000D instead of a handle,
// and sessions with no desktop at all were certified as started
// (FD-43: the first-visit "improvement" was this false positive).
//
// This driver boots the same deskpin-anchored harness session the
// suite's boot64-smoke uses (cmd /c echo vkboot, session=1, fresh
// browser profile per boot, bwretry=2 so the launcher restart
// stopgap's budget is pre-exhausted and the FIRST attempt is what is
// measured), captures the full console stream, and classifies the
// boot twice from the same evidence:
//
//   OLD certification — the pre-FD-44 rule: a deskpin READY line
//     with any hex value certifies the start.
//   NEW certification — the hardened rule the launcher implements
//     (FD-44): the READY value is a plausible window handle
//     (nonzero, top bit clear — NTSTATUS values have the top bits
//     set), the X wire server accepted at least <x11-min> clients
//     (a healthy boot makes five; dead boots stall at two), and
//     explorer.exe was actually opened. A boot that is OLD-certified
//     but not NEW-certified is a FALSE-READY.
//
// The page's own bwRuntime.ready() is sampled once after the
// observation window and recorded as a third column, so a dist
// carrying the hardened launcher can be checked against the offline
// rule (they must agree), and a dist carrying the old launcher shows
// the in-page false positive directly.
//
// Method note: on this shared, heavily loaded box the browser's
// console-event delivery to the driver can lag the guest by minutes
// (the renderer queues events while the emulator saturates the CPU);
// the complete stream is only guaranteed once the boot ends. The
// authoritative classification therefore runs OFFLINE, after
// teardown, over the console.log the harness writes — the verdicts
// never depend on live delivery. Live polling is kept only as an
// early exit for boots whose evidence completes early. Ready-time
// fields are live-observation stamps and are null when the stream
// did not deliver live; the rates are the deliverable.
//
// One boot per invocation: each boot gets a fresh browser profile and
// a probe slot through the project's ProbeTest machinery, so other
// lanes' probes interleave fairly between boots. A boot is a
// measurement, not a test: the driver exits 0 whenever the boot was
// observed, whatever the verdict; it exits nonzero only when the
// observation itself failed.
//
// Usage:
//   WINE_DIST_DIR=<dist> node tools/browser/startup-measurement.mjs \
//       --arm=B --index=3 [--arch=x64] [--window-ms=360000] [--x11-min=5]
//   node tools/browser/startup-measurement.mjs --summarize
//
// Results land in test-results/suite-readygate-<arm>-<index>/boot.json
// (console.log alongside, written by ProbeTest); --summarize folds
// every such boot.json into test-results/readygate-summary.json and
// prints the per-arm table.

import { readFileSync, writeFileSync, readdirSync, existsSync } from 'node:fs';
import { resolve, join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { ProbeTest, extraUrlParams } from '../../web/tests/suite/lib/probe-base.mjs';

const TREE_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
process.chdir(TREE_ROOT);

function loadavg() {
  try {
    return readFileSync('/proc/loadavg', 'utf8').trim().split(/\s+/).slice(0, 3).map(Number);
  } catch { return null; }
}

function arg(name, fallback) {
  const hit = process.argv.find(a => a.startsWith(`--${name}=`));
  return hit ? hit.split('=').slice(1).join('=') : fallback;
}

const hwndPlausible = hex => {
  if (!hex) return false;
  const v = parseInt(hex, 16);
  return Number.isFinite(v) && v > 0 && v < 0x80000000;
};

// Classify a complete console stream. Pure: same lines, same verdict,
// regardless of when they were delivered.
export function classifyLines(lines, x11Min) {
  const c = {
    deskpinHwnd: null, deskpinLines: 0, hwndPlausible: false,
    x11Accepts: 0, explorerOpened: false, winstationSeen: false,
    echoSeen: false, doomSignature: false,
    oldCert: false, newCert: false, falseReady: false,
  };
  for (const line of lines) {
    let m;
    if ((m = /deskpin: READY hwnd=0x([0-9a-f]+)/i.exec(line))) {
      c.deskpinLines++;
      if (c.deskpinHwnd === null) c.deskpinHwnd = m[1].toLowerCase();
      if (hwndPlausible(m[1])) c.hwndPlausible = true;
    }
    if (line.includes('XWireServer: accepted X11 client connection')) c.x11Accepts++;
    if (/OPENED '[^']*explorer\.exe'/i.test(line)) c.explorerOpened = true;
    if (/create_winstation/i.test(line)) c.winstationSeen = true;
    if (/^log: vkboot(?:\s*$|[0-9a-f]{4}: )/i.test(line)) c.echoSeen = true;
    if (line.includes('The explorer process failed to start')) c.doomSignature = true;
  }
  c.oldCert = c.deskpinLines > 0;
  c.newCert = c.hwndPlausible && c.x11Accepts >= x11Min && c.explorerOpened;
  c.falseReady = c.oldCert && !c.newCert;
  return c;
}

class StartupMeasurement extends ProbeTest {
  constructor(opts) {
    super({ tag: `readygate-${opts.arm}-${opts.index}`, timeoutMs: opts.windowMs + 300000 });
    this.opts = opts;
  }

  async run({ page }) {
    const { arch, windowMs, x11Min } = this.opts;
    const t0 = Date.now();
    const live = {
      deskpinAtMs: null, evidenceAtMs: null, doomAtMs: null, scanned: 0,
    };
    const scanLive = () => {
      const lines = this.consoleLines;
      let plausible = false, x11 = 0, explorer = false;
      for (; live.scanned < lines.length; live.scanned++) {
        const line = lines[live.scanned];
        const t = Date.now() - t0;
        let m;
        if ((m = /deskpin: READY hwnd=0x([0-9a-f]+)/i.exec(line))) {
          if (live.deskpinAtMs === null) live.deskpinAtMs = t;
          if (hwndPlausible(m[1])) plausible = true;
        }
        if (line.includes('XWireServer: accepted X11 client connection')) x11++;
        if (/OPENED '[^']*explorer\.exe'/i.test(line)) explorer = true;
        if (line.includes('The explorer process failed to start') && live.doomAtMs === null) {
          live.doomAtMs = t;
        }
      }
      live.plausible = (live.plausible || plausible);
      live.x11 = (live.x11 || 0) + x11;
      live.explorer = (live.explorer || explorer);
      if (live.evidenceAtMs === null && live.plausible && live.x11 >= x11Min && live.explorer) {
        live.evidenceAtMs = Date.now() - t0;
      }
    };

    const params = new URLSearchParams({
      p: 'cmd /c echo vkboot', session: '1', persist: '0', fresh: '1',
      arch, bwretry: '2', winedbg: '+loaddll,+server', ...extraUrlParams(),
    });
    let gotoError = null;
    try {
      await page.goto(`${this._server.url}/runtime/index.html?${params}`);
    } catch (e) {
      gotoError = String(e && e.message || e);
    }

    let outcome = 'window-expired';
    if (gotoError) {
      outcome = 'goto-failed';
    } else {
      for (;;) {
        scanLive();
        const now = Date.now() - t0;
        if (live.evidenceAtMs !== null && now - live.evidenceAtMs > 10000) {
          outcome = 'certified-live';
          break;
        }
        if (live.deskpinAtMs !== null && !live.plausible && now - live.deskpinAtMs > 60000) {
          // The pin prints READY exactly once; an NTSTATUS-shaped
          // value there is the FD-40 fingerprint and no later line
          // can turn this boot into a real start.
          outcome = 'false-ready-fingerprint-live';
          break;
        }
        if (live.doomAtMs !== null && now - live.doomAtMs > 45000) {
          outcome = 'dead-after-signature-live';
          break;
        }
        if (now > windowMs) break;
        await page.waitForTimeout(500);
      }
    }

    // One page-cert sample after the window: the launcher's own
    // verdict at the end of the boot. Raced so a saturated renderer
    // cannot hang the measurement; 'unknown' when it never answers.
    let pageCert = 'unknown';
    if (!gotoError) {
      try {
        pageCert = await Promise.race([
          page.evaluate(() => {
            try { return globalThis.bwRuntime?.ready?.() === true; } catch { return false; }
          }),
          page.waitForTimeout(30000).then(() => 'unknown'),
        ]);
      } catch { pageCert = 'unknown'; }
    }

    this.measurement = {
      arm: this.opts.arm, index: Number(this.opts.index), arch,
      distDir: resolve(process.env.WINE_DIST_DIR || this.distDir),
      buildInfo: this.buildInfo || null,
      startedAt: new Date(t0).toISOString(),
      loadStart: this.loadAtStart, loadEnd: loadavg(),
      windowMs, x11Min, outcome, gotoError,
      liveDeskpinAtMs: live.deskpinAtMs, liveEvidenceAtMs: live.evidenceAtMs,
      pageCert,
      consoleLinesAtExit: this.consoleLines.length,
    };
  }
}

function finalize(test) {
  const m = test.measurement;
  const logFile = join(test.dir, 'console.log');
  const lines = existsSync(logFile) ? readFileSync(logFile, 'utf8').split('\n') : [];
  const c = classifyLines(lines, m.x11Min);
  const outcome = m.gotoError ? 'goto-failed'
    : c.newCert ? 'certified'
    : c.falseReady && !c.hwndPlausible && c.deskpinLines > 0 ? 'false-ready'
    : c.doomSignature ? 'dead-after-signature'
    : c.oldCert ? 'pin-only'
    : 'no-start-evidence';
  const result = {
    ...m, outcome, ...c,
    pageCertOutcome: m.pageCert,
    readyMsOldObserved: c.oldCert ? m.liveDeskpinAtMs : null,
    readyMsNewObserved: c.newCert ? m.liveEvidenceAtMs : null,
    consoleLinesTotal: lines.length,
  };
  writeFileSync(join(test.dir, 'boot.json'), JSON.stringify(result, null, 2));
  console.log(`[readygate] arm=${result.arm} #${result.index} outcome=${result.outcome} ` +
    `old=${result.oldCert} new=${result.newCert} page=${result.pageCert} ` +
    `hwnd=${result.deskpinHwnd} x11=${result.x11Accepts} explorer=${result.explorerOpened} ` +
    `echo=${result.echoSeen} load=${result.loadStart}`);
  return result;
}

function summarize() {
  const dir = join(TREE_ROOT, 'test-results');
  const boots = [];
  for (const name of readdirSync(dir)) {
    if (!name.startsWith('suite-readygate-')) continue;
    const file = join(dir, name, 'boot.json');
    if (existsSync(file)) {
      const b = JSON.parse(readFileSync(file, 'utf8'));
      // Calibration boots (CAL*/DBG arms) stay out of the rates.
      if (typeof b.index === 'number' && b.arm && !/^(CAL|DBG)/.test(b.arm) && !b.arm.includes('DBG')) boots.push(b);
    }
  }
  boots.sort((a, b) => a.arm.localeCompare(b.arm) || a.index - b.index);
  const median = xs => {
    if (!xs.length) return null;
    const s = [...xs].sort((a, b) => a - b);
    return s[Math.floor(s.length / 2)];
  };
  const arms = {};
  for (const b of boots) {
    const a = arms[b.arm] ||= { arm: b.arm, n: 0, oldCert: 0, newCert: 0, pageCertTrue: 0, falseReady: 0, boots: [] };
    a.n++;
    a.oldCert += b.oldCert ? 1 : 0;
    a.newCert += b.newCert ? 1 : 0;
    a.pageCertTrue += b.pageCert === true ? 1 : 0;
    a.falseReady += b.falseReady ? 1 : 0;
    a.boots.push(b);
  }
  for (const a of Object.values(arms)) {
    a.medianReadyMsNewObserved = median(a.boots.filter(b => b.newCert && b.readyMsNewObserved).map(b => b.readyMsNewObserved));
    a.medianReadyMsOldObserved = median(a.boots.filter(b => b.oldCert && b.readyMsOldObserved).map(b => b.readyMsOldObserved));
    a.meanLoad1m = a.boots.reduce((s, b) => s + (b.loadStart ? b.loadStart[0] : 0), 0) / a.n;
    delete a.boots;
  }
  const summary = { generatedAt: new Date().toISOString(), arms, boots };
  writeFileSync(join(dir, 'readygate-summary.json'), JSON.stringify(summary, null, 2));
  for (const a of Object.values(arms)) {
    console.log(`arm ${a.arm}: n=${a.n} old-cert=${a.oldCert} new-cert=${a.newCert} ` +
      `page-cert=${a.pageCertTrue} false-ready=${a.falseReady} ` +
      `medianReadyNewObs=${a.medianReadyMsNewObserved} medianReadyOldObs=${a.medianReadyMsOldObserved} ` +
      `meanLoad1m=${a.meanLoad1m.toFixed(1)}`);
  }
}

if (process.argv.includes('--summarize')) {
  summarize();
} else {
  const opts = {
    arm: arg('arm', null),
    index: arg('index', null),
    arch: arg('arch', 'x64'),
    windowMs: Number(arg('window-ms', '360000')),
    x11Min: Number(arg('x11-min', '5')),
  };
  if (!opts.arm || opts.index === null) {
    console.error('usage: startup-measurement.mjs --arm=<name> --index=<n> [--arch=x64] [--window-ms=360000] [--x11-min=5]');
    process.exit(2);
  }
  // probe-base ends its live console.log stream during teardown; a
  // console event landing in the end() window raises
  // ERR_STREAM_WRITE_AFTER_END as an unhandled stream 'error' and
  // would kill the measurement after the boot itself completed.
  // That one error is known-benign here (the console.log is written
  // live and is complete either way); anything else stays fatal.
  process.on('uncaughtException', err => {
    if (err && err.code === 'ERR_STREAM_WRITE_AFTER_END') {
      console.error('[readygate] ignored probe-base stream end race (ERR_STREAM_WRITE_AFTER_END)');
      return;
    }
    throw err;
  });
  const test = new StartupMeasurement(opts);
  test.loadAtStart = loadavg();
  try {
    await test.setup();
    const page = await test.newPage();
    await test.run({ page });
  } finally {
    await test.teardown();
  }
  finalize(test);
}
