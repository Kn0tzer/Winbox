// tools/run-tests.mjs — Orchestrates ALL fast (non-browser) tests.
//
// Runs:
//   1. Legacy tests: node --test web/tests/*.test.mjs
//   2. Suite unit tests: node web/tests/suite/runner.mjs --unit
//
// Exit code 0 = everything passed. Non-zero = something failed.
// Use --json for machine-readable summary.

import { spawn } from "node:child_process";

const jsonOutput = process.argv.includes("--json");

function log(...a) { if (!jsonOutput) console.log(...a); }

function run(cmd, args, label) {
  return new Promise(resolve => {
    log(`\n### ${label}`);
    log(`$ ${cmd} ${args.join(" ")}`);
    const t0 = Date.now();
    const proc = spawn(cmd, args, { stdio: "inherit" });
    proc.on("close", code => {
      const ms = Date.now() - t0;
      log(`### ${label}: exit ${code} (${ms}ms)`);
      resolve({ label, code, ms });
    });
    proc.on("error", e => {
      log(`### ${label}: spawn error: ${e.message}`);
      resolve({ label, code: 127, ms: 0, error: e.message });
    });
  });
}

const results = [];
results.push(await run("node", ["--test", "web/tests/*.test.mjs"], "legacy tests (web/tests/*.test.mjs)"));
results.push(await run("node", ["web/tests/suite/runner.mjs", "--unit"], "suite unit tests (web/tests/suite/unit/)"));

const failed = results.filter(r => r.code !== 0);

log("\n==================================================");
if (failed.length === 0) {
  log("ALL TESTS PASSED");
} else {
  log(`FAILURES: ${failed.map(r => r.label).join(", ")}`);
}
log("==================================================");

if (jsonOutput) {
  console.log(JSON.stringify({ results, allPassed: failed.length === 0 }));
}

process.exit(failed.length === 0 ? 0 : 1);
