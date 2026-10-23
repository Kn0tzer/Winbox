// Repeatable isolated game probe. GAME=portal|celeste|gangbeasts node ...
// Evidence records the exact runtime and overlays; a mapped window never passes.
import { chromium } from '@playwright/test';
import { mkdir, readFile, writeFile, mkdtemp, symlink, rm, stat } from 'node:fs/promises';
import { createWriteStream } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { resolve, join } from 'node:path';
import { tmpdir } from 'node:os';
import { serve } from '../../tools/browser/serve.mjs';
import { acquireProbeSlot } from './probe-slots.mjs';
import { waitForBuildIdle } from './build-lock.mjs';
import { hasPortalMenu, summarizeRenderedFrames } from './portal-evidence.mjs';
import { portalMenuText } from './portal-ocr.mjs';

const repo = resolve(import.meta.dirname, '../..');
const runtimeRoot = resolve(process.env.PROBE_DIST || join(repo, 'dist'));
const profiles = {
  portal: { arch: 'x86', exe: 'hl2.exe', renderer: 'd3d9', args: ['-game', 'portal', '-novid', '-windowed', '-w', '1024', '-h', '768'] },
  celeste: { arch: 'x86', exe: 'Celeste.exe', renderer: 'opengl', args: [] },
  gangbeasts: { arch: 'x64', exe: 'Gang Beasts.exe', renderer: 'd3d11', args: ['-force-d3d11', '-screen-fullscreen', '0', '-screen-width', '1024', '-screen-height', '768'] },
  gl32: { arch: 'x86', exe: 'trigl32.exe', renderer: 'opengl', args: [], fixture: true },
  gl64: { arch: 'x64', exe: 'trigl.exe', renderer: 'opengl', args: [], fixture: true },
};
const game = process.env.GAME || 'portal';
if (!profiles[game]) throw Error(`Unknown GAME: ${game}`);
const profile = { ...profiles[game], renderer: process.env.PROBE_RENDERER || profiles[game].renderer };
const launcherApp = process.env.PROBE_APP === '1';
const installedDirect3D = launcherApp || process.env.PROBE_INSTALLED_D3D === '1';
if (!['d3d9', 'd3d11', 'opengl'].includes(profile.renderer)) throw Error(`Invalid renderer: ${profile.renderer}`);
const tag = process.env.TAG || `${game}-${Date.now()}`;
if (!/^[\w.-]+$/.test(tag)) throw Error('Invalid TAG');
const dir = join(repo, 'test-results', tag);
await mkdir(dir, { recursive: true });
const root = await mkdtemp(join(tmpdir(), 'bw-game-'));
const gamePath = resolve(process.env.GAME_ZIP || `/home/ubuntu/kn0tzer/games/${game === 'celeste' ? 'celeste-drmfree' : game}.zip`);
const result = { game, profile, tag, startedAt: new Date().toISOString(), menuVerified: false,
  gameplayVerified: false, installedDirect3D, launcherApp, pass: false, samples: [] };
