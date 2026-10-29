// SYSTEM-PLACEMENT 32-BIT D3D12 probe (lane-d3d12-32): the 32-bit sibling
// of d3d12-system.mjs. Boots a 32-bit session (?arch=x86: wine32.zip +
// prefix32.zip + /opt/wine-stable/bin/wine -- the launcher's ARCH32 loader;
// /usr/lib/wine/wine resolves NLS data via the Debian layout and the loader
// NULL-derefs in ntdll during environ init), uploads ONLY tri12-32.exe and the
// 32-bit libvk32 loader (tools/rootfs64/libvk32/libvulkan.so.1, found via
// LD_LIBRARY_PATH=/home/username -- the same wiring the 32-bit DXVK path
// uses), and spawns with WINEDLLOVERRIDES=d3d12=n;d3d12core=n so the guest
// must resolve the PE32 vkd3d-proton pair from wine32.zip system placement
// (opt/wine-stable/lib/wine/i386-windows) -- the shipping path. Nothing
// else is uploaded: vulkan-1.dll / winevulkan.dll resolve from wine32.zip.
// WINEDEBUG=+loaddll in the spawn env records which DLL files the loader
// actually picked (summary.loadDllLines). Verdict: "tri12: RESULT 0" plus
// cornflower clear pixels, as in the 64-bit probe.
// Run from repo root: TAG=unique TIMEOUT=900000 WINE_DIST_DIR=<dist> node web/tests/d3d12-32-system.mjs
import { chromium } from "@playwright/test";
import { serve } from "../../tools/browser/serve.mjs";
import { mkdir, writeFile, readFile } from "node:fs/promises";
import { parseV2Chunk, V2FrameAssembler, decodeV2Frame } from "../vkwebgpu.mjs";
import { createHash } from "node:crypto";
import { inflateSync } from "node:zlib";
import { acquireProbeSlot } from '/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs';

