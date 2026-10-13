// 32-bit minimal Win32 window test: pure user32, NO D3D9, NO Vulkan.
// Goal: isolate whether 32-bit GUI/windowing works, or if failure is D3D9-specific.
// Also tests stderr32.exe (32-bit console stderr routing).
import { chromium } from "@playwright/test";
import { serve } from "/home/ubuntu/boxedwine64/tools/browser/serve.mjs";
import { acquireProbeSlot } from "/home/ubuntu/boxedwine64/web/tests/probe-slots.mjs";
import { waitForBuildIdle } from "/home/ubuntu/boxedwine64/web/tests/build-lock.mjs";

const tag = "win32min32-2";
const dir = "/home/ubuntu/boxedwine64/test-results/" + tag;
const { mkdir, writeFile, readFile } = await import("node:fs/promises");
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

  // 32-bit with Wine bootstrap (p=cmd /c echo vkboot)
  const url = "http://127.0.0.1:" + port + "/runtime/index.html?arch=x86&p=cmd%20%2Fc%20echo%20vkboot&session=1&persist=0&";
  await page.goto(url, { timeout: 60000 });
  console.log("NAVIGATED");
  await page.waitForFunction(() => window.bwRuntime && window.bwRuntime.fs, null, { timeout: 120000 });
  console.log("RUNTIME READY");

  // Wait for Wine bootstrap sentinel (vkboot)
  console.log("WAITING FOR VKBOOT SENTINEL...");
  const bootDeadline = Date.now() + 180000;
  while (Date.now() < bootDeadline) {
    if (logs.some(l => /^log: vkboot\s*$/.test(l))) break;
    await page.waitForTimeout(1000);
  }
  const vkbootSeen = logs.some(l => /^log: vkboot\s*$/.test(l));
  console.log("VKBOOT SEEN:", vkbootSeen);
  if (!vkbootSeen) {
    console.log("BOOTSTRAP FAILED - dumping logs");
    await writeFile(dir + "/console.log", logs.join("\n"));
    await writeFile(dir + "/summary.json", JSON.stringify({ tag, vkbootSeen: false, error: "bootstrap timeout" }));
    await writeFile(dir + "/summary.json", JSON.stringify({ tag, vkbootSeen: false, error: "bootstrap timeout" }));
    await browser.close(); server.close();
    await slot.release();
    process.exit(1);
  }

  // Stage test binaries (no DXVK, no Vulkan - pure Win32)
  const assets = [
    ["win32min32.exe", "/home/ubuntu/boxedwine64/tools/win32test/win32min32.exe"],
    ["stderr32.exe", "/home/ubuntu/boxedwine64/tools/win32test/stderr32.exe"],
  ];
  for (const [name, path] of assets) {
    const bytes = await readFile(path);
    const b64 = bytes.toString("base64");
    await page.evaluate(({ name, b64 }) => {
      const fs = window.bwRuntime.fs();
      fs.mkdirTree("/root/home/username");
      fs.writeFile("/root/home/username/" + name, Uint8Array.from(atob(b64), c => c.charCodeAt(0)));
      try {
        window.bwRuntime.call("bw64_register_file", ["string"], ["/home/username/" + name]);
      } catch (e) { console.log("register_file note:", e.message); }
    }, { name, b64 });
    console.log("STAGED", name, bytes.length);
  }

  // Test 1: stderr32.exe (console, tests stderr routing)
  console.log("=== TEST 1: stderr32.exe ===");
  const spawn1 = await page.evaluate(() => window.bwRuntime.call("bw64_spawn", ["string", "string"],
    ["/usr/lib/wine/wine\\nZ:\\\\\\\\home\\\\\\\\username\\\\\\\\stderr32.exe", "WINEDEBUG=+loaddll"]));
  console.log("SPAWNED stderr32", spawn1);
  await page.waitForTimeout(30000);

  // Test 2: win32min32.exe (GUI window, pure user32)
  console.log("=== TEST 2: win32min32.exe ===");
  const spawn2 = await page.evaluate(() => window.bwRuntime.call("bw64_spawn", ["string", "string"],
    ["/usr/lib/wine/wine\\nZ:\\\\\\\\home\\\\\\\\username\\\\\\\\win32min32.exe", "WINEDEBUG=+loaddll"]));
  console.log("SPAWNED win32min", spawn2);
  await page.waitForTimeout(30000);

  const logText = logs.join("\n");
  await writeFile(dir + "/console.log", logText);

  const stderrHello = logText.includes("STDERR32: HELLO-STDERR");
  const stdoutHello = logText.includes("STDERR32: HELLO-STDOUT");
  const winStart = logText.includes("WIN32MIN: START");
  const winStartErr = logText.includes("WIN32MIN: START-ERR");
  const regOk = logText.includes("WIN32MIN: REGISTERCLASS-OK");
  const createOk = logText.includes("WIN32MIN: CREATEWINDOW-OK");
  const shown = logText.includes("WIN32MIN: SHOWN");
  const winDone = logText.includes("WIN32MIN: DONE");
  const regFail = logText.includes("WIN32MIN: REGISTERCLASS-FAILED");
  const createFail = logText.includes("WIN32MIN: CREATEWINDOW-FAILED");

  const summary = { tag, vkbootSeen, spawn1, spawn2, stderrHello, stdoutHello, winStart, winStartErr, regOk, createOk, shown, winDone, regFail, createFail };
  await writeFile(dir + "/summary.json", JSON.stringify(summary, null, 2));
  console.log("SUMMARY", JSON.stringify(summary, null, 2));

  await browser.close();
  server.close();
} finally {
  await slot.release();
}
