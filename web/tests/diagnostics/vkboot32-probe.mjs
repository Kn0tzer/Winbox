import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

import { chromium } from "@playwright/test";
import { serve } from "../../../tools/browser/serve.mjs";
import { writeFile, mkdir } from "node:fs/promises";
import { acquireProbeSlot } from "../probe-slots.mjs";

const TAG = process.env.PROBE_TAG || "vkboot32-test";
const dir = `${diagnosticsRepoRoot}/test-results/${TAG}`;
await mkdir(dir, { recursive: true });

const slot = await acquireProbeSlot();
console.log("Got slot", slot.index);
try {
  const server = await serve("dist");
  const browser = await chromium.launch({
    executablePath: "/usr/local/bin/chromium", headless: true,
    args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu",
      "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader", "--enable-features=Vulkan"],
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [];
  page.on("console", m => logs.push(`${m.type()}: ${m.text()}`));
  page.on("pageerror", e => logs.push(`PAGEERROR: ${e.message}`));
  const flush = () => writeFile(`${dir}/console.log`, logs.join("\n"));

  // Real 32-bit PE execution path WITH prefix (not --version)
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?arch=x86&session=1&novideo=1&persist=0&p=cmd%20%2Fc%20echo%20vkboot`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout: 300000 });
  console.log("SESSION READY");

  // Wait up to 180s and capture logs
  for (let i = 0; i < 90; i++) {
    await page.waitForTimeout(2000);
    await flush();
    const hasWinebootErr = logs.some(l => l.includes("run_wineboot failed") || l.includes("failed to start wineboot"));
    const hasVkboot = logs.some(l => /vkboot/.test(l) && !l.includes("echo vkboot"));
    const hasKernel32Err = logs.some(l => l.includes("could not load kernel32.dll"));
    const hasWinebootStart = logs.some(l => l.includes("wineboot") && l.includes("start"));
    if (i % 15 === 0) console.log(`t=${i*2}s winebootErr=${hasWinebootErr} kernel32Err=${hasKernel32Err} vkboot=${hasVkboot}`);
    if (hasVkboot) {
      console.log("VKBOOT FOUND!");
      break;
    }
    // If we see a definitive failure, break early
    if (hasWinebootErr && hasKernel32Err && i > 30) {
      console.log("Definitive failure detected, breaking");
      break;
    }
  }
  await flush();
  console.log("DONE");
  // Print key lines
  const key = logs.filter(l => l.includes("wineboot") || l.includes("kernel32") || l.includes("vkboot") || l.includes("WINEBOOT"));
  console.log("=== KEY LINES ===");
  key.slice(0, 30).forEach(l => console.log(l));
  await browser.close();
  server.close();
} finally {
  await slot.release();
}
