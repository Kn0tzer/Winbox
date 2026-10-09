// Single-exe D3D smoke runner. Env:
//   EXE_PATH   guest exe path in repo (e.g. tools/d3dtests/d10bare.exe)
//   EXE_NAME   file name (e.g. d10bare.exe)
//   DONE_RE    regex for completion line (e.g. "d10bare: DONE")
//   ASSETS     JSON array of [name, repoPath]
//   SPAWN_ENV  env string for bw64_spawn
//   TAG        result tag
import { chromium } from "@playwright/test";
import { serve } from "../../tools/browser/serve.mjs";
import { mkdir, writeFile, readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';

const tag = process.env.TAG || `single-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error("TAG bad");
const dir = `test-results/d3d-single-${tag}`;
await mkdir(dir, { recursive: true });
const exePath = process.env.EXE_PATH, exeName = process.env.EXE_NAME;
const doneRe = new RegExp(process.env.DONE_RE || "DONE");
const assets = JSON.parse(process.env.ASSETS || "[]");
const spawnEnv = process.env.SPAWN_ENV || "";

const server = await serve(process.env.WINE_DIST_DIR || "dist");
const slot = await acquireProbeSlot();
console.log("SLOT ACQUIRED");
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || "/usr/local/bin/chromium", headless: true,
  args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu",
    "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader", "--enable-features=Vulkan"],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [], errors = [];
page.on("console", m => logs.push(`${m.type()}: ${m.text()}`));
page.on("pageerror", e => errors.push(e.message));
const timeout = Number(process.env.TIMEOUT || 600000);
try {
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log("SESSION READY");
  const bt = Date.now() + timeout;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bt) throw new Error("bootstrap timeout");
    await page.waitForTimeout(500);
  }
  console.log("BOOTSTRAP COMPLETE");
  for (const [name, path] of [[exeName, exePath], ...assets]) {
    const bytes = await readFile(path);
    console.log("STAGE", name, bytes.length, createHash("sha256").update(bytes).digest("hex").slice(0, 12));
    const b64 = bytes.toString("base64");
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree("/root/home/username");
      fs.writeFile("/root/home/username/" + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      if (window.bwRuntime.call("bw64_register_file", ["string"], ["/home/username/" + name]) !== 1)
        throw new Error("register failed " + name);
    }, { name, b64 });
  }
  await page.evaluate(([exe, env]) => window.bwRuntime.call("bw64_spawn", ["string", "string"],
    ["/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\" + exe, env]), [exeName, spawnEnv]);
  console.log("SPAWNED");
  const until = Date.now() + timeout;
  let last = 0;
  while (Date.now() < until) {
    if (Date.now() - last > 20000) { last = Date.now(); console.log("TAIL", JSON.stringify(logs.slice(-3))); }
    if (logs.some(l => doneRe.test(l))) { await page.waitForTimeout(8000); break; }
    await page.waitForTimeout(1000);
  }
  console.log("FINISHED done=", logs.some(l => doneRe.test(l)));
} catch (e) { console.log("ERROR", e.message); }
await writeFile(`${dir}/console.log`, logs.join("\n"));
await writeFile(`${dir}/errors.log`, errors.join("\n"));
// vkCanvas screenshot for lit-pixel evidence.
try {
  const shot = await page.evaluate(() => document.getElementById("vkCanvas")?.toDataURL("image/png"));
  if (shot) await writeFile(`${dir}/canvas.png`, Buffer.from(shot.split(",")[1], "base64"));
  console.log("CANVAS", shot ? "saved" : "missing");
} catch (e) { console.log("CANVAS ERROR", e.message); }
console.log("LOGS WRITTEN", dir);
await slot.release();
await browser.close();
server.close();
