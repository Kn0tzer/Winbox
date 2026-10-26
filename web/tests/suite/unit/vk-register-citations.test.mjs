// Vulkan gap-register citation guard (added 2026-10-25, citation re-pin lane).
//
// docs/known-gaps/vulkan.md cites file:line for every audited behavior.
// Those citations rot silently as the cited files grow: audit wave 2 found
// ~30% of sampled citations stale (systematic +46..+98 drift, a +54 cluster,
// one wrong file). This test re-checks every mechanically checkable citation
// so the rot fails the suite instead of accumulating.
//
// What it checks (same rules as tools/vk-register-citation-audit.py):
//   marker/snippet rows - the quoted marker or code snippet in the row must
//     appear verbatim in the cited file within 6 lines of a cited line
//     (a marker truncated with "..." is checked by its prefix);
//   log-token rows - a hyphenated token the row presents as a log/feature
//     string must appear in the cited file within 6 lines of a cited line;
//   function rows - a backticked identifier defined in the cited file must
//     be called/defined within 6 lines of a cited line, or the citation
//     must lie inside the function's definition span;
//   structural rows - "(default" / "catch" rows: the literal must exist in
//     the cited neighbourhood (25 lines);
//   duplicate scanner rows citing a checked primary row's exact site are
//     checked by inheritance.
// What it cannot check: rows whose only construct is a descriptive label,
// a counter/field name, or prose (reported as UNCHECKABLE). Coverage is
// pinned below: if checked coverage shrinks or the uncheckable count
// grows, this test fails, so weakening a citation into uncheckability is
// itself a suite failure. A small EXEMPT list covers rows whose named
// construct legitimately lives in a different file than the citation
// (bridge log token vs page-tier guard) or is a format/feature name;
// each exemption names its reason and any change to the list is a
// visible edit to this file. Rows flagged NEEDS RE-DISPOSITION by the
// re-pin are OPEN in the register and excluded here; the register's own
// OPEN count tracks them.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';

const root = new URL('../../../../', import.meta.url);
const read = (rel) => readFileSync(new URL(rel, root), 'utf8');
const TOLERANCE = 6;

const EXEMPT = [
  ['stencil state guard', 'page guard cited; log tokens live in vk64bridge.cpp audit'],
  ['blend operation guard', 'page guard cited; log token lives in vk64bridge.cpp audit'],
  ['template descriptor default', 'vulkancommon.cpp default cited; descriptor-type token is the cross-referenced bridge rejection'],
  ['clear rectangle layers', 'page clear guard cited; layered-framebuffer token is emitted at bridge framebuffer creation, as the disposition states'],
  ['stats field', 'diagnostic snapshot field cited; noteDrop in the disposition is a cross-reference'],
  ['opaque depth upload', 'backticked construct is a WebGPU format name, not the row site; upload block verified manually'],
];

function splitCells(line) {
  const parts = []; let cur = '';
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (c === '\\' && line[i + 1] === '|') { cur += '|'; i++; continue; }
    if (c === '|') { parts.push(cur); cur = ''; continue; }
    cur += c;
  }
  parts.push(cur);
  return parts.map(s => s.trim());
}

const CITE_RE = /([\w./-]+\.(?:cpp|h|mjs|c|py|sh|json|md|txt|yml)):([\d][\d,\-]*)/g;
function parseCitations(cell) {
  const out = [];
  for (const m of cell.matchAll(CITE_RE)) {
    const f = m[1];
    for (const part of m[2].replace(/,$/, '').split(',')) {
      const p = part.trim(); if (!p) continue;
      if (p.includes('-')) {
        const [a, b] = p.split('-', 2);
        if (/^\d+$/.test(a) && /^\d+$/.test(b)) out.push([f, +a, +b]);
      } else if (/^\d+$/.test(p)) out.push([f, +p, +p]);
    }
  }
  return out;
}

const fileCache = new Map();
function fileLines(f) {
  if (!fileCache.has(f)) {
    const p = new URL(f, root);
    fileCache.set(f, existsSync(p) ? read(f).split('\n') : null);
  }
  return fileCache.get(f);
}

