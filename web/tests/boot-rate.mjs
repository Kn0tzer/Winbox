// boot-rate.mjs — session boot-rate harness for the Winbox session-reliability
// campaign. Boots a lightweight guest session through the SAME page path the
// suite's bootGuest() uses (runtime/index.html?session=1, waiting on
// bwRuntime.ready()) N times, classifying each boot as started / race-failed /
// other, with the box load recorded per boot. ready() is the exact signal a
// real demo boot waits on, so this measures the shipped product path, not a
// probe rig.
//
// Usage:
//   BOOTRATE_DIST=<dist> node web/tests/boot-rate.mjs
// Env:
//   BOOTRATE_DIST        dist to serve (default: dist)
//   BOOTRATE_N           boots to attempt (default 10)
//   BOOTRATE_BUDGET_MS   per-boot ready budget (default 360000)
//   BOOTRATE_TAG         results dir tag (default bootrate-<timestamp>)
//   BOOTRATE_URL_PARAMS  extra URL params, e.g. "drvinit=1"
//   BOOTRATE_ARCH        x64 (default) or x86
// Results: test-results/<tag>/results.jsonl + boot-<i>/console.log + summary.json

import { chromium } from "@playwright/test";
import { serve } from "../../tools/browser/serve.mjs";
import { acquireProbeSlot } from "./probe-slots.mjs";
import { waitForBuildIdle } from "./build-lock.mjs";
import { mkdir, mkdtemp, readdir, symlink, rm, readFile, writeFile } from "node:fs/promises";
import { readFileSync } from "node:fs";
import { resolve, join } from "node:path";
import { tmpdir } from "node:os";

const DIST = resolve(process.env.BOOTRATE_DIST || "dist");
const N = Number(process.env.BOOTRATE_N || 10);
const BUDGET_MS = Number(process.env.BOOTRATE_BUDGET_MS || 360000);
const ARCH = process.env.BOOTRATE_ARCH || "x64";
const TAG = process.env.BOOTRATE_TAG || ("bootrate-" + new Date().toISOString().replace(/[-:T]/g, "").slice(0, 14));
const EXTRA = process.env.BOOTRATE_URL_PARAMS ? Object.fromEntries(new URLSearchParams(process.env.BOOTRATE_URL_PARAMS)) : {};
const OUT = join("test-results", TAG);

const RE = {
  deskpinReady: /deskpin: READY hwnd=0x[0-9a-f]+/i,
  signature: /The explorer process failed to start/,
  explorerExit: /exit_group.*explorer\.exe/,
  serverExit: /exit_group.*wineserver/,
  vkboot: /^log: vkboot\s*$/m,
};

const loadavg = () => readFileSync("/proc/loadavg", "utf8").trim().split(/\s+/).slice(0, 3).map(Number);

await mkdir(OUT, { recursive: true });

// Temp root of symlinks to the dist, exactly like probe-base.mjs setup().
const root = await mkdtemp(join(tmpdir(), "bw-bootrate-"));
for (const f of await readdir(DIST)) await symlink(join(DIST, f), join(root, f));
const server = await serve(root);
server.url = "http://127.0.0.1:" + server.address().port;

const buildInfo = JSON.parse(await readFile(join(DIST, "runtime/BUILD_INFO.json"), "utf8"));
console.log(`boot-rate: dist git_sha=${buildInfo.git_sha} wasm=${buildInfo.wasm_sha256.slice(0, 8)} n=${N} budget=${BUDGET_MS}ms arch=${ARCH} tag=${TAG}`);

const browserServer = await chromium.launchServer({
  executablePath: process.env.CHROME_PATH || "/usr/local/bin/chromium", headless: true,
  args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu",
    "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader", "--enable-features=Vulkan",
    "--remote-debugging-address=127.0.0.1", "--remote-debugging-port=0"],
});
const browser = await chromium.connect(browserServer.wsEndpoint());

