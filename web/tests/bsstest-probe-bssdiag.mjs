// bsstest probe: runs bsstest.exe in-browser, captures BSSDIAG + BSSTEST output.
// Run from repo root: TAG=unique TIMEOUT=600000 node web/tests/bsstest-probe.mjs
import { chromium } from "@playwright/test";
import { serve } from "../../tools/browser/serve.mjs";
import { mkdir, writeFile, readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';

const tag = process.env.TAG || `bsstest-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error("TAG must contain only letters, numbers, _, . or -");
const dir = `test-results/bssdiag-${tag}`;
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
const timeout = Number(process.env.TIMEOUT || 600000);
let summary = { passed: false };
try {
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0&bssdiag=0x14000d030`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log("SESSION READY");
  const assets = [
    ["bsstest.exe", "tools/vkcov/bsstest.exe"],
    ["vulkan-1.dll", "tools/vkd3d/probe/vulkan-1.dll"],
    ["libvulkan.so.1", "tools/rootfs64/libvk64/libvulkan.so.1"],
  ];
  for (const [name, path] of assets) {
    const bytes = await readFile(path);
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
  while (!logs.some(l => /log: vkboot/.test(l))) {
    if (Date.now() > bootstrapUntil) throw new Error("Wine bootstrap sentinel timed out");
    await flush();
    await page.waitForTimeout(500);
  }
  console.log("BOOTSTRAP COMPLETE");
  const spawn = await page.evaluate(() => window.bwRuntime.call("bw64_spawn", ["string", "string"],
    ["/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\bsstest.exe", "BW64_BSSDIAG=img\nBW64_VKTRACE=0\nWINEDLLOVERRIDES=vulkan-1=n;winevulkan=n\nLD_LIBRARY_PATH=/home/username"]));
  console.log("SPAWN", spawn);
  const until = Date.now() + timeout;
  while (Date.now() < until) {
    await flush();
    await page.waitForTimeout(2000);
    if (logs.some(l => /BSSTEST: (PASS|FAIL)/.test(l))) {
      console.log("BSSTEST DONE");
      await page.waitForTimeout(3000);
      break;
    }
    if (errors.some(l => /unreachable|memory access out of bounds|Aborted/.test(l))) break;
    if (logs.some(l => /^error:.*(memory access out of bounds|unreachable|Aborted|worker sent an error)/.test(l))) break;
  }
  await flush();
  const bssLines = logs.filter(l => l.includes("BSSDIAG") || l.includes("BSSTEST"));
  console.log("=== BSSDIAG+BSSTEST lines ===");
  for (const l of bssLines) console.log(l);
  summary.passed = logs.some(l => /BSSTEST: PASS/.test(l));
  summary.bssLines = bssLines;
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
console.log("SUMMARY", JSON.stringify({ passed: summary.passed }));
process.exit(summary.passed ? 0 : 1);
