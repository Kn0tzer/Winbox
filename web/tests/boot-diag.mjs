// boot-diag.mjs — diagnostic v2 for the session-reliability lane.
// Boots ONE session on the suite page path and watches the guest-side state.
// When the boot shows the failure pattern (pin window-creation failure text
// seen, or 150s without readiness), it spawns a FRESH desktop-pin process
// into the still-running session and records: the fresh pin's pid, whether
// it stays alive, and whether the session reaches READY afterwards.
// Question answered: can a pin process started after the failure still pin
// the session (=> supervision/respawn can rescue), or is the session itself
// unrecoverable by then?

import { chromium } from "@playwright/test";
import { serve } from "../../tools/browser/serve.mjs";
import { acquireProbeSlot } from "./probe-slots.mjs";
import { mkdir, mkdtemp, readdir, symlink, rm, readFile, writeFile } from "node:fs/promises";
import { readFileSync } from "node:fs";
import { resolve, join } from "node:path";
import { tmpdir } from "node:os";

const DIST = resolve(process.env.BOOTRATE_DIST || "dist");
const TAG = process.env.BOOTDIAG_TAG || "boot-diag";
const FALLBACK_MS = Number(process.env.BOOTDIAG_FALLBACK_MS || 150000);
const OBSERVE_MS = Number(process.env.BOOTDIAG_OBSERVE_MS || 300000);
const OUT = join("test-results", TAG);
await mkdir(OUT, { recursive: true });

const root = await mkdtemp(join(tmpdir(), "bw-bootdiag-"));
for (const f of await readdir(DIST)) await symlink(join(DIST, f), join(root, f));
const server = await serve(root);
server.url = "http://127.0.0.1:" + server.address().port;
const buildInfo = JSON.parse(await readFile(join(DIST, "runtime/BUILD_INFO.json"), "utf8"));
console.log(`boot-diag2: sha=${buildInfo.git_sha.slice(0, 9)} observe=${OBSERVE_MS}ms`);

const slot = await acquireProbeSlot({ timeoutMs: 600000, kind: "heavy" });
const browserServer = await chromium.launchServer({
  executablePath: process.env.CHROME_PATH || "/usr/local/bin/chromium", headless: true,
  args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu",
    "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader", "--enable-features=Vulkan",
    "--remote-debugging-address=127.0.0.1", "--remote-debugging-port=0"],
});
const browser = await chromium.connect(browserServer.wsEndpoint());
const lines = [];
const page = await browser.newPage();
page.on("console", m => lines.push(`${m.type()}: ${m.text()}`));
page.on("pageerror", e => lines.push(`[pageerror] ${e.message}`));
const t0 = Date.now();
const el = () => Date.now() - t0;
const has = re => re.test(lines.join("\n"));
const ready = async () => { try { return await page.evaluate(() => globalThis.bwRuntime?.ready?.() === true); } catch { return false; } };
const call = (fn, types, args) => page.evaluate(([f, t, a]) => globalThis.bwRuntime.call(f, t, a), [fn, types, args]);
const events = [];
const note = msg => { const s = `t=${el()}ms ${msg}`; events.push(s); console.log(s); };
try {
  const params = new URLSearchParams({ p: "cmd /c echo vkboot", session: "1", persist: "0", fresh: "1", arch: "x64" });
  await page.goto(`${server.url}/runtime/index.html?${params}`);
  // Phase 1: wait for the failure pattern (or fallback deadline, or ready).
  let triggered = "fallback";
  while (el() < FALLBACK_MS) {
    if (await ready()) { triggered = "ready"; break; }
    if (has(/The explorer process failed to start/)) { triggered = "signature"; break; }
    await page.waitForTimeout(2000);
  }
  note(`phase1 end: triggered=${triggered} explorerExit=${has(/exit_group.*explorer\.exe \/desktop/)} pinPrintFail=${has(/nodrv_CreateWindow/)}`);
  if (triggered !== "ready") {
    // Phase 2 (diag3): fresh EXPLORER first (recreate the desktop), wait,
    // then a fresh pin.
    const expRet = await call("bw64_spawn", ["string", "string"],
      ["/usr/lib/wine/wine64\nC:\\windows\\system32\\explorer.exe\n/desktop", ""]);
    note(`fresh explorer spawn ret=${expRet}`);
    await page.waitForTimeout(20000);
    const spawnRet = await call("bw64_spawn", ["string", "string"],
      ["/usr/lib/wine/wine64\nZ:\\home\\username\\deskpin.exe", ""]);
    const freshPid = null;
    note(`fresh pin spawn ret=${spawnRet} (post-explorer)`);
    const tInt = Date.now();
    let lastLog = 0;
    while (Date.now() - tInt < OBSERVE_MS) {
      if (await ready()) { note(`READY after intervention (+${Date.now() - tInt}ms)`); break; }
      if (Date.now() - lastLog > 30000) {
        lastLog = Date.now();
        const alive = true ? await call("bw64_pid_alive", ["number"], [14]).catch(() => "?") : "?";
        note(`observing +${Date.now() - tInt}ms: ready=false freshPinAlive=${alive} pinReadyText=${has(/deskpin: READY/)} explorerExit=${has(/exit_group.*explorer\.exe \/desktop/)}`);
      }
      await page.waitForTimeout(2000);
    }
    note(`final: ready=${await ready()} pinReadyText=${has(/deskpin: READY/)}`);
  }
  await writeFile(join(OUT, "result.json"), JSON.stringify({
    sha: buildInfo.git_sha, triggered, events,
    load: readFileSync("/proc/loadavg", "utf8").trim(),
  }, null, 2));
} finally {
  await writeFile(join(OUT, "console.log"), lines.join("\n") + "\n");
  try { await page.close(); } catch {}
  await browser.close();
  await browserServer.close();
  server.close();
  await slot.release();
  await rm(root, { recursive: true, force: true });
}
console.log("boot-diag2 done");
