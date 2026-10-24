import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile, readdir, stat } from 'node:fs/promises';
import { dirname, posix, relative, resolve } from 'node:path';

const repo = resolve(import.meta.dirname, '../../../../');
const web = resolve(repo, 'web');
const build = await readFile(resolve(repo, 'tools/browser/build.mjs'), 'utf8');

function exportedFiles(source) {
  const names = new Set(['direct-folder.mjs', 'frame-metrics.mjs']);
  const list = /for\s*\(const name of \[([\s\S]*?)\]\) await copyFile\(resolve\(root, 'web', name\)/.exec(source)?.[1];
  assert.ok(list, 'build.mjs web export file list marker not found');
  for (const [, name] of list.matchAll(/'([^']+)'/g)) names.add(name);
  names.add('spirvfix.mjs'); // Copied from tools/spirvfix/spirvfix.mjs.
  return names;
}

function relativeImports(source) {
  const specifiers = [];
  const pattern = /(?:\bfrom\s*|\bimport\s*\(|\bimport\s+)['"]([^'"]+)['"]|new\s+URL\(\s*['"]([^'"]+)['"]/g;
  for (const match of source.matchAll(pattern)) {
    const specifier = match[1] || match[2];
    if (specifier.startsWith('.')) specifiers.push(specifier);
  }
  return specifiers;
}

function outputPathForSource(sourceFile, specifier) {
  const sourceRelative = relative(web, sourceFile);
  const sourceBase = sourceRelative === 'runtime.html' ? 'runtime/index.html' : sourceRelative;
  const target = posix.normalize(posix.join(posix.dirname(sourceBase), specifier));
  if (target === '../tools/spirvfix/spirvfix.mjs') return 'spirvfix.mjs';
  return target.replace(/^\.\//, '');
}

function isExported(path, names) {
  if (names.has(path)) return true;
  return ['assets/', 'vendor/', 'runtime/'].some(prefix => path.startsWith(prefix));
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
      if (sourceTarget.startsWith(web + '/') && /\.(?:mjs|js|html)$/.test(sourceTarget)
        && !target.startsWith('vendor/')) queue.push(sourceTarget);
    }
  }
  assert.deepEqual(failures, [], `local modules omitted from browser export:\n${failures.join('\n')}`);
  for (const module of sources.filter(path => path.endsWith('.mjs'))) {
    const name = relative(web, module);
    assert.ok(isExported(name, copied), `web/${name} is not included in the browser export`);
  }
});

test('Caddy isolation header requirements are documented for static hosts', async () => {
  const docs = await readFile(resolve(repo, 'tools/browser/README.md'), 'utf8');
  assert.match(docs, /Caddyfile/);
  assert.match(docs, /Cross-Origin-Opener-Policy.*same-origin/i);
  assert.match(docs, /Cross-Origin-Embedder-Policy.*require-corp/i);
  assert.match(docs, /_headers/);
});
