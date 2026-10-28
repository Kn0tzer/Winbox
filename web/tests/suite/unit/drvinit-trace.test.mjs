// web/tests/suite/unit/drvinit-trace.test.mjs — BW64_DRVINIT driver-init
// step trace, campaign section 4.35.
//
// The section 4.18 driver-init flake (guest init_vulkan fails, app never
// connects to X, never traps to the bridge) could not be localized
// because the init steps were logged by different subsystems with no
// common clock, and process starts / the libvulkan dlopen were not
// logged at all. The fix is instrumentation, not behavior: an opt-in
// BW64_DRVINIT=1 trace emitting one `drvinit: t=<ms> step=<name>` line
// per init step across the emulator core, the X server, the Vulkan
// bridge and the page tier. The live behavioral proof is a tri12 boot
// with ?drvinit=1 producing the full ordered step sequence (and a boot
// without it producing none); these guards pin the wiring and the
// off-by-default gate.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = rel => readFileSync(new URL(rel, import.meta.url), 'utf8');

test('the shared header gates on BW64_DRVINIT and stays silent when off', () => {
  const h = read('../../../../include/drvinittrace.h');
  assert.ok(h.includes('std::getenv("BW64_DRVINIT")'), 'the gate reads BW64_DRVINIT');
  assert.ok(h.includes('"drvinit: t=%ums step=%s %s"'), 'step lines carry the shared prefix, clock and step name');
  assert.ok(h.includes('if (!bw64DrvInitEnabled()) return;'), 'step emission early-returns when disabled');
  assert.ok(h.includes('if (!bw64DrvInitEnabled() || !path) return false;'), 'the driver-path matcher checks the gate before any string work (hot openat path)');
  assert.ok(h.includes('bw64DrvInitForcedFlag'), 'the launcher can force the trace when getenv is unreachable (pthread workers)');
});

test('the launcher seeds BW64_DRVINIT from ?drvinit=1', () => {
  const launcher = read('../../../../project/emscripten/wine64-launcher.js');
  assert.ok(launcher.includes('if (param("drvinit") === "1") ENV["BW64_DRVINIT"] = "1";'),
    'the ?drvinit=1 URL param seeds the emulator env, mirroring ?vktrace=1');
});

test('emulator-core steps: process start/exit and driver-module opens', () => {
  const kprocess = read('../../../../source/kernel/kprocess.cpp');
  assert.ok(kprocess.includes('#include "drvinittrace.h"'), 'kprocess includes the trace header');
  assert.ok(kprocess.includes('bw64DrvInitStep("proc-start"'), 'process starts are a step (boot order is the race surface)');
  const syscall = read('../../../../source/kernel/syscall64.cpp');
  assert.ok(syscall.includes('bw64DrvInitStep("proc-exit"'), 'process exits close the per-pid step sequence');
  assert.ok(syscall.includes('bw64DrvInitStep("dll-open"'), 'driver-module opens are a step');
  assert.ok(syscall.includes('bw64DrvInitIsDriverPath(path)'), 'opens are filtered to the driver-module set');
});

test('X-server steps: accept, handshake, first window', () => {
  const server = read('../../../../source/x11wire/xwireserver.cpp');
  assert.ok(server.includes('bw64DrvInitStep("x11-accept"'), 'accepts are a step (the flake signature is accept count 2 vs 5)');
  const conn = read('../../../../source/x11wire/xwireconnection.cpp');
  assert.ok(conn.includes('bw64DrvInitStep("x11-handshake"'), 'handshakes are a step (accept without handshake = half-open client)');
  assert.ok(conn.includes('bw64DrvInitStep("x11-first-window"'), 'the first window (desktop up) is a step');
  assert.ok(conn.includes('bw64DrvInitOnce(s_firstWindow)'), 'the first-window step is a milestone, not a per-window line');
});

test('bridge steps: witness and first-occurrence Vulkan milestones', () => {
  const bridge = read('../../../../source/vulkan/vk64bridge.cpp');
  assert.ok(bridge.includes('bw64DrvInitStep("vk-witness"'), 'the shim witness (guest dlopened this libvulkan) is a step');
  for (const step of ['vk-instance', 'vk-enum-devices', 'vk-device', 'vk-surface', 'vk-submit']) {
    assert.ok(bridge.includes(`diStep = "${step}"`), `milestone ${step} is wired in the dispatch`);
  }
  assert.ok(bridge.includes('void bw64_drvinit_enable()'), 'the page can force the trace via ccall');
});

test('page-tier steps: adapter enumeration and device flag publication', () => {
  const tier = read('../../../vkwebgpu.mjs');
  assert.ok(tier.includes("get('drvinit') === '1'"), 'the page gate reads ?drvinit=1');
  assert.ok(tier.includes('function drvInitPageStep(log, step, detail)'), 'the page step helper exists');
  assert.ok(tier.includes('if (!drvInitPageEnabled) return;'), 'page steps are silent when off');
  const adapters = tier.split("drvInitPageStep(log, 'adapter'").length - 1;
  const devices = tier.split("drvInitPageStep(log, 'device'").length - 1;
  assert.equal(adapters, 2, 'both renderers log the adapter step');
  assert.equal(devices, 2, 'both renderers log the device step after publishing the bwVk* flags');
});
