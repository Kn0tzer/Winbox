import { fileURLToPath as diagnosticsFileURLToPath } from 'node:url';
const diagnosticsRepoRoot = diagnosticsFileURLToPath(new URL('../../../', import.meta.url));
process.chdir(diagnosticsRepoRoot);

// Fresh CreateDesktopW diagnostic probe (32-bit).
// Runs tools/desktoptest/desktoptest3.exe under ?arch=x86 and reads Z:\tmp\dtresult3.txt
import { chromium } from "@playwright/test";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import { writeFileSync, unlinkSync, existsSync } from "node:fs";

const TAG = process.env.PROBE_TAG || "20261007-dt3diag-1";
const dir = `${diagnosticsRepoRoot}/test-results/${TAG}`;
await mkdir(dir, { recursive: true });

const LOCK = "/tmp/bw64-probe.lock";
if (existsSync(LOCK)) { console.log("LOCK EXISTS, aborting"); process.exit(2); }
writeFileSync(LOCK, String(process.pid));

let server, browser;
const logs = [];
const flush = () => writeFile(`${dir}/console.log`, logs.join("\n")).catch(()=>{});
const summary = { tag: TAG, arch: "x86", boot32: false, spawned: false, output: null, result: "UNKNOWN" };
const timeout = Number(process.env.TIMEOUT || 300000);
const t0 = Date.now();
const elapsed = () => ((Date.now() - t0) / 1000).toFixed(0) + "s";

try {
  const { serve } = await import("../../../tools/browser/serve.mjs");
  server = await serve(process.env.WINE_DIST_DIR || (diagnosticsRepoRoot + '/dist'));
  browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH || "/usr/local/bin/chromium", headless: true,
    args: ["--no-sandbox", "--disable-dev-shm-usage", "--enable-unsafe-webgpu",
      "--enable-unsafe-swiftshader", "--use-angle=vulkan", "--use-vulkan=swiftshader", "--enable-features=Vulkan"],
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  page.on("console", m => logs.push(`${m.type()}: ${m.text()}`));
  page.on("pageerror", e => { logs.push(`PAGEERROR: ${e.message}`); });

  await page.goto(`http://127.0.0.1:${server.address().port}/runtime/index.html?p=cmd%20%2Fc%20echo%20vkboot&arch=x86&session=1&novideo=1&persist=0&gltrace=0`);
  // Wait for ready, but tolerate the wineboot hang: poll up to timeout
  try {
    await page.waitForFunction(() => window.bwRuntime?.ready?.() === true, null, { timeout });
    console.log("SESSION READY in", elapsed());
  } catch (e) {
    console.log("ready() timed out in", elapsed(), "- proceeding anyway to check vkboot");
  }
  const stripAnsi = s => s.replace(/\u001b\[[0-9;?]*[a-zA-Z]/g, "");
  const bootUntil = Date.now() + timeout;
  while (!logs.some(l => /^log: vkboot\s*$/.test(stripAnsi(l)))) {
    if (Date.now() > bootUntil) throw new Error("Wine bootstrap sentinel timed out");
    await flush();
    await page.waitForTimeout(2000);
  }
  summary.boot32 = true;
  console.log("BOOT32 OK in", elapsed());

  // Stage desktoptest3.exe into the guest fs
  const bytes = await readFile((diagnosticsRepoRoot + '/tools/desktoptest/desktoptest3.exe'));
  const b64 = bytes.toString("base64");
  await page.evaluate(({ b64 }) => {
    const fs = window.bwRuntime.fs();
    fs.mkdirTree("/root/home/username");
    fs.writeFile("/root/home/username/desktoptest3.exe", Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
    const r = window.bwRuntime.call("bw64_register_file", ["string"], ["/home/username/desktoptest3.exe"]);
    if (r !== 1) throw new Error("register failed: " + r);
  }, { b64 });
  console.log("STAGED", bytes.length, "bytes in", elapsed());

  // Spawn via wine. Newline-separated argv embedded in fn body (avoids double-escaping).
  const spawn = await page.evaluate(() => window.bwRuntime.call("bw64_spawn", ["string", "string"],
    ["/usr/lib/wine/wine\nZ:\\\\home\\\\username\\\\desktoptest3.exe", "WINEDLLPATH=/usr/lib/i386-linux-gnu/wine"]));
  summary.spawned = true;
  console.log("SPAWN result", spawn, "in", elapsed());

  // Poll for Z:\tmp\dtresult3.txt (Z: -> /tmp in guest)
  const runUntil = Date.now() + 120000;
  let fileContent = null;
  while (Date.now() < runUntil) {
    await flush();
    try {
      fileContent = await page.evaluate(() => {
        try {
          const data = window.bwRuntime.fs().readFile("/tmp/dtresult3.txt");
          return new TextDecoder().decode(data);
        } catch (e) { return null; }
      });
      if (fileContent) break;
    } catch (e) { /* page may be busy */ }
    await page.waitForTimeout(2000);
  }
  console.log("DTRESULT:", JSON.stringify(fileContent));
  summary.output = fileContent;
  summary.result = fileContent ? (fileContent.includes("SUCCESS") ? "SUCCESS" : "FAILED") : "NO_OUTPUT";
  await flush();
} catch (e) {
  console.log("FAIL", String(e).slice(0, 300));
  summary.error = String(e).slice(0, 300);
  await flush();
} finally {
  await writeFile(`${dir}/summary.json`, JSON.stringify(summary, null, 2)).catch(()=>{});
  try { await browser.close(); } catch {}
  try { server.close(); } catch {}
  try { unlinkSync(LOCK); } catch {}
}
console.log("SUMMARY", JSON.stringify(summary));
