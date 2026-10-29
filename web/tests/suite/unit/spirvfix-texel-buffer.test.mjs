// spirvfix R12 (texel-buffer-to-2d): a uniform texel buffer (usamplerBuffer +
// texelFetch) is converted to a 8192-wide 2D texture so naga/WebGPU can serve
// it. This is the path that unblocks DXVK device creation (its HUD renderer
// eagerly creates an R8_UINT texel buffer view). Storage texel buffers and
// size queries must NOT be converted — the module passes through untouched.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { applySpirvFix, parseSpirv } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../../web/vendor/naga/node.js';

await naga.init();
const b64 = s => new Uint8Array(Buffer.from(s.replace(/\s/g, ''), 'base64'));

// glslangValidator -V of: layout(binding=1) uniform usamplerBuffer +
// texelFetch(buf, int) (the DXVK HUD text vertex shader's shape).
const HUD_VERT = b64(
  'AwIjBwAAAQALAAgALgAAAAAAAAARAAIAAQAAABEAAgAuAAAACwAGAAEAAABHTFNMLnN0ZC40NTAAAAAADgADAAAAAAABAAAADwAI' +
  'AAAAAAAEAAAAbWFpbgAAAAARAAAAHQAAACQAAAADAAMAAgAAAMIBAAAFAAQABAAAAG1haW4AAAAABQADAAgAAABnAAAABQAFAAwA' +
  'AAB0ZXh0X2J1ZmZlcgAFAAQAEQAAAHZfaW4AAAAABQAEAB0AAABvX2dseXBoAAUABgAiAAAAZ2xfUGVyVmVydGV4AAAAAAYABgAi' +
  'AAAAAAAAAGdsX1Bvc2l0aW9uAAYABwAiAAAAAQAAAGdsX1BvaW50U2l6ZQAAAAAGAAcAIgAAAAIAAABnbF9DbGlwRGlzdGFuY2UA' +
  'BgAHACIAAAADAAAAZ2xfQ3VsbERpc3RhbmNlAAUAAwAkAAAAAAAAAEcABAAMAAAAIQAAAAEAAABHAAQADAAAACIAAAAAAAAARwAE' +
  'ABEAAAAeAAAAAAAAAEcABAAdAAAAHgAAAAAAAABHAAMAIgAAAAIAAABIAAUAIgAAAAAAAAALAAAAAAAAAEgABQAiAAAAAQAAAAsA' +
  'AAABAAAASAAFACIAAAACAAAACwAAAAMAAABIAAUAIgAAAAMAAAALAAAABAAAABMAAgACAAAAIQADAAMAAAACAAAAFQAEAAYAAAAg' +
  'AAAAAAAAACAABAAHAAAABwAAAAYAAAAZAAkACQAAAAYAAAAFAAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAbAAMACgAAAAkAAAAgAAQA' +
  'CwAAAAAAAAAKAAAAOwAEAAsAAAAMAAAAAAAAABYAAwAOAAAAIAAAABcABAAPAAAADgAAAAIAAAAgAAQAEAAAAAEAAAAPAAAAOwAE' +
  'ABAAAAARAAAAAQAAACsABAAGAAAAEgAAAAAAAAAgAAQAEwAAAAEAAAAOAAAAFQAEABYAAAAgAAAAAQAAABcABAAZAAAABgAAAAQA' +
  'AAAgAAQAHAAAAAMAAAAGAAAAOwAEABwAAAAdAAAAAwAAABcABAAfAAAADgAAAAQAAAArAAQABgAAACAAAAABAAAAHAAEACEAAAAO' +
  'AAAAIAAAAB4ABgAiAAAAHwAAAA4AAAAhAAAAIQAAACAABAAjAAAAAwAAACIAAAA7AAQAIwAAACQAAAADAAAAKwAEABYAAAAlAAAA' +
  'AAAAACsABAAOAAAAJwAAAAAAAAArAAQADgAAACgAAAAAAIA/IAAEACwAAAADAAAAHwAAADYABQACAAAABAAAAAAAAAADAAAA+AAC' +
  'AAUAAAA7AAQABwAAAAgAAAAHAAAAPQAEAAoAAAANAAAADAAAAEEABQATAAAAFAAAABEAAAASAAAAPQAEAA4AAAAVAAAAFAAAAG4A' +
  'BAAWAAAAFwAAABUAAABkAAQACQAAABgAAAANAAAAXwAFABkAAAAaAAAAGAAAABcAAABRAAUABgAAABsAAAAaAAAAAAAAAD4AAwAI' +
  'AAAAGwAAAD0ABAAGAAAAHgAAAAgAAAA+AAMAHQAAAB4AAAA9AAQADwAAACYAAAARAAAAUQAFAA4AAAApAAAAJgAAAAAAAABRAAUA' +
  'DgAAACoAAAAmAAAAAQAAAFAABwAfAAAAKwAAACkAAAAqAAAAJwAAACgAAABBAAUALAAAAC0AAAAkAAAAJQAAAD4AAwAtAAAAKwAA' +
  'AP0AAQA4AAEA');
