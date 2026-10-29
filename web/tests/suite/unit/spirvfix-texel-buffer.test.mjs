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

// --- Finding 4 (metabindfix 4.32): uses of the image VARIABLE ----------
// A storage texel buffer consumed through OpImageTexelPointer +
// OpAtomicIAdd (the InterlockedAdd-on-RWBuffer shape). The texel
// pointer names the image VARIABLE, not a load result, so the consumer
// trace never sees it: pre-fix, R12S rewrote the image type to 2D,
// left the pointer's scalar coordinate untouched, and emitted invalid
// SPIR-V while logging success. The module below ALSO carries a plain
// load+store (so the traced set is non-empty and the conversion would
// otherwise proceed): R12S must refuse on the variable use alone.
function spvWords(insns) {
  const words = [0x07230203, 0x00010000, 0, 32, 0];
  for (const [op, ...operands] of insns) words.push(((operands.length + 1) << 16) | op, ...operands);
  const b = new Uint8Array(words.length * 4);
  const d = new DataView(b.buffer);
  words.forEach((w, i) => d.setUint32(i * 4, w, true));
  return b;
}
function texelModule(withAtomic) {
  const insns = [
    [17, 1], [17, 47],                       // Shader, ImageBuffer
    [14, 0, 1],                               // OpMemoryModel Logical GLSL450
    [15, 5, 15, 0x6e69616d, 0],               // OpEntryPoint GLCompute %main "main"
    [16, 15, 17, 1, 1, 1],                    // OpExecutionMode LocalSize 1 1 1
    [71, 5, 34, 0], [71, 5, 33, 0],           // OpDecorate %var DescriptorSet 0 / Binding 0
    [21, 1, 32, 0],                           // %uint
    [21, 2, 32, 1],                           // %int
    [25, 3, 1, 5, 0, 0, 0, 2, 34],            // %img = OpTypeImage %uint Buffer ... R32ui
    [32, 4, 0, 3],                            // %ptrImg = OpTypePointer UniformConstant %img
    [32, 6, 11, 1],                           // %ptrScalar = OpTypePointer Image %uint
    [19, 7],                                  // %void
    [33, 8, 7],                               // %fnTy
    [24, 9, 1, 4],                            // %uvec4
    [59, 4, 5, 0],                            // %var = OpVariable %ptrImg UniformConstant
    [43, 2, 10, 0],                           // %c0 = OpConstant %int 0
    [43, 1, 11, 1],                           // %one = OpConstant %uint 1
    [43, 1, 12, 1],                           // %scope = OpConstant %uint 1 (Device)
    [43, 1, 13, 0],                           // %sem = OpConstant %uint 0
    [44, 9, 14, 11, 11, 11, 11],              // %texel = OpConstantComposite %uvec4 (1,1,1,1)
    [54, 7, 15, 0, 8],                        // %main = OpFunction
    [248, 16],                                // OpLabel
    [61, 3, 17, 5],                           // %loaded = OpLoad %img %var
    [99, 17, 10, 14],                         // OpImageWrite %loaded %c0 %texel
  ];
  if (withAtomic) insns.push(
    [60, 6, 18, 5, 10, 10],                   // %tp = OpImageTexelPointer %ptrScalar %var %c0 %c0
    [194, 1, 19, 18, 12, 13, 11],             // %r = OpAtomicIAdd %uint %tp %scope %sem %one
  );
  insns.push([253], [56]);                    // OpReturn, OpFunctionEnd
  return spvWords(insns);
}

test('R12S: a texel-pointer atomic naming the image variable refuses the conversion', () => {
  const mod = texelModule(true);
  const r = applySpirvFix(mod);
  assert.ok(!r.applied.includes('storage-texel-buffer-to-2d'),
    'a module whose image variable feeds OpImageTexelPointer must not be converted');
  assert.ok(r.log.some(l => l.includes('R12S') && l.includes('variable used by op 60')),
    'the refusal names the variable use: ' + JSON.stringify(r.log));
  const out = parseSpirv(r.bytes);
  const img = out.insns.find(i => i.op === 25);
  assert.equal(img.words[2], 5, 'the image type stays Dim=Buffer on refusal');
});

test('R12S: the same module without the atomic converts (the vetting is not over-broad)', () => {
  const r = applySpirvFix(texelModule(false));
  assert.ok(r.applied.includes('storage-texel-buffer-to-2d'), JSON.stringify(r.applied));
  const out = parseSpirv(r.bytes);
  const img = out.insns.find(i => i.op === 25);
  assert.equal(img.words[2], 1, 'Dim Buffer -> 2D');
});
