// JRCXZ regression boot probe: boots 64-bit Wine, waits for vkboot sentinel,
// scans for CPU faults / illegal-instruction / unhandled-opcode errors.
import { chromium } from "@playwright/test";
import { serve } from "/home/ubuntu/boxedwine64/tools/browser/serve.mjs";
import { mkdir, writeFile } from "node:fs/promises";
import { acquireProbeSlot } from "/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs";

const slot = await acquireProbeSlot();
try {
const dir = "test-results/jrcxz-boot1";
await mkdir(dir, { recursive: true });
const server = await serve("dist");
const browser = await chromium.launch({
  executablePath: "/usr/local/bin/chromium", headless: true,
  args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu",
    "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader", "--enable-features=Vulkan"],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [], errors = [];
page.on("console", m => logs.push(m.type() + ": " + m.text()));
page.on("pageerror", e => errors.push(e.message));
const flush = () => writeFile(dir + "/console.log", logs.join("\n"));
const timeout = 900000;
const t0 = Date.now();
let passed = false, error = null, cpuFaults = 0;
try {
  await page.goto("http://127.0.0.1:" + server.address().port + "/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=0&persist=0");
  await page.waitForFunction(() => window.bwRuntime && window.bwRuntime.ready && window.bwRuntime.ready() === true, null, { timeout });
  const deadline = Date.now() + timeout;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > deadline) throw new Error("vkboot sentinel timed out");
    await flush();
    await page.waitForTimeout(1000);
  }
  const bootMs = Date.now() - t0;
  await page.waitForTimeout(3000);
  await flush();
  const faultRe = /illegal instruction|unhandled opcode|CPU64.*fault|SIGILL/i;
  const faultLines = logs.filter(l => faultRe.test(l));
  cpuFaults = faultLines.length;
  passed = cpuFaults === 0;
  if (!passed) error = "found " + cpuFaults + " cpu-fault lines";
  console.log("BOOT OK in " + bootMs + "ms, cpuFaults=" + cpuFaults);
  await writeFile(dir + "/summary.json", JSON.stringify({ passed, bootMs, cpuFaults, faultLines: faultLines.slice(0,10), errors: errors.slice(0,5) }, null, 2));
} catch (e) {
  console.log("PROBE FAILED:", e.message);
  await flush();
  await writeFile(dir + "/summary.json", JSON.stringify({ passed: false, error: e.message, errors: errors.slice(0,5) }));
} finally {
  await browser.close();
  server.close();
}
} finally {
  slot.release();
}