// layout(binding=0, rgba32f) uniform imageBuffer + imageLoad/imageStore.
const STORAGE_FRAG = b64(
  'AwIjBwAAAQALAAgAGQAAAAAAAAARAAIAAQAAABEAAgAvAAAACwAGAAEAAABHTFNMLnN0ZC40NTAAAAAADgADAAAAAAABAAAADwAG' +
  'AAQAAAAEAAAAbWFpbgAAAAAWAAAAEAADAAQAAAAHAAAAAwADAAIAAADCAQAABQAEAAQAAABtYWluAAAAAAUABAAJAAAAaW1nX2J1' +
  'ZgAFAAQAFgAAAG9fY29sb3IARwADAAkAAAAZAAAARwAEAAkAAAAhAAAAAAAAAEcABAAJAAAAIgAAAAAAAABHAAQAFgAAAB4AAAAA' +
  'AAAAEwACAAIAAAAhAAMAAwAAAAIAAAAVAAQABgAAACAAAAAAAAAAGQAJAAcAAAAGAAAABQAAAAAAAAAAAAAAAAAAAAIAAAAgAAAA' +
  'IAAEAAgAAAAAAAAABwAAADsABAAIAAAACQAAAAAAAAAVAAQACwAAACAAAAABAAAAKwAEAAsAAAAMAAAAAwAAABcABAANAAAABgAA' +
  'AAQAAAArAAQABgAAAA4AAAABAAAAKwAEAAYAAAAPAAAAAgAAACsABAAGAAAAEAAAAAMAAAArAAQABgAAABEAAAAEAAAALAAHAA0A' +
  'AAASAAAADgAAAA8AAAAQAAAAEQAAABYAAwATAAAAIAAAABcABAAUAAAAEwAAAAQAAAAgAAQAFQAAAAMAAAAUAAAAOwAEABUAAAAW' +
  'AAAAAwAAACsABAATAAAAFwAAAAAAgD8sAAcAFAAAABgAAAAXAAAAFwAAABcAAAAXAAAANgAFAAIAAAAEAAAAAAAAAAMAAAD4AAIA' +
  'BQAAAD0ABAAHAAAACgAAAAkAAABjAAQACgAAAAwAAAASAAAAPgADABYAAAAYAAAA/QABADgAAQA=');
