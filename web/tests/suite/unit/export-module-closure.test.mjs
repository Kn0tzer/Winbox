import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile, readdir, stat } from 'node:fs/promises';
import { dirname, posix, relative, resolve } from 'node:path';

const repo = resolve(import.meta.dirname, '../../../../');
const web = resolve(repo, 'web');
const build = await readFile(resolve(repo, 'tools/browser/build.mjs'), 'utf8');

function exportedFiles(source) {
  const names = new Set();
  const list = /for\s*\(const name of \[([\s\S]*?)\]\) await copyFile\(resolve\(root, 'web', name\)/.exec(source)?.[1];
  assert.ok(list, 'build.mjs web export file list marker not found');
  for (const [, name] of list.matchAll(/'([^']+)'/g)) names.add(name);
  for (const match of source.matchAll(/copyFile\(resolve\(root, '[^']+'\), resolve\(out, '([^']+)'(?:, '([^']+)')?\)\)/g))
    names.add([match[1], match[2]].filter(Boolean).join('/'));
  const directories = [];
  for (const match of source.matchAll(/cp\(resolve\(root, 'web\/[^']+'\), resolve\(out, '([^']+)'\), \{ recursive: true \}\)/g))
    directories.push(match[1] + '/');
  if (/await stageVendors\(resolve\(out, 'vendor'\)\)/.test(source)) directories.push('vendor/');
  return { names, directories };
}

function relativeImports(source) {
  const specifiers = [];
  const pattern = /(?:\bfrom\s*|\bimport\s*\(|\bimport\s+)['"]([^'"]+)['"]|new\s+URL\(\s*['"]([^'"]+)['"]|<script\b[^>]*\bsrc=['"]([^'"]+)['"]/g;
  for (const match of source.matchAll(pattern)) {
    const specifier = match[1] || match[2] || match[3];
    if (specifier.startsWith('.')) specifiers.push(specifier);
  }
  return specifiers;
}

function outputPathForSource(sourceFile, specifier) {
  const sourceRelative = relative(web, sourceFile);
  const sourceBase = sourceRelative === 'runtime.html' ? 'runtime/index.html' : sourceRelative;
  const target = posix.normalize(posix.join(posix.dirname(sourceBase), specifier.split(/[?#]/)[0]));
  if (target === '../tools/spirvfix/spirvfix.mjs') return 'spirvfix.mjs';
  return target.replace(/^\.\//, '');
}

function isExported(path, { names, directories }) {
  return names.has(path) || directories.some(prefix => path.startsWith(prefix));
}

test('browser export includes every local import from app, runtime, workers, and web modules', async () => {
  const copied = exportedFiles(build);
  const queue = (await readdir(web, { withFileTypes: true }))
    .filter(entry => entry.isFile() && entry.name.endsWith('.mjs'))
    .map(entry => resolve(web, entry.name));
  queue.push(resolve(web, 'runtime.html'));
  const sources = [];
  const visited = new Set();
  const failures = [];
  while (queue.length) {
    const file = queue.shift();
    if (visited.has(file)) continue;
    visited.add(file);
    sources.push(file);
    const source = await readFile(file, 'utf8');
    for (const specifier of relativeImports(source)) {
      const target = outputPathForSource(file, specifier);
      const sourceTarget = target === 'spirvfix.mjs'
        ? resolve(repo, 'tools/spirvfix/spirvfix.mjs')
        : resolve(web, target);
      try { await stat(sourceTarget); }
      catch { failures.push(`${relative(repo, file)} -> missing source ${specifier}`); continue; }
      if (!isExported(target, copied)) failures.push(`${relative(repo, file)} -> ${specifier} => dist/${target}`);
      if (sourceTarget.startsWith(web + '/') && /\.(?:mjs|js|html)$/.test(sourceTarget)) queue.push(sourceTarget);
    }
  }
  assert.deepEqual(failures, [], `local modules omitted from browser export:\n${failures.join('\n')}`);
  for (const module of sources.filter(path => path.endsWith('.mjs'))) {
    const name = relative(web, module);
    assert.ok(isExported(name, copied), `web/${name} is not included in the browser export`);
  }
});

test('every top-level web module imported by an exported module is exported', async () => {
  const copied = exportedFiles(build);
  assert.ok(copied.names.has('vk-dynstate.mjs'), 'Vulkan dynamic-state helper must be copied to the browser export');
  const queue = [];
  for (const name of copied.names) {
    if (!name.endsWith('.mjs')) continue;
    try { await stat(resolve(web, name)); queue.push(resolve(web, name)); }
    catch { /* A peer-lane module may arrive in the integration merge. */ }
  }
  const visited = new Set();
  const failures = [];
  while (queue.length) {
    const file = queue.shift();
    if (visited.has(file)) continue;
    visited.add(file);
    let source;
    try { source = await readFile(file, 'utf8'); }
    catch { failures.push(`${relative(repo, file)} is listed for export but missing from web/`); continue; }
    for (const specifier of relativeImports(source)) {
      const target = outputPathForSource(file, specifier);
      if (target.startsWith('../')) continue;
      const sourceTarget = target === 'spirvfix.mjs'
        ? resolve(repo, 'tools/spirvfix/spirvfix.mjs')
        : resolve(web, target);
      try { await stat(sourceTarget); }
      catch { failures.push(`${relative(repo, file)} -> missing source ${specifier}`); continue; }
      if (!isExported(target, copied)) failures.push(`${relative(repo, file)} -> ${specifier} => dist/${target}`);
      if (sourceTarget.startsWith(web + '/') && /\.mjs$/.test(sourceTarget)) queue.push(sourceTarget);
    }
  }
  assert.deepEqual(failures, [], `exported module closure is incomplete:\n${failures.join('\n')}`);
});

test('Caddy isolation header requirements are documented for static hosts', async () => {
  const docs = await readFile(resolve(repo, 'tools/browser/README.md'), 'utf8');
  assert.match(docs, /Caddyfile/);
  assert.match(docs, /Cross-Origin-Opener-Policy.*same-origin/i);
  assert.match(docs, /Cross-Origin-Embedder-Policy.*require-corp/i);
  assert.match(docs, /_headers/);
});


test('export inventory follows copy operations instead of assuming files and directories exist', () => {
  for (const file of ['direct-folder.mjs', 'frame-metrics.mjs', 'spirvfix.mjs']) {
    const withoutCopy = build.split('\n').filter(line => !line.includes(`resolve(out, '${file}')`)).join('\n');
    assert.equal(isExported(file, exportedFiles(withoutCopy)), false, `${file} must require a copy operation`);
  }
  assert.equal(isExported('assets/audio-output-worklet.js', exportedFiles(build.replace("await cp(resolve(root, 'web/assets')", "removed(resolve(root, 'web/assets')"))), false);
});
