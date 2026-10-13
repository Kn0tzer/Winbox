// D3D conformance matrix: D3D9/D3D10/D3D11 (DXVK) + D3D12 (vkd3d-proton).
// Runs the d3dconf{9,10,11,12}.exe guest programs, ONE FRESH BROWSER SESSION
// PER SUITE (a previous suite's late guest output contaminated the next
// suite's run), parses per-op machine-readable logs, screenshots vkCanvas
// for lit-pixel evidence, and writes test-results/d3d-conformance-<tag>/.
//
// Run from repo root: TAG=d3dconf1 TIMEOUT=3600000 node web/tests/d3d-conformance.mjs
// Uses existing dist artifacts, preserving their provenance. No build/deploy.
import { chromium } from "@playwright/test";
import { serve } from "../../tools/browser/serve.mjs";
import { mkdir, writeFile, readFile } from "node:fs/promises";
import { parseV2Chunk, V2FrameAssembler, decodeV2Frame } from "../vkwebgpu.mjs";
import { createHash } from "node:crypto";
import { inflateSync } from "node:zlib";
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';

const tag = process.env.TAG || `d3dconf-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error("TAG must contain only letters, numbers, _, . or -");
const dir = `test-results/d3d-conformance-${tag}`;
await mkdir(dir, { recursive: true });

// SUITES env: comma-separated subset of suite ids to run (default: all).
const onlySuites = (process.env.SUITES || "").split(",").map(s => s.trim()).filter(Boolean);

const suites = [
  {
    id: "d3d9", exe: "d3dconf9.exe", exePath: "tools/d3dtests/d3dconf9.exe",
    assets: [
      ["d3d9.dll", "tools/dxvk/build.w64/src/d3d9/d3d9.dll"],
      ["libvulkan.so.1", "tools/rootfs64/libvk64/libvulkan.so.1"],
    ],
    env: "BW64_VKTRACE=2\nBW64_VKFRAME=1\nWINEDLLOVERRIDES=d3d9=n\nLD_LIBRARY_PATH=/home/username",
    clear: [0, 51, 102],
  },
  {
    id: "d3d10", exe: "d3dconf10.exe", exePath: "tools/d3dtests/d3dconf10.exe",
    assets: [
      ["d3d10core.dll", "tools/dxvk/build.w64/src/d3d10/d3d10core.dll"],
      ["d3d11.dll", "tools/dxvk/build.w64/src/d3d11/d3d11.dll"],
      ["dxgi.dll", "tools/dxvk/build.w64/src/dxgi/dxgi.dll"],
      ["libvulkan.so.1", "tools/rootfs64/libvk64/libvulkan.so.1"],
    ],
    env: "BW64_VKTRACE=2\nBW64_VKFRAME=1\nWINEDLLOVERRIDES=d3d10core=n;d3d11=n;dxgi=n\nLD_LIBRARY_PATH=/home/username",
    clear: [26, 26, 77],
  },
  {
    id: "d3d11", exe: "d3dconf11.exe", exePath: "tools/dxvk/probe/d3dconf11.exe",
    assets: [
      ["d3d11.dll", "tools/dxvk/build.w64/src/d3d11/d3d11.dll"],
      ["dxgi.dll", "tools/dxvk/build.w64/src/dxgi/dxgi.dll"],
      ["libvulkan.so.1", "tools/rootfs64/libvk64/libvulkan.so.1"],
    ],
    env: "BW64_VKTRACE=2\nBW64_VKFRAME=1\nWINEDLLOVERRIDES=d3d11=n;dxgi=n\nLD_LIBRARY_PATH=/home/username",
    clear: [51, 0, 102],
  },
  {
    id: "d3d12", exe: "d3dconf12.exe", exePath: "tools/vkd3d/probe/d3dconf12.exe",
    assets: [
      ["d3d12.dll", "tools/vkd3d/build.w64/libs/d3d12/d3d12.dll"],
      ["d3d12core.dll", "tools/vkd3d/build.w64/libs/d3d12core/d3d12core.dll"],
      ["dxgi.dll", "tools/dxvk/build.w64/src/dxgi/dxgi.dll"],
      ["libvulkan.so.1", "tools/rootfs64/libvk64/libvulkan.so.1"],
      ["bw64vulkan.dll", "tools/vkd3d/probe/bw64vulkan.dll"],
      ["vulkan-1.dll", "tools/vkd3d/probe/bw64vulkan.dll"],
    ],
    env: "BW64_VKTRACE=2\nBW64_VKFRAME=1\nWINEDLLOVERRIDES=d3d12=n;d3d12core=n;dxgi=n;vulkan-1=n;winevulkan=n\nLD_LIBRARY_PATH=/home/username",
    clear: [102, 51, 0],
  },
];

function decodePng(png) {
  let width, height, channels, pos = 8;
  const data = [];
  while (pos + 12 <= png.length) {
    const n = png.readUInt32BE(pos), type = png.toString("ascii", pos + 4, pos + 8), b = png.subarray(pos + 8, pos + 8 + n);
    if (type === "IHDR") {
      width = b.readUInt32BE(0); height = b.readUInt32BE(4);
      channels = ({ 2: 3, 6: 4 })[b[9]];
      if (b[8] !== 8 || !channels) throw new Error("Unsupported PNG pixel encoding");
    } else if (type === "IDAT") data.push(b);
    pos += 12 + n;
  }
  const raw = inflateSync(Buffer.concat(data)), stride = width * channels, rgb = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) for (let x = 0; x < stride; x++) {
    const a = x >= channels ? rgb[y * stride + x - channels] : 0;
    const b2 = y ? rgb[(y - 1) * stride + x] : 0;
    const c = y && x >= channels ? rgb[(y - 1) * stride + x - channels] : 0;
    const p = a + b2 - c, pa = Math.abs(p - a), pb = Math.abs(p - b2), pc = Math.abs(p - c);
    const f = raw[y * (stride + 1)];
    const predictor = [0, a, b2, (a + b2) >> 1, pa <= pb && pa <= pc ? a : pb <= pc ? b2 : c][f];
    if (predictor === undefined) throw new Error("Invalid PNG row filter");
    rgb[y * stride + x] = (raw[y * (stride + 1) + x + 1] + predictor) & 255;
  }
  return { width, height, channels, rgb };
}

function litPixels(png, clear) {
  const { width, height, channels, rgb } = decodePng(png);
  const at = (x, y) => [...rgb.subarray((y * width + x) * channels, (y * width + x) * channels + 3)];
  const bg = [0, 40, 100];
  const close = (a, b) => a.every((v, i) => Math.abs(v - b[i]) < 24);
  let lit = 0;
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
    const px = at(x, y);
    if (!close(px, clear) && !close(px, bg)) lit++;
  }
  return { width, height, lit, center: at(Math.floor(width / 2), Math.floor(height / 2)) };
}

function parseOps(lines, tagName) {
  const ops = [];
  const re = new RegExp(tagName + ": OP (\\S+)\\s+RESULT (OK|FAIL|SKIP)(?: \\(([^)]*)\\))? hr=0x([0-9A-Fa-f]+) ms=(\\d+)");
  for (const line of lines) {
    const m = line.match(re);
    if (m) ops.push({ name: m[1].trim(), result: m[2], note: m[3] || null, hr: "0x" + m[4], ms: Number(m[5]) });
  }
  return ops;
}

const server = await serve(process.env.WINE_DIST_DIR || "dist");
const slot = await acquireProbeSlot();
console.log("PROBE SLOT ACQUIRED");
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || "/usr/local/bin/chromium", headless: true,
  args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu",
    "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader", "--enable-features=Vulkan"],
});
const timeout = Number(process.env.TIMEOUT || 3600000);
const suiteTimeout = Number(process.env.SUITE_TIMEOUT || 900000);
const results = {};
const allLogs = [];

async function runSuite(s) {
  console.log(`=== SUITE ${s.id} (fresh session) ===`);
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const logs = [], errors = [];
  const assembler = new V2FrameAssembler();
  let decodedDraws = 0;
  page.on("console", m => logs.push(`${m.type()}: ${m.text()}`));
  page.on("pageerror", e => errors.push(e.message));
  await page.exposeFunction("saveVkChunk", async b64 => {
    const bytes = Buffer.from(b64, "base64");
    const records = assembler.ingest(parseV2Chunk(bytes));
    if (records) {
      const frame = decodeV2Frame(records);
      decodedDraws += frame.passes.flatMap(p => p.ops).filter(o => o.op === "draw").length;
    }
  });
  const tagBase = s.exe.replace(/\.exe$/, "");
  const doneRe = new RegExp(tagBase + ": DONE");
  const exitRe = new RegExp("exit_group syscall, status=\\d+.*cmd=.*" + tagBase.replace(".", "\\."));
  const suiteStart = Date.now();
  const res = { exe: s.exe, ops: [], pixels: null, decodedDraws: 0, done: false, wallMs: 0 };
  try {
    await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0`);
    await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
    const bt = Date.now() + timeout;
    while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
      if (Date.now() > bt) throw new Error("bootstrap timeout");
      await page.waitForTimeout(500);
    }
    await page.evaluate(async () => {
      const { installVkPageTier } = await import("../vkwebgpu.mjs");
      window.bwVkTier = installVkPageTier();
      window.bwVkTierReady = Promise.resolve(window.bwVkTier);
      const original = window.bwVkChunk;
      window.bwVkChunk = (bytes, flags) => {
        let str = "";
        for (let i = 0; i < bytes.length; i += 8192) str += String.fromCharCode(...bytes.subarray(i, i + 8192));
        window.saveVkChunk(btoa(str));
        return original(bytes, flags);
      };
    });
    const provenance = [];
    for (const [name, path] of [[s.exe, s.exePath], ...s.assets]) {
      const bytes = await readFile(path);
      provenance.push({ name, path, bytes: bytes.length, sha256: createHash("sha256").update(bytes).digest("hex") });
      const b64 = bytes.toString("base64");
      await page.evaluate(({ name, b64 }) => {
        const fs = window.bwRuntime.fs();
        fs.mkdirTree("/root/home/username");
        fs.writeFile("/root/home/username/" + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
        if (window.bwRuntime.call("bw64_register_file", ["string"], ["/home/username/" + name]) !== 1)
          throw new Error("Guest VFS registration failed for " + name);
      }, { name, b64 });
    }
    res.provenance = provenance;
    console.log("ASSETS STAGED", s.id, JSON.stringify(provenance.map(p => [p.name, p.bytes])));
    const spawn = await page.evaluate(([exe, env]) => window.bwRuntime.call("bw64_spawn", ["string", "string"],
      ["/usr/lib/wine/wine64\nZ:\\\\home\\\\username\\\\" + exe, env]), [s.exe, s.env]);
    res.spawn = spawn;
    console.log("SPAWNED", s.exe, spawn);
    const suiteUntil = Date.now() + suiteTimeout;
    let lastPrint = 0, exitSeenAt = 0;
    while (Date.now() < suiteUntil) {
      if (Date.now() - lastPrint > 30000) {
        lastPrint = Date.now();
        console.log("PROGRESS", s.id, JSON.stringify({ draws: decodedDraws, tail: logs.slice(-2) }));
      }
      // Guest stderr arrives late and out of order: after DONE, wait until the
      // parsed op count matches the DONE tally (ok+fail+skip) or 90s pass.
      const tallyRe = new RegExp(tagBase + ": DONE ok=(\\d+) fail=(\\d+) skip=(\\d+)");
      let expected = -1;
      if (logs.some(l => doneRe.test(l))) {
        const tm = logs.map(l => l.match(tallyRe)).find(m => m);
        expected = tm ? (Number(tm[1]) + Number(tm[2]) + Number(tm[3])) : -1;
        const t1 = Date.now();
        while (Date.now() - t1 < 90000) {
          if (expected > 0 && parseOps(logs, tagBase).length >= expected) break;
          await page.waitForTimeout(2000);
        }
        break;
      }
      if (!exitSeenAt && logs.some(l => exitRe.test(l))) { exitSeenAt = Date.now(); console.log("EXIT SEEN", s.id); }
      if (exitSeenAt && Date.now() - exitSeenAt > 90000) { console.log("DRAIN TIMEOUT", s.id); break; }
      if (errors.some(l => /unreachable|memory access out of bounds|Aborted/.test(l))) break;
      await page.waitForTimeout(1000);
    }
    const shot = await page.evaluate(() => document.getElementById("vkCanvas")?.toDataURL("image/png"));
    if (shot) {
      const bytes = Buffer.from(shot.split(",")[1], "base64");
      await writeFile(`${dir}/canvas-${s.id}.png`, bytes);
      try { res.pixels = litPixels(bytes, s.clear); } catch (e) { res.pixels = { error: e.message }; }
    }
    await writeFile(`${dir}/console-${s.id}.log`, logs.join("\n"));
    res.ops = parseOps(logs, tagBase);
    res.decodedDraws = decodedDraws;
    res.done = logs.some(l => doneRe.test(l));
    res.pageErrors = errors.slice();
    res.wallMs = Date.now() - suiteStart;
    console.log(`SUITE ${s.id} DONE ops=${res.ops.length} lit=${res.pixels?.lit} draws=${decodedDraws} done=${res.done}`);
  } catch (e) {
    console.log(`SUITE ${s.id} ERROR`, e.message);
    res.error = e.message;
    await writeFile(`${dir}/console-${s.id}.log`, logs.join("\n")).catch(() => {});
  } finally {
    allLogs.push(`===== ${s.id} =====`, ...logs);
    await page.close().catch(() => {});
  }
  return res;
}

try {
  const runList = onlySuites.length ? suites.filter(s => onlySuites.includes(s.id)) : suites;
  if (!runList.length) throw new Error("SUITES filter matched nothing");
  for (const s of runList) results[s.id] = await runSuite(s);
} catch (e) {
  console.log("RUNNER ERROR", e.message);
  results.runnerError = e.message;
} finally {
  await writeFile(`${dir}/console-full.log`, allLogs.join("\n"));
  await writeFile(`${dir}/results.json`, JSON.stringify(results, null, 2));
  console.log("RESULTS WRITTEN", dir);
  await slot.release();
  await browser.close();
  server.close();
}