// usamplerBuffer consumed by textureSize() — no 2D size expresses the length.
const QSIZE_VERT = b64(
  'AwIjBwAAAQALAAgAIAAAAAAAAAARAAIAAQAAABEAAgAuAAAAEQACADIAAAALAAYAAQAAAEdMU0wuc3RkLjQ1MAAAAAAOAAMAAAAA' +
  'AAEAAAAPAAcAAAAAAAQAAABtYWluAAAAAAkAAAAaAAAAAwADAAIAAADCAQAABQAEAAQAAABtYWluAAAAAAUABAAJAAAAb19jb2xv' +
  'cgAFAAMADQAAAGJ1ZgAFAAYAGAAAAGdsX1BlclZlcnRleAAAAAAGAAYAGAAAAAAAAABnbF9Qb3NpdGlvbgAGAAcAGAAAAAEAAABn' +
  'bF9Qb2ludFNpemUAAAAABgAHABgAAAACAAAAZ2xfQ2xpcERpc3RhbmNlAAYABwAYAAAAAwAAAGdsX0N1bGxEaXN0YW5jZQAFAAMA' +
  'GgAAAAAAAABHAAQACQAAAB4AAAAAAAAARwAEAA0AAAAhAAAAAAAAAEcABAANAAAAIgAAAAAAAABHAAMAGAAAAAIAAABIAAUAGAAA' +
  'AAAAAAALAAAAAAAAAEgABQAYAAAAAQAAAAsAAAABAAAASAAFABgAAAACAAAACwAAAAMAAABIAAUAGAAAAAMAAAALAAAABAAAABMA' +
  'AgACAAAAIQADAAMAAAACAAAAFQAEAAYAAAAgAAAAAAAAABcABAAHAAAABgAAAAQAAAAgAAQACAAAAAMAAAAHAAAAOwAEAAgAAAAJ' +
  'AAAAAwAAABkACQAKAAAABgAAAAUAAAAAAAAAAAAAAAAAAAABAAAAAAAAABsAAwALAAAACgAAACAABAAMAAAAAAAAAAsAAAA7AAQA' +
  'DAAAAA0AAAAAAAAAFQAEABAAAAAgAAAAAQAAABYAAwAUAAAAIAAAABcABAAVAAAAFAAAAAQAAAArAAQABgAAABYAAAABAAAAHAAE' +
  'ABcAAAAUAAAAFgAAAB4ABgAYAAAAFQAAABQAAAAXAAAAFwAAACAABAAZAAAAAwAAABgAAAA7AAQAGQAAABoAAAADAAAAKwAEABAA' +
  'AAAbAAAAAAAAACsABAAUAAAAHAAAAAAAAAAsAAcAFQAAAB0AAAAcAAAAHAAAABwAAAAcAAAAIAAEAB4AAAADAAAAFQAAADYABQAC' +
  'AAAABAAAAAAAAAADAAAA+AACAAUAAAA9AAQACwAAAA4AAAANAAAAZAAEAAoAAAAPAAAADgAAAGgABAAQAAAAEQAAAA8AAAB8AAQA' +
  'BgAAABIAAAARAAAAUAAHAAcAAAATAAAAEgAAABIAAAASAAAAEgAAAD4AAwAJAAAAEwAAAEEABQAeAAAAHwAAABoAAAAbAAAAPgAD' +
  'AB8AAAAdAAAA/QABADgAAQA=');

test('uniform texel buffer converts to a wrapped 2D texture naga accepts', () => {
  const r = applySpirvFix(HUD_VERT);
  assert.ok(r.applied.includes('texel-buffer-to-2d'), JSON.stringify(r));
  const mod = parseSpirv(r.bytes);
  // SampledBuffer capability (46) gone; image Dim flipped Buffer(5) -> 2D(1).
  assert.ok(!mod.insns.some(i => i.op === 17 && i.words[0] === 46), 'SampledBuffer capability must be dropped');
  const img = mod.insns.find(i => i.op === 25);
  assert.equal(img.words[2], 1, 'OpTypeImage Dim must become 2D (1)');
  // End to end: naga translates and the fetch is a wrapped textureLoad.
  const wgsl = naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes });
  assert.match(wgsl, /texture_2d<u32>/);
  assert.match(wgsl, /textureLoad\(/);
  assert.match(wgsl, /% 8192u/);
  assert.match(wgsl, /\/ 8192u/);
});

test('storage texel buffer converts (R12S): writes round-trip via the deferred readback', () => {
  // Pre-metabind this module passed through untouched, on the premise that
  // shader writes to a texture copy could never reach guest memory. The V2
  // deferred readback (bridge GPU-write marks + V2_BUFFER_READBACK) removed
  // that premise, so R12S converts storage texel buffers like R12 converts
  // uniform ones.
  const r = applySpirvFix(STORAGE_FRAG);
  assert.ok(r.applied.includes('storage-texel-buffer-to-2d'), JSON.stringify(r.applied));
  assert.ok(!r.applied.includes('texel-buffer-to-2d'), 'the uniform rewrite must not claim storage types');
  const mod = parseSpirv(r.bytes);
  const img = mod.insns.find(i => i.op === 25);
  assert.equal(img.words[2], 1, 'OpTypeImage Dim must become 2D (1)');
  assert.ok(!mod.insns.some(i => i.op === 17 && (i.words[0] === 46 || i.words[0] === 47)),
    'the Buffer-image capabilities must be dropped once no Buffer image remains');
  const wgsl = naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes });
  assert.match(wgsl, /texture_storage_2d</);
  assert.match(wgsl, /textureStore\(/);
});