const tag = process.env.TAG || `d3d12-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw new Error("TAG must contain only letters, numbers, _, . or -");
const dir = `test-results/dxvk-${tag}`;
await mkdir(dir, { recursive: true });

// Exe selection: TRI12DRAW_PATH -> RGB triangle; SCENE12_PATH -> complex scene
// (3 PSOs, 60 draws/frame, depth buffer); TRI12_PATH -> override for tri12.exe;
// default tri12.exe (clear-only). Exe base name + result tag derive from path.
const scene12Path = process.env.SCENE12_PATH;
const drawMode = !!process.env.TRI12DRAW_PATH || !!scene12Path;
const triExePath = process.env.TRI12DRAW_PATH || scene12Path
  || (process.env.TRI12_PATH || "tools/vkd3d/probe/tri12-32.exe");
const triExeName = triExePath.split("/").pop();
const resultTag = process.env.BW_32_RESULTTAG || "tri12"; // guest verdict prefix ("tri12" / "tri12draw")
const resultRe = new RegExp(resultTag + ":\\s*RESULT\\s*[01]");
const failRe = new RegExp(resultTag + ":\\s*FAIL");

function withTestUrlParams(rawUrl) {
  const url = new URL(rawUrl);
  for (const [key, value] of new URLSearchParams(process.env.BW_TEST_URL_PARAMS || ""))
    url.searchParams.set(key, value);
  return url.toString();
}

// Simple clear-color analysis: count pixels far from the page-tier default
// background (0,40,100). A working clear shows cornflower (100,149,237).
function clearPixels(png) {
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
  const at = (x, y) => [...rgb.subarray((y * width + x) * channels, (y * width + x) * channels + 3)];
  const bg = [0, 40, 100], close = (a, b) => a.every((v, i) => Math.abs(v - b[i]) < 24);
  const cornflower = [100, 149, 237];
  let nonBackground = 0, cornflowerPx = 0;
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
    const px = at(x, y);
    if (!close(px, bg)) { nonBackground++; if (close(px, cornflower)) cornflowerPx++; }
  }
  return { width, height, nonBackground, cornflowerPx,
    center: at(Math.floor(width / 2), Math.floor(height / 2)) };
}

// Draw-mode pixel analysis: tri12draw clears to dark blue (0,51,102) and draws
// an RGB triangle. Count pixels far from both the clear color and the page
// background (0,40,100) -- those are the triangle pixels.
function drawPixels(png) {
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
  const at = (x, y) => [...rgb.subarray((y * width + x) * channels, (y * width + x) * channels + 3)];
  const bg = [0, 40, 100], darkBlue = [0, 51, 102];
  const close = (a, b) => a.every((v, i) => Math.abs(v - b[i]) < 24);
  let trianglePx = 0;
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
    const px = at(x, y);
    if (!close(px, darkBlue) && !close(px, bg)) trianglePx++;
  }
  return { width, height, trianglePx,
    center: at(Math.floor(width / 2), Math.floor(height / 2)) };
}

const server = await serve(process.env.WINE_DIST_DIR || "dist");
const slot = await acquireProbeSlot();
console.log("PROBE SLOT ACQUIRED");
const browser = await chromium.launch({
  executablePath: process.env.CHROME_PATH || "/usr/local/bin/chromium", headless: true,
  args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu",
    "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader", "--enable-features=Vulkan"],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const logs = [], errors = [], chunkFiles = [], decodedFrames = [];
const capturedAssembler = new V2FrameAssembler();
// 32-bit cmd wraps its stdout in ANSI cursor escapes (ESC[?25l ... ESC[?25h),
// which would defeat the strict "^log: vkboot$" sentinel and RESULT regexes;
// strip CSI sequences from console text as it is captured.
const stripAnsi = s => s.replace(/\x1b\[[0-9;?]*[a-zA-Z]/g, "");
page.on("console", m => logs.push(`${m.type()}: ${stripAnsi(m.text())}`));
page.on("pageerror", e => errors.push(e.message));
const flush = () => writeFile(`${dir}/console.log`, logs.join("\n"));
await page.exposeFunction("saveVkChunk", async b64 => {
  const file = `chunk-${String(chunkFiles.length).padStart(4, "0")}.bin`;
  chunkFiles.push(file);
  const bytes = Buffer.from(b64, "base64");
  const records = capturedAssembler.ingest(parseV2Chunk(bytes));
  if (records) {
    const frame = decodeV2Frame(records);
    decodedFrames.push({ frame: frame.frameNo, w: frame.width, h: frame.height,
      draws: frame.passes.flatMap(p => p.ops).filter(o => o.op === "draw").length,
      ops: frame.passes.flatMap(p => p.ops).map(o => o.op).join(",") });
  }
  await writeFile(`${dir}/${file}`, bytes);
});
const timeout = Number(process.env.TIMEOUT || 1800000);
let summary = { passed: false }, pixels = null;
const provenance = [];
try {
  // BW_32_URL_ENV: JSON object applied by the launcher to the whole session
  // env from process start -- the channel that reaches bridge getenv()
  // diagnostics (BW64_FRAMELOG / BW64_VKDUMP) whose spawn-env merge does not.
  const urlEnv = process.env.BW_32_URL_ENV ? `&env=${encodeURIComponent(process.env.BW_32_URL_ENV)}` : "";
  await page.goto(withTestUrlParams(`http://127.0.0.1:${server.address().port}/runtime/index.html?arch=x86&p=cmd%20%2Fc%20echo%20vkboot&session=1&novideo=1&persist=0&gltrace=0${urlEnv}`));
  await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
  console.log("SESSION READY");
  const assets = [
    [triExeName, triExePath],
    ["libvulkan.so.1", "tools/rootfs64/libvk32/libvulkan.so.1"],
  ];  // the d3d12 pair itself is NOT uploaded: system placement must serve it.
  for (const [name, path] of assets) {
    const bytes = await readFile(path);
    provenance.push({ name, path, bytes: bytes.length, sha256: createHash("sha256").update(bytes).digest("hex") });
    const b64 = bytes.toString("base64");
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree("/root/home/username");
      fs.writeFile("/root/home/username/" + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      const registered = window.bwRuntime.call("bw64_register_file", ["string"], ["/home/username/" + name]);
      if (registered !== 1) throw new Error("Guest VFS registration failed for " + name);
    }, { name, b64 });
  }
  console.log("ASSETS STAGED", JSON.stringify(provenance.map(p => [p.name, p.bytes])));
  const bootstrapUntil = Date.now() + timeout;
  while (!logs.some(l => /^log: vkboot\s*$/.test(l))) {
    if (Date.now() > bootstrapUntil) throw new Error("Wine bootstrap sentinel timed out");
    await flush();
    await page.waitForTimeout(500);
  }
  console.log("BOOTSTRAP COMPLETE");
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
  const spawnEnv = "BW64_VKTRACE=2\nBW64_VKFRAME=1\nWINEDEBUG=" + (process.env.BW_32_WINEDEBUG || "+loaddll")
    + "\nLD_LIBRARY_PATH=/home/username\nWINEDLLOVERRIDES=" + (process.env.BW_SYS_OVERRIDES || "d3d12=n;d3d12core=n")
    + (process.env.BW_32_EXTRA_ENV ? "\n" + process.env.BW_32_EXTRA_ENV : "");
  const spawn = await page.evaluate(([exeName, envStr]) => window.bwRuntime.call("bw64_spawn", ["string", "string"],
    ["/opt/wine-stable/bin/wine\nZ:\\\\home\\\\username\\\\" + exeName, envStr]), [triExeName, spawnEnv]);
  console.log("SPAWN", spawn);
  const until = Date.now() + timeout;
  let lastPrint = 0, lastRendered = 0, shot = null;
  while (Date.now() < until) {
    const stats = await page.evaluate(() => window.bwVkStats?.());
    if (stats?.v2?.rendered > lastRendered) {
      lastRendered = stats.v2.rendered;
      // WebGPU's backing texture is not readable with toDataURL after present.
      shot = await page.locator("#vkCanvas").screenshot();
      await page.screenshot({ path: `${dir}/page.png` });
    }
    if (Date.now() - lastPrint > 15000) {
      lastPrint = Date.now();
      console.log("PROGRESS", JSON.stringify({ chunks: chunkFiles.length, stats, tail: logs.slice(-2) }));
      await flush();
    }
    if (logs.some(l => resultRe.test(l))) {
      await page.waitForTimeout(3000);
      break;
    }
    if (logs.some(l => new RegExp('exit_group syscall, status=[1-9]\\d*.*cmd=.*' + triExeName.replace('.', '\\.')).test(l))) break;
    if (errors.some(l => /unreachable|memory access out of bounds|Aborted/.test(l))) break;
    await page.waitForTimeout(250);
  }
  const stats = await page.evaluate(() => window.bwVkStats?.());
  if (!shot) shot = await page.locator("#vkCanvas").screenshot().catch(() => null);
  if (shot) {
    const bytes = shot;
    await writeFile(`${dir}/canvas.png`, bytes);
    pixels = drawMode ? drawPixels(bytes) : clearPixels(bytes);
  }
  const joined = logs.join("\n");
  const renderErrors = logs.filter(l => /page tier WebGPU error|frame rejected|chunk rejected|cannot translate/.test(l));
  // vkd3d gate diagnostics: capture the exact failing requirement from guest log
  const gateLines = logs.filter(l => /not supported|Lacking support|required|E_INVALIDARG|is not supported/i.test(l)).slice(0, 20);
  summary = {
    passed: new RegExp(resultTag + ":\\s*RESULT\\s*0").test(joined)
        && !logs.some(l => /maintenance5 and\/or maintenance6 not supported|Lacking support|D3D12CreateDevice failed/i.test(l))
        && !logs.some(l => failRe.test(l)),
    tri12Result:
      (new RegExp(resultTag + ":\\s*RESULT\\s*0").test(joined) ? 0
        : new RegExp(resultTag + ":\\s*RESULT\\s*1").test(joined) ? 1 : null),
    tri12FailLines: logs.filter(l => failRe.test(l)),
    gateLines,
    framesBuilt: (joined.match(/vk64: FRAME \d+ built/g) || []).length,
    chunksCaptured: chunkFiles.length, decodedFrames,
    guestExitLines: logs.filter(l => new RegExp('exit_group syscall.*cmd=.*' + triExeName.replace('.', '\\.')).test(l)),
    provenance, stats, pixels, errors, renderErrors,
    loadDllLines: logs.filter(l => /loaddll|Loaded/i.test(l) && /d3d12|vulkan|dxgi/i.test(l)).slice(0, 40),
    vk32Traps: (joined.match(/vk32: trap/g) || []).length,
    vkInstanceLines: logs.filter(l => /vkCreateInstance|vkEnumeratePhysicalDevices|vkCreateDevice/.test(l)).slice(0, 20),
  };
} catch (e) {
  summary.failure = String(e.stack || e);
} finally {
  await flush();
  await page.screenshot({ path: `${dir}/final-page.png` }).catch(() => {});
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2));
  await writeFile(`${dir}/chunks.json`, JSON.stringify(chunkFiles));
  console.log("D3D12 VERDICT", JSON.stringify(summary));
  await slot.release().catch(() => {}); console.log("PROBE SLOT RELEASED");
  await browser.close(); server.close();
  if (!summary.passed) process.exitCode = 1;
}
