import { readFile, readdir } from 'node:fs/promises';
import { dirname, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../../../..');
const silentBranchFiles = [
  'source/vulkan/vk64bridge.cpp',
  'source/opengl/gl64bridge.cpp',
  'source/kernel/syscall64.cpp',
];
const logCall = /\b(?:klog(?:_fmt)?|vk64DropLog|bwLog(?:Rl)?|noteUnsupported|log(?:ger)?|warn(?:ing)?|printf|fprintf)\s*\(/i;

function maskComments(source) {
  return source.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*/g, match => match.replace(/[^\n]/g, ' '));
}

function maskCommentsAndStrings(source) {
  return maskComments(source).replace(/("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')/g,
    match => match.replace(/[^\n]/g, ' '));
}

function matching(source, opening, openChar, closeChar) {
  let depth = 0;
  for (let i = opening; i < source.length; i++) {
    if (source[i] === openChar) depth++;
    else if (source[i] === closeChar && --depth === 0) return i;
  }
  return -1;
}

function unloggedDefaultBranches(source) {
  const code = maskCommentsAndStrings(source);
  let count = 0;
  for (const sw of code.matchAll(/\bswitch\s*\(/g)) {
    const paren = code.indexOf('(', sw.index);
    const closeParen = matching(code, paren, '(', ')');
    const openBrace = code.indexOf('{', closeParen + 1);
    if (closeParen < 0 || openBrace < 0) continue;
    const closeBrace = matching(code, openBrace, '{', '}');
    if (closeBrace < 0) continue;
    const body = code.slice(openBrace + 1, closeBrace);
    const labels = [];
    let depth = 0;
    for (let i = 0; i < body.length; i++) {
      if (body[i] === '{') depth++;
      else if (body[i] === '}') depth--;
      else if (depth === 0 && (i === 0 || !/[\w$]/.test(body[i - 1]))) {
        const match = /(?:default\s*:|case\b[^:;{}]*:)/.exec(body.slice(i));
        if (match?.index === 0) labels.push({ at: i, isDefault: match[0].startsWith('default') , length: match[0].length });
      }
    }
    for (let i = 0; i < labels.length; i++) {
      const label = labels[i];
      if (!label.isDefault) continue;
      const branchEnd = labels[i + 1]?.at ?? body.length;
      const branch = source.slice(openBrace + 1 + label.at + label.length, openBrace + 1 + branchEnd);
      if (/\b(?:return|break)\b/.test(maskCommentsAndStrings(branch)) && !logCall.test(branch)) count++;
    }
  }
  return count;
}

function unloggedErrnoReturns(source) {
  let count = 0;
  const pattern = /\breturn\s+(?:\([^)]+\)\s*){0,2}-\s*K?_?E(?:NOSYS|INVAL)\b[^;]*;/g;
  for (const match of source.matchAll(pattern)) {
    const line = source.slice(0, match.index).split('\n').length - 1;
    const prior = source.split('\n').slice(Math.max(0, line - 4), line + 1).join('\n');
    if (!logCall.test(prior)) count++;
  }
  return count;
}

async function sourceFiles(directory, extensions, current = directory) {
  const files = [];
  for (const entry of await readdir(current, { withFileTypes: true })) {
    const path = resolve(current, entry.name);
    if (entry.isDirectory()) files.push(...await sourceFiles(directory, extensions, path));
    else if (entry.isFile() && extensions.has(path.slice(path.lastIndexOf('.')))) files.push(path);
  }
  return files;
}

export async function scanSilentFailures() {
  const sourceDir = resolve(repo, 'source');
  const webDir = resolve(repo, 'web');
  const cppFiles = await sourceFiles(sourceDir, new Set(['.cpp', '.cc', '.c', '.h', '.hpp']));
  const webFiles = (await readdir(webDir, { withFileTypes: true }))
    .filter(entry => entry.isFile() && entry.name.endsWith('.mjs'))
    .map(entry => resolve(webDir, entry.name));
  const result = {};
  for (const file of [...cppFiles, ...webFiles]) {
    const text = await readFile(file, 'utf8');
    const noComments = maskComments(text);
    const emptyCatch = [...noComments.matchAll(/\bcatch\s*(?:\([^)]*\))?\s*\{\s*\}/g)].length;
    const unloggedDefault = silentBranchFiles.includes(relative(repo, file)) ? unloggedDefaultBranches(text) : 0;
    const unloggedErrno = relative(repo, file) === 'source/kernel/syscall64.cpp' ? unloggedErrnoReturns(text) : 0;
    if (emptyCatch || unloggedDefault || unloggedErrno)
      result[relative(repo, file)] = { emptyCatch, unloggedDefault, unloggedErrno };
  }
  return result;
}