// glslangValidator -V of the vkd3d meta-clear shape: writeonly
// uimageBuffer (format Unknown) + push constants + imageStore.
const META_COMP = b64(
  'AwIjBwAAAQALAAgAMQAAAAAAAAARAAIAAQAAABEAAgAvAAAAEQACADgAAAALAAYAAQAAAEdMU0wuc3RkLjQ1MAAAAAAOAAMAAAAAAAEAAAAPAAYABQAAAA' +
  'QAAABtYWluAAAAAAwAAAAQAAYABAAAABEAAABAAAAAAQAAAAEAAAADAAMAAgAAAMIBAAAFAAQABAAAAG1haW4AAAAABQADAAgAAABpZHgABQAIAAwAAABn' +
  'bF9HbG9iYWxJbnZvY2F0aW9uSUQAAAAFAAMAFQAAAFBDAAAGAAYAFQAAAAAAAABjbGVhcl92YWx1ZQAGAAUAFQAAAAEAAABvZmZzZXQAAAYABQAVAAAAAg' +
  'AAAGV4dGVudAAABQADABcAAABwYwAABQADACMAAABkc3QARwAEAAwAAAALAAAAHAAAAEcAAwAVAAAAAgAAAEgABQAVAAAAAAAAACMAAAAAAAAASAAFABUA' +
  'AAABAAAAIwAAABAAAABIAAUAFQAAAAIAAAAjAAAAGAAAAEcAAwAjAAAAGQAAAEcABAAjAAAAIQAAAAAAAABHAAQAIwAAACIAAAAAAAAARwAEADAAAAALAA' +
  'AAGQAAABMAAgACAAAAIQADAAMAAAACAAAAFQAEAAYAAAAgAAAAAQAAACAABAAHAAAABwAAAAYAAAAVAAQACQAAACAAAAAAAAAAFwAEAAoAAAAJAAAAAwAA' +
  'ACAABAALAAAAAQAAAAoAAAA7AAQACwAAAAwAAAABAAAAKwAEAAkAAAANAAAAAAAAACAABAAOAAAAAQAAAAkAAAAXAAQAEwAAAAkAAAAEAAAAFwAEABQAAA' +
  'AGAAAAAgAAAB4ABQAVAAAAEwAAABQAAAAUAAAAIAAEABYAAAAJAAAAFQAAADsABAAWAAAAFwAAAAkAAAArAAQABgAAABgAAAACAAAAIAAEABkAAAAJAAAA' +
  'BgAAABQAAgAcAAAAGQAJACEAAAAJAAAABQAAAAAAAAAAAAAAAAAAAAIAAAAAAAAAIAAEACIAAAAAAAAAIQAAADsABAAiAAAAIwAAAAAAAAArAAQABgAAAC' +
  'UAAAABAAAAKwAEAAYAAAAqAAAAAAAAACAABAArAAAACQAAABMAAAArAAQACQAAAC4AAABAAAAAKwAEAAkAAAAvAAAAAQAAACwABgAKAAAAMAAAAC4AAAAv' +
  'AAAALwAAADYABQACAAAABAAAAAAAAAADAAAA+AACAAUAAAA7AAQABwAAAAgAAAAHAAAAQQAFAA4AAAAPAAAADAAAAA0AAAA9AAQACQAAABAAAAAPAAAAfA' +
  'AEAAYAAAARAAAAEAAAAD4AAwAIAAAAEQAAAD0ABAAGAAAAEgAAAAgAAABBAAYAGQAAABoAAAAXAAAAGAAAAA0AAAA9AAQABgAAABsAAAAaAAAArwAFABwA' +
  'AAAdAAAAEgAAABsAAAD3AAMAHwAAAAAAAAD6AAQAHQAAAB4AAAAfAAAA+AACAB4AAAD9AAEA+AACAB8AAAA9AAQAIQAAACQAAAAjAAAAQQAGABkAAAAmAA' +
  'AAFwAAACUAAAANAAAAPQAEAAYAAAAnAAAAJgAAAD0ABAAGAAAAKAAAAAgAAACAAAUABgAAACkAAAAnAAAAKAAAAEEABQArAAAALAAAABcAAAAqAAAAPQAE' +
  'ABMAAAAtAAAALAAAAGMABAAkAAAAKQAAAC0AAAD9AAEAOAABAA==');
