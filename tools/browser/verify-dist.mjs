import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFile, readdir } from 'node:fs/promises';
import { dirname, relative, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const dist = resolve(repo, process.env.BW_TEST_DIST || process.env.WINE_BUILD_OUT || 'dist');
const fail = message => { throw new Error(`Browser export verification failed: ${message}`); };

async function walk(directory) {
  const files = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const path = resolve(directory, entry.name);
    if (entry.isDirectory()) files.push(...await walk(path));
    else if (entry.isFile() || (entry.isSymbolicLink() && entry.name.endsWith('.mjs'))) files.push(path);
    // Symlinked optional game archives and external files are not source modules.
  }
  return files;
}

const files = await walk(dist);
if (!files.length) fail(`no files found in ${dist}`);
const modules = files.filter(path => path.endsWith('.mjs'));
if (!modules.length) fail(`no .mjs files found in ${dist}`);
for (const module of modules) {
  try { execFileSync(process.execPath, ['--check', module], { stdio: 'pipe' }); }
  catch (error) {
    const details = error.stderr?.toString() || error.stdout?.toString() || error.message;
    fail(`syntax error in ${relative(dist, module)}\n${details}`);
  }
}

for (const path of files.filter(path => /\.(?:mjs|js|html)$/.test(path))) {
  if ((await readFile(path, 'utf8')).includes('__RUNTIME_VERSION__'))
    fail(`unsubstituted __RUNTIME_VERSION__ in ${relative(dist, path)}`);
}

for (const path of [resolve(dist, 'app.mjs'), resolve(dist, 'runtime/index.html')]) {
  const source = await readFile(path, 'utf8').catch(() => null);
  if (!source) fail(`missing versioned shell ${relative(dist, path)}`);
  if (source.includes('__RUNTIME_VERSION__')) fail(`unsubstituted __RUNTIME_VERSION__ in ${relative(dist, path)}`);
  if (!/runtime(?:\/index\.html)?\?v=[a-f0-9]{12}/.test(source)) {
    // The runtime version can also be used through a URLSearchParams query.
    if (!source.includes('runtimeReleaseId') && !source.includes('runtimeUrl'))
      fail(`runtime release version is not referenced in ${relative(dist, path)}`);
  }
}

const manifestPath = resolve(dist, 'runtime/dependencies.json');
const manifest = JSON.parse(await readFile(manifestPath, 'utf8').catch(() => fail('missing runtime/dependencies.json')));
if (!manifest.archive || !/^[a-f0-9]{64}$/.test(manifest.sha256 || '')) fail('dependencies.json has no valid archive name or SHA-256');
const archivePath = resolve(dirname(manifestPath), manifest.archive);
if (!archivePath.startsWith(resolve(dist, 'runtime') + sep)) fail('dependencies.json archive escapes runtime/');
const archive = await readFile(archivePath).catch(() => fail(`missing dependency archive ${manifest.archive}`));
const actualHash = createHash('sha256').update(archive).digest('hex');
if (actualHash !== manifest.sha256) fail(`dependency archive hash mismatch: expected ${manifest.sha256}, got ${actualHash}`);
if (manifest.bytes !== archive.byteLength) fail(`dependency archive size mismatch: expected ${manifest.bytes}, got ${archive.byteLength}`);

const docs = await readFile(resolve(repo, 'tools/browser/README.md'), 'utf8');
for (const expectation of [
  /Caddyfile/i,
  /Cross-Origin-Opener-Policy\s*:\s*same-origin/i,
  /Cross-Origin-Embedder-Policy\s*:\s*require-corp/i,
  /_headers/,
]) if (!expectation.test(docs)) fail(`tools/browser/README.md does not document ${expectation}`);

console.log(`Browser export verified: ${modules.length} .mjs syntax files; runtime version substituted; dependency archive ${manifest.sha256.slice(0, 12)} verified; host header expectations documented.`);
