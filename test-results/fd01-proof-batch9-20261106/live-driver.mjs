#!/usr/bin/env node
// live-driver.mjs — cold session-start measurement through the PUBLIC
// route (deploy evidence, deploy-finalbatch-20261031). Boots the
// shipped session page at the public URL with a brand-new browser
// profile per boot (the first-time-visitor shape) and the launcher's
// restart budget pre-exhausted (bwretry=2), so the FIRST attempt is
// what is measured and no start is a rescue. The full console stream
// is captured per boot and classified OFFLINE under the hardened
// FD-47 readiness rule, with the page's own verdict as a column:
//
//   pageReady    — bwRuntime.ready() resolved inside the window
//                  (the shipped launcher's own certification; on the
//                  new build this is the hardened in-page rule, whose
//                  X-accept element lives in-page)
//   plausible    — a deskpin READY line whose value is a plausible
//                  window handle: nonzero, top bit clear (NTSTATUS
//                  values like 0xC000000D have the top bits set)
//   explorer     — the kernel OPENED spawn-signal line for
//                  explorer.exe is present in the stream
//   certified    — pageReady && plausible && explorer
//   false-ready  — pageReady && !certified (the FD-43 shape)
//
// Usage:
//   node live-driver.mjs --base=https://kn0tzer.work.gd/winbox \
//       --arch=x64 --n=6 --budget-ms=900000 --tag=live-baseline-x64 \
//       --out=<dir>
// Results: <out>/<tag>/results.jsonl + boot-<i>/console.log +
// summary.json. One boot per probe slot (heavy), released between
// boots so other lanes' probes interleave.

import { chromium } from "@playwright/test";
import { acquireProbeSlot } from "../../web/tests/probe-slots.mjs";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";

const args = Object.fromEntries(process.argv.slice(2).map(a => {
  const m = a.match(/^--([^=]+)=(.*)$/); return m ? [m[1], m[2]] : [a, true];
}));
const BASE = (args.base || "https://kn0tzer.work.gd/winbox").replace(/\/$/, "");
const ARCH = args.arch || "x64";
const N = Number(args.n || 6);
const BUDGET_MS = Number(args["budget-ms"] || 900000);
const TAG = args.tag || "live";
const OUT = join(args.out || ".", TAG);
await mkdir(OUT, { recursive: true });

const RE = {
  deskpin: /deskpin: READY hwnd=0x([0-9a-f]+)/i,
  signature: /The explorer process failed to start/,
  explorerOpened: /OPENED '[^']*explorer\.exe' -> fd \d+/i,
  explorerExit: /exit_group.*explorer\.exe/,
  serverExit: /exit_group.*wineserver/,
  restart: /session: restarting session boot/,
  bwfail: /BW64-FAIL .*reason=deskpin-hwnd-not-a-handle/,
  memfault: /memory access out of bounds|Cannot enlarge memory|SEGV|segmentation fault/i,
};
const loadavg = () => readFileSync("/proc/loadavg", "utf8").trim().split(/\s+/).slice(0, 3).map(Number);