// layout(binding=0, rgba32ui) uniform uimageBuffer + imageLoad/imageStore.
const RW_COMP = b64(
  'AwIjBwAAAQALAAgAIwAAAAAAAAARAAIAAQAAABEAAgAvAAAACwAGAAEAAABHTFNMLnN0ZC40NTAAAAAADgADAAAAAAABAAAADwAGAAUAAAAEAAAAbWFpbg' +
  'AAAAAMAAAAEAAGAAQAAAARAAAAQAAAAAEAAAABAAAAAwADAAIAAADCAQAABQAEAAQAAABtYWluAAAAAAUAAwAIAAAAaWR4AAUACAAMAAAAZ2xfR2xvYmFs' +
  'SW52b2NhdGlvbklEAAAABQADABQAAAB2AAAABQADABcAAABidWYARwAEAAwAAAALAAAAHAAAAEcABAAXAAAAIQAAAAAAAABHAAQAFwAAACIAAAAAAAAARw' +
  'AEACIAAAALAAAAGQAAABMAAgACAAAAIQADAAMAAAACAAAAFQAEAAYAAAAgAAAAAQAAACAABAAHAAAABwAAAAYAAAAVAAQACQAAACAAAAAAAAAAFwAEAAoA' +
  'AAAJAAAAAwAAACAABAALAAAAAQAAAAoAAAA7AAQACwAAAAwAAAABAAAAKwAEAAkAAAANAAAAAAAAACAABAAOAAAAAQAAAAkAAAAXAAQAEgAAAAkAAAAEAA' +
  'AAIAAEABMAAAAHAAAAEgAAABkACQAVAAAACQAAAAUAAAAAAAAAAAAAAAAAAAACAAAAHgAAACAABAAWAAAAAAAAABUAAAA7AAQAFgAAABcAAAAAAAAAKwAE' +
  'AAkAAAAeAAAAAQAAACwABwASAAAAHwAAAB4AAAAeAAAAHgAAAB4AAAArAAQACQAAACEAAABAAAAALAAGAAoAAAAiAAAAIQAAAB4AAAAeAAAANgAFAAIAAA' +
  'AEAAAAAAAAAAMAAAD4AAIABQAAADsABAAHAAAACAAAAAcAAAA7AAQAEwAAABQAAAAHAAAAQQAFAA4AAAAPAAAADAAAAA0AAAA9AAQACQAAABAAAAAPAAAA' +
  'fAAEAAYAAAARAAAAEAAAAD4AAwAIAAAAEQAAAD0ABAAVAAAAGAAAABcAAAA9AAQABgAAABkAAAAIAAAAYgAFABIAAAAaAAAAGAAAABkAAAA+AAMAFAAAAB' +
  'oAAAA9AAQAFQAAABsAAAAXAAAAPQAEAAYAAAAcAAAACAAAAD0ABAASAAAAHQAAABQAAACAAAUAEgAAACAAAAAdAAAAHwAAAGMABAAbAAAAHAAAACAAAAD9' +
  'AAEAOAABAA==');
