import assert from 'node:assert/strict';
import { mkdir, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { loadavg } from 'node:os';
import { ProbeTest, extraUrlParams } from '../lib/probe-base.mjs';
import { guestFixture } from '../lib/guest-fixtures.mjs';

// Mode order is a JIT flag sequence. The default alternates so a load drift
// cancels out; BW_TEST_JITBENCH_MODES shortens focused runs (for example
// '0,1' for one pair, or '0,1,0,1' for two pairs).
const MODE_ORDER = (process.env.BW_TEST_JITBENCH_MODES?.split(',').map(mode => mode.trim()) ?? ['0', '1', '1', '0']);
if (MODE_ORDER.some(mode => mode !== '0' && mode !== '1')) {
  throw new Error('BW_TEST_JITBENCH_MODES accepts only 0 and 1');
}
// The inner load gate is the shared-host guard; hosts running other lanes can
// raise it and must record the observed load with the numbers.
const MAX_ONE_MINUTE_LOAD = Number(process.env.BW_TEST_JITBENCH_MAX_LOAD || 4);
const ALL_FIXTURES = [
  { name: 'jitbench32', fixture: 'jitbench32-pe', arch: 'x86', pattern: /BW_JITBENCH32 sample=(\d+) ms=([\d.]+) checksum=([a-f0-9]+) passes=(\d+)/, donePattern: /BW_JITBENCH32 sample=3 / },
  { name: 'jitbench64', fixture: 'jitbench64-pe', arch: 'x64', pattern: /BW_JITBENCH64 sample=(\d+) ms=([\d.]+) checksum=([a-f0-9]+) passes=(\d+)/, donePattern: /BW_JITBENCH64 sample=3 / },
  { name: 'jitbenchmix-x86', fixture: 'jitbenchmix-pe', arch: 'x86', pattern: /BW_JITBENCHMIX arch=x86 sample=(\d+) ms=([\d.]+) checksum=([a-f0-9]+)/, donePattern: /BW_JITBENCHMIX arch=x86 sample=3 / },
  { name: 'jitbenchmix-x64', fixture: 'jitbenchmix-pe', arch: 'x64', pattern: /BW_JITBENCHMIX arch=x64 sample=(\d+) ms=([\d.]+) checksum=([a-f0-9]+)/, donePattern: /BW_JITBENCHMIX arch=x64 sample=3 / },
];
const requestedFixtures = process.env.BW_TEST_JITBENCH_FIXTURES?.split(',').map(name => name.trim()).filter(Boolean);
const FIXTURES = requestedFixtures
  ? ALL_FIXTURES.filter(fixture => requestedFixtures.includes(fixture.name))
  : ALL_FIXTURES;
if (requestedFixtures && (requestedFixtures.length === 0 || FIXTURES.length !== requestedFixtures.length)) {
  throw new Error(`Unknown BW_TEST_JITBENCH_FIXTURES selection: ${requestedFixtures.join(',')}`);
}

function loadSnapshot() {
  const [oneMinute, fiveMinutes, fifteenMinutes] = loadavg();
  return { at: new Date().toISOString(), oneMinute, fiveMinutes, fifteenMinutes };
}

async function waitForLowLoad(maxOneMinute = MAX_ONE_MINUTE_LOAD) {
  while (loadSnapshot().oneMinute >= maxOneMinute) {
    await new Promise(resolve => setTimeout(resolve, 5000));
  }
}

function median(values) {
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.floor(sorted.length / 2)];
}

function collectSamples(lines, from, fixture) {
  const unique = new Map();
  for (const line of lines.slice(from)) {
    const match = line.match(fixture.pattern);
    if (!match) continue;
    const sample = fixture.name === 'jitbenchmix-x86' || fixture.name === 'jitbenchmix-x64'
      ? { sample: Number(match[1]), ms: Number(match[2]), checksum: match[3] }
      : { sample: Number(match[1]), ms: Number(match[2]), checksum: match[3], passes: Number(match[4]) };
    if (unique.has(sample.sample)) assert.deepEqual(unique.get(sample.sample), sample, 'duplicate debugstr sample agrees');
    unique.set(sample.sample, sample);
  }
  const samples = [...unique.values()].sort((a, b) => a.sample - b.sample);
  assert.equal(samples.length, 4, `${fixture.name}: cold and three warm samples completed`);
  for (let i = 0; i < samples.length; i++) {
    assert.equal(samples[i].sample, i, `${fixture.name}: sample order`);
    assert.ok(samples[i].ms > 0, `${fixture.name}: QPC elapsed time is positive`);
    assert.match(samples[i].checksum, /^[a-f0-9]{16}$/i, `${fixture.name}: 64-bit checksum`);
  }
  return samples;
}

