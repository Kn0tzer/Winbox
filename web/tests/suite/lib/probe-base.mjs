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
import { serve } from "../../../tools/browser/serve.mjs";
import { mkdir } from "node:fs/promises";
import { acquireProbeSlot } from "../../probe-slots.mjs";

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
    this._slot = null;
    this._server = null;
    this._browser = null;
  }

  async setup() {
    await mkdir(this.dir, { recursive: true });
    this._slot = await acquireProbeSlot({ timeoutMs: this.timeoutMs });
    this._server = await serve(process.env.WINE_DIST_DIR || this.distDir);
    this._browser = await chromium.launch({ headless: true });
  }

  async teardown() {
    if (this._browser) { try { await this._browser.close(); } catch {} this._browser = null; }
    if (this._server) { try { await this._server.close(); } catch {} this._server = null; }
    if (this._slot) { try { await this._slot.release(); } catch {} this._slot = null; }
  }

  // Subclasses implement this. ctx = { page, console, dir, server }.
  async run(ctx) {
    throw new Error("ProbeTest subclass must implement run(ctx)");
  }

  // Helper: create a page that captures console output.
  async newPage() {
    const page = await this._browser.newPage();
    page.on("console", msg => {
      const text = `[${msg.type()}] ${msg.text()}`;
      this.consoleLines.push(text);
    });
    page.on("pageerror", err => {
      this.consoleLines.push(`[pageerror] ${err.message}`);
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
}