// Storage texel buffer consumed by imageSize() -- no 2D shape expresses
// the length, so R12S must refuse and leave the module untouched.
const QSIZE_COMP = b64(
  'AwIjBwAAAQALAAgAHAAAAAAAAAARAAIAAQAAABEAAgAvAAAAEQACADIAAAALAAYAAQAAAEdMU0wuc3RkLjQ1MAAAAAAOAAMAAAAAAAEAAAAPAAUABQAAAA' +
  'QAAABtYWluAAAAABAABgAEAAAAEQAAAAEAAAABAAAAAQAAAAMAAwACAAAAwgEAAAUABAAEAAAAbWFpbgAAAAAFAAMABwAAAE91dAAGAAQABwAAAAAAAAB2' +
  'AAAABQADAAkAAABvAAAABQADAA4AAABidWYARwADAAcAAAADAAAASAAFAAcAAAAAAAAAIwAAAAAAAABHAAQACQAAACEAAAABAAAARwAEAAkAAAAiAAAAAA' +
  'AAAEcABAAOAAAAIQAAAAAAAABHAAQADgAAACIAAAAAAAAARwAEABsAAAALAAAAGQAAABMAAgACAAAAIQADAAMAAAACAAAAFQAEAAYAAAAgAAAAAAAAAB4A' +
  'AwAHAAAABgAAACAABAAIAAAAAgAAAAcAAAA7AAQACAAAAAkAAAACAAAAFQAEAAoAAAAgAAAAAQAAACsABAAKAAAACwAAAAAAAAAZAAkADAAAAAYAAAAFAA' +
  'AAAAAAAAAAAAAAAAAAAgAAAB4AAAAgAAQADQAAAAAAAAAMAAAAOwAEAA0AAAAOAAAAAAAAACAABAASAAAAAgAAAAYAAAAXAAQAFwAAAAYAAAAEAAAAFwAE' +
  'ABkAAAAGAAAAAwAAACsABAAGAAAAGgAAAAEAAAAsAAYAGQAAABsAAAAaAAAAGgAAABoAAAA2AAUAAgAAAAQAAAAAAAAAAwAAAPgAAgAFAAAAPQAEAAwAAA' +
  'APAAAADgAAAGgABAAKAAAAEAAAAA8AAAB8AAQABgAAABEAAAAQAAAAQQAFABIAAAATAAAACQAAAAsAAAA+AAMAEwAAABEAAAA9AAQADAAAABQAAAAOAAAA' +
  'QQAFABIAAAAVAAAACQAAAAsAAAA9AAQABgAAABYAAAAVAAAAUAAHABcAAAAYAAAAFgAAABYAAAAWAAAAFgAAAGMABAAUAAAACwAAABgAAAD9AAEAOAABAA' +
  '==');
test('texel buffer consumed by a size query is refused, module untouched', () => {
  const r = applySpirvFix(QSIZE_VERT);
  assert.ok(!r.applied.includes('texel-buffer-to-2d'));
  assert.ok(r.log.some(l => l.includes('R12') && l.includes('refused')), JSON.stringify(r.log));
  const mod = parseSpirv(r.bytes);
  assert.ok(mod.insns.some(i => i.op === 17 && i.words[0] === 46), 'capability must survive a refusal');
});

test('R12S: the vkd3d meta-clear module converts and takes the injected view format', () => {
  // Without a bound view there is no honest format: R12S converts the
  // shape but the type stays Unknown and naga keeps failing loudly.
  const bare = applySpirvFix(META_COMP);
  assert.ok(bare.applied.includes('storage-texel-buffer-to-2d'), JSON.stringify(bare.applied));
  assert.throws(() => naga.translate({ from: 'spirv', to: 'wgsl', source: bare.bytes }),
    'an Unknown-format storage image must stay untranslatable without the signature');
  // With the page-side signature for the bound view (set 0, binding 0,
  // Rg32ui = SPIR-V format 35), R14 injects the format and naga accepts.
  const r = applySpirvFix(META_COMP, { storageFormats: '0:0=35' });
  assert.ok(r.applied.includes('storage-image-format'), JSON.stringify(r.applied));
  const mod = parseSpirv(r.bytes);
  const img = mod.insns.find(i => i.op === 25);
  assert.equal(img.words[2], 1, 'Dim Buffer -> 2D');
  assert.equal(img.words[7], 35, 'the view format is injected into the image type');
  assert.deepEqual(mod.insns.filter(i => i.op === 17).map(i => i.words[0]), [1],
    'Shader is the only capability left (ImageBuffer and WriteWithoutFormat are gone)');
  const wgsl = naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes });
  assert.match(wgsl, /texture_storage_2d<rg32uint,write>/);
  assert.match(wgsl, /textureStore\(/);
  assert.match(wgsl, /% 8192u/);
  assert.match(wgsl, /\/ 8192u/);
});

