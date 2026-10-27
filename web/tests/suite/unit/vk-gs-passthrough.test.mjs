// GUARD (geometry-stage disposition): WebGPU has no geometry stage, but
// DXVK's meta copy/blit/resolve machinery builds VS+GS+FS pipelines whose
// constructors throw on ANY creation failure — rejecting geometry pipelines
// at creation killed Gang Beasts in its first scene (2 frames presented,
// then a fatal CS-thread exception). The disposition pinned here:
//   - modules declaring SPIR-V Geometry (capability 2) pass creation: meta
//     FRAGMENT modules (which declare it only because they read gl_Layer)
//     are translation-served via spirvfix R11, and true geometry modules
//     are recorded on the pipeline;
//   - geometry pipelines are created and recorded (Pipeline.gs), never
//     serialised; their draws are governed at submit: SERVED by stage
//     omission when the geometry module is the proven layer-passthrough
//     shape AND strip-equivalent to the fragment stage (see
//     source/vulkan/vk_gs_passthrough.h), LOUD-DROPPED with the
//     geometry-stage-draw counter otherwise.
// The harness half is non-vacuous both ways: the real DXVK fullscreen
// geometry module is recognised, five valid-SPIR-V mutants of it are not,
// and the serve check accepts the meta copy/resolve fragments while
// refusing the blit fragment (its location-0 input is fed by the geometry
// stage from a DIFFERENT vertex output, so omission would corrupt it).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../../../', import.meta.url));
const bridge = readFileSync(join(root, 'source/vulkan/vk64bridge.cpp'), 'utf8');

function functionBody(source, header) {
  const start = source.indexOf(header);
  assert.notEqual(start, -1, `missing ${header}`);
  const open = source.indexOf('{', start);
  let depth = 0;
  for (let i = open; i < source.length; i++) {
    if (source[i] === '{') depth++;
    else if (source[i] === '}' && --depth === 0) return source.slice(open, i + 1);
  }
  throw new Error(`unbalanced function ${header}`);
}

