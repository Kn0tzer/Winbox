import { test } from 'node:test';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, dirname, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { check } from '../lib/assert.mjs';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const roots = [
  'source/emulation/cpu/wasm/jitWasmCodeGen.cpp',
  'source/emulation/cpu/jit64wasm_runtime.cpp',
  'source/vulkan',
  'source/opengl',
];

function sourceFiles(path) {
  const full = join(repoRoot, path);
  if (statSync(full).isFile()) return [full];
  const files = [];
  for (const entry of readdirSync(full, { withFileTypes: true })) {
    const child = join(full, entry.name);
    if (entry.isDirectory()) files.push(...sourceFiles(relative(repoRoot, child)));
    else if (/\.(?:c|cc|cpp|h|hpp)$/.test(entry.name)) files.push(child);
  }
  return files;
}

function emJsPointerIssues(file, source) {
  const issues = [];
  const header = /\bEM_JS\s*\(\s*[^,]+,\s*([^,]+),\s*\(([^)]*)\)\s*,\s*\{/g;
  for (const match of source.matchAll(header)) {
    const openBrace = match.index + match[0].lastIndexOf('{');
    let depth = 1;
    let end = openBrace + 1;
    let quote = null;
    let escaped = false;
    for (; end < source.length && depth; end++) {
      const ch = source[end];
      if (quote) {
        if (escaped) escaped = false;
        else if (ch === '\\') escaped = true;
        else if (ch === quote) quote = null;
      } else if (ch === '"' || ch === "'" || ch === '`') quote = ch;
      else if (ch === '{') depth++;
      else if (ch === '}') depth--;
    }
    const body = source.slice(openBrace + 1, end - 1);
    const leadingStatements = body.match(/^\s*(?:(?:[A-Za-z_]\w*\s*>>>=\s*0;|[A-Za-z_]\w*\s*=\s*[A-Za-z_]\w*\s*>>>\s*0;)\s*)*/)?.[0] ?? '';
    const params = match[2].split(',').map(param => param.trim()).filter(Boolean);
    const protectedParams = params.filter(param => {
      const name = param.match(/([A-Za-z_]\w*)\s*$/)?.[1];
      if (!name) return false;
      const type = param.slice(0, param.lastIndexOf(name)).trim();
      const pointerLike = param.includes('*') || /(?:ptr|pointer|address|base)$/i.test(name);
      const sizeLike = /^(?:int|unsigned|U32|S32|size_t)\b/.test(type) &&
        /(?:size|count|length|bytes|byteCount|len)$/i.test(name);
      return pointerLike || sizeLike;
    });
    for (const param of protectedParams) {
      const name = param.match(/([A-Za-z_]\w*)\s*$/)[1];
      const normalized = new RegExp(`\\b${name}\\s*(?:>>>=\\s*0|=\\s*${name}\\s*>>>\\s*0)`).test(leadingStatements);
      if (!normalized) {
        const line = source.slice(0, match.index).split('\n').length;
        issues.push(`${relative(repoRoot, file)}:${line} ${match[1].trim()} parameter ${name}`);
      }
    }
  }
  return issues;
}

test('EM_JS pointer and size parameters are normalized as unsigned before use', () => {
  const issues = roots.flatMap(root => sourceFiles(root))
    .flatMap(file => emJsPointerIssues(file, readFileSync(file, 'utf8')));
  check(issues.length === 0,
    `EM_JS parameters may become negative above 2 GiB:\n${issues.join('\n')}`);
});