test('R12S: a load+store storage texel module converts with wrapped reads and writes', () => {
  const r = applySpirvFix(RW_COMP);
  assert.ok(r.applied.includes('storage-texel-buffer-to-2d'), JSON.stringify(r.applied));
  const wgsl = naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes });
  assert.match(wgsl, /texture_storage_2d<rgba32uint,read_write>/);
  assert.match(wgsl, /textureLoad\(/);
  assert.match(wgsl, /textureStore\(/);
});

test('R12S: a storage texel buffer consumed by a size query is refused, module untouched', () => {
  const r = applySpirvFix(QSIZE_COMP);
  assert.ok(!r.applied.includes('storage-texel-buffer-to-2d'));
  assert.ok(r.log.some(l => l.includes('R12S') && l.includes('refused')), JSON.stringify(r.log));
  assert.deepEqual([...r.bytes], [...QSIZE_COMP]);
  const mod = parseSpirv(r.bytes);
  assert.ok(mod.insns.some(i => i.op === 17 && i.words[0] === 47), 'ImageBuffer capability must survive a refusal');
});

// The REAL cs_clear_uav_buffer_uint module, captured from the guest chunk
// stream (cs12 probe, SHADER record of the first meta submit). It carries
// two shapes that each refused the R12S conversion when this fixture was
// pinned, while the float twin converted -- so the uint meta clear died on
// naga's ImageBuffer capability and BUF read back zeros:
//   1. its image load is %35, numerically equal to the Offset decoration
//      literal (35) in the push block's OpMemberDecorate set -- a blanket
//      operand scan mistook the literal for a use of the image value;
//   2. its OpImageWrite carries a flag-only ZeroExtend image-operand mask
//      (8192, no id operands), which a length-only check refused wholesale.
const META_UINT_COMP = b64(
  'AwIjBwAGAQALAAgAMAAAAAAAAAARAAIAAQAAABEAAgAvAAAAEQACADgAAAALAAYAAQAAAEdMU0wu' +
  'c3RkLjQ1MAAAAAAOAAMAAAAAAAEAAAAPAAgABQAAAAQAAABtYWluAAAAAAwAAAAXAAAAIgAAABAA' +
  'BgAEAAAAEQAAAIAAAAABAAAAAQAAAAMAAwACAAAAwgEAAAUABAAEAAAAbWFpbgAAAAAFAAUACAAA' +
  'AHRocmVhZF9pZAAAAAUACAAMAAAAZ2xfR2xvYmFsSW52b2NhdGlvbklEAAAABQAFABUAAAB1X2lu' +
  'Zm9fdAAAAAAGAAYAFQAAAAAAAABjbGVhcl92YWx1ZQAGAAYAFQAAAAEAAABkc3Rfb2Zmc2V0AAAG' +
  'AAYAFQAAAAIAAABkc3RfZXh0ZW50AAAFAAQAFwAAAHVfaW5mbwAABQADACIAAABkc3QARwAEAAwA' +
  'AAALAAAAHAAAAEcAAwAVAAAAAgAAAEgABQAVAAAAAAAAACMAAAAAAAAASAAFABUAAAABAAAAIwAA' +
  'ABAAAABIAAUAFQAAAAIAAAAjAAAAGAAAAEcAAwAiAAAAGQAAAEcABAAiAAAAIQAAAAAAAABHAAQA' +
  'IgAAACIAAAAAAAAAEwACAAIAAAAhAAMAAwAAAAIAAAAVAAQABgAAACAAAAABAAAAIAAEAAcAAAAH' +
  'AAAABgAAABUABAAJAAAAIAAAAAAAAAAXAAQACgAAAAkAAAADAAAAIAAEAAsAAAABAAAACgAAADsA' +
  'BAALAAAADAAAAAEAAAArAAQACQAAAA0AAAAAAAAAIAAEAA4AAAABAAAACQAAABcABAATAAAACQAA' +
  'AAQAAAAXAAQAFAAAAAYAAAACAAAAHgAFABUAAAATAAAAFAAAABQAAAAgAAQAFgAAAAkAAAAVAAAA' +
  'OwAEABYAAAAXAAAACQAAACsABAAGAAAAGAAAAAIAAAAgAAQAGQAAAAkAAAAGAAAAFAACABwAAAAZ' +
  'AAkAIAAAAAkAAAAFAAAAAAAAAAAAAAAAAAAAAgAAAAAAAAAgAAQAIQAAAAAAAAAgAAAAOwAEACEA' +
  'AAAiAAAAAAAAACsABAAGAAAAJAAAAAEAAAArAAQABgAAACkAAAAAAAAAIAAEACoAAAAJAAAAEwAA' +
  'ACsABAAJAAAALQAAAIAAAAArAAQACQAAAC4AAAABAAAALAAGAAoAAAAvAAAALQAAAC4AAAAuAAAA' +
  'NgAFAAIAAAAEAAAAAAAAAAMAAAD4AAIABQAAADsABAAHAAAACAAAAAcAAABBAAUADgAAAA8AAAAM' +
  'AAAADQAAAD0ABAAJAAAAEAAAAA8AAAB8AAQABgAAABEAAAAQAAAAPgADAAgAAAARAAAAPQAEAAYA' +
  'AAASAAAACAAAAEEABgAZAAAAGgAAABcAAAAYAAAADQAAAD0ABAAGAAAAGwAAABoAAACxAAUAHAAA' +
  'AB0AAAASAAAAGwAAAPcAAwAfAAAAAAAAAPoABAAdAAAAHgAAAB8AAAD4AAIAHgAAAD0ABAAgAAAA' +
  'IwAAACIAAABBAAYAGQAAACUAAAAXAAAAJAAAAA0AAAA9AAQABgAAACYAAAAlAAAAPQAEAAYAAAAn' +
  'AAAACAAAAIAABQAGAAAAKAAAACYAAAAnAAAAQQAFACoAAAArAAAAFwAAACkAAAA9AAQAEwAAACwA' +
  'AAArAAAAYwAFACMAAAAoAAAALAAAAAAgAAD5AAIAHwAAAPgAAgAfAAAA/QABADgAAQA=');