const results = [];
async function oneBoot(i) {
  const dir = join(OUT, `boot-${i}`);
  await mkdir(dir, { recursive: true });
  const profile = await mkdtemp(join(tmpdir(), "bw-live-profile-"));
  const lines = [];
  const loadStart = loadavg();
  let t0 = Date.now();
  let pageReady = false, readyMs = null, restarts = 0, note = "";
  let slot, ctx;
  try {
    // Slot waits can span other lanes' whole builds (the interlock
    // holds new probes back while a build is pending); allow for
    // that and treat an acquire failure as a recorded non-boot,
    // never a driver crash. The observation budget starts at goto.
    slot = await acquireProbeSlot({ timeoutMs: 2400000, kind: "heavy" });
    ctx = await chromium.launchPersistentContext(profile, {
      executablePath: process.env.CHROME_PATH || "/usr/local/bin/chromium",
      headless: true,
      args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu",
        "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader",
        "--enable-features=Vulkan"],
    });
    const page = ctx.pages()[0] || await ctx.newPage();
    page.on("console", m => lines.push(`${m.type()}: ${m.text()}`));
    page.on("pageerror", e => lines.push(`[pageerror] ${e.message}`));
    const params = new URLSearchParams({
      p: "cmd /c echo vkboot", session: "1", persist: "0", fresh: "1",
      arch: ARCH, bwretry: "2",
    });
    t0 = Date.now();
    await page.goto(`${BASE}/runtime/index.html?${params}`, { timeout: 120000 });
    let linesBase = 0, explorerExitAt = 0, serverExitAt = 0;
    while (Date.now() - t0 < BUDGET_MS) {
      let ready = false;
      try { ready = await page.evaluate(() => globalThis.bwRuntime?.ready?.() === true); } catch {}
      if (ready) { pageReady = true; readyMs = Date.now() - t0; break; }
      const ri = lines.reduce((a, l, idx) => RE.restart.test(l) ? idx : a, -1);
      if (ri >= linesBase) { linesBase = ri + 1; restarts++; explorerExitAt = 0; serverExitAt = 0; }
      const attemptText = lines.slice(linesBase).join("\n");
      const now = Date.now();
      if (!serverExitAt && RE.serverExit.test(attemptText)) serverExitAt = now;
      if (!explorerExitAt && RE.explorerExit.test(attemptText)) explorerExitAt = now;
      if (serverExitAt && now - serverExitAt > 10000) { note = "wineserver exited"; break; }
      if (explorerExitAt && now - explorerExitAt > 45000) { note = "explorer exited, no recovery in 45s grace"; break; }
      await page.waitForTimeout(1000);
    }
    if (!pageReady && !note) note = "budget exhausted";
  } catch (e) {
    note = "harness error: " + String(e).slice(0, 200);
  } finally {
    await writeFile(join(dir, "console.log"), lines.join("\n") + "\n");
    try { await ctx?.close(); } catch {}
    await rm(profile, { recursive: true, force: true });
    try { await slot?.release(); } catch {}
  }
  const text = lines.join("\n");
  const m = text.match(RE.deskpin);
  const hwnd = m ? parseInt(m[1], 16) : null;
  const plausible = hwnd !== null && hwnd !== 0 && hwnd < 0x80000000;
  const explorer = RE.explorerOpened.test(text);
  const certified = pageReady && plausible && explorer;
  const cls = certified ? "certified"
    : pageReady ? "false-ready"
    : (!plausible && (RE.signature.test(text) || RE.explorerExit.test(text) || RE.serverExit.test(text))) ? "race-failed"
    : "other";
  const rec = {
    boot: i, arch: ARCH, class: cls, pageReady, readyMs, wallMs: Date.now() - t0,
    hwnd: m ? "0x" + m[1].toLowerCase() : null, plausible, explorer,
    restarts, signature: RE.signature.test(text),
    bwfailNotHandle: RE.bwfail.test(text),
    memfaults: (text.match(new RegExp(RE.memfault, "gi")) || []).length,
    loadStart, loadEnd: loadavg(), note,
  };
  results.push(rec);
  console.log(`boot ${i}: ${cls}${readyMs ? " in " + (readyMs / 1000).toFixed(1) + "s" : ""} hwnd=${rec.hwnd} restarts=${restarts} (load ${loadStart[0]} -> ${rec.loadEnd[0]}) ${note}`);
  await writeFile(join(OUT, "results.jsonl"), results.map(r => JSON.stringify(r)).join("\n") + "\n");
  return rec;
}

for (let i = 1; i <= N; i++) await oneBoot(i);

const certified = results.filter(r => r.class === "certified");
const readyTimes = certified.map(r => r.readyMs).filter(x => x !== null).sort((a, b) => a - b);
const summary = {
  base: BASE, arch: ARCH, n: results.length,
  pageReady: results.filter(r => r.pageReady).length,
  certified: certified.length,
  falseReady: results.filter(r => r.class === "false-ready").length,
  raceFailed: results.filter(r => r.class === "race-failed").length,
  other: results.filter(r => r.class === "other").length,
  rescues: results.filter(r => r.restarts > 0).length,
  medianCertifiedReadyMs: readyTimes.length ? readyTimes[Math.floor(readyTimes.length / 2)] : null,
};
await writeFile(join(OUT, "summary.json"), JSON.stringify(summary, null, 2) + "\n");
console.log("SUMMARY " + JSON.stringify(summary));
