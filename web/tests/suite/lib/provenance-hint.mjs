// web/tests/suite/lib/provenance-hint.mjs — the single source of the "no build
// export" message used by provenance-artifacts.test.mjs and by runner.mjs --unit.
//
// provenance-artifacts fails on purpose when no export exists: a probe must not
// pass without proving its build. The failure says how to fix it, so the fix
// does not have to be read out of the test source.
import { existsSync } from 'node:fs';
import { join, resolve } from 'node:path';

export const BUILD_INFO_REL = 'runtime/BUILD_INFO.json';

export function provenanceDistRoot(repoRoot, env = process.env) {
  return resolve(repoRoot, env.BW_TEST_DIST || 'dist');
}

export function missingExportMessage(distRoot) {
  return `no build export at ${distRoot} (${BUILD_INFO_REL} is missing). ` +
    'Fix: run tools/bw64-build.sh --export-only (writes dist/ in this checkout), ' +
    'or set BW_TEST_DIST=<dist> to test an existing export.';
}

// The hint for the runner, or null when the export is present.
export function provenanceHint(repoRoot, env = process.env) {
  const distRoot = provenanceDistRoot(repoRoot, env);
  if (existsSync(join(distRoot, BUILD_INFO_REL))) return null;
  return missingExportMessage(distRoot);
}