let slot, server, browser, browserServer, persistentContext, logFile;
const logs = [];
let stopping = false;
process.on('SIGTERM', () => { stopping = true; });
process.on('SIGINT', () => { stopping = true; });
try {
  slot = await acquireProbeSlot();
  await waitForBuildIdle();
  result.build = JSON.parse(await readFile(join(runtimeRoot, 'runtime/BUILD_INFO.json'), 'utf8'));
  if (createHash('sha256').update(await readFile(join(runtimeRoot, 'runtime/boxedwine64.wasm'))).digest('hex') !== result.build.wasm_sha256)
    throw new Error('Runtime hash does not match BUILD_INFO.json');
  await symlink(runtimeRoot, join(root, 'dist'));
  // Source shader.mjs resolves its pre-pass relative to the export parent.
  await symlink(join(runtimeRoot, 'tools'), join(root, 'tools'));
  await symlink(gamePath, join(root, 'game.zip'));
  await symlink(dir, join(root, 'evidence'));
  const overlay = join(dir, 'overlay.zip');
  const assets = [];
  if (process.env.PROBE_ASSET) assets.push([resolve(process.env.PROBE_ASSET), `home/username/apps/${process.env.PROBE_EXE || profile.exe}`]);
  if (profile.renderer.startsWith('d3d') && !installedDirect3D) {
    const arch = profile.arch === 'x86' ? '32' : '64';
    execFileSync('python3', [join(repo, 'tools/rootfs64/build_vulkan_shims.py'), '--check', '--arch', arch]);
    const dlls = profile.renderer === 'd3d9' ? ['d3d9'] : ['d3d11', 'dxgi'];
    for (const dll of dlls) {
      const file = join(repo, `tools/dxvk/build.w${arch}/src/${dll}/${dll}.dll`);
      assets.push([file, `home/username/apps/${dll}.dll`]);
      if (game === 'portal') assets.push([file, `home/username/apps/bin/${dll}.dll`]);
    }
    const shim = resolve(process.env.PROBE_VULKAN_SHIM || join(repo, `tools/rootfs64/libvk${arch}/libvulkan.so.1`));
    const shimBytes = await readFile(shim);
    const expectedMachine = arch === '32' ? 3 : 62;
    if (shimBytes.length < 20 || shimBytes.toString('hex', 0, 4) !== '7f454c46' || shimBytes[4] !== (arch === '32' ? 1 : 2) ||
        shimBytes.readUInt16LE(18) !== expectedMachine) throw Error('Vulkan shim architecture does not match guest');
    result.vulkanShim = { path: shim, sha256: createHash('sha256').update(shimBytes).digest('hex') };
    assets.push([shim, 'home/username/apps/libvulkan.so.1']);
  }
  if (profile.renderer === 'opengl' && (game === 'gl32' || (game === 'celeste' && process.env.GL32 === '1'))) {
    const shim = join(repo, 'tools/rootfs64/libgl32/libGL.so.1');
    // ZIP mounts keep their first entry; the base Wine archive already owns
    // /usr/lib's Mesa library. LD_LIBRARY_PATH searches this unique path first.
    assets.push([shim, 'home/username/apps/libGL.so.1']);
    assets.push([shim, 'usr/lib/i386-linux-gnu/libGL.so.1']);
  }
  execFileSync('python3', ['-c', `import zipfile,json,sys,pathlib
with zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_STORED) as z:
 for src,dst in json.loads(sys.argv[2]):z.write(src,dst)
 if sys.argv[3]:
  root=pathlib.Path(sys.argv[3]).resolve()
  if not (root/'mono/mono-2.0/bin/libmono-2.0-x86.dll').is_file():raise ValueError('Not a Wine Mono directory')
  for src in sorted(root.rglob('*')):
   if src.is_file():z.write(src,'home/username/apps/'+src.relative_to(root).as_posix())
`, overlay, JSON.stringify(assets), process.env.MONO32_DIR || '']);
  result.overlaySha256 = createHash('sha256').update(await readFile(overlay)).digest('hex');
  result.gameArchive = { path: gamePath, bytes: (await stat(gamePath)).size };
  server = await serve(root, Number(process.env.PROBE_PORT || 0));
  const browserOptions = { executablePath: process.env.CHROME_PATH || '/usr/local/bin/chromium', headless: true,
    viewport: { width: 1280, height: 900 },
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader',
      '--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan',
      ...(process.env.PROBE_CHROME_ARGS ? JSON.parse(process.env.PROBE_CHROME_ARGS) : [])] };
  let page;
  if (process.env.PROBE_USER_DATA_DIR) {
    persistentContext = await chromium.launchPersistentContext(resolve(process.env.PROBE_USER_DATA_DIR), browserOptions);
    browser = persistentContext.browser();
    page = persistentContext.pages()[0] || await persistentContext.newPage();
    result.browserProfile = resolve(process.env.PROBE_USER_DATA_DIR);
  } else {
    browserServer = await chromium.launchServer({ ...browserOptions,
      args: [...browserOptions.args, '--remote-debugging-address=127.0.0.1', '--remote-debugging-port=0'] });
    await writeFile(join(dir, '.browser.ws'), browserServer.wsEndpoint(), { mode: 0o600 });
    const userData = browserServer.process().spawnargs.find(a => a.startsWith('--user-data-dir='))?.split('=').slice(1).join('=');
    if (userData) {
      const port = (await readFile(join(userData, 'DevToolsActivePort'), 'utf8')).split('\n')[0];
      await writeFile(join(dir, '.browser.cdp'), `http://127.0.0.1:${port}`, { mode: 0o600 });
    }
    browser = await chromium.connect(browserServer.wsEndpoint());
    page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  }
  const launcher = await readFile(resolve(process.env.PROBE_LAUNCHER || join(runtimeRoot, 'runtime/wine64-launcher.js')));
  result.launcherSha256 = createHash('sha256').update(launcher).digest('hex');
  await page.route(url => url.pathname.endsWith('/wine64-launcher.js'), route => route.fulfill({ contentType:'text/javascript', body:launcher }));
  await page.addInitScript(() => {
    let renderer;
    globalThis.__gameFrames = [];
    globalThis.__gameProbeNavigationAt = performance.now();
    globalThis.__gameFirstFrameAt = null;
    globalThis.__gameGlFirstFrameAt = null;
    globalThis.__gameGlFrames = [];
    let glDrawCount = 0, sampledDrawCount = 0, previousRaf = performance.now();
    const glPrototype = globalThis.WebGL2RenderingContext?.prototype;
    for (const name of ['drawArrays', 'drawElements', 'drawArraysInstanced', 'drawElementsInstanced']) {
      const original = glPrototype?.[name];
      if (typeof original !== 'function') continue;
      glPrototype[name] = function(...args) { glDrawCount++; return original.apply(this, args); };
    }
    const sampleGlFrames = now => {
      if (glDrawCount !== sampledDrawCount) {
        if (globalThis.__gameGlFirstFrameAt === null) globalThis.__gameGlFirstFrameAt = now;
        globalThis.__gameGlFrames.push({ start: previousRaf, end: now });
        if (globalThis.__gameGlFrames.length > 8192) globalThis.__gameGlFrames.shift();
        sampledDrawCount = glDrawCount;
      }
      previousRaf = now;
      requestAnimationFrame(sampleGlFrames);
    };
    requestAnimationFrame(sampleGlFrames);
    Object.defineProperty(globalThis, 'bwVkRenderer2', { configurable: true, get: () => renderer,
      set(value) {
        renderer = value;
        const render = value.render.bind(value);
        value.render = async (frame, options) => {
          globalThis.__gameLastFrame = frame;
          const start = performance.now();
          const outcome = await render(frame, options);
          globalThis.__gameLastOutcome = outcome;
          if (outcome?.rendered && options?.present !== false) {
            const end = performance.now();
            if (globalThis.__gameFirstFrameAt === null) globalThis.__gameFirstFrameAt = end;
            const samples = globalThis.__gameFrames;
            samples.push({ start, end });
            if (samples.length > 8192) samples.shift();
            // The probe measures renderer submission latency. Waiting for the
            // entire GPU queue here back-pressures rendering and can hide frames.
            value.device.queue.onSubmittedWorkDone().catch(() => {});
          }
          return outcome;
        };
      } });
  });
  logFile = createWriteStream(join(dir, 'console.log'));
  const record = line => { logs.push(line); logFile.write(line + '\n'); };
  const t0 = Date.now();
  page.on('console', m => record(`[${((Date.now() - t0)/1000).toFixed(1)}s] ${m.type()}: ${m.text()}`));
  page.on('pageerror', e => record(`pageerror: ${e.stack || e.message}`));
  page.on('crash', () => record('pagecrash: the page crashed'));
  browserServer?.process().on('exit', (code, signal) => { console.error(`browser-exit: code=${code} signal=${signal}`); record(`browser-exit: code=${code} signal=${signal}`); });
  browserServer?.process().stderr?.on('data', d => record('chrome-stderr: ' + String(d).trimEnd()));
  page.on('response', r => { if (r.status() >= 400) record(`http-error: ${r.status()} ${new URL(r.url()).pathname}`); });
  const base = `http://127.0.0.1:${server.address().port}`;
  const params = new URLSearchParams({ gamezip: `${base}/game.zip`, gamezipsize: String(result.gameArchive.bytes),
    arch: profile.arch, exe: process.env.PROBE_EXE || profile.exe, args: process.env.GAME_ARGS || JSON.stringify(profile.args),
    session: '0', persist: '0', fresh: '1', installation: process.env.PROBE_INSTALLATION || game });
  if (assets.length || process.env.MONO32_DIR) {
    params.set('overlay', `${base}/evidence/overlay.zip`);
    params.set('overlaysize', String((await stat(overlay)).size));
  }
  if (!installedDirect3D) params.set('ldpath', '/home/username/apps');
  if (process.env.PROBE_JIT) params.set('jit', process.env.PROBE_JIT === '0' ? '0' : '1');
  if (process.env.PROBE_EXTRA) for (const [key, value] of new URLSearchParams(process.env.PROBE_EXTRA)) params.set(key, value);
  if (process.env.WINEDEBUG) params.set('winedbg', process.env.WINEDEBUG);
  else if (profile.fixture) params.set('winedbg', '+debugstr');
  if (profile.renderer === 'd3d9') params.set('dlloverrides', 'd3d9=n');
  if (profile.renderer === 'd3d11') params.set('dlloverrides', 'd3d11=n;dxgi=n');
  if (process.env.GLTRACE) params.set('gltrace', process.env.GLTRACE);
  if (process.env.XWIRE === '1') params.set('xwire', '1');
  if (process.env.RIPSAMPLE) params.set('ripsample', '1');
  if (launcherApp) {
    await page.goto(`${base}/dist/`, { timeout: 60000 });
    await page.locator(`[data-game="${game}"]`).click();
    const iframe = page.locator('#display iframe');
    await iframe.waitFor({ timeout: 60000 });
    const launched = new URL(await iframe.getAttribute('src'), page.url());
    result.launchParams = Object.fromEntries(launched.searchParams);
    if (launched.searchParams.has('overlay') || launched.searchParams.has('ldpath'))
      throw Error('Launcher must use installed graphics libraries');
  } else {
    result.launchParams = Object.fromEntries(params);
    await page.goto(`${base}/dist/runtime/index.html?${params}`, { timeout: 60000 });
  }
  const cdp = await browser.newBrowserCDPSession();
  result.browser = await cdp.send('Browser.getVersion');
  result.adapter = await page.evaluate(async () => { const a = await navigator.gpu?.requestAdapter();
    return a?.info && { vendor: a.info.vendor, architecture: a.info.architecture, device: a.info.device, description: a.info.description }; });
  const timeoutMs = Number(process.env.TIMEOUT_MS || 600000);
  const measureMs = Number(process.env.MEASURE_MS || 120000);
  let menuStart;
  let firstFrameMeasureStart;
  let stackDumped = false;
  while (!stopping && Date.now() - t0 < timeoutMs) {
    await page.waitForTimeout(15000);
    if (process.env.DUMP_STACKS === '1' && !stackDumped && Date.now() - t0 >= 30000) {
      stackDumped = true;
      await page.locator('#canvas').focus();
      await page.keyboard.press('ScrollLock');
    }
    const snap = await page.evaluate(() => {
      const runtime = globalThis.bwRuntime || document.querySelector('#display iframe')?.contentWindow.bwRuntime;
      const fs = runtime?.fs();
      const guestLogs = {};
      const guestDirectories = {};
      for (const path of ['/root', '/root/home/username', '/root/home/username/apps']) {
        try { guestDirectories[path] = fs.readdir(path).slice(0, 100); }
        catch (e) { guestDirectories[path] = String(e); }
      }
      for (const path of ['/root/home/username/apps/error_log.txt', '/root/home/username/apps/errorLog.txt', '/root/home/username/apps/ErrorLog.txt',
        '/root/home/username/apps/player.log', '/root/home/username/apps/console.log', '/root/home/username/apps/portal/console.log',
        '/root/home/username/.wine/drive_c/users/username/AppData/LocalLow/Boneloaf/Gang Beasts/Player.log']) {
        try { guestLogs[path] = fs.readFile(path, {encoding:'utf8'}).slice(-65536); } catch {}
      }
      return { now:performance.now(), firstFrameAt:globalThis.__gameFirstFrameAt ?? globalThis.__gameGlFirstFrameAt, navigationAt:globalThis.__gameProbeNavigationAt,
        stats:globalThis.bwVkStats?.(), metrics:globalThis.bwVkTier?.metrics?.(), frames:globalThis.__gameFrames.length ? globalThis.__gameFrames : globalThis.__gameGlFrames,
        gl:globalThis.bwGlStats, guestLogs, guestDirectories,
        canvas:[...document.querySelectorAll('canvas')].map(c=>({id:c.id, width:c.width, height:c.height})) };
    });
    const shot = await page.screenshot();
    await writeFile(join(dir, 'current.png'), shot);
    const hash = createHash('sha256').update(shot).digest('hex');
    let text = '';
    if (game === 'portal') {
      const ocr = await portalMenuText(join(dir, 'current.png'));
      text = ocr.text;
      if (ocr.timedOut) result.ocrTimeouts = (result.ocrTimeouts || 0) + 1;
      if (hasPortalMenu(text) && menuStart === undefined) {
        menuStart = snap.now; result.menuVerified = true; result.bootToMenuSeconds = snap.now/1000;
        await writeFile(join(dir, 'menu.png'), shot);
      }
    }
    result.samples.push({ elapsedSeconds: (Date.now()-t0)/1000, hash, stats:snap.stats, metrics:snap.metrics, gl:snap.gl, text });
    if (result.timeToFirstFrameSeconds == null && snap.firstFrameAt != null) result.timeToFirstFrameSeconds = (snap.firstFrameAt - snap.navigationAt) / 1000;
    if (process.env.PROBE_MEASURE_AFTER_FIRST_FRAME === '1' && firstFrameMeasureStart == null && snap.firstFrameAt != null) firstFrameMeasureStart = snap.firstFrameAt;
    result.canvas = snap.canvas;
    result.guestLogs = snap.guestLogs;
    result.guestDirectories = snap.guestDirectories;
    const measurementStart = menuStart ?? firstFrameMeasureStart;
    if (measurementStart !== undefined && snap.now > measurementStart) result.measurement = summarizeRenderedFrames(snap.frames, measurementStart, snap.now);
    result.errors = logs.filter(l => /pageerror:|Native panic stack|SHADER COMPILE FAIL|device lost|d3d11: failed|http-error:/.test(l)).slice(-12);
    await writeFile(join(dir, 'summary.json'), JSON.stringify(result, null, 2));
    console.log(JSON.stringify({ dir, seconds: Math.round((Date.now()-t0)/1000), menuVerified:result.menuVerified,
      rendered:snap.stats?.v2?.rendered, rejected:snap.stats?.v2?.rejected, gl:snap.gl,
      fps:result.measurement?.renderedFps, last:logs.slice(-2).map(l=>l.slice(0,160)) }));
    if (result.errors.some(l => /pageerror:|Native panic stack|http-error:/.test(l))) break;
    if (profile.fixture && logs.some(l => /trigl: RESULT 0/.test(l))) { result.pass = true; break; }
    if (menuStart !== undefined && snap.now - menuStart >= measureMs) {
      result.pass = result.measurement.renderedFrames > 0;
      if (process.env.HOLD_OPEN !== '1') break;
    }
    if (firstFrameMeasureStart !== undefined && snap.now - firstFrameMeasureStart >= measureMs) {
      result.pass = result.timeToFirstFrameSeconds != null;
      if (process.env.HOLD_OPEN !== '1') break;
    }
  }
  await page.screenshot({ path: join(dir, 'final.png') });
} catch (e) { result.error = e.stack || String(e); }
finally {
  result.finishedAt = new Date().toISOString();
  await writeFile(join(dir, 'summary.json'), JSON.stringify(result, null, 2));
  logFile?.end();
  await persistentContext?.close().catch(() => {});
  if (!persistentContext) await browser?.close().catch(() => {});
  await browserServer?.close().catch(() => {});
  await rm(join(dir, '.browser.ws'), { force: true });
  await rm(join(dir, '.browser.cdp'), { force: true });
  server?.close();
  await slot?.release();
  await rm(root, { recursive: true, force: true });
  console.log(JSON.stringify({ dir, pass:result.pass, error:result.error, measurement:result.measurement }));
}
if (!result.pass) process.exitCode = 1;