test('layer-passthrough matcher recognises the real DXVK fullscreen geometry module and rejects mutants', () => {
  const dir = mkdtempSync(join(tmpdir(), 'vk-gs-passthrough-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include "source/vulkan/vk_gs_passthrough.h"
#include <cstdio>
#include <cstring>
#include <vector>
static std::vector<uint32_t> readWords(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint32_t> w((size_t)n / 4);
    if (n > 0 && fread(w.data(), 1, (size_t)n, f) != (size_t)n) return {};
    fclose(f); return w;
}
int main(int argc, char** argv) {
    if (argc >= 3 && !strcmp(argv[1], "shape")) {
        auto w = readWords(argv[2]);
        printf("%s\\n", vkgs::isLayerPassthroughGeom(w.data(), w.size()) ? "true" : "false");
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "serve")) {
        auto g = readWords(argv[2]); auto f = readWords(argv[3]);
        printf("%s\\n", vkgs::stripServeOk(g.data(), g.size(), f.data(), f.size()) ? "true" : "false");
        return 0;
    }
    return 2;
}
`);
    execFileSync('c++', ['-std=c++17', '-I', root, join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    const run = (...args) => execFileSync(join(dir, 'check'), args, { encoding: 'utf8' }).trim();
    const geom = join(root, 'testdata/spirv/geometry/dxvk-fullscreen.geom.spv');
    const live = (f) => join(root, 'testdata/spirv/dxvk-live', f);

    // The one positive: DXVK's real fullscreen geometry module.
    assert.equal(run('shape', geom), 'true', 'the real dxvk_fullscreen_geom is the layer-passthrough shape');
    // Non-geometry modules are not the shape.
    assert.equal(run('shape', live('dxvk-meta-copy-color-2d.frag.spv')), 'false');
    assert.equal(run('shape', live('dxvk-fullscreen.vert.spv')), 'false');
    // Mutants of the real module (valid SPIR-V, one semantic change each).
    for (const m of ['mutant-m1-amp', 'mutant-m2-layerconst', 'mutant-m3-posconst', 'mutant-m4-viewport', 'mutant-m5-fadd']) {
      assert.equal(run('shape', join(root, 'testdata/spirv/geometry', `${m}.spv`)), 'false', `${m} must not match the passthrough shape`);
    }
    // Serve verdicts: copy/resolve fragments consume no chain inputs, so
    // omitting the stage is sound; the blit fragment reads location 0,
    // which the geometry stage feeds from its input location 1, so its
    // draws must take the loud-drop path instead.
    for (const f of ['dxvk-meta-copy-color-1d.frag.spv', 'dxvk-meta-copy-color-2d.frag.spv',
                     'dxvk-meta-copy-color-ms.frag.spv', 'dxvk-meta-resolve-f.frag.spv']) {
      assert.equal(run('serve', geom, live(f)), 'true', `${f} serves by stage omission`);
    }
    assert.equal(run('serve', geom, live('dxvk-meta-blit-2d.frag.spv')), 'false',
      'blit fragment input location 0 is renumbered by the geometry stage: not strip-equivalent');
    assert.equal(run('serve', geom, live('dxvk-fullscreen.vert.spv')), 'false',
      'a vertex module is not a fragment stage');
    assert.equal(run('serve', live('dxvk-meta-copy-color-2d.frag.spv'), live('dxvk-meta-copy-color-2d.frag.spv')), 'false',
      'a non-passthrough first module never serves');
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('capability 2 and 35 pass module creation under the geometry disposition', () => {
  const table = functionBody(bridge, 'static const char* vk64SpirvCapabilityName(');
  assert.ok(table.includes('case 2: return nullptr;'), 'Geometry passes at module creation (disposition governs pipelines/draws)');
  assert.ok(!table.includes('case 2: return "Geometry"'), 'Geometry must not be a named creation rejection');
  assert.ok(table.includes('case 35: return nullptr;'), 'SampleRateShading is a translation-gated pass (naga translates SampleId)');
  assert.ok(!table.includes('case 35: return "SampleRateShading"'), 'SampleRateShading must not be a named creation rejection');
  const audit = functionBody(bridge, 'static VkResult vk64AuditSpirv(');
  assert.ok(audit.includes('c != 2 &&'), 'cap 2 is exempt from the unknown-capability rejection');
  assert.ok(audit.includes('c != 35 &&'), 'cap 35 is exempt from the unknown-capability rejection');
  // The fixed-function half of sample shading still rejects at pipeline time.
  const pipeAudit = functionBody(bridge, 'static bool vk64AuditGraphicsPipeline(');
  assert.match(pipeAudit, /a\.sampleShading[^\n]*vk64Unsupported\("sample-shading"[^\n]*reject = true/);
});

test('geometry pipelines are recorded, not rejected, in both guest ABIs', () => {
  const audit = functionBody(bridge, 'static bool vk64AuditGraphicsPipeline(');
  const geomBlock = audit.slice(audit.indexOf('extra & 0x8u'), audit.indexOf('extra & 0x40u'));
  assert.ok(geomBlock.includes('vk64Unsupported("geometry-stage"'), 'non-passthrough geometry pipelines log their disposition');
  assert.ok(!geomBlock.includes('reject = true'), 'geometry stage must NOT reject pipeline creation');
  assert.ok(geomBlock.includes('gsPassthroughShape'), 'the passthrough shape is the silent (served) half');
  for (const feature of ['tessellation-control-stage', 'tessellation-evaluation-stage', 'task-stage', 'mesh-stage']) {
    assert.match(audit, new RegExp(`vk64Unsupported\\("${feature}"[^\\n]*reject = true`), `${feature} still rejects at creation`);
  }
  assert.equal((bridge.match(/audit\.gsPassthroughShape = gm->geomPassthrough/g) || []).length, 2,
    'both ABIs pre-scan the geometry stage module flag for the audit');
  assert.equal((bridge.match(/p->gs = st\.module;/g) || []).length, 2,
    'both ABIs record the geometry module on the pipeline');
  assert.equal((bridge.match(/vkgs::stripServeOk\(/g) || []).length, 2,
    'both ABIs compute the strip-serve verdict at pipeline creation');
  assert.equal((bridge.match(/m->geomPassthrough = vkgs::isLayerPassthroughGeom\(/g) || []).length, 2,
    'both vkCreateShaderModule handlers classify their module');
  assert.match(bridge, /bool geomPassthrough = false;/);
  assert.match(bridge, /U64 gs = 0;/);
  assert.match(bridge, /bool gsServe = false;/);
  assert.match(bridge, /#include "vk_gs_passthrough\.h"/);
});

test('draws under a non-served geometry pipeline are loud-dropped at submit in both serializers', () => {
  assert.equal((bridge.match(/vk64Unsupported\("geometry-stage-draw"/g) || []).length, 4,
    'V2 serializer (draw + draw-indexed) and the V1 debug emitter (draw + draw-indexed) drop loudly');
  assert.equal((bridge.match(/gsPipe->gs && !gsPipe->gsServe/g) || []).length, 2, 'V2 drops are gated on the serve verdict');
  assert.equal((bridge.match(/pipe->gs && !pipe->gsServe/g) || []).length, 2, 'V1 drops are gated on the serve verdict');
  // The geometry module is recorded but never serialised to the page tier:
  // only the vertex and fragment modules are ever handed to emitShader.
  assert.ok(!/emitShader\([^)]*gs/.test(bridge), 'the geometry module must never reach emitShader');
});
