// Chunk-forensic probe: runs a D3D9 guest exe, saves RAW bridge chunk bytes
// to test-results/<TAG>/chunks/chunk-XXXX.bin for offline decoding.
// Env: TAG, EXE_PATH, EXE_NAME, ASSETS (json [[name,path]..]), GUEST_ENV,
//      STOP_RE (regex on console logs to stop, e.g. "d3dconf9: DONE"),
//      MAX_CHUNKS (stop after this many chunks; 0 = no limit),
//      SCREENSHOT=1 to capture vkCanvas at end.
import { chromium } from "@playwright/test";
import { serve } from "/home/ubuntu/boxedwine64/tools/browser/serve.mjs";
import { mkdir, writeFile, readFile } from "node:fs/promises";
import { acquireProbeSlot } from "/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs";

const tag = process.env.TAG || `forensic-${Date.now()}`;
const dir = `test-results/${tag}`;
const cdir = `${dir}/chunks`;
await mkdir(cdir, { recursive: true });
const exePath = process.env.EXE_PATH, exeName = process.env.EXE_NAME;
const assets = JSON.parse(process.env.ASSETS || "[]");
const guestEnv = process.env.GUEST_ENV || "";
const stopRe = process.env.STOP_RE ? new RegExp(process.env.STOP_RE) : null;
const maxChunks = Number(process.env.MAX_CHUNKS || 0);

const server = await serve(process.env.WINE_DIST_DIR || "dist");
const slot = await acquireProbeSlot();
console.log("SLOT ACQUIRED");
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || "/usr/local/bin/chromium", headless: true,
  args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu",
    "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader", "--enable-features=Vulkan"],
});
const timeout = Number(process.env.TIMEOUT || 1800000);
const logs = [];
let chunkCount = 0, stop = false;
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  page.on("console", m => { logs.push(`${m.type()}: ${m.text()}`); if (stopRe && stopRe.test(m.text())) stop = true; });
  page.on("pageerror", e => logs.push(`pageerror: ${e.message}`));
  await page.exposeFunction("saveVkChunk", async b64 => {
    const idx = String(chunkCount++).padStart(4, "0");
    await writeFile(`${cdir}/chunk-${idx}.bin`, Buffer.from(b64, "base64"));
    if (chunkCount % 20 === 0) console.log("CHUNKS", chunkCount);
    if (maxChunks > 0 && chunkCount >= maxChunks) stop = true;
  });
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  const bt = Date.now() + 90000;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bt) { console.log("BOOTSTRAP TIMEOUT"); break; }
    await page.waitForTimeout(500);
  }
  await page.evaluate(async () => {
    const { installVkPageTier } = await import("../vkwebgpu.mjs");
    window.bwVkTier = installVkPageTier();
    const original = window.bwVkChunk;
    window.bwVkChunk = (bytes, flags) => {
      let str = "";
      for (let i = 0; i < bytes.length; i += 8192) str += String.fromCharCode(...bytes.subarray(i, i + 8192));
      window.saveVkChunk(btoa(str));
      return original(bytes, flags);
    };
  });
  for (const [name, path] of [[exeName, exePath], ...assets]) {
    const bytes = await readFile(path);
    const b64 = bytes.toString("base64");
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree("/root/home/username");
      fs.writeFile("/root/home/username/" + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      if (window.bwRuntime.call("bw64_register_file", ["string"], ["/home/username/" + name]) !== 1)
        throw new Error("VFS registration failed for " + name);
    }, { name, b64 });
  }
  console.log("ASSETS STAGED");
  const spawn = await page.evaluate(([exe, env]) => window.bwRuntime.call("bw64_spawn", ["string", "string"],
    ["/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\" + exe, env]), [exeName, guestEnv]);
  console.log("SPAWNED", spawn);
  const until = Date.now() + timeout;
  let lastLog = 0;
  while (Date.now() < until && !stop) {
    await page.waitForTimeout(5000);
    if (Date.now() - lastLog > 30000) {
      lastLog = Date.now();
      console.log("PROGRESS chunks=" + chunkCount, "tail=" + JSON.stringify(logs.slice(-3)));
    }
  }
  console.log("STOPPED chunks=", chunkCount);
  if (process.env.SCREENSHOT === "1") {
    const shot = await page.evaluate(() => document.getElementById("vkCanvas")?.toDataURL("image/png"));
    if (shot) await writeFile(`${dir}/canvas.png`, Buffer.from(shot.split(",")[1], "base64"));
  }
  await writeFile(`${dir}/console.log`, logs.join("\n"));
  await page.close();
} finally {
  console.log("CHUNKS SAVED", chunkCount, "to", cdir);
  await slot.release();
  await browser.close();
  server.close();
}
