import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

import { chromium } from "@playwright/test";
import { serve } from "../../../tools/browser/serve.mjs";
import { writeFile, mkdir } from "node:fs/promises";
import { acquireProbeSlot } from "../probe-slots.mjs";
const TAG = "20261007-wb4";
const dir = `test-results/${TAG}`;
await mkdir(dir, { recursive: true });
const slot = await acquireProbeSlot();
try {
  const server = await serve("dist");
  const browser = await chromium.launch({
    executablePath: "/usr/local/bin/chromium", headless: true,
    args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu", "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader", "--enable-features=Vulkan"],
  });
  const page = await browser.newPage();
  const logs = [];
  page.on("console", m => logs.push(`${m.type()}: ${m.text()}`));
  page.on("pageerror", e => logs.push(`PAGEERROR: ${e.message}`));
  const flush = () => writeFile(`${dir}/console.log`, logs.join("\n"));
  // Try with full Windows path like 64-bit does
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?arch=x86&session=1&novideo=1&persist=0&p=C:%5Cwindows%5Csystem32%5Cwineboot.exe%20--init`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout: 300000 });
  console.log("READY");
  for (let i = 0; i < 20; i++) {
    await page.waitForTimeout(2000);
    await flush();
    if (logs.some(l => l.includes("create_winstation"))) break;
  }
  await flush();
  console.log("DONE winstation=", logs.some(l => l.includes("create_winstation")));
  await browser.close();
  server.close();
} finally { await slot.release(); }
