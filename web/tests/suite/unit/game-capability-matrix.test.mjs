// Inventory and evidence validation; source guards do not establish playability.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { gamePrerequisites, prerequisiteEvidence, prerequisiteSelection } from '../lib/game-prerequisites.mjs';
const root = resolve(import.meta.dirname, '../../../..');
const suite = resolve(import.meta.dirname, '..');

test('actual Celeste executable is i386', t => {
  const zip = process.env.CELESTE_ZIP || '/home/ubuntu/kn0tzer/games/celeste-drmfree.zip';
  if (!existsSync(zip)) { t.skip('Celeste archive unavailable; bitness unverified'); return; }
  const machine = execFileSync('python3', ['-c', `import zipfile,struct,sys
z=zipfile.ZipFile(sys.argv[1]); names=[n for n in z.namelist() if n.rsplit('/',1)[-1]=='Celeste.exe']
assert len(names)==1
b=z.read(names[0]); pe=struct.unpack_from('<I',b,0x3c)[0]
assert b[:2]==b'MZ' and b[pe:pe+4]==bytes([80,69,0,0])
print(hex(struct.unpack_from('<H',b,pe+4)[0]))`, zip], { encoding: 'utf8' }).trim();
  assert.equal(machine, '0x14c');
});

test('every prerequisite names an existing probe, including Gang Beasts', () => {
  assert.deepEqual(Object.keys(gamePrerequisites).sort(), ['celeste','gangbeasts','portal']);
  for (const [game, profile] of Object.entries(gamePrerequisites)) {
    assert.ok(['x86','x64'].includes(profile.architecture));
    assert.ok(profile.uncoveredBehavior.length > 0, game + ': retain explicit acceptance gaps');
    for (const category of ['unit','browser']) {
      const files = new Set(readdirSync(join(suite, category)).filter(f => f.endsWith('.test.mjs')));
      for (const name of prerequisiteSelection(game, category)) assert.ok(files.has(name+'.test.mjs'), game+'/'+category+'/'+name);
    }
  }
});
test('Celeste required subsystems map to executable guest probes', () => {
  const probes = prerequisiteSelection('celeste', 'browser');
  for (const required of ['boot32-vkboot', 'gl32-triangle', 'mono-runtime', 'win32-clock', 'input-fullscreen', 'input-keyboard'])
    assert.ok(probes.has(required), `Celeste prerequisite missing: ${required}`);
});

function passing(game, hash='runtime-a') {
  return ['unit','browser'].flatMap(category => gamePrerequisites[game][category]
    .map(name => ({name,category,passed:true,wasmSha256:category==='browser'?hash:null,runtimeCodeHash:category==='browser'?'code-a':null})));
}
test('complete prerequisite evidence never claims game playability', () => {
  for (const game of Object.keys(gamePrerequisites)) {
    const evidence = prerequisiteEvidence(game, passing(game));
    assert.equal(evidence.prerequisiteChecksPassed, true);
    assert.equal(evidence.fullGameVerified, false);
    assert.equal(evidence.wasmSha256, 'runtime-a');
  }
});
test('missing, failed, unproven or mixed-runtime probes cannot pass the preflight', () => {
  for (const game of Object.keys(gamePrerequisites)) {
    const complete = passing(game);
    assert.equal(prerequisiteEvidence(game, complete.slice(1)).prerequisiteChecksPassed, false);
    const failed = structuredClone(complete); failed[0].passed=false;
    assert.equal(prerequisiteEvidence(game, failed).prerequisiteChecksPassed, false);
    const mixed = structuredClone(complete); mixed.find(r=>r.category==='browser').wasmSha256='runtime-b';
    assert.equal(prerequisiteEvidence(game, mixed).prerequisiteChecksPassed, false);
    const mixedCode = structuredClone(complete); mixedCode.find(r=>r.category==='browser').runtimeCodeHash='code-b';
    assert.equal(prerequisiteEvidence(game, mixedCode).prerequisiteChecksPassed, false);
    const noHash = structuredClone(complete); noHash.find(r=>r.category==='browser').wasmSha256=null;
    assert.equal(prerequisiteEvidence(game, noHash).prerequisiteChecksPassed, false);
  }
  assert.throws(()=>prerequisiteSelection('unknown','browser'), /Unknown game/);
});
test('the historical conformance report exposes its actual result buckets', () => {
  const report=readFileSync(join(root,'docs/reports/d3d-conformance-MATRIX.md'),'utf8');
  for (const column of ['total','fully working','slow/flaky','fully broken']) assert.ok(report.includes(column));
});
test('compatibility documentation retains game-specific verification status', () => {
  const doc=readFileSync(join(root,'docs/game-compatibility.md'),'utf8');
  for(const game of ['Celeste','Portal','Gang Beasts']) assert.ok(doc.includes(game));
  assert.ok(/verified/i.test(doc));
});
