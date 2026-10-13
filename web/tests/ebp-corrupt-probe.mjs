// Minimal 32-bit boot probe for EBP=0xB4B corruption capture.
// Boots 32-bit Wine and watches for EBP_CORRUPT log from the diagnostic trap.
// Does NOT stage or spawn test PEs - just the bootstrap.
import { chromium } from "@playwright/test";
import { serve } from "/home/ubuntu/boxedwine64/tools/browser/serve.mjs";
import { acquireProbeSlot } from "/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs";
import { waitForBuildIdle } from "/home/ubuntu/boxedwine64/web/tests/build-lock.mjs";

const tag = process.argv[2] || "ebp-corrupt-1";
const dir = "/home/ubuntu/boxedwine64/test-results/" + tag;
const { mkdir, writeFile } = await import("node:fs/promises");
await mkdir(dir, { recursive: true });

const slot = await acquireProbeSlot();
console.log("SLOT", slot.index);
try {
  await waitForBuildIdle();
  const server = await serve("/home/ubuntu/boxedwine64/dist");
  const port = server.address().port;
  console.log("SERVER", port);

  const browser = await chromium.launch({
    executablePath: "/usr/local/bin/chromium",
    headless: true,
    args: ["--no-sandbox","--disable-dev-shm-usage","--enable-unsafe-webgpu","--enable-unsafe-swiftshader","--use-angle=vulkan","--use-vulkan=swiftshader","--enable-features=Vulkan"]
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [];
  page.on("console", m => logs.push(m.type() + ": " + m.text()));
  page.on("pageerror", e => logs.push("pageerror: " + e.message));

  const url = "http://127.0.0.1:" + port + "/runtime/index.html?arch=x86&p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&";
  await page.goto(url, { timeout: 60000 });
  console.log("NAVIGATED");
  await page.waitForFunction(() => window.bwRuntime && window.bwRuntime.fs, null, { timeout: 120000 });
  console.log("RUNTIME READY");

  // Watch for EBP_CORRUPT or vkboot or crash, up to 5 minutes
  console.log("WATCHING FOR EBP_CORRUPT...");
  const deadline = Date.now() + 300000;
  let found = null;
  while (Date.now() < deadline) {
    const text = logs.join("\n");
    if (text.includes("EBP_CORRUPT")) { found = "EBP_CORRUPT"; break; }
    if (/^log: vkboot\s*$/m.test(text)) { found = "VKBOOT"; break; }
    if (text.includes("Page Fault at 00000000") || text.includes("0xD3F8")) { found = "CRASH"; break; }
    await page.waitForTimeout(2000);
  }
  console.log("RESULT:", found);

  const logText = logs.join("\n");
  await writeFile(dir + "/console.log", logText);
  // Extract EBP_CORRUPT lines and surrounding context
  const lines = logText.split("\n");
  const corruptIdx = lines.findIndex(l => l.includes("EBP_CORRUPT"));
  let context = "";
  if (corruptIdx >= 0) {
    const start = Math.max(0, corruptIdx - 5);
    const end = Math.min(lines.length, corruptIdx + 15);
    context = lines.slice(start, end).join("\n");
    await writeFile(dir + "/ebp-corrupt-context.txt", context);
  }
  await writeFile(dir + "/summary.json", JSON.stringify({ tag, result: found, hasCorrupt: corruptIdx >= 0 }, null, 2));
  console.log("SUMMARY", JSON.stringify({ tag, result: found, hasCorrupt: corruptIdx >= 0 }));

  await browser.close();
  server.close();
} finally {
  await slot.release();
}
