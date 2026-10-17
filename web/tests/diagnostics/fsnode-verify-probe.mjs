import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

import { chromium } from "@playwright/test";
import { serve } from "../../../tools/browser/serve.mjs";
import { writeFile, mkdir } from "node:fs/promises";
import { acquireProbeSlot } from "../probe-slots.mjs";

const TAG = process.env.PROBE_TAG || "20261007-fsnode1";
const dir = "test-results/" + TAG;
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
  page.on("console", m => logs.push(m.type() + ": " + m.text()));
  page.on("pageerror", e => logs.push("PAGEERROR: " + e.message));
  const flush = () => writeFile(dir + "/console.log", logs.join("\n"));

  await page.goto("http://127.0.0.1:" + server.address().port + "/runtime/index.html?arch=x86&session=1&novideo=1&persist=0&p=cmd%20%2Fc%20echo%20vkboot");
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout: 300000 });
  console.log("SESSION READY");

  for (let i = 0; i < 150; i++) {
    await page.waitForTimeout(2000);
    await flush();
    const hasWinstation = logs.some(l => l.includes("create_winstation"));
    const corruption = logs.filter(l => l.includes("could not open working directory")).length;
    const hasVkboot = logs.some(l => /vkboot/.test(l));
    if (i % 10 === 0) console.log("t=" + i*2 + "s winstation=" + hasWinstation + " corruption=" + corruption + " vkboot=" + hasVkboot);
    if (hasWinstation) break;
  }
  await flush();
  console.log("DONE");
  await browser.close();
  server.close();
} finally {
  await slot.release();
}