test('R12S: the captured uint meta-clear module converts (decoration-literal id collision + ZeroExtend mask)', () => {
  const r = applySpirvFix(META_UINT_COMP, { storageFormats: '0:0=35' });
  assert.ok(r.applied.includes('storage-texel-buffer-to-2d'), JSON.stringify({ applied: r.applied, log: r.log }));
  const mod = parseSpirv(r.bytes);
  const img = mod.insns.find(i => i.op === 25);
  assert.equal(img.words[2], 1, 'Dim Buffer -> 2D');
  assert.equal(img.words[7], 35, 'the view format is injected into the image type');
  const wgsl = naga.translate({ from: 'spirv', to: 'wgsl', source: r.bytes });
  assert.match(wgsl, /texture_storage_2d<rg32uint,write>/);
  assert.match(wgsl, /textureStore\(/);
});

test('R12S: an id-carrying image operand (Lod) on the write still refuses the conversion', () => {
  // Word-patch the captured module: ZeroExtend mask (8192) -> Lod mask (2)
  // plus the coordinate id appended as the Lod operand (word count +1).
  const src = new Uint32Array(META_UINT_COMP.buffer, META_UINT_COMP.byteOffset, META_UINT_COMP.length / 4);
  let wi = -1;
  for (let i = 5; i < src.length;) { const n = src[i] >> 16, op = src[i] & 0xffff; if (op === 99) { wi = i; break; } i += n || 1; }
  assert.ok(wi > 0, 'fixture has an OpImageWrite');
  assert.equal(src[wi + 4], 8192, 'fixture write carries the ZeroExtend mask');
  const out = new Uint32Array(src.length + 1);
  out.set(src.subarray(0, wi + 4), 0);
  out[wi] = (6 << 16) | 99;   // word count 5 -> 6
  out[wi + 4] = 2;            // Lod mask (id-carrying)
  out[wi + 5] = src[wi + 2];  // Lod operand: the coordinate id
  out.set(src.subarray(wi + 5), wi + 6);
  const r = applySpirvFix(new Uint8Array(out.buffer));
  assert.ok(!r.applied.includes('storage-texel-buffer-to-2d'), JSON.stringify(r.applied));
  assert.ok(r.log.some(l => l.includes('R12S') && l.includes('refused')), JSON.stringify(r.log));
});
