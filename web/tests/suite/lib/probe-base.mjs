// web/tests/suite/lib/probe-base.mjs — Base class for browser probe tests.
//
// Every browser test extends ProbeTest and implements run(). The base handles:
//   - Probe slot acquisition (via probe-slots.mjs)
//   - Playwright browser launch
//   - Static file serving (via tools/browser/serve.mjs)
//   - Test-result directory creation
//   - Console log capture
//   - Clean shutdown
//
// Usage:
//   import { ProbeTest } from '../lib/probe-base.mjs';
//   import { checkGreaterThan } from '../lib/assert.mjs';
//
//   export default class D3D11TriangleTest extends ProbeTest {
//     constructor() {
//       super({ tag: 'd3d11-triangle', timeoutMs: 900000 });
//     }
//     async run(ctx) {
//       // ctx.page, ctx.console, ctx.dir are available
//       const lit = await ctx.page.evaluate(() => window.__lit);
//       checkGreaterThan(lit, 0, 'D3D11 triangle rendered pixels');
//     }
//   }
//
// The runner instantiates the class, calls setup(), run(ctx), teardown().
// Any thrown Error (including from check*) = FAIL with the message.
// Return normally = PASS.

import { chromium } from "@playwright/test";
import { serve } from "../../../../tools/browser/serve.mjs";
import { mkdir, mkdtemp, readdir, symlink, rm, readFile, writeFile, stat } from "node:fs/promises";
import { resolve, join } from "node:path";
import { tmpdir } from "node:os";
import { createHash } from "node:crypto";
import { createWriteStream } from "node:fs";
import { acquireProbeSlot } from "../../probe-slots.mjs";
import { waitForBuildIdle } from "../../build-lock.mjs";

export async function withTimeout(promise, ms, what) {
  let timer;
  try {
    return await Promise.race([promise, new Promise((_, reject) => {
      timer = setTimeout(() => reject(new Error(`${what} timed out after ${ms}ms`)), ms);
    })]);
  } finally { clearTimeout(timer); }
}

export const terminalText = text => text
  .replace(/\x1b\[(\d*)C/g, (_, n) => ' '.repeat(Number(n || 1)))
  .replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, '');

export class ProbeTest {
  constructor({ tag, timeoutMs = 900000, distDir = "dist" } = {}) {
    if (!tag || !/^[\w.-]+$/.test(tag)) {
      throw new Error("ProbeTest tag must contain only letters, numbers, _, . or -");
    }
    this.tag = tag;
    this.timeoutMs = timeoutMs;
    this.distDir = distDir;
    this.dir = `test-results/suite-${tag}`;
    this.consoleLines = [];
    this.rawConsoleLines = [];
    this._slot = null;
    this._server = null;
    this._browser = null;
  }

