// Chunk-capturing probe for d3dconf11b. Saves V2 chunks to disk for forensics.
import { chromium } from "@playwright/test";
import { serve } from "/home/ubuntu/boxedwine64/tools/browser/serve.mjs";
import { mkdir, writeFile, readFile } from "node:fs/promises";
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';

const tag = process.env.TAG || `chunkcap-${Date.now()}`;
const dir = `test-results/chunkcap-${tag}`;
await mkdir(dir, { recursive: true });

const server = await serve("dist");
const slot = await acquireProbeSlot();
console.log("SLOT ACQUIRED");
const browser = await chromium.launch({
  executablePath: "/usr/local/bin/chromium", headless: true,
  args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu",
    "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader", "--enable-features=Vulkan"],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [], errors = [];
page.on("console", m => logs.push(`${m.type()}: ${m.text()}`));
page.on("pageerror", e => errors.push(e.message));

let chunkCount = 0;
await page.exposeFunction("saveChunk", async b64 => {
  if (chunkCount < 5) {
    await writeFile(`${dir}/chunk-${chunkCount}.bin`, Buffer.from(b64, "base64"));
    chunkCount++;
  }
});

try {
  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0`);
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout: 600000 });
  console.log("SESSION READY");
  const bt = Date.now() + 600000;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bt) throw new Error("bootstrap timeout");
    await page.waitForTimeout(500);
  }
  console.log("BOOTSTRAP COMPLETE");
  await page.evaluate(() => {
    window.bwVkTierReady = Promise.resolve(window.bwVkTier);
    const original = window.bwVkChunk;
    window.bwVkChunk = (bytes, flags) => {
      let str = "";
      for (let i = 0; i < bytes.length; i += 8192) str += String.fromCharCode(...bytes.subarray(i, i + 8192));
      window.saveChunk(btoa(str));
      return original(bytes, flags);
    };
  });
  const assets = [["d3dconf11b.exe","tools/dxvk/probe/d3dconf11b.exe"],["d3d11.dll","tools/dxvk/build.w64/src/d3d11/d3d11.dll"],["dxgi.dll","tools/dxvk/build.w64/src/dxgi/dxgi.dll"],["libvulkan.so.1","tools/rootfs64/libvk64/libvulkan.so.1"]];
  for (const [name, path] of assets) {
    const bytes = await readFile("/home/ubuntu/boxedwine64/" + path);
    const b64 = bytes.toString("base64");
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree("/root/home/username");
      fs.writeFile("/root/home/username/" + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      if (window.bwRuntime.call("bw64_register_file", ["string"], ["/home/username/" + name]) !== 1)
        throw new Error("register failed " + name);
    }, { name, b64 });
  }
  const spawnEnv = "BW64_VKTRACE=2\nBW64_VKFRAME=1\nWINEDLLOVERRIDES=d3d11=n;dxgi=n\nLD_LIBRARY_PATH=/home/username";
  await page.evaluate(([exe, env]) => window.bwRuntime.call("bw64_spawn", ["string", "string"],
    ["/usr/lib/wine/wine64\\nZ:\\\\\\\\home\\\\\\\\username\\\\\\\\" + exe, env]), ["d3dconf11b.exe", spawnEnv]);
  console.log("SPAWNED");
  const until = Date.now() + 420000;
  while (Date.now() < until) {
    if (logs.some(l => /d3dconf11b: DONE/.test(l))) { await page.waitForTimeout(5000); break; }
    await page.waitForTimeout(1000);
  }
  console.log("FINISHED done=", logs.some(l => /d3dconf11b: DONE/.test(l)), "chunks=", chunkCount);
} catch (e) { console.log("ERROR", e.message); }
await writeFile(`${dir}/console.log`, logs.join("\n"));
console.log("LOGS WRITTEN", dir);
await slot.release();
await browser.close();
server.close();
