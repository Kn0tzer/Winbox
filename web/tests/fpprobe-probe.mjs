// fpprobe probe (FD-87 write-loss lane): boots one Wine session from this
// tree's dist, stages the fpprobe fixtures, spawns each in turn, and captures
// their FP* output lines (plus any BSSDIAG lines when SPAWNENV asks for them).
// Run from repo root:
//   TAG=unique EXES=fpprobe7-x64.exe,fpprobe8-x64.exe node web/tests/fpprobe-probe.mjs
// Optional: SPAWNENV="BW64_BSSDIAG=img|BW64_VKTRACE=0" (pipe-separated lines).
import { chromium } from "@playwright/test";
import { serve } from "../../tools/browser/serve.mjs";
import { mkdir, writeFile, readFile, appendFile } from "node:fs/promises";
import { acquireProbeSlot } from "/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs";

const tag = process.env.TAG || `fpprobe-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error("TAG must contain only letters, numbers, _, . or -");
const dir = `test-results/fd87loss/${tag}`;
await mkdir(dir, { recursive: true });

const exes = (process.env.EXES || "fpprobe7-x64.exe").split(",").map(s => s.trim()).filter(Boolean);
const extraEnv = (process.env.SPAWNENV || "").split("|").map(s => s.trim()).filter(Boolean);
const timeout = Number(process.env.TIMEOUT || 600000);

const server = await serve(process.env.WINE_DIST_DIR || "dist");
const slot = await acquireProbeSlot();
console.log("PROBE SLOT ACQUIRED");
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || "/usr/local/bin/chromium", headless: true,
  args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu",
    "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader", "--enable-features=Vulkan"],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [], errors = [];
page.on("console", m => logs.push(`${m.type()}: ${m.text()}`));
page.on("pageerror", e => errors.push(e.message));
let flushedCount = 0;
const flush = async () => {
  if (logs.length === flushedCount) return;
  const chunk = logs.slice(flushedCount).join("\n") + "\n";
  flushedCount = logs.length;
  await appendFile(`${dir}/console.log`, chunk);
};
let summary = { passed: false, exes, fpLines: [] };
try {
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0${process.env.URLPARAMS || ""}`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log("SESSION READY");
  const assets = [...exes, "fphelp.dll"];
  for (const name of assets) {
    const bytes = await readFile(`test-results/fd87loss/fixtures/${name}`);
    const b64 = bytes.toString("base64");
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree("/root/home/username");
      fs.writeFile("/root/home/username/" + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      const registered = window.bwRuntime.call("bw64_register_file", ["string"], ["/home/username/" + name]);
      if (registered !== 1) throw new Error("Guest VFS registration failed for " + name);
    }, { name, b64 });
  }
  console.log("ASSETS STAGED");
  const bootstrapUntil = Date.now() + timeout;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootstrapUntil) throw new Error("Wine bootstrap sentinel timed out");
    await flush();
    await page.waitForTimeout(500);
  }
  console.log("BOOTSTRAP COMPLETE");
  const envBlock = [...extraEnv, "LD_LIBRARY_PATH=/home/username"].join("\n");
  for (const exe of exes) {
    const before = logs.length;
    const spawn = await page.evaluate(({ exe, envBlock }) => window.bwRuntime.call(
      "bw64_spawn", ["string", "string"],
      ["/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\" + exe, envBlock]), { exe, envBlock });
    console.log("SPAWN", exe, spawn);
    // Wait for this exe's FP output lines; end 15 s after the last NEW
    // FP-matching line (the session emits METRICS lines forever, so raw
    // console quiet is not a usable signal). Hard cap 240 s per exe.
    const until = Date.now() + Math.min(timeout, 1200000);
    const fpRe = /FP\d|final|bad=|acc=|zeros/;
    let lastFpCount = 0, lastFpAt = 0;
    while (Date.now() < until) {
      await page.waitForTimeout(1000);
      await flush();
      const fresh = logs.slice(before);
      const fpCount = fresh.filter(l => fpRe.test(l)).length;
      if (fpCount !== lastFpCount) { lastFpCount = fpCount; lastFpAt = Date.now(); }
      if (fpCount > 0 && Date.now() - lastFpAt > 15000) break;
      if (errors.some(l => /unreachable|memory access out of bounds|Aborted/.test(l))) break;
    }
    const fresh = logs.slice(before).filter(l => /FP\d|final|bad|acc|zeros|sinv|traj/i.test(l));
    console.log(`=== ${exe} output lines ===`);
    for (const l of fresh) console.log(l);
    summary.fpLines.push(...fresh);
  }
  summary.passed = summary.fpLines.length > 0;
} catch (e) {
  console.log("PROBE ERROR", e.message);
  summary.error = e.message;
} finally {
  await flush();
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  await browser.close();
  await slot.release();
  console.log("SLOT RELEASED");
}
console.log("SUMMARY", JSON.stringify({ passed: summary.passed, lines: summary.fpLines.length }));
process.exit(0);
