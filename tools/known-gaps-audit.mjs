#!/usr/bin/env node
// tools/known-gaps-audit.mjs — finds markers of known failures, unimplemented
// support, and incompatibilities, and groups them by the lane that owns the file.
//
// Usage:
//   node tools/known-gaps-audit.mjs                # table of hits by owning area
//   node tools/known-gaps-audit.mjs --json         # summary and every hit as JSON
//   node tools/known-gaps-audit.mjs --write-hits   # regenerate the UNOWNED hit lists in docs/known-gaps/coverage.md
//   node tools/known-gaps-audit.mjs --check        # exit 1 if an UNOWNED file is neither registered nor allowlisted
//   node tools/known-gaps-audit.mjs --root=<dir>   # audit another checkout or fixture tree
//
// An UNOWNED file must appear in the register block of docs/known-gaps/coverage.md
// with a hit count that may not grow, or in docs/known-gaps/allowlist.json with a reason.
import { existsSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

// Case variants matter: "Not implemented" and "Not supported" are how most of the tree writes it.
// ENOSYS also matches K_ENOSYS, so a silent `return -K_ENOSYS;` with no log is still a hit.
export const PATTERN = /[Uu]nsupported|[Nn]ot [Ii]mplemented|[Uu]nimplemented|[Nn]ot [Ss]upported|TODO|FIXME|XXX|ENOSYS|kpanic\(|stub/;

// A line is a gap when PATTERN matches it, except for the loud forms that do not hide a gap:
// a `throw` or `#error` fails at the call site, and an ENOSYS return that logs is not silent.
// TODO, FIXME, XXX and kpanic( always count.
const ALWAYS_GAP = /TODO|FIXME|XXX|kpanic\(/;
const LOUD_THROW = /\bthrow\b|#error\b/;
const LOG_CALL = /\bklog(_fmt)?\(|\bkdebug\(|\bkwarn\(/;
export function isGapLine(text) {
	if (!PATTERN.test(text)) return false;
	if (ALWAYS_GAP.test(text)) return true;
	if (LOUD_THROW.test(text)) return false;
	if (/ENOSYS/.test(text) && LOG_CALL.test(text)) return false;
	return true;
}

// Top-level directories scanned. `docs`, `.github`, `test-results` and `node_modules` are not code.
export const SCAN_ROOTS = ['source', 'web', 'project', 'tools', 'platform', 'include', 'scripts', 'lib', 'tasks', 'testdata'];
const CODE_EXT = /\.(c|cc|cpp|cxx|h|hh|hpp|m|mm|mjs|js|cjs|ts|py|sh|cs|html)$/i;

// Third-party and generated trees. They are pruned before the walk, not scanned.
export const EXCLUDED_PREFIXES = [
  'lib/',                           // vendored libraries: imgui, asmjit, glew, zlib, simde, tiny-process
  'source/vulkan/vk/',              // Khronos Vulkan headers
  'tools/dxvk/',                    // DXVK sources
  'tools/vkd3d/src/',               // vkd3d-proton, SPIRV-Headers, SPIRV-Cross, SPIRV-Tools sources
  'tools/browser/zig/',             // Zig toolchain
  'tools/browser/disasm/capstone/', // Capstone sources
  'web/vendor/',                    // symlinked vendored modules
  'web/runtime/',                   // gitignored build mirror
];
const EXCLUDED_NAMES = new Set(['node_modules', '__pycache__', 'CMakeFiles', '.git', 'build', 'dist', 'out', 'obj']);
// Single files: the vendored stb header, and this audit's own pattern and test.
export const EXCLUDED_FILES = new Set([
  'source/util/stb_image.h',
  'tools/known-gaps-audit.mjs',
  'web/tests/suite/unit/known-gaps-audit.test.mjs',
]);

// Ownership. The first matching prefix wins; a file matching nothing is UNOWNED.
// Boot/prefix entries come before the broad `tools/` catch-all for tests/tools.
export const OWNERS = [
  { area: 'kernel', prefixes: [
    'source/kernel/', 'source/emulation/cpu/',
    'include/kprocess.h', 'include/kthread.h', 'include/syscall64.h', 'include/kerror.h',
    'include/cpu64.h', 'include/jit64.h', 'include/jit64wasm.h',
  ] },
  { area: 'gl', prefixes: ['source/opengl/', 'web/gl-', 'tools/opengl/', 'tools/glcov/'] },
  { area: 'vulkan', prefixes: [
    'source/vulkan/', 'web/vk', 'tools/vulkan/', 'tools/vkcov/', 'tools/vkd3d/',
    'tools/rootfs32/libvk', 'tools/rootfs64/libvk', 'tools/rootfs64/build_vulkan_shims.py', 'tools/rootfs64/buildvk.sh',
  ] },
  { area: 'boot/prefix', prefixes: [
    'project/emscripten/', 'tools/browser/', 'tools/buildWine/', 'tools/rootfs32/', 'tools/rootfs64/',
    'tools/debianFileSystem/', 'tools/build-prefix32.sh', 'tools/bw64-build.sh',
  ] },
  { area: 'tests/tools', prefixes: ['web/tests/', 'source/test/', 'testdata/', 'tools/'] },
];

export const UNOWNED = 'UNOWNED';
const REG_BEGIN = '<!-- known-gaps:register:begin -->';
const REG_END = '<!-- known-gaps:register:end -->';
const HITS_BEGIN = '<!-- known-gaps:hits:begin -->';
const HITS_END = '<!-- known-gaps:hits:end -->';
const SEVERITIES = new Set(['high', 'medium', 'low']);

export function ownerOf(rel) {
  for (const o of OWNERS) if (o.prefixes.some(p => rel.startsWith(p))) return o.area;
  return UNOWNED;
}

function termOf(m) {
  return m[0].startsWith('klog_fmt(') ? `klog_fmt not ${m[1]}` : m[0];
}

// Walks one top-level directory. Symlinks are not followed, and each is recorded so the
// report shows what was not read. Read errors other than a missing root are rethrown.
function walk(root, rel, out) {
  let entries;
  try {
    entries = readdirSync(join(root, rel), { withFileTypes: true });
  } catch (e) {
    if (e.code === 'ENOENT') return out;
    throw e;
  }
  entries.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
  for (const ent of entries) {
    const childRel = `${rel}/${ent.name}`;
    if (ent.isSymbolicLink()) { out.symlinks.push(childRel); continue; }
    if (ent.isDirectory()) {
      if (EXCLUDED_NAMES.has(ent.name)) continue;
      if (EXCLUDED_PREFIXES.some(p => `${childRel}/`.startsWith(p))) continue;
      walk(root, childRel, out);
    } else if (ent.isFile() && CODE_EXT.test(ent.name) && !EXCLUDED_FILES.has(childRel)) {
      if (EXCLUDED_PREFIXES.some(p => childRel.startsWith(p))) continue;
      out.files++;
      const lines = readFileSync(join(root, childRel), 'utf8').split('\n');
      lines.forEach((text, i) => {
        if (!isGapLine(text)) return;
        out.hits.push({ file: childRel, line: i + 1, term: termOf(PATTERN.exec(text)), text: text.trim().slice(0, 160) });
      });
    }
  }
  return out;
}

export function scan(root) {
  const out = { hits: [], files: 0, symlinks: [] };
  for (const top of SCAN_ROOTS) walk(root, top, out);
  for (const h of out.hits) h.area = ownerOf(h.file);
  return out;
}

export function summarize(hits) {
  const areas = new Map();
  for (const area of [...OWNERS.map(o => o.area), UNOWNED]) {
    areas.set(area, { area, owned: area !== UNOWNED, hits: 0, files: new Map() });
  }
  const terms = {};
  for (const h of hits) {
    const a = areas.get(h.area);
    a.hits++;
    a.files.set(h.file, (a.files.get(h.file) || 0) + 1);
    terms[h.term] = (terms[h.term] || 0) + 1;
  }
  return {
    total: hits.length,
    areas: [...areas.values()].map(a => ({
      area: a.area,
      owned: a.owned,
      hits: a.hits,
      files: a.files.size,
      topFiles: [...a.files].sort((x, y) => y[1] - x[1] || (x[0] < y[0] ? -1 : 1)).slice(0, 3)
        .map(([file, hits]) => ({ file, hits })),
    })),
    terms,
  };
}

function groupByFile(hits) {
  const byFile = new Map();
  for (const h of hits) {
    if (!byFile.has(h.file)) byFile.set(h.file, []);
    byFile.get(h.file).push(h);
  }
  return byFile;
}

// Reads the machine-readable register block. A malformed block is an error, not an empty register.
export function parseRegister(md) {
  const b = md.indexOf(REG_BEGIN);
  const e = md.indexOf(REG_END);
  if (b < 0 || e < b) throw new Error(`coverage.md has no register block (${REG_BEGIN} ... ${REG_END})`);
  const fence = md.slice(b + REG_BEGIN.length, e).match(/```json\n([\s\S]*?)\n```/);
  if (!fence) throw new Error('register block must hold one ```json fence');
  const reg = JSON.parse(fence[1]);
  for (const [file, entry] of Object.entries(reg)) {
    if (!Number.isInteger(entry.count) || entry.count < 0) throw new Error(`register ${file}: count must be a non-negative integer`);
    if (!SEVERITIES.has(entry.severity)) throw new Error(`register ${file}: severity must be high, medium or low`);
  }
  return reg;
}

export function parseAllowlist(json) {
  const { entries = [] } = JSON.parse(json);
  const map = {};
  for (const e of entries) {
    if (!e.file) throw new Error('allowlist entry has no file');
    if (typeof e.reason !== 'string' || !e.reason.trim()) throw new Error(`allowlist entry ${e.file} needs a non-empty reason`);
    if (!Number.isInteger(e.count) || e.count < 0) throw new Error(`allowlist entry ${e.file}: count must be a non-negative integer`);
    if (map[e.file]) throw new Error(`duplicate allowlist entry ${e.file}`);
    map[e.file] = e;
  }
  return map;
}

// Pure check: returns violations (fail CI) and notes (stale register or allowlist entries).
export function checkUnowned(hits, register, allowlist) {
  const violations = [];
  const notes = [];
  const byFile = groupByFile(hits.filter(h => h.area === UNOWNED));
  for (const [file, list] of byFile) {
    const n = list.length;
    const allow = allowlist[file];
    const reg = register[file];
    if (allow) {
      if (n > allow.count) violations.push(`${file}: ${n} unowned hits exceed the allowlisted ${allow.count} (first: line ${list[0].line})`);
      else if (n < allow.count) notes.push(`${file}: allowlist says ${allow.count}, now ${n} — lower the count`);
    } else if (reg) {
      if (n > reg.count) violations.push(`${file}: ${n} unowned hits exceed the registered ${reg.count} — new silent gap (first: line ${list[0].line})`);
      else if (n < reg.count) notes.push(`${file}: register says ${reg.count}, now ${n} — lower the count`);
    } else {
      violations.push(`${file}: ${n} unowned hit(s) not in the register or allowlist (first: line ${list[0].line}: ${list[0].text})`);
    }
  }
  for (const file of Object.keys(register)) if (!byFile.has(file)) notes.push(`${file}: register entry has no unowned hits — remove it`);
  for (const file of Object.keys(allowlist)) if (!byFile.has(file)) notes.push(`${file}: allowlist entry has no unowned hits — remove it`);
  return { violations, notes };
}

export function renderHits(hits) {
  const out = [HITS_BEGIN, '', '_Generated by `node tools/known-gaps-audit.mjs --write-hits`; do not edit by hand._', ''];
  const byFile = groupByFile(hits.filter(h => h.area === UNOWNED));
  for (const file of [...byFile.keys()].sort()) {
    const list = byFile.get(file);
    out.push(`### \`${file}\` (${list.length})`, '', '| Line | Match | Text |', '| --- | --- | --- |');
    for (const h of list) {
      const text = h.text.replace(/\|/g, '\\|').replace(/`/g, "'");
      out.push(`| ${h.line} | \`${h.term}\` | ${text} |`);
    }
    out.push('');
  }
  out.push(HITS_END);
  return out.join('\n');
}

export function writeHitsSection(md, section) {
  const b = md.indexOf(HITS_BEGIN);
  const e = md.indexOf(HITS_END);
  if (b < 0 || e < b) return `${md.replace(/\n*$/, '\n')}\n${section}\n`;
  return md.slice(0, b) + section + md.slice(e + HITS_END.length);
}

function table(headers, rows) {
  const widths = headers.map((h, i) => Math.max(h.length, ...rows.map(r => String(r[i]).length)));
  const fmt = r => r.map((c, i) => String(c).padEnd(widths[i])).join('  ').trimEnd();
  return [fmt(headers), fmt(widths.map(w => '-'.repeat(w))), ...rows.map(fmt)].join('\n');
}

function readOptional(path, fallback) {
  return existsSync(path) ? readFileSync(path, 'utf8') : fallback;
}

function formatTable(summary, hits, register, allowlist) {
  const lines = [];
  lines.push(table(['AREA', 'OWNED', 'HITS', 'FILES', 'TOP FILES'], summary.areas.map(a => [
    a.area, a.owned ? 'yes' : 'no', a.hits, a.files,
    a.topFiles.map(f => `${f.file} ${f.hits}`).join(', '),
  ])));
  const unowned = groupByFile(hits.filter(h => h.area === UNOWNED));
  if (unowned.size) {
    lines.push('', 'UNOWNED files:');
    lines.push(table(['FILE', 'HITS', 'REGISTER'], [...unowned].sort((a, b) => b[1].length - a[1].length || (a[0] < b[0] ? -1 : 1)).map(([file, list]) => {
      const status = allowlist[file] ? 'allowlisted' : register[file] ? `registered ${register[file].count}` : 'MISSING';
      return [file, list.length, status];
    })));
  }
  return lines.join('\n');
}

function main(argv) {
  const rootArg = argv.find(a => a.startsWith('--root='));
  const root = rootArg ? resolve(rootArg.slice('--root='.length)) : resolve(dirname(fileURLToPath(import.meta.url)), '..');
  const covPath = join(root, 'docs/known-gaps/coverage.md');
  const allowPath = join(root, 'docs/known-gaps/allowlist.json');
  const { hits, files, symlinks } = scan(root);
  const summary = summarize(hits);

  if (argv.includes('--write-hits')) {
    const md = readFileSync(covPath, 'utf8');
    writeFileSync(covPath, writeHitsSection(md, renderHits(hits)));
    console.log(`wrote UNOWNED hit lists to ${covPath}`);
    return 0;
  }

  if (argv.includes('--json')) {
    console.log(JSON.stringify({ pattern: PATTERN.source, roots: SCAN_ROOTS, excludedPrefixes: EXCLUDED_PREFIXES,
      filesScanned: files, skippedSymlinks: symlinks, summary, hits }, null, 2));
    return 0;
  }

  const checking = argv.includes('--check');
  const register = readRegister(covPath, checking);
  const allowlist = parseAllowlist(readOptional(allowPath, '{"entries":[]}'));

  if (checking) {
    const { violations, notes } = checkUnowned(hits, register, allowlist);
    const unownedFiles = new Set(hits.filter(h => h.area === UNOWNED).map(h => h.file)).size;
    console.log(`known-gaps: ${files} files, ${hits.length} hits, ${unownedFiles} UNOWNED files (${symlinks.length} symlinks not followed)`);
    for (const n of notes) console.log(`note: ${n}`);
    if (violations.length) {
      for (const v of violations) console.error(`FAIL ${v}`);
      console.error(`known-gaps: ${violations.length} unowned file(s) need a register entry or an allowlist reason`);
      return 1;
    }
    console.log('known-gaps: every UNOWNED hit is registered or allowlisted');
    return 0;
  }

  console.log(`known-gaps: ${files} files scanned, ${hits.length} hits (${symlinks.length} symlinks not followed)`);
  console.log(formatTable(summary, hits, register, allowlist));
  return 0;
}

// --check requires a valid register; the table view still shows hits without one.
function readRegister(covPath, required) {
  if (!existsSync(covPath)) {
    if (required) throw new Error(`missing ${covPath}`);
    return {};
  }
  try {
    return parseRegister(readFileSync(covPath, 'utf8'));
  } catch (e) {
    if (required) throw e;
    return {};
  }
}

if (process.argv[1] && fileURLToPath(import.meta.url) === resolve(process.argv[1])) {
  process.exitCode = main(process.argv.slice(2));
}