const results = [];
async function oneBoot(i) {
  const dir = join(OUT, `boot-${String(i).padStart(2, "0")}`);
  await mkdir(dir, { recursive: true });
  const lines = [];
  const page = await browser.newPage();
  page.on("console", m => lines.push(`${m.type()}: ${m.text()}`));
  page.on("pageerror", e => lines.push(`[pageerror] ${e.message}`));
  const loadStart = loadavg();
  const t0 = Date.now();
  const params = new URLSearchParams({ p: "cmd /c echo vkboot", session: "1", persist: "0", fresh: "1", arch: ARCH, ...EXTRA });
  let cls = "other", readyMs = null, note = "";
  // The launcher's self-healing restart navigates the page and boots again
  // inside this same measurement. Terminal-condition flags (explorer /
  // wineserver exit) must therefore be scoped to the current attempt --
  // lines before the last restart line belong to a dead attempt -- while
  // classification flags describe the whole user-visible boot.
  let linesBase = 0, restarts = 0;
  const scan = () => {
    const attemptText = lines.slice(linesBase).join("\n");
    const text = lines.join("\n");
    return {
      signature: RE.signature.test(text),
      explorerExit: RE.explorerExit.test(attemptText),
      serverExit: RE.serverExit.test(attemptText),
      deskpinReady: RE.deskpinReady.test(text),
    };
  };
  try {
    await page.goto(`${server.url}/runtime/index.html?${params}`);
    let explorerExitAt = 0, serverExitAt = 0;
    while (Date.now() - t0 < BUDGET_MS) {
      let ready = false;
      try { ready = await page.evaluate(() => globalThis.bwRuntime?.ready?.() === true); } catch {}
      if (ready) { cls = "started"; readyMs = Date.now() - t0; break; }
      const ri = lines.reduce((a, l, idx) => /session: restarting session boot/.test(l) ? idx : a, -1);
      if (ri >= linesBase) { linesBase = ri + 1; restarts++; explorerExitAt = 0; serverExitAt = 0; }
      const s = scan();
      const now = Date.now();
      if (!serverExitAt && s.serverExit) serverExitAt = now;
      if (!explorerExitAt && s.explorerExit) explorerExitAt = now;
      // Terminal: the wineserver is gone — the session can never ready.
      if (serverExitAt && now - serverExitAt > 10000) { note = "wineserver exited"; break; }
      // Terminal: explorer died (desktop cascade) and no recovery in grace.
      if (explorerExitAt && now - explorerExitAt > 45000) { note = "explorer exited, no recovery in 45s grace"; break; }
      await page.waitForTimeout(1000);
    }
    if (cls !== "started") {
      const s = scan();
      // The race family: the deskpin/explorer signature, or explorer dead
      // with the pin never ready. Pin-ready-but-page-unready is a DIFFERENT
      // defect and stays "other" so it cannot hide inside the race bucket.
      cls = (!s.deskpinReady && (s.signature || s.explorerExit || s.serverExit)) ? "race-failed" : "other";
      if (!note) note = "budget exhausted";
    }
  } catch (e) {
    note = "harness error: " + String(e).slice(0, 200);
  } finally {
    await writeFile(join(dir, "console.log"), lines.join("\n") + "\n");
    try { await page.close(); } catch {}
  }
  const s = scan();
  const rec = { boot: i, class: cls, readyMs, wallMs: Date.now() - t0, loadStart, loadEnd: loadavg(),
    deskpinReady: s.deskpinReady, signature: s.signature, explorerExit: s.explorerExit, serverExit: s.serverExit,
    restarts, rescued: cls === "started" && restarts > 0,
    vkboot: RE.vkboot.test(lines.join("\n")), note };
  results.push(rec);
  console.log(`boot ${i}: ${cls}${readyMs ? " in " + (readyMs / 1000).toFixed(1) + "s" : ""} (load ${loadStart[0]} -> ${rec.loadEnd[0]}) ${note}`);
  await writeFile(join(OUT, "results.jsonl"), results.map(r => JSON.stringify(r)).join("\n") + "\n");
  return rec;
}

for (let i = 1; i <= N; i++) {
  await waitForBuildIdle();
  let slot;
  try {
    slot = await acquireProbeSlot({ timeoutMs: 600000, kind: "heavy" });
  } catch (e) {
    console.error("could not acquire probe slot: " + String(e).slice(0, 200));
    break;
  }
  try { await oneBoot(i); } finally { await slot.release(); }
}

const started = results.filter(r => r.class === "started");
const summary = { dist: { git_sha: buildInfo.git_sha, wasm: buildInfo.wasm_sha256 }, n: results.length,
  started: started.length, rescued: results.filter(r => r.rescued).length,
  raceFailed: results.filter(r => r.class === "race-failed").length,
  other: results.filter(r => r.class === "other").length,
  rate: results.length ? started.length / results.length : 0,
  medianReadyMs: started.length ? started.map(r => r.readyMs).sort((a, b) => a - b)[Math.floor(started.length / 2)] : null,
  budgetMs: BUDGET_MS, arch: ARCH };
await writeFile(join(OUT, "summary.json"), JSON.stringify(summary, null, 2));
console.log("SUMMARY " + JSON.stringify(summary));
await browser.close();
await browserServer.close();
server.close();
await rm(root, { recursive: true, force: true });
