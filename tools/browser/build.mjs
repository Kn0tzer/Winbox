import { cp, mkdir, copyFile, rm, symlink, stat, readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
import { execFileSync } from 'node:child_process';
import { stageVendors } from './stage-vendor.mjs';
const root = resolve(import.meta.dirname, '../..');
const out = resolve(root, process.env.WINE_BUILD_OUT || 'dist');
await mkdir(out, { recursive: true });
if (out === root || out === resolve(root, 'web')) throw new Error('Build output must be separate from source files');
await copyFile(resolve(root, 'web/direct-folder.mjs'), resolve(out, 'direct-folder.mjs'));
await copyFile(resolve(root, 'web/frame-metrics.mjs'), resolve(out, 'frame-metrics.mjs'));
for (const name of ['index.html', 'style.css', 'app.mjs', 'clipboard-bridge.mjs', 'launch.mjs', 'demos.mjs', 'dependencies.mjs', 'package-archive.mjs', 'session-storage.mjs', 'installation.mjs', 'startup.mjs', 'presentation.mjs', 'network.mjs', 'enhancement.mjs', 'enhancement-shaders.mjs', 'files.mjs', 'games.mjs', 'storage.mjs', 'webgpu.mjs', 'shader.mjs', 'shader-cache.mjs', 'naga-rt.mjs', 'vk-dynstate.mjs', 'vkwebgpu.mjs', 'vk-frame-capture.mjs', 'vk-protocol.mjs', 'vk-log.mjs', 'wgsl-utils.mjs', 'gl-webgpu.mjs', 'gl-webgpu-utils.mjs', 'bridge-recovery.mjs', 'sparse-memfs.js']) await copyFile(resolve(root, 'web', name), resolve(out, name));
await cp(resolve(root, 'web/assets'), resolve(out, 'assets'), { recursive: true });
// Built-in game archives are staged under /games so the launcher catalog can
// range-fetch them. WINE_GAMES_DIR selects the source; WINE_GAMES_COPY=1 copies
// the archives instead of linking them (link is the default: they are 100+ MB).
const gamesOut = resolve(out, 'games');
async function findGames() {
  for (const dir of [process.env.WINE_GAMES_DIR, resolve(root, 'web/games')].filter(Boolean)) {
    try { await stat(resolve(dir, 'baldi.zip')); return dir; } catch { /* try the next source */ }
  }
  return null;
}
const gamesSrc = await findGames();
// Always drop the previous staging first, including the no-source branch: a
// stale dist/games left behind by an earlier build would still serve an old
// (or truncated) archive while this build reports the catalog as unavailable.
await rm(gamesOut, { recursive: true, force: true });
if (gamesSrc) {
  if (process.env.WINE_GAMES_COPY === '1') await cp(gamesSrc, gamesOut, { recursive: true });
  else await symlink(gamesSrc, gamesOut, 'dir');
  console.log(`Built-in games: ${gamesOut} -> ${gamesSrc} (${process.env.WINE_GAMES_COPY === '1' ? 'copied' : 'linked'})`);
} else {
  console.warn('Optional local game archives not staged; launcher demos use the hosted catalog.');
}

await stageVendors();
await stageVendors(resolve(out, 'vendor'));
await mkdir(resolve(out, 'runtime'), { recursive: true });
execFileSync('python3', [resolve(root, 'tools/browser/stage-dependencies.py'), resolve(out, 'runtime')], { stdio: 'inherit' });
await copyFile(resolve(root, 'tools/spirvfix/spirvfix.mjs'), resolve(out, 'spirvfix.mjs'));
// Runtime binaries come from the makefile target the build used. BW64_BUILD_TARGET
// mirrors project/emscripten/makefile (wasm64-mt = interpreter build,
// wasm64-mt-jit = the same runtime with -DBOXEDWINE_WASM_JIT).
const buildTargets = {
  'wasm64-mt': 'Wasm64Mt',
  'wasm64-mt-jit': 'Wasm64MtJit',
};
const buildTarget = process.env.BW64_BUILD_TARGET || 'wasm64-mt';
const buildDir = buildTargets[buildTarget];
if (!buildDir) throw new Error(`Unknown BW64_BUILD_TARGET: ${buildTarget}`);
for (const name of ['boxedwine64.js', 'boxedwine64.wasm', 'BUILD_INFO.json']) await copyFile(resolve(root, 'project/emscripten/Build', buildDir, name), resolve(out, 'runtime', name));
await copyFile(resolve(root, 'project/emscripten/wine64-launcher.js'), resolve(out, 'runtime/wine64-launcher.js'));
for (const name of ['glibc-rootfs64.zip', 'wine64.zip', 'prefix64.zip', 'glibc-rootfs32.zip', 'wine32.zip', 'prefix32.zip']) await copyFile(resolve(root, 'web/runtime', name), resolve(out, 'runtime', name));
// Keep pinned archives intact. Strip the browser-irrelevant winemenubuilder
// RunServices autostart from exported prefixes, then stage fonts and the
// DirectWrite Arial alias before recording final desktop-helper prefix hashes.
for (const name of ['prefix32.zip', 'prefix64.zip'])
  execFileSync('python3', [resolve(root, 'tools/rootfs64/prepare_prefixes.py'), 'sanitize', '--archive', resolve(out, 'runtime', name)], { stdio: 'inherit' });
execFileSync('python3', [resolve(root, 'tools/browser/stage-wine32-fonts.py'), resolve(out, 'runtime')]);
// Record final prefix hashes after both font aliases and desktop helpers.
execFileSync('python3', [resolve(root, 'tools/browser/stage-desktop-pin.py'), resolve(out, 'runtime')]);
// Hard completeness gate: the shipped runtime set must be complete before
// this export can succeed. A web/runtime zip that silently lost a staged
// member class (the 32-bit ALSA-to-OSS bridge went missing exactly this
// way, 2026-10-25: the manual stage-alsa32.sh post-step was never applied
// and no check named the members) fails the build here, naming the missing
// members, instead of shipping. The checker reads the canonical
// web/runtime zips plus this export's freshly built support pack.
execFileSync('python3', [resolve(root, 'tools/rootfs64/check_runtime_completeness.py')], { stdio: 'inherit', env: { ...process.env, BW64_SUPPORT_DIR: resolve(out, 'runtime') } });
await copyFile(resolve(root, 'web/runtime.html'), resolve(out, 'runtime/index.html'));
// Keep the cached runtime shell and loader together across UI deployments.
const runtimeBuild = JSON.parse(await readFile(resolve(out, 'runtime/BUILD_INFO.json'), 'utf8'));
const runtimeVersion = createHash('sha256').update(runtimeBuild.wasm_sha256).update(runtimeBuild.js_sha256)
  .update(await readFile(resolve(root, 'web/runtime.html')))
  .update(await readFile(resolve(root, 'project/emscripten/wine64-launcher.js')))
  .update(await readFile(resolve(root, 'web/bridge-recovery.mjs')))
  .update(await readFile(resolve(root, 'web/vkwebgpu.mjs')))
  .update(await readFile(resolve(root, 'web/gl-webgpu.mjs')))
  .update(await readFile(resolve(root, 'web/gl-webgpu-utils.mjs')))
  .update(await readFile(resolve(out, 'runtime/dependencies.json'))).digest('hex').slice(0, 12);
for (const name of ['app.mjs', 'runtime/index.html']) {
  const path = resolve(out, name);
  await writeFile(path, (await readFile(path, 'utf8')).replaceAll('__RUNTIME_VERSION__', runtimeVersion));
}
await copyFile(resolve(root, 'license.txt'), resolve(out, 'LICENSE.txt'));
await copyFile(resolve(root, 'project/emscripten/coi-serviceworker.js'), resolve(out, 'coi-serviceworker.js'));
// Pages cannot set isolation headers. Register the same-origin worker at the
// export root, so both the app and its runtime iframe are controlled.
const index = resolve(out, 'index.html');
await writeFile(index, (await readFile(index, 'utf8')).replace('</head>', '<script src="coi-serviceworker.js"></script></head>'));
console.log(`Browser export: ${out}`);
