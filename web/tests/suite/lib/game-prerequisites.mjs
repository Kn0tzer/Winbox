// Bounded subsystem checks to run before expensive game acceptance checks.
// Passing these checks establishes their stated contracts, never playability.
const kernel = ['unix-connect-handoff', 'fork-fd-snapshot', 'poll-deadline', 'xwire-input2', 'xwire-pointer-grab'];
const input = ['win32-clock', 'input-fullscreen', 'input-keyboard', 'input-mouse-rel'];
export const gamePrerequisites = {
  celeste: {
    architecture: 'x86',
    unit: [...kernel, 'zip-stored-seek', 'zip-decompression', 'gl32-shared-renderer'],
    browser: [...input, 'boot32-vkboot', 'gl32-triangle', 'mono-runtime'],
    uncoveredBehavior: ['FNA/SDL2 game rendering workload', '32-bit FMOD audio playback', 'interactive level movement and jumping'],
  },
  portal: {
    architecture: 'x86',
    unit: [...kernel, 'zip-stored-seek', 'zip-decompression', 'v2-replay-state'],
    browser: [...input, 'boot32-vkboot', 'd3d9-32'],
    uncoveredBehavior: ['Source engine material/shader workload', '32-bit game audio playback', 'interactive map loading, movement and camera control'],
  },
  gangbeasts: {
    architecture: 'x64',
    unit: [...kernel, 'zip-stored-seek', 'zip-decompression', 'v2-replay-state'],
    browser: [...input, 'memory-shared', 'd3d11-comprehensive', 'd3d11-triangle', 'audio-tone'],
    uncoveredBehavior: ['Unity runtime and asset-loading workload', 'game-specific audio integration', 'interactive match movement and grabbing'],
  },
};

export function prerequisiteSelection(game, category) {
  const profile = gamePrerequisites[game];
  if (!profile) throw new Error(`Unknown game prerequisite profile: ${game}`);
  return new Set(profile[category]);
}

export function prerequisiteEvidence(game, results) {
  const profile = gamePrerequisites[game];
  if (!profile) throw new Error(`Unknown game prerequisite profile: ${game}`);
  const missing = [];
  for (const category of ['unit', 'browser']) for (const name of profile[category]) {
    if (!results.some(r => r.category === category && r.name === name && r.passed)) missing.push(`${category}/${name}`);
  }
  const hashes = new Set(results.filter(r => r.category === 'browser').map(r => r.wasmSha256).filter(Boolean));
  const browserResults = results.filter(r => r.category === 'browser');
  const codeHashes = new Set(browserResults.map(r => r.runtimeCodeHash).filter(Boolean));
  const sameRuntime = hashes.size === 1 && codeHashes.size === 1 && browserResults.every(r => r.wasmSha256 && r.runtimeCodeHash);
  return { game, architecture: profile.architecture, prerequisiteChecksPassed: missing.length === 0 && sameRuntime,
    missing, wasmSha256: sameRuntime ? [...hashes][0] : null,
    runtimeCodeHash: sameRuntime ? [...codeHashes][0] : null,
    fullGameVerified: false, uncoveredBehavior: profile.uncoveredBehavior };
}
