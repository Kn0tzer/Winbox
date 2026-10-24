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
    args: ['+set', 'r_fullscreen', '0', '+set', 'cl_renderer', 'opengl1',
      '+set', 'r_vbo', '0', '+map', 'q3dm1'],
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
const probeJit = process.env.PROBE_JIT ?? process.env.JIT ?? '0';
const probeRecover = process.env.PROBE_RECOVER ?? '1';
const wildJump = process.env.WILDJUMP === '1';
const unimplDump = process.env.UNIMPLDUMP === '1';
const guestDiagnostics = process.env.GUEST_DIAGNOSTICS === '1';
const guestTraceExe = process.env.GUEST_TRACE_EXE || exe;
if (!['x86', 'x64'].includes(arch) || !Array.isArray(args) || args.some(arg => typeof arg !== 'string'))
  throw new Error('Invalid GAME_ARCH or GAME_ARGS');
if (!['0', '1'].includes(probeJit) || !['0', '1'].includes(probeRecover))
  throw new Error('PROBE_JIT and PROBE_RECOVER must be 0 or 1');

const outputDir = resolve(process.env.OUT || join(workRoot, 'evidence', game));
const gameZip = resolve(process.env.GAME_ZIP || join(workRoot, `${game}-probe.zip`));
const dist = resolve(process.env.WINE_DIST_DIR || join(repo, 'dist'));
const mapTimeoutSeconds = Number(process.env.MAP_TIMEOUT_SECONDS || 8 * 60);
const captureSeconds = process.env.CAPTURE_SECONDS
  ? process.env.CAPTURE_SECONDS.split(',').map(value => Number(value.trim()))
  : [60, 120, 240];
const probeForwardAtSeconds = process.env.PROBE_FORWARD_AT_SECONDS === undefined
  ? null
  : Number(process.env.PROBE_FORWARD_AT_SECONDS);