  async setup() {
    await mkdir(this.dir, { recursive: true });
    this._log = createWriteStream(join(this.dir, 'console.log'));
    this._slot = await acquireProbeSlot({ timeoutMs: this.timeoutMs });
    await waitForBuildIdle();
    const dist = resolve(process.env.WINE_DIST_DIR || this.distDir);
    this.buildInfo = JSON.parse(await readFile(join(dist, 'runtime/BUILD_INFO.json'), 'utf8'));
    const wasm = await readFile(join(dist, 'runtime/boxedwine64.wasm'));
    if (createHash('sha256').update(wasm).digest('hex') !== this.buildInfo.wasm_sha256) {
      throw new Error('Runtime hash does not match BUILD_INFO.json');
    }
    this._root = await mkdtemp(join(tmpdir(), 'bw-suite-'));
    for (const file of await readdir(dist)) await symlink(join(dist, file), join(this._root, file));
    await mkdir(join(this._root, '__fixtures'));
    this._server = await withTimeout(serve(this._root), 60000, "static server start");
    this._server.url = 'http://127.0.0.1:' + this._server.address().port;
    this._browserServer = await withTimeout(chromium.launchServer({
      executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
      args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu',
        '--enable-unsafe-swiftshader', '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan',
        '--remote-debugging-address=127.0.0.1', '--remote-debugging-port=0'],
    }), 120000, "chromium.launchServer");
    this._browser = await withTimeout(chromium.connect(this._browserServer.wsEndpoint()), 30000, 'browser connect');
    await writeFile(join(this.dir, '.browser.ws'), this._browserServer.wsEndpoint(), { mode: 0o600 });
    const userData = this._browserServer.process().spawnargs.find(a => a.startsWith('--user-data-dir='))?.split('=').slice(1).join('=');
    if (userData) {
      const port = (await readFile(join(userData, 'DevToolsActivePort'), 'utf8')).split('\n')[0];
      await writeFile(join(this.dir, '.browser.cdp'), `http://127.0.0.1:${port}`, { mode: 0o600 });
    }
  }

  async teardown() {
    if (this._log) { await new Promise(r => this._log.end(r)); this._log = null; }
    await writeFile(join(this.dir, 'BUILD_INFO.json'), JSON.stringify(this.buildInfo || null, null, 2));
    if (this._browser) { try { await withTimeout(this._browser.close(), 15000, 'browser close'); } catch {} this._browser = null; }
    if (this._browserServer) { await withTimeout(this._browserServer.close(), 15000, 'browser server close'); this._browserServer = null; }
    await rm(join(this.dir, '.browser.ws'), { force: true });
    await rm(join(this.dir, '.browser.cdp'), { force: true });
    if (this._server) { try { await withTimeout(new Promise(r => this._server.close(r)), 10000, 'server close'); } catch {} this._server = null; }
    if (this._root) { await rm(this._root, { recursive: true, force: true }); this._root = null; }
    if (this._slot) { await withTimeout(this._slot.release(), 5000, 'slot release'); this._slot = null; }
  }

  // Subclasses implement this. ctx = { page, console, dir, server }.
  async run(ctx) {
    throw new Error("ProbeTest subclass must implement run(ctx)");
  }

  // Helper: create a page that captures console output.
  async newPage() {
    const page = await this._browser.newPage();
    page.on("console", msg => {
      const text = `${msg.type()}: ${msg.text()}`;
      this.rawConsoleLines.push(text);
      this._log?.write(text + '\n');
      this.consoleLines.push(terminalText(text));
    });
    page.on("pageerror", err => {
      this.consoleLines.push(`[pageerror] ${err.message}`);
      this.rawConsoleLines.push(`pageerror: ${err.stack || err.message}`);
      this._log?.write(`pageerror: ${err.stack || err.message}\n`);
    });
    return page;
  }

  // Helper: count console lines matching a regex (for ^log: vkboot$ style checks).
  countConsole(pattern) {
    const re = typeof pattern === "string" ? new RegExp(pattern) : pattern;
    return this.consoleLines.filter(l => re.test(l)).length;
  }

  // Helper: wait for a condition in the page with timeout.
  async waitForPageFn(page, fn, timeoutMs = 120000) {
    await page.waitForFunction(fn, null, { timeout: timeoutMs });
  }

  async bootGuest(page, parameters = {}) {
    const params = new URLSearchParams({ p: 'cmd /c echo vkboot', session: '1', persist: '0', fresh: '1', ...parameters });
    await page.goto(`${this._server.url}/runtime/index.html?${params}`);
    await this.waitForPageFn(page, () => globalThis.bwRuntime?.ready?.() === true, 600000);
  }

  async mountArchive(file, name) {
    if (!/^[\w.-]+$/.test(name)) throw new Error('Invalid fixture name');
    const absolute = resolve(file);
    const info = await stat(absolute);
    await symlink(absolute, join(this._root, '__fixtures', name));
    return { url: `${this._server.url}/__fixtures/${name}`, bytes: info.size };
  }

  async stageFile(page, name, file) {
    const data = (await readFile(file)).toString('base64');
    await page.evaluate(({ name, data }) => {
      const fs = globalThis.bwRuntime.fs();
      fs.mkdirTree('/root/home/username');
      fs.writeFile('/root/home/username/' + name, Uint8Array.from(atob(data), c => c.charCodeAt(0)));
      if (globalThis.bwRuntime.call('bw64_register_file', ['string'], ['/home/username/' + name]) !== 1) {
        throw new Error('Guest registration failed: ' + name);
      }
    }, { name, data });
  }

  async spawnGuest(page, exe, { arch = 'x64', env = '' } = {}) {
    const before = this.consoleLines.length;
    const result = await page.evaluate(({ exe, arch, env }) => globalThis.bwRuntime.call('bw64_spawn', ['string', 'string'],
      [`${arch === 'x86' ? '/opt/wine-stable/bin/wine' : '/usr/lib/wine/wine64'}\nZ:\\home\\username\\${exe}`,
        'LD_LIBRARY_PATH=/home/username\n' + env]), { exe, arch, env });
    if (result <= 0) throw new Error(`Guest spawn failed for ${exe}: ${result}`);
    return before;
  }

  async waitForLog(page, pattern, timeoutMs = 180000, from = 0) {
    const until = Date.now() + timeoutMs;
    do {
      const error = this.consoleLines.find(l => /\[pageerror\]|Native panic stack|abort\(|unreachable|RuntimeError: memory access out of bounds/.test(l));
      if (error) throw new Error(error);
      const line = this.consoleLines.slice(from).find(l => pattern.test(l));
      if (line) return line;
      await page.waitForTimeout(250);
    } while (Date.now() < until);
    throw new Error(`Guest output ${pattern} not seen within ${timeoutMs}ms; last lines:\n${this.consoleLines.slice(-12).join('\n')}`);
  }
}
