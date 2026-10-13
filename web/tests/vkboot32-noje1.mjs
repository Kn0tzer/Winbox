import { chromium } from "@playwright/test";
import { serve } from "/home/ubuntu/boxedwine64/tools/browser/serve.mjs";
import { acquireProbeSlot } from "/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs";
import { waitForBuildIdle } from "/home/ubuntu/boxedwine64/web/tests/build-lock.mjs";
import { readFileSync } from "node:fs";
const tag = "vkboot32-noje1";
const dir = "/home/ubuntu/boxedwine64/test-results/" + tag;
const { mkdir, writeFile } = await import("node:fs/promises");
await mkdir(dir, { recursive: true });
const slot = await acquireProbeSlot();
console.log("SLOT", slot.index);
try {
  await waitForBuildIdle();
  const server = await serve("/home/ubuntu/boxedwine64/dist-inst");
  const port = server.address().port;
  console.log("SERVER", port);
  const browser = await chromium.launch({
    executablePath: "/usr/local/bin/chromium",
    headless: true,
    args: ["--no-sandbox","--disable-dev-shm-usage","--enable-unsafe-webgpu","--enable-unsafe-swiftshader","--use-angle=vulkan","--use-vulkan=swiftshader","--enable-features=Vulkan"]
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [];
  let logText = "";
  page.on("console", m => { logs.push(m.type() + ": " + m.text()); logText += "\n" + m.text(); });
  page.on("pageerror", e => { logs.push("pageerror: " + e.message); logText += "\npageerror: " + e.message; });
  const url = "http://127.0.0.1:" + port + "/runtime/index.html?arch=x86&p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&";
  await page.goto(url, { timeout: 60000 });
  console.log("NAVIGATED");
  const t0 = Date.now();
  let doneReason = "timeout";
  while (Date.now() - t0 < 420000) {
    await page.waitForTimeout(3000);
    if (logText.includes("INSTTRACE end")) { doneReason = "insttrace-dump"; break; }
    if (/^log: vkboot$/m.test(logText)) { doneReason = "vkboot"; break; }
  }
  console.log("EXIT-REASON", doneReason);
  await page.waitForTimeout(5000);
  logText = logs.join("\n");
  await writeFile(dir + "/console.log", logText);
  const riphits = (logText.match(/RIPHIT.*/g) || []);
  const vkbootCount = (logText.match(/^log: vkboot$/gm) || []).length;
  const esiZero = (logText.match(/ESI=00000000/g) || []).length;
  let buildInfo = null;
  try { buildInfo = readFileSync("/home/ubuntu/boxedwine64/dist-inst/runtime/INST_TRACE_BUILD.txt", "utf8").trim(); } catch {}
  const summary = { tag, doneReason, vkbootFound: vkbootCount > 0, esiZero,
    riphits, riphitsCount: riphits.length,
    instBuild: buildInfo, logLines: logs.length };
  await writeFile(dir + "/summary.json", JSON.stringify(summary, null, 2));
  console.log("SUMMARY", JSON.stringify(summary));
  await browser.close();
  server.close();
} finally { await slot.release(); }
console.log("DONE");