const TOKEN_HYPH = /^[a-z][a-z0-9]*(?:-[a-z0-9]+)+$/;
const LOG_CTX = /(logs?|noteUnsupported|noteDrop|noteSkippedDraw|vk64Unsupported|warns|warning|diagnostic|rejects)/;

function extractConstruct(cells) {
  const evidence = cells[2] || '';
  const disp = cells[4] || '';
  const spans = [];
  const m = evidence.match(/marker:\s*`([^`]+)`/);
  if (m) spans.push(m[1]);
  for (const s of evidence.matchAll(/`([^`]{25,})`/g)) {
    if ((s[1].includes('"') || s[1].includes("'") || s[1].includes('{')) && !spans.includes(s[1])) spans.push(s[1]);
  }
  for (const s of spans) {
    let t = s.trim().replace(/^\/\/\s?/, '');
    if (t.includes('...')) t = t.split('...')[0].trimEnd();
    if (t.length >= 12) return ['marker', t];
  }
  for (const src of [evidence, disp]) {
    if (!LOG_CTX.test(src)) continue;
    for (const tok of src.matchAll(/`([^`]+)`/g)) {
      if (TOKEN_HYPH.test(tok[1])) return ['token', tok[1]];
    }
  }
  const label = cells[1] || '';
  const lit = label.includes('(default') ? 'default:' : (label.includes('catch') ? 'catch' : null);
  if (lit) {
    const cites0 = parseCitations(label);
    if (cites0.length) {
      const flines = fileLines(cites0[0][0]);
      if (flines) {
        const hits = [];
        flines.forEach((l, i) => { if (l.includes(lit)) hits.push(i + 1); });
        if (hits.some(h => cites0.some(([f, s, e]) => f === cites0[0][0] && s - 25 <= h && h <= e + 25))) return ['literal', lit];
      }
    }
  }
  const cites = parseCitations(cells[1]);
  if (cites.length) {
    const lines = fileLines(cites[0][0]);
    if (lines) {
      for (const src of [evidence, disp]) {
        for (const tok of src.matchAll(/`([A-Za-z_][A-Za-z0-9_]*)`/g)) {
          const name = tok[1];
          if (name.length < 4) continue;
          const defRe = new RegExp(`(?:^|[\\s*&])${name}\\s*\\(`);
          if (lines.some(l => defRe.test(l) && (l.includes('{') || l.includes('function')))) return ['func', name];
        }
      }
    }
  }
  return [null, null];
}

function auditRegister() {
  const regLines = read('docs/known-gaps/vulkan.md').split('\n');
  const rows = [];
  let inAppendix = false;
  regLines.forEach((line, idx) => {
    if (line.startsWith('## Spot-check appendix')) inAppendix = true;
    if (inAppendix || !line.trimStart().startsWith('|')) return;
    const cells = splitCells(line);
    if (cells.length < 5 || cells[1].startsWith('---') || cells[1].includes('File:line')) return;
    const cites = parseCitations(cells[1]);
    if (!cites.length) return;
    const row = { line: idx + 1, cites, label: cells[1] };
    rows.push(row);
    if (line.includes('NEEDS RE-DISPOSITION (citation re-pin')) { row.verdict = 'FLAGGED'; return; }
    if (EXEMPT.some(([s]) => row.label.includes(s))) { row.verdict = 'EXEMPT'; return; }
    const [kind, construct] = extractConstruct(cells);
    if (!kind) { row.verdict = 'UNCHECKABLE'; return; }
    row.kind = kind; row.construct = construct;
    const f0 = cites[0][0];
    const lines = fileLines(f0);
    if (!lines) { row.verdict = 'MISSING'; row.detail = 'cited file not found'; return; }
    let hits;
    if (kind === 'func') {
      const re = new RegExp(`\\b${construct}\\s*\\(`);
      hits = []; lines.forEach((l, i) => { if (re.test(l)) hits.push(i + 1); });
    } else {
      hits = []; lines.forEach((l, i) => { if (l.includes(construct)) hits.push(i + 1); });
    }
    if (!hits.length) { row.verdict = 'MISSING'; row.detail = 'construct not in cited file'; return; }
    const tol = kind === 'literal' ? 25 : TOLERANCE;
    let near = hits.some(h => cites.some(([f, s, e]) => f === f0 && s - tol <= h && h <= e + tol));
    if (!near && kind === 'func') {
      const defRe = new RegExp(`(?:^|[\\s*&])${construct}\\s*\\(`);
      for (let d = 0; d < lines.length; d++) {
        if (!defRe.test(lines[d]) || !(lines[d].includes('{') || lines[d].includes('function'))) continue;
        let depth = 0, end = -1;
        for (let j = d; j < lines.length; j++) {
          depth += (lines[j].match(/\{/g) || []).length - (lines[j].match(/\}/g) || []).length;
          if (j > d && depth <= 0) { end = j + 1; break; }
        }
        if (end > 0 && cites.some(([f, s, e]) => f === f0 && d + 1 <= s && e <= end)) { near = true; break; }
      }
    }
    row.verdict = near ? 'MATCH' : 'DRIFT';
    if (!near) {
      const [s, e] = [cites[0][1], cites[0][2]];
      row.nearest = hits.reduce((a, b) => (Math.min(Math.abs(b - s), Math.abs(b - e)) < Math.min(Math.abs(a - s), Math.abs(a - e)) ? b : a), hits[0]);
    }
  });
  const matchedSites = new Set();
  for (const r of rows) if (r.verdict === 'MATCH') for (const c of r.cites) matchedSites.add(c.join(':'));
  for (const r of rows) {
    if (r.verdict === 'UNCHECKABLE' && r.cites.some(c => matchedSites.has(c.join(':')))) r.verdict = 'MATCH-INHERITED';
  }
  return rows;
}

const rows = auditRegister();
const by = (v) => rows.filter(r => r.verdict === v);

test('vulkan register: no mechanically checkable citation has drifted or gone missing', () => {
  const bad = [...by('DRIFT'), ...by('MISSING')];
  assert.equal(bad.length, 0,
    bad.map(r => `reg:${r.line} ${r.verdict} cites=${JSON.stringify(r.cites)} construct=${JSON.stringify(r.construct)} nearest=${r.nearest || ''} ${r.detail || ''}`).join('\n'));
});

test('vulkan register: citation-check coverage does not shrink', () => {
  const checked = by('MATCH').length + by('MATCH-INHERITED').length;
  assert.ok(checked >= 203, `checked coverage fell to ${checked} (pinned floor 203; re-pin baseline 203)`);
  assert.ok(by('UNCHECKABLE').length <= 102,
    `uncheckable rows grew to ${by('UNCHECKABLE').length} (pinned ceiling 102) — a citation was weakened into uncheckability or a new uncheckable row was added`);
});

test('vulkan register: exemptions stay exactly the pinned adjudicated set', () => {
  // 2026-10-26 (portal-d24 lane): the D24S8/D32S8 transfer-destination row
  // retired its 'opaque depth upload' exemption -- its disposition is now
  // IMPLEMENTED with a verbatim marker citation that is checked directly,
  // so the exempt set shrinks 8 -> 7 (the label still covers the upload row).
  assert.equal(by('EXEMPT').length, 7, `exempt rows: ${by('EXEMPT').map(r => r.line).join(', ')}`);
  for (const [label] of EXEMPT) {
    assert.ok(rows.some(r => r.label.includes(label)), `pinned exemption label not found in register: ${label}`);
  }
});

test('vulkan register: re-pin flagged rows stay visible and few', () => {
  const flagged = by('FLAGGED');
  assert.ok(flagged.length <= 3, `flagged rows grew to ${flagged.length}`);
  for (const r of flagged) {
    const line = read('docs/known-gaps/vulkan.md').split('\n')[r.line - 1];
    assert.ok(line.includes('OPEN'), `flagged row reg:${r.line} no longer reads OPEN`);
  }
});
