import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

import { chromium } from "@playwright/test";
import { serve } from "../../../tools/browser/serve.mjs";
import { writeFile, mkdir } from "node:fs/promises";
import { acquireProbeSlot } from "../probe-slots.mjs";

const TAG = process.env.PROBE_TAG || "vkboot32-real";
const dir = `test-results/${TAG}`;
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

  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?arch=x86&session=1&novideo=1&persist=0&p=cmd%20%2Fc%20echo%20vkboot`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout: 300000 });
  console.log("SESSION READY");

  let foundVkbootOutput = false;
  let winebootFailed = false;
  for (let i = 0; i < 150; i++) {
    await page.waitForTimeout(2000);
    await flush();
    const outputLines = logs.filter(l => {
      if (l.includes("argv") || l.includes("Launching") || l.includes("Command line")) return false;
      return /vkboot/.test(l);
    });
    if (outputLines.length > 0) {
      console.log("Found vkboot output lines:");
      outputLines.forEach(l => console.log("  " + l));
      foundVkbootOutput = true;
      break;
    }
    winebootFailed = logs.some(l => l.includes("run_wineboot failed") || l.includes("failed to start wineboot"));
    const kernel32Failed = logs.some(l => l.includes("could not load kernel32.dll"));
    if (i % 15 === 0) {
      console.log(`t=${i*2}s winebootFailed=${winebootFailed} kernel32Failed=${kernel32Failed} logs=${logs.length}`);
      const recent = logs.filter(l => l.includes("wineboot") || l.includes("doSignal") || l.includes("socket")).slice(-3);
      recent.forEach(l => console.log("  " + l.substring(0, 130)));
    }
    if (winebootFailed && kernel32Failed && i > 30) break;
  }
  await flush();
  console.log(`DONE: vkbootOutput=${foundVkbootOutput} winebootFailed=${winebootFailed}`);
  await browser.close();
  server.close();
} finally {
  await slot.release();
}
