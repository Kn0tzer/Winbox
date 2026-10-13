import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

import { chromium } from "@playwright/test";
import { serve } from "../../../tools/browser/serve.mjs";
import { writeFile, mkdir } from "node:fs/promises";
import { acquireProbeSlot } from "../probe-slots.mjs";
const TAG = "20261007-b64cmp1";
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
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?session=1&novideo=1&persist=0&p=cmd%20%2Fc%20echo%20hi`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout: 300000 });
  await page.waitForTimeout(15000);
  await writeFile(`${dir}/console.log`, logs.join("\n"));
  console.log("DONE");
  await browser.close();
  server.close();
} finally { await slot.release(); }
