import { chromium } from "@playwright/test";
import { serve } from "/home/ubuntu/boxedwine64/tools/browser/serve.mjs";
import { acquireProbeSlot } from "/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs";
import { waitForBuildIdle } from "/home/ubuntu/boxedwine64/web/tests/build-lock.mjs";

const tag = "vkboot32-nullsafe3";
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

  const url = "http://127.0.0.1:" + port + "/runtime/index.html?arch=x86&p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&fresh=1";
  await page.goto(url, { timeout: 60000 });
  console.log("NAVIGATED");

  try {
    await page.waitForFunction(() => {
      return document.title.includes("done") || window.__vkbootDone === true;
    }, null, { timeout: 300000 });
    console.log("DONE-WAIT");
  } catch (e) {
    console.log("TIMEOUT waiting");
  }

  await page.waitForTimeout(15000);
  const logText = logs.join("\n");
  await writeFile(dir + "/console.log", logText);

  const count = (re) => (logText.match(re) || []).length;
  const lines = (s, n) => logText.split("\n").filter(l => l.includes(s)).slice(0, n || 5);

  const vkbootCount = count(/vkboot/gi);
  const summary = {
    tag,
    freeInvalid: count(/free\(\): invalid pointer/gi),
    segAccess: count(/SEG_ACCESS/g),
    segAccess110048: count(/SEG_ACCESS[^\n]*00110048/gi),
    segAccessLines: lines("SEG_ACCESS"),
    segMapper: count(/SEG_MAPPER/g),
    segMapperFBB4: count(/SEG_MAPPER[^\n]*1064FBB4/gi),
    segMapperLines: lines("SEG_MAPPER", 10),
    nullskip: count(/NULLSKIP/g),
    nullskipLines: lines("NULLSKIP", 10),
    skip14: count(/SKIP14/g),
    skip14Lines: lines("SKIP14", 10),
    gotfix: count(/GOTFIX/g),
    pebwatch: count(/PEBWATCH/gi),
    binpatch: count(/BINPATCH/g),
    inittable: count(/INITTABLE_SKIP/g),
    findenv: count(/FINDENV_INTERCEPT/g),
    vkbootCount: vkbootCount,
    vkbootFound: /vkboot/.test(logText) && vkbootCount > 2,
    crashLines: logText.split("\n").filter(l => /crash|FATAL|SIGSEGV/i.test(l)).slice(0, 20),
    logLines: logText.split("\n").length
  };
  await writeFile(dir + "/summary.json", JSON.stringify(summary, null, 2));
  console.log("SUMMARY " + JSON.stringify(summary, null, 2));

  await browser.close();
  server.close();
} finally {
  await slot.release();
}
