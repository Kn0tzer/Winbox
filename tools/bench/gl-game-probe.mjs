#!/usr/bin/env node
// Short GL-on-WebGPU launch probe. It uses the runtime's gamezip import path,
// the same archive flow used by the built-in games catalog in web/games.mjs.
// GAME=q3|mindustry node tools/bench/gl-game-probe.mjs
import { chromium } from '@playwright/test';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { mkdir, mkdtemp, readFile, rm, stat, symlink, writeFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from '../../web/tests/probe-slots.mjs';
import { waitForBuildIdle } from '../../web/tests/build-lock.mjs';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const workRoot = '/tmp/winbox-finish/r2/work-glwgpu';
const profiles = {
  q3: {
    root: join(workRoot, 'q3/game/app'),
    exe: 'quake3e.x64.exe',
    arch: 'x64',
    args: ['+set', 'r_fullscreen', '0', '+set', 'cl_renderer', 'opengl1'],
    skip: new Set([
      'spearmint-1.0.3-windows.zip',
      'baseq3/pak9hqq37.pk3',
      'baseq3/dc_mappack.pk3',
      'baseq3/xcsv_bq3hi-res.pk3',
      'baseq3/zzzz-Quake_Live_Mappack.pk3',
    ]),
  },
  mindustry: {
    root: join(workRoot, 'mindustry/Mindustry'),
    exe: 'Mindustry.exe',
    arch: 'x64',
    args: [],
    skip: new Set(),
  },
};

const game = process.env.GAME;
if (!profiles[game]) throw new Error('Set GAME=q3 or GAME=mindustry');
const profile = profiles[game];
const gameRoot = resolve(process.env.GAME_ROOT || profile.root);
const exe = process.env.GAME_EXE || profile.exe;
const arch = process.env.GAME_ARCH || profile.arch;
const args = process.env.GAME_ARGS ? JSON.parse(process.env.GAME_ARGS) : profile.args;
const glVersion = process.env.GL_VERSION || '3.2';
if (!['x86', 'x64'].includes(arch) || !Array.isArray(args) || args.some(arg => typeof arg !== 'string'))
  throw new Error('Invalid GAME_ARCH or GAME_ARGS');

const outputDir = resolve(process.env.OUT || join(workRoot, 'evidence', game));
const gameZip = resolve(process.env.GAME_ZIP || join(workRoot, `${game}-probe.zip`));
const dist = resolve(process.env.WINE_DIST_DIR || join(repo, 'dist'));
await mkdir(outputDir, { recursive: true });

const zipScript = String.raw`import os, sys, zipfile
from pathlib import Path
root = Path(sys.argv[1]).resolve()
target = Path(sys.argv[2]).resolve()
game = sys.argv[3]
skip = set(sys.argv[4].splitlines()) if sys.argv[4] else set()
if not root.is_dir():
    raise SystemExit(f'game root does not exist: {root}')
count = 0
with zipfile.ZipFile(target, 'w', compression=zipfile.ZIP_STORED, allowZip64=True) as archive:
    for path in sorted(root.rglob('*')):
        if path.is_symlink() or not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        if game == 'q3' and (relative.startswith('missionpack/') or relative.startswith('dc/') or relative.startswith('osp/')):
            continue
        if relative in skip:
            continue
        archive.write(path, 'home/username/apps/' + relative)
        count += 1
print(f'created {target}: {count} files, {target.stat().st_size} bytes')
`;
if (process.env.REUSE_GAME_ZIP !== '1')
  execFileSync('python3', ['-c', zipScript, gameRoot, gameZip, game, [...profile.skip].join('\n')], { stdio: 'inherit' });

let slot;
let server;
let browser;
let serverRoot;
const lines = [];
const pendingConsole = [];
const pageErrors = [];
let appLaunchFailure = '';
const result = {
  game,
  exe,
  arch,
  args,
  glVersion,
  backend: 'WebGPU default',
  gameRoot,
  gameZip,
  outputDir,
  mapped: false,
  captures: [],
  startedAt: new Date().toISOString(),
};
const record = line => {
  lines.push(line);
  if (line.includes('wine: failed to open') && line.includes(exe)) appLaunchFailure = line;
  if (/pageerror|http-error|first window mapped|gl-webgpu|gl64: host GL|TODO:|Unhandled exception|failed to open/.test(line))
    process.stdout.write(line.split('\n')[0].slice(0, 500) + '\n');
};

try {
  await waitForBuildIdle();
  const build = JSON.parse(await readFile(join(dist, 'runtime/BUILD_INFO.json'), 'utf8'));
  const wasmHash = createHash('sha256').update(await readFile(join(dist, 'runtime/boxedwine64.wasm'))).digest('hex');
  if (wasmHash !== build.wasm_sha256) throw new Error('Runtime WASM does not match BUILD_INFO.json');
  result.buildId = build.build_id || build.wasm_sha256.slice(0, 12);
  result.gameZipBytes = (await stat(gameZip)).size;

  serverRoot = await mkdtemp(join(tmpdir(), 'bw-gl-game-'));
  await symlink(dist, join(serverRoot, 'dist'), 'dir');
  await symlink(gameZip, join(serverRoot, 'game.zip'));
  server = await serve(serverRoot);
  slot = await acquireProbeSlot();
  browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
    headless: true,
    args: [
      '--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader',
      '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan',
    ],
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
  await page.addInitScript(() => {
    globalThis.__bwGlGameProbe = { mappedAt: 0, messages: [] };
    addEventListener('message', event => {
      const message = event.data;
      if (message?.wine !== true || message.type !== 'log') return;
      const line = String(message.data ?? '');
      const state = globalThis.__bwGlGameProbe;
      state.messages.push({ at: Date.now(), line });
      if (!state.mappedAt && line.includes('first window mapped')) state.mappedAt = Date.now();
    });
  });
  page.on('console', message => {
    pendingConsole.push((async () => {
      const values = [];
      for (const arg of message.args()) {
        try { values.push(await arg.jsonValue()); }
        catch { values.push(arg.toString()); }
      }
      const rendered = values.map(value => typeof value === 'string' ? value : JSON.stringify(value)).join(' ');
      record(`console ${message.type()}: ${rendered || message.text()}`);
    })());
  });
  page.on('pageerror', error => {
    const line = `pageerror: ${error.stack || error.message}`;
    pageErrors.push(line);
    record(line);
  });
  page.on('response', response => {
    if (response.status() >= 400) record(`http-error ${response.status()}: ${response.url()}`);
  });

  const base = `http://127.0.0.1:${server.address().port}`;
  const query = new URLSearchParams({
    gamezip: `${base}/game.zip`,
    gamezipsize: String(result.gameZipBytes),
    exe,
    arch,
    args: JSON.stringify(args),
    glversion: glVersion,
    session: '0',
    persist: '0',
    fresh: '1',
  });
  const launchAt = Date.now();
  result.launchUrl = `${base}/dist/runtime/index.html?${query}`;
  await page.goto(result.launchUrl, { waitUntil: 'domcontentloaded', timeout: 60000 });
  record(`launched ${game} through gamezip import: ${exe}`);

  const mapDeadline = launchAt + 8 * 60 * 1000;
  let mappedAt = 0;
  while (Date.now() < mapDeadline) {
    mappedAt = await page.evaluate(() => globalThis.__bwGlGameProbe?.mappedAt || 0);
    if (mappedAt) break;
    if (pageErrors.length || appLaunchFailure) break;
    await page.waitForTimeout(1000);
  }
  result.mapped = Boolean(mappedAt);
  result.mappedWaitSeconds = mappedAt ? Number(((mappedAt - launchAt) / 1000).toFixed(1)) : null;
  if (!mappedAt) {
    const path = join(outputDir, 'timeout.png');
    await page.screenshot({ path, fullPage: true });
    result.captures.push({ label: 'timeout', secondsAfterLaunch: Number(((Date.now() - launchAt) / 1000).toFixed(1)), path });
  } else {
    const milestones = [60, 120, 240];
    for (const seconds of milestones) {
      const due = mappedAt + seconds * 1000;
      while (Date.now() < due) await page.waitForTimeout(Math.min(1000, due - Date.now()));
      const sample = await page.evaluate(() => ({
        at: Date.now(),
        canvas: [...document.querySelectorAll('canvas')].map(canvas => ({ id: canvas.id, width: canvas.width, height: canvas.height })),
        gl: globalThis.bwGlStats || null,
        runtimeReady: Boolean(globalThis.bwRuntime?.ready?.()),
        messages: globalThis.__bwGlGameProbe?.messages.slice(-12) || [],
      }));
      const label = `mapped-plus-${seconds}s`;
      const path = join(outputDir, `${label}.png`);
      await page.screenshot({ path, fullPage: true });
      result.captures.push({ label, secondsAfterMapped: seconds, path, sample });
      record(`${label}: ${path}`);
      await writeFile(join(outputDir, 'summary.json'), JSON.stringify(result, null, 2));
    }
  }
  result.pageErrors = pageErrors;
  result.appLaunchFailure = appLaunchFailure || null;
  result.finishedAt = new Date().toISOString();
} catch (error) {
  result.error = error.stack || String(error);
  result.finishedAt = new Date().toISOString();
} finally {
  await Promise.allSettled(pendingConsole);
  await writeFile(join(outputDir, 'console.log'), lines.join('\n') + (lines.length ? '\n' : ''));
  await writeFile(join(outputDir, 'summary.json'), JSON.stringify(result, null, 2));
  await browser?.close().catch(() => {});
  if (server) await new Promise(resolveClose => server.close(resolveClose));
  await slot?.release();
  if (serverRoot) await rm(serverRoot, { recursive: true, force: true });
  record(JSON.stringify({ game, mapped: result.mapped, mappedWaitSeconds: result.mappedWaitSeconds, error: result.error || null, outputDir }));
}
if (!result.mapped || result.error || pageErrors.length) process.exitCode = 1;