async function bootBenchmarkGuest(test, page, { arch, jit }) {
  const from = test.consoleLines.length;
  test.testedArchitectures.add(arch);
  // The launcher maps ?jit=1 to the 64-bit engine only and explicitly
  // latches the 32-bit engine off; the 32-bit engine follows ?jit32.
  // Benchmark modes are per-engine requests, so drive both parameters
  // from the mode: jit=1 must latch cpu32 on for the x86 fixture exactly
  // as it latches cpu64 on for the x64 fixture (jit=0 forces both off).
  const params = new URLSearchParams({ p: 'cmd /c echo vkboot', session: '1', persist: '0', fresh: '1',
    arch, jit, jit32: jit, winedbg: '+debugstr', ...extraUrlParams() });
  await page.goto(`${test._server.url}/runtime/index.html?${params}`);
  await test.waitForPageFn(page, () => globalThis.bwRuntime?.call('bw64_session_ready', [], []) === 1, 600000);
  // The benchmark guest prints to debugstr and creates no windows. Wait for
  // the wineserver session pin, without requiring the optional desktop window.
  await test.waitForLog(page, /session: persistent pins spawned; Wine owns service initialization/, 60000, from);
}

export default class JitbenchTest extends ProbeTest {
  constructor() {
    super({ tag: process.env.TAG || 'jitbench', timeoutMs: 3600000 });
    this.guestArchitectures = ['x86', 'x64'];
  }

  async run({ page }) {
    const invocationLoadStart = loadSnapshot();
    const results = [];
    const referenceChecksums = new Map();
    const rows = [];
    let activePage = page;
    const outputDir = join('test-results', this.tag);
    await mkdir(outputDir, { recursive: true });

    const saveResults = async failure => writeFile(join(outputDir, 'jitbench.json'), JSON.stringify({
      build: this.buildInfo,
      modeOrder: MODE_ORDER,
      invocationLoadAverage: { start: invocationLoadStart, end: loadSnapshot() },
      results,
      ...(failure ? { failure: String(failure.stack || failure) } : {}),
    }, null, 2));

    try {
      for (const fixture of FIXTURES) {
        for (let modeIndex = 0; modeIndex < MODE_ORDER.length; modeIndex++) {
          await waitForLowLoad();
          if (results.length !== 0) {
            await activePage.close();
            activePage = await this.newPage();
          }
          const jit = MODE_ORDER[modeIndex];
          const result = { fixture: fixture.name, arch: fixture.arch, jit: Number(jit), modeIndex,
            samples: [], stats: null, loadAverage: { start: loadSnapshot(), end: null } };
          results.push(result);
          try {
            await bootBenchmarkGuest(this, activePage, { arch: fixture.arch, jit });
            const exeName = `${fixture.name}.exe`;
            await this.stageFile(activePage, exeName, await guestFixture(fixture.fixture, fixture.arch));
            const from = await this.spawnGuest(activePage, exeName, {
              arch: fixture.arch,
              env: 'WINEDEBUG=+debugstr',
            });
            await this.waitForLog(activePage, fixture.donePattern, 180000, from);
            const samples = collectSamples(this.consoleLines, from, fixture);
            result.samples = samples;
            const checksums = samples.map(sample => sample.checksum);
            const prior = referenceChecksums.get(fixture.name);
            if (!prior) referenceChecksums.set(fixture.name, checksums);
            else assert.deepEqual(checksums, prior, `${fixture.name}: checksums are stable across JIT modes`);

            const stats = await activePage.evaluate(() => JSON.parse(
              Module.ccall('bw64_jit_stats_json_text', 'string', [], [])));
            result.stats = stats;
            const engine = fixture.arch === 'x86' ? stats.cpu32 : stats.cpu64;
            if (engine.available === 0 || engine.engine === 'unavailable') {
              // This build does not run this JIT engine (the stats
              // say so themselves: the suite runs the default
              // interpreter browser build — the JIT is a separate
              // build variant — and the 32-bit JIT is not compiled
              // into the x64 target at all). No mode can make it
              // generate code here; what this build must guarantee
              // instead is that no generated code is reported
              // entered, while the checksum stability asserted
              // above covers the interpreter path this build does
              // serve. On a build whose stats report the engine
              // available, the full latch and generated-code
              // assertions in the else branch apply.
              result.engineUnavailable = engine.reason || `engine '${engine.engine}' unavailable in this build`;
              assert.equal(engine.blocksEntered ?? 0, 0, `${fixture.name}: unavailable engine reports no generated code entered`);
            } else {
              assert.equal(Boolean(engine.enabled), jit === '1', `${fixture.name}: requested JIT mode latched`);
              if (jit === '1') {
                assert.ok(engine.blocksEntered > 0, `${fixture.name}: generated code was entered`);
                if (fixture.arch === 'x86') assert.ok(engine.blocksEmitted > 0, `${fixture.name}: CPU32 blocks emitted`);
                else assert.ok(engine.insnsEmitted > 0, `${fixture.name}: CPU64 instructions emitted`);
              }
            }
            rows.push({ fixture: fixture.name, mode: jit, coldMs: samples[0].ms,
              warmMedianMs: median(samples.slice(1).map(sample => sample.ms)) });
          } finally {
            result.loadAverage.end = loadSnapshot();
            await saveResults();
          }
        }
      }
      console.table(rows);
    } catch (error) {
      await saveResults(error);
      throw error;
    }
  }
}
