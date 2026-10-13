import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

// Probe: capture MMAP_KERNEL logs during 32-bit boot to identify 0xC0874D56 creator
import { chromium } from "@playwright/test";
import { serve } from "../../../tools/browser/serve.mjs";
import { writeFile, mkdir } from "node:fs/promises";

const TAG = process.env.PROBE_TAG || "20261006-mmap-trace1";
const dir = (diagnosticsRepoRoot + '/test-results/') + TAG;
await mkdir(dir, { recursive: true });

const server = await serve((diagnosticsRepoRoot + '/dist'));
const browser = await chromium.launch({
  executablePath: "/usr/local/bin/chromium", headless: true,
  args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu",
    "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader", "--enable-features=Vulkan"],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [];
page.on("console", m => logs.push(m.type() + ": " + m.text()));
page.on("pageerror", e => logs.push("PAGEERROR: " + e.message));
const flush = () => writeFile(dir + "/console.log", logs.join("\n"));
const timeout = 300000;
const t0 = Date.now();
try {
  await page.goto("http://127.0.0.1:" + server.address().port + "/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&arch=x86&session=1&novideo=1&persist=0&gltrace=0");
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log("SESSION READY");
  // Wait for vkboot or timeout - we just need the boot logs
  const bootUntil = Date.now() + 180000;
  const stripAnsi = s => s.replace(/\x1b\[[0-9;?]*[a-zA-Z]/g, "");
  while (!logs.some(l => /^log: vkboot\s*$/.test(stripAnsi(l)))) {
    if (Date.now() > bootUntil) break;
    await flush();
    await page.waitForTimeout(2000);
  }
  await flush();
  // Extract MMAP_KERNEL and CALL_E32_ZERO lines
  const relevant = logs.filter(l => /MMAP_KERNEL|CALL_E32_ZERO|MPROTECT_KERNEL/.test(l));
  console.log("RELEVANT LINES:", relevant.length);
  relevant.slice(0, 50).forEach(l => console.log(" ", l.slice(0, 200)));
  await writeFile(dir + "/mmap-trace.txt", relevant.join("\n"));
} catch (e) {
  console.log("FAIL", String(e).slice(0, 200));
} finally {
  await flush();
  await browser.close(); server.close();
}