const probeForwardHoldMs = Number(process.env.PROBE_FORWARD_HOLD_MS || 3000);
if (!Number.isFinite(mapTimeoutSeconds) || mapTimeoutSeconds <= 0 ||
    captureSeconds.some(value => !Number.isFinite(value) || value <= 0) ||
    (probeForwardAtSeconds !== null &&
      (!Number.isFinite(probeForwardAtSeconds) || !captureSeconds.includes(probeForwardAtSeconds))) ||
    !Number.isFinite(probeForwardHoldMs) || probeForwardHoldMs <= 0)
  throw new Error('Invalid map timeout, capture schedule, or forward-input probe setting');
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
  jit: probeJit,
  recover: probeRecover,
  guestDiagnostics,
  guestTraceExe: guestDiagnostics ? guestTraceExe : null,
  wildJump,
  unimplDump,
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
  if (/pageerror|http-error|first window mapped|gl-webgpu|gl64: host GL|BW_THREAD_DUMP|RIPSAMPLE|SYSTRACE|MEMSTATS|TODO:|Unhandled exception|failed to open|BW64_WILDJUMP|CPU64:.*(?:unimpl|#GP)/.test(line))
    process.stdout.write(line.split('\n')[0].slice(0, 500) + '\n');
};

async function collectGuestCrashReports(page, label) {
  const reports = await page.evaluate(() => {
    const fs = globalThis.bwRuntime?.fs?.();
    if (!fs) return { error: 'bwRuntime.fs unavailable', files: [] };
    const roots = ['/root/home/username/apps', '/root/home/username', '/root/tmp',
      '/home/username/apps', '/home/username', '/tmp'];
    const files = [];
    const seen = new Set();
    for (const root of roots) {
      let entries;
      try { entries = fs.readdir(root); } catch { continue; }
      for (const name of entries) {
        if (!/^hs_err_pid\d+\.log$/.test(name)) continue;
        const path = root + '/' + name;
        if (seen.has(path)) continue;
        seen.add(path);
        try {
          files.push({ path, content: fs.readFile(path, { encoding: 'utf8' }) });
        } catch {}
      }
    }
    return { files };
  });
  result.guestCrashReports ||= [];
  const captured = [];
  for (let i = 0; i < (reports.files || []).length; i++) {
    const report = reports.files[i];
    const path = join(outputDir, label + '-hs_err-' + (i + 1) + '.log');
    await writeFile(path, report.content);
    captured.push({ guestPath: report.path, path });
  }
  result.guestCrashReports.push({ label, files: captured, error: reports.error || null });
  if (captured.length) {
    const paths = captured.map(file => file.guestPath).join(', ');
    record('captured guest JVM crash reports: ' + paths);
  }
  return captured;
}

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
  // Load waiting is caller-managed with winbox-wait-load.sh. After its single
  // bounded wait, launch even if the machine remains busy instead of polling.
  browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium',
    headless: true,
    args: [
      '--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader',
      '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan',
    ],
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
  if (guestDiagnostics) {
    await page.route('**/wine64-launcher.js*', async route => {
      const response = await route.fetch();
      const body = await response.text();
      const marker = 'if (param("unimpldump") === "1") ENV["BW64_UNIMPLDUMP"] = "1";';
      if (!body.includes(marker)) throw new Error('Could not install guest diagnostic environment flags');
      const diagnosticFlags = [
        'if (param("bw64diag") === "threads") {',
        '  ENV["BW_THREAD_DUMP"] = "1";',
        '  ENV["BW64_CRASHRING"] = "1";',
        '  ENV["BW64_WSREAD"] = "1";',
        `  ENV["BW64_TRACE_EXE"] = ${JSON.stringify(guestTraceExe)};`,
        '  ENV["BW64_MEMSTATS"] = "1";',
        '}',
        marker,
      ].join('\n');
      await route.fulfill({ response, body: body.replace(marker, diagnosticFlags) });
      result.diagnosticLauncherPatched = true;
    });
  }
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
    jit: probeJit,
    recover: probeRecover,
  });
  if (wildJump) query.set('wildjump', '1');
  if (unimplDump) query.set('unimpldump', '1');
  if (guestDiagnostics) {
    query.set('bw64diag', 'threads');
    query.set('ripsample', '1');
  }
  if (process.env.GL_TRACE) query.set('gltrace', process.env.GL_TRACE);
  const launchAt = Date.now();
  result.launchUrl = `${base}/dist/index.html?${query}`;
  await page.goto(result.launchUrl, { waitUntil: 'domcontentloaded', timeout: 60000 });
  record(`launched ${game} through gamezip import: ${exe}`);

  const mapDeadline = launchAt + mapTimeoutSeconds * 1000;
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
    await collectGuestCrashReports(page, 'timeout');
    result.captures.push({ label: 'timeout', secondsAfterLaunch: Number(((Date.now() - launchAt) / 1000).toFixed(1)), path });
  } else {
    for (const seconds of captureSeconds) {
      const due = mappedAt + seconds * 1000;
      while (Date.now() < due) await page.waitForTimeout(Math.min(1000, due - Date.now()));
      if (guestDiagnostics) {
        const invoked = await page.evaluate(() => {
          const module = globalThis.Module;
          if (typeof module?._bw64_dump_guest_threads === 'function') {
            module._bw64_dump_guest_threads();
            return 'export';
          }
          if (typeof module?.ccall === 'function') {
            module.ccall('bw64_dump_guest_threads', null, [], []);
            return 'ccall';
          }
          return 'unavailable';
        });
        record(`BW_THREAD_DUMP_DRIVER invoked via ${invoked}`);
        await page.waitForTimeout(250);
      }
      const sample = await page.evaluate(() => {
        const messages = globalThis.__bwGlGameProbe?.messages || [];
        return {
          at: Date.now(),
          canvas: [...document.querySelectorAll('canvas')].map(canvas => ({ id: canvas.id, width: canvas.width, height: canvas.height })),
          gl: globalThis.bwGlStats || null,
          runtimeReady: Boolean(globalThis.bwRuntime?.ready?.()),
          messages: messages.slice(-12),
          progress: messages.filter(message => /\[(?:I|W|E)\]/.test(message.line)),
          metrics: messages.filter(message => message.line.startsWith('bw64: METRICS ')).slice(-4),
        };
      });
      const label = `mapped-plus-${seconds}s`;
      const path = join(outputDir, `${label}.png`);
      await page.screenshot({ path, fullPage: true });
      await collectGuestCrashReports(page, label);
      result.captures.push({ label, secondsAfterMapped: seconds, path, sample });
      record(`${label}: ${path}`);
      await writeFile(join(outputDir, 'summary.json'), JSON.stringify(result, null, 2));
      if (seconds === probeForwardAtSeconds) {
        const inputPath = join(outputDir, `${label}-after-w.png`);
        await page.keyboard.down('w');
        try {
          await page.waitForTimeout(probeForwardHoldMs);
        } finally {
          await page.keyboard.up('w').catch(() => {});
        }
        await page.waitForTimeout(1000);
        await page.screenshot({ path: inputPath, fullPage: true });
        result.inputProbe = { key: 'w', binding: '+forward', holdMs: probeForwardHoldMs, path: inputPath };
        record(`forward input probe: ${inputPath}`);
        await writeFile(join(outputDir, 'summary.json'), JSON.stringify(result, null, 2));
      }
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
  result.webgpuActive = lines.some(line => line.includes('host GL up (WebGPU)'))
    && !lines.some(line => line.includes('restored WebGL2'));
  result.webgpuFailures = lines.filter(line => /bridge-recover: gl-webgpu|gl64 fixed-function .*failed/.test(line));
  await writeFile(join(outputDir, 'console.log'), lines.join('\n') + (lines.length ? '\n' : ''));
  await writeFile(join(outputDir, 'summary.json'), JSON.stringify(result, null, 2));
  await browser?.close().catch(() => {});
  if (server) await new Promise(resolveClose => server.close(resolveClose));
  await slot?.release();
  if (serverRoot) await rm(serverRoot, { recursive: true, force: true });
  record(JSON.stringify({ game, mapped: result.mapped, mappedWaitSeconds: result.mappedWaitSeconds, error: result.error || null, outputDir }));
}
if (!result.mapped || !result.webgpuActive || result.webgpuFailures?.length || result.error || pageErrors.length) process.exitCode = 1;
