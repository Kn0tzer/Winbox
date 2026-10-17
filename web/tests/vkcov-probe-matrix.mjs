// vkcov-matrix probe: matrix variant with extra library staging (libunwind, liblzma).
// Run from repo root: TAG=unique TIMEOUT=1800000 node web/tests/vkcov-probe.mjs
// Uses acquireProbeSlot(); stages vkcov.exe + vulkan-1.dll PE shim.
// Parses "VKCOV: name=rc1,rc2:us1:us2" lines from the guest log.
import { chromium } from "@playwright/test";
import { serve } from "../../tools/browser/serve.mjs";
import { mkdir, writeFile, readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';

const tag = process.env.TAG || `vkcov-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error("TAG must contain only letters, numbers, _, . or -");
const dir = `test-results/bridge-coverage-${tag}`;
await mkdir(dir, { recursive: true });

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
const flush = () => writeFile(`${dir}/console.log`, logs.join("\n"));
const timeout = Number(process.env.TIMEOUT || 1800000);
let summary = { passed: false };
const provenance = [];
try {
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log("SESSION READY");
  const assets = [
    ["vkcov.exe", process.env.VKCOV_PATH || "tools/vkcov/vkcov.exe"],
    // PE Vulkan shim: app dir wins loader search order over Wine builtin.
    ["vulkan-1.dll", process.env.VK_SHIM_PATH || "tools/vkd3d/probe/vulkan-1.dll"],
    ["libvulkan.so.1", "tools/rootfs64/libvk64/libvulkan.so.1"],
    // Staged per parent instruction 2026-10-09: guest probes for these during
    // bootstrap (libunwind.so.8, liblzma.so.5); missing libs were implicated
    // in a stuck bootstrap. Harmless if not needed (LD_LIBRARY_PATH already
    // includes the staging dir).
    ["libunwind.so.8", "/usr/lib/x86_64-linux-gnu/libunwind.so.8"],
    ["liblzma.so.5", "/usr/lib/x86_64-linux-gnu/liblzma.so.5"],
  ];
  for (const [name, path] of assets) {
    const bytes = await readFile(path);
    provenance.push({ name, path, bytes: bytes.length, sha256: createHash("sha256").update(bytes).digest("hex") });
    const b64 = bytes.toString("base64");
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree("/root/home/username");
      fs.writeFile("/root/home/username/" + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      const registered = window.bwRuntime.call("bw64_register_file", ["string"], ["/home/username/" + name]);
      if (registered !== 1) throw new Error("Guest VFS registration failed for " + name);
    }, { name, b64 });
  }
  console.log("ASSETS STAGED", JSON.stringify(provenance.map(p => [p.name, p.bytes])));
  const bootstrapUntil = Date.now() + timeout;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootstrapUntil) throw new Error("Wine bootstrap sentinel timed out");
    await flush();
    await page.waitForTimeout(500);
  }
  console.log("BOOTSTRAP COMPLETE");
  const spawn = await page.evaluate(() => window.bwRuntime.call("bw64_spawn", ["string", "string"],
    ["/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\vkcov.exe", "BW64_VKTRACE=0\nWINEDLLOVERRIDES=vulkan-1=n;winevulkan=n\nLD_LIBRARY_PATH=/home/username"]));
  console.log("SPAWN", spawn);
  const until = Date.now() + timeout;
  let lastPrint = 0;
  while (Date.now() < until) {
    if (Date.now() - lastPrint > 15000) {
      lastPrint = Date.now();
      const n = logs.filter(l => l.includes("VKCOV:")).length;
      console.log("PROGRESS", JSON.stringify({ vkcovLines: n, tail: logs.slice(-2) }));
      await flush();
    }
    if (logs.some(l => /vkcov: RESULT [01]/.test(l))) {
      await page.waitForTimeout(3000);
      break;
    }
    if (logs.some(l => new RegExp('exit_group syscall, status=[1-9]\\d*.*cmd=.*vkcov\\.exe').test(l))) break;
    if (errors.some(l => /unreachable|memory access out of bounds|Aborted/.test(l))) break;
    if (logs.some(l => /^error:.*(memory access out of bounds|unreachable|Aborted|worker sent an error)/.test(l))) break;
    await page.waitForTimeout(1000);
  }
  const joined = logs.join("\n");
  const vkcovLines = logs.filter(l => l.includes("VKCOV:"));
  const tryLines = logs.filter(l => l.includes("VKCOV-TRY:"));
  const resultMatch = joined.match(/vkcov: RESULT ([01])/);
  // Parse per-handler results
  const results = {};
  for (const l of vkcovLines) {
    const m = l.match(/VKCOV: (\S+)=(-?\d+),(-?\d+):(\d+):(\d+)/);
    if (m) results[m[1]] = { rc1: Number(m[2]), rc2: Number(m[3]), us1: Number(m[4]), us2: Number(m[5]) };
  }
  const lastTry = tryLines.length ? tryLines[tryLines.length - 1].match(/VKCOV-TRY: (\S+)/)?.[1] : null;
  summary = {
    passed: resultMatch?.[1] === "0",
    vkcovResult: resultMatch ? Number(resultMatch[1]) : null,
    handlersTested: Object.keys(results).length,
    lastTryBeforeEnd: lastTry,
    provenance, errors,
  };
  await writeFile(`${dir}/vkcov-results.json`, JSON.stringify(results, null, 2));
} catch (e) {
  summary.failure = String(e.stack || e);
} finally {
  await flush();
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  console.log("VKCOV VERDICT", JSON.stringify(summary));
  await slot.release().catch(() => {}); console.log("PROBE SLOT RELEASED");
  await browser.close(); server.close();
  if (!summary.passed) process.exitCode = 1;
}
