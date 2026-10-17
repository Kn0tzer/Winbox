// Run a real suite probe once, then retain its browser for interactive checks.
// TEST_PROBE=celeste-boot HOLD_MS=600000 node web/tests/hold-suite-probe.mjs
import { writeFile } from 'node:fs/promises';
import { withTimeout } from './suite/lib/probe-base.mjs';

const name = process.env.TEST_PROBE || 'celeste-boot';
if (!/^[a-z0-9-]+$/.test(name)) throw new Error('Invalid TEST_PROBE');
const holdMs = Number(process.env.HOLD_MS || 600000);
if (!Number.isInteger(holdMs) || holdMs < 0 || holdMs > 900000) throw new Error('HOLD_MS must be 0..900000');
const { default: Probe } = await import(`./suite/browser/${name}.test.mjs`);
const probe = new Probe();
const result = { name, startedAt: new Date().toISOString(), passed: false, fullGameVerified: false };
let stopping = false;
process.on('SIGTERM', () => { stopping = true; });
process.on('SIGINT', () => { stopping = true; });
try {
  await probe.setup();
  const page = await probe.newPage();
  console.log(JSON.stringify({ dir: probe.dir, stage: 'running', name }));
  try {
    await withTimeout(probe.run({ page, console: probe.consoleLines, dir: probe.dir, server: probe._server }),
      probe.timeoutMs, name);
    result.passed = true;
  } catch (error) { result.error = error.stack || String(error); }
  result.wasmSha256 = probe.buildInfo?.wasm_sha256;
  result.runtimeCodeHash = probe.runtimeCodeHash;
  result.architectures = [...probe.testedArchitectures];
  await writeFile(probe.dir + '/probe-result.json', JSON.stringify(result, null, 2));
  console.log(JSON.stringify({ dir: probe.dir, stage: 'interactive', passed: result.passed, error: result.error, holdMs }));
  const until = Date.now() + holdMs;
  while (!stopping && Date.now() < until) await new Promise(resolve => setTimeout(resolve, 1000));
  const pageNow = probe._browser.contexts().flatMap(context => context.pages()).findLast(page => !page.isClosed());
  if (pageNow) await pageNow.screenshot({ path: probe.dir + '/interactive-final.png' });
} finally {
  result.finishedAt = new Date().toISOString();
  await writeFile(probe.dir + '/probe-result.json', JSON.stringify(result, null, 2));
  await probe.teardown();
}
if (!result.passed) process.exitCode = 1;
