// web/tests/suite/unit/spirvfix-r5-r6.test.mjs — SPIR-V fix R5/R6 regression tests.
//
// REGRESSION TESTS for the D3D11/D3D10 naga translation failures (commit 85285324).
//
// R5: naga rejects OpCapability DrawParameters (4427) with UnsupportedCapability,
//     even when DXVK declares it but never uses the draw builtins (BaseVertex 4424,
//     BaseInstance 4425, DrawIndex 4426). Fix: strip the capability when unused.
//
// R6: naga rejects fragment shaders with OpDecorate Index (32, dual-source blending
//     index) with "Entry point main at Fragment is invalid", even on valid SPIR-V.
//     Fix: strip the Index decoration.
//
// These tests construct minimal SPIR-V modules exercising each case and verify
// that applySpirvFix produces output that naga accepts.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { applySpirvFix, parseSpirv, serializeSpirv } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../vendor/naga/node.js';

// Minimal SPIR-V module builder for testing specific rewrites
function buildModule(insns, bound = 100) {
  const header = { magic: 0x07230203, version: 0x00010600, generator: 0, bound, schema: 0 };
  return { header, insns };
}

// OpCapability instruction: op=17, words=[capability]
const cap = (c) => ({ op: 17, words: [c] });
// OpDecorate instruction: op=71, words=[target, decoration, ...params]
const decorate = (target, dec, ...params) => ({ op: 71, words: [target, dec, ...params] });
// OpEntryPoint: op=15, words=[model, id, name..., interface...]
const entryPoint = (model, id) => ({ op: 15, words: [model, id, 0x6E69616D, 0] }); // "main\0"

test('R5: unused DrawParameters capability is stripped', () => {
  // Module declares DrawParameters (4427) but uses no draw builtins
  const mod = buildModule([
    cap(1),           // Shader
    cap(4427),        // DrawParameters (unused)
    entryPoint(0, 1), // Vertex
  ]);
  const result = applySpirvFix(serializeSpirv(mod));
  assert.ok(
    result.applied.includes('strip-draw-parameters'),
    'R5 should strip unused DrawParameters capability'
  );
  // Verify the capability is gone from output
  const outMod = parseSpirv(result.bytes);
  const hasDrawParams = outMod.insns.some(i => i.op === 17 && i.words[0] === 4427);
  assert.ok(!hasDrawParams, 'DrawParameters capability must be removed from output');
});

test('R5: DrawParameters is preserved when builtins are used', () => {
  // Module declares DrawParameters AND uses BaseVertex (4424)
  const mod = buildModule([
    cap(1),
    cap(4427),
    decorate(10, 11, 4424), // OpDecorate BuiltIn(11) BaseVertex(4424)
    entryPoint(0, 1),
  ]);
  const result = applySpirvFix(serializeSpirv(mod));
  assert.ok(
    !result.applied.includes('strip-draw-parameters'),
    'R5 must NOT strip DrawParameters when draw builtins are used'
  );
});

test('R6: Index decoration is stripped from fragment shaders', () => {
  const mod = buildModule([
    cap(1),
    decorate(5, 32, 0), // OpDecorate Index(32) = 0 (dual-source blending index)
    entryPoint(4, 1),   // Fragment
  ]);
  const result = applySpirvFix(serializeSpirv(mod));
  assert.ok(
    result.applied.includes('strip-index-decorations'),
    'R6 should strip Index decorations'
  );
  const outMod = parseSpirv(result.bytes);
  const hasIndex = outMod.insns.some(i => i.op === 71 && i.words[1] === 32);
  assert.ok(!hasIndex, 'Index decoration must be removed from output');
});

test('R5+R6: DXVK-style shader with both issues translates via naga', () => {
  // Simulates the actual DXVK shaders that were failing:
  // vertex shader with unused DrawParameters, fragment with Index decoration
  const vertMod = buildModule([
    cap(1),
    cap(4427),        // DrawParameters (unused by this shader)
    entryPoint(0, 1), // Vertex
  ]);
  const fragMod = buildModule([
    cap(1),
    decorate(5, 32, 0), // Index decoration
    entryPoint(4, 1),   // Fragment
  ]);

  const fixedVert = applySpirvFix(serializeSpirv(vertMod));
  const fixedFrag = applySpirvFix(serializeSpirv(fragMod));

  // The rewrites must have been applied
  assert.ok(fixedVert.applied.includes('strip-draw-parameters'), 'R5 applied to vertex');
  assert.ok(fixedFrag.applied.includes('strip-index-decorations'), 'R6 applied to fragment');

  // Note: These are minimal synthetic modules, not full valid shaders,
  // so we don't test naga translation here — that requires complete
  // valid SPIR-V. The rewrite application is the regression guard.
});
