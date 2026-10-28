// tools/spirvfix/spirvfix.mjs — SPIR-V pre-pass for naga's spv-in front end.
//
// WHAT: a SPIR-V binary rewriter (pure ESM, no imports — runs in node AND in
// the page) that rewrites the module patterns naga 30.2.0 rejects, before
// web/shader.mjs hands the bytes to translate({from:'spirv',...}).
//
// WHY (root causes; verified against the corpus in
// testdata/spirv via tools/spirvfix/gate.mjs):
//   R1 "split-combined-sampler": naga's lookup_sampled_image table has exactly
//      one writer — parse_image_couple, reached only from OpSampledImage. A
//      module that declares a UniformConstant of OpTypeSampledImage (glslang's
//      shape for `uniform sampler2D`) and OpLoads it straight into an
//      OpImageSample* fails with InvalidId(<the load id>) (vkcube's cube.frag:
//      InvalidId(40); corpus frags: InvalidId(14/28/34)). Rewrite: split each
//      combined uniform into image + sampler uniforms, and replace every
//      SampledImage OpLoad with image-load + sampler-load + OpSampledImage
//      (naga's own dialect).
//   R2 "fold-spec-constants": naga rejects OpSpecConstantOp
//      (UnsupportedInstruction(Type, SpecConstantOp); corpus p4). Rewrite:
//      replace every OpSpecConstant* with an equivalent OpConstant* holding
//      the DEFAULT value, folding SpecConstantOp transitively (scalar and
//      vector int/uint/float/bool ALU + composites). Non-default
//      specialization values are baked — documented: the page tier has no
//      specialization API, so defaults are the only values that could ever
//      reach the GPU here.
//   R5 "strip-draw-parameters": naga rejects OpCapability DrawParameters
//      (UnsupportedCapability(DrawParameters)) even when DXVK declares it
//      without using the draw builtins (BaseVertex 4424, BaseInstance 4425,
//      DrawIndex 4426). Rewrite: drop the capability when none of those
//      builtins are decorated. Verified against DXVK D3D11 shaders.
//   R6 "strip-index-decorations": naga rejects fragment entry points carrying
//      OpDecorate Index (32, dual-source blending index) with "Entry point
//      main at Fragment is invalid", on SPIR-V that spirv-val accepts. DXVK
//      emits Index 0 on single-source outputs where it is meaningless.
//      Rewrite: drop the decoration. Verified against DXVK D3D11 shaders.
//   R7 "strip-float-controls": naga rejects every SPV_KHR_float_controls
//      capability (4464 DenormPreserve, 4465 DenormFlushToZero, 4466
//      SignedZeroInfNanPreserve, 4467 RoundingModeRTE, 4468 RoundingModeRTZ)
//      with UnsupportedCapability. vkd3d-proton emits 4466; DXVK's DXBC
//      compiler emits 4465 (+ OpExecutionMode 4460) to state D3D FP
//      semantics (first seen on a real workload in the Tropics D3D11 vertex
//      shader captured 2026-10-26). Rewrite: drop the capabilities, their
//      OpExecutionMode instructions (4459-4463), and the extension line.
//      Safe for rendering: the family only governs denorm / rounding /
//      NaN-inf edge-case behaviour, none of which WGSL can express anyway.
//   R13 "uniform-block-to-storage": the bridge advertises
//      VK_KHR_uniform_buffer_standard_layout, whose std430 uniform-block
//      packing WGSL's uniform address space cannot express (strides and
//      struct sizes there round up to 16) but WGSL's storage address space
//      expresses byte-for-byte. A DescriptorSet-bound uniform block whose
//      layout is storage-expressible but not uniform-expressible is demoted
//      to a read-only storage variable; scalar-packed blocks match neither
//      layout and are left to fail loudly. See the rule body for the exact
//      conditions and the fail-closed trace.
//   R8 "strip-unused-physical-storage-buffer": naga rejects OpCapability
//      PhysicalStorageBufferAddresses (5346/5347) and the
//      SPV_KHR_physical_storage_buffer extension. vkd3d-proton emits them
//      for shaders that never actually use physical pointers (e.g. when a
//      descriptor table is present it compiles all shaders with the
//      PhysicalStorageBuffer64 memory model). Rewrite, only when no
//      OpConvertUToPtr/OpConvertPtrToU and no PhysicalStorageBuffer pointer
//      types exist: drop the capability + extension, rewrite OpMemoryModel
//      addressing to Logical. Safe: with no physical pointers the addressing
//      model is unobservable.
//
// Interface: applySpirvFix(bytes: Uint8Array) -> { bytes, applied, log }.
// applied is [] when the module needs nothing (byte-identical passthrough).
// Idempotent: a second run finds no combined loads and no spec ops.

// --- opcode numbers (Khronos SPIR-V headers, unified1) ----------------------
const Op = {
  Name: 5, MemberName: 6, ExtInstImport: 10, MemoryModel: 14, EntryPoint: 15,
  ExecutionMode: 16,
  TypeVoid: 19, TypeBool: 20, TypeInt: 21, TypeFloat: 22, TypeVector: 23,
  TypeMatrix: 24, TypeImage: 25, TypeSampler: 26, TypeSampledImage: 27,
  TypeArray: 28, TypeRuntimeArray: 29, TypeStruct: 30, TypePointer: 32,
  TypeFunction: 33,
  ConstantTrue: 41, ConstantFalse: 42, Constant: 43, ConstantComposite: 44,
  ConstantSampler: 45, ConstantNull: 46,
  SpecConstantTrue: 48, SpecConstantFalse: 49, SpecConstant: 50,
  SpecConstantComposite: 51, SpecConstantOp: 52,
  Function: 54, FunctionParameter: 55, FunctionEnd: 56, FunctionCall: 57,
  Variable: 59, Load: 61, Store: 62, AccessChain: 65, InBoundsAccessChain: 66, PtrAccessChain: 67,
  Decorate: 71, MemberDecorate: 72, GroupDecorate: 74,
  SampledImage: 86,
  Capability: 17, ImageFetch: 95, Image: 100, Bitcast: 124, UDiv: 134, UMod: 137,
  CompositeConstruct: 80, FunctionParameter: 55,
  ImageSampleImplicitLod: 87, ImageSampleExplicitLod: 88,
  ImageSampleDrefImplicitLod: 89, ImageSampleDrefExplicitLod: 90,
  ImageSampleProjImplicitLod: 91, ImageSampleProjExplicitLod: 92,
  ImageGather: 96, ImageDrefGather: 97,
  CompositeExtract: 81, CopyObject: 83,
  Select: 169, Label: 248,
};
// opcodes whose instruction words start with (result-type, result-id).
// Verified against tools/dxvk/src/include/spirv/include/spirv/unified1/spirv.h:
// result-less opcodes (Store 62, CopyMemory 63/64, ImageWrite 99, ConvertFToS
// 110 HAS a result, EmitVertex 218-221, barriers 224/225, AtomicStore 228,
// control-flow 246-253, ...) are excluded. Unknown opcodes default to
// result-less: framing always uses the word count, so attribution can only
// ever miss analysis input, never misalign the stream.
const HAS_TYPE_ID = new Set([12, 41, 42, 43, 44, 46, 48, 49, 50, 51, 52, 54, 55, 57, 59, 60,
  61, 65, 66, 67, 68, 69, 70, 77, 78, 79, 80, 81, 82, 83, 84, 86]);
for (let op = 87; op <= 251; op++) {
  if (![99, 218, 219, 220, 221, 224, 225, 228, 246, 247, 248, 249, 250, 251,
        252, 253, 255, 256, 257, 258].includes(op)) HAS_TYPE_ID.add(op);
}
HAS_TYPE_ID.add(5381); // OpIsHelperInvocationEXT: result type + result id
// opcodes with a result-id but no result-type (labels; untyped type decls)
const HAS_ID_ONLY = new Set([19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
  31, 32, 33, 45, 248]);
// Opcodes that can consume a pointer-typed operand: stores and copies
// through it, access chains based on it, calls taking it as an argument,
// pointer copies/casts, and select/phi nodes over pointer values. R11 uses
// this to detect a BuiltIn pointer escaping its loads.
const POINTER_USE_OPS = new Set([57, 60, 62, 63, 64, 65, 66, 67, 68, 70, 83, 121, 122, 169, 245]);
const Dec = { SpecId: 1, Binding: 33, DescriptorSet: 34 };
const SC = { UniformConstant: 0, Function: 7 };

// --- module model ------------------------------------------------------------
export function parseSpirv(bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length < 20 || bytes.length % 4 !== 0)
    throw new Error('spirvfix: not a SPIR-V byte buffer');
  const words = new Uint32Array(bytes.buffer, bytes.byteOffset, bytes.length / 4);
  if (words[0] !== 0x07230203) throw new Error('spirvfix: bad SPIR-V magic');
  const header = { magic: words[0], version: words[1], generator: words[2], bound: words[3], schema: words[4] };
  const insns = [];
  let at = 5;
  while (at < words.length) {
    const w = words[at], wc = w >>> 16, op = w & 0xffff;
    if (wc < 1 || at + wc > words.length) throw new Error('spirvfix: truncated instruction at word ' + at);
    const operands = Array.from(words.subarray(at + 1, at + wc));
    let resultType = null, resultId = null;
    if (HAS_TYPE_ID.has(op) && operands.length >= 2) { resultType = operands[0]; resultId = operands[1]; }
    else if (HAS_ID_ONLY.has(op) && operands.length >= 1) { resultId = operands[0]; }
    insns.push({ op, words: operands, resultType, resultId });
    at += wc;
  }
  return { header, insns };
}

export function serializeSpirv(mod) {
  let n = 5;
  for (const i of mod.insns) n += 1 + i.words.length;
  const out = new Uint32Array(n);
  out[0] = mod.header.magic; out[1] = mod.header.version;
  out[2] = mod.header.generator; out[3] = mod.header.bound; out[4] = mod.header.schema;
  let at = 5;
  for (const i of mod.insns) {
    out[at++] = ((i.words.length + 1) << 16) + i.op;
    for (const w of i.words) out[at++] = w >>> 0;
  }
  return new Uint8Array(out.buffer);
}

function strOf(insn, from) {
  const bs = [];
  for (let k = from; k < insn.words.length; k++) {
    let w = insn.words[k];
    for (let b = 0; b < 4; b++) { const c = w & 0xff; if (c === 0) return bs; bs.push(c); w >>>= 8; }
  }
  return bs;
}
function strWords(s) {
  const bs = [...s].map(c => c.charCodeAt(0) & 0xff);
  bs.push(0);
  while (bs.length % 4) bs.push(0);
  const out = [];
  for (let k = 0; k < bs.length; k += 4)
    out.push((bs[k] | (bs[k + 1] << 8) | (bs[k + 2] << 16) | (bs[k + 3] << 24)) >>> 0);
  return out;
}
function decodeStr(insn) {
  let s = '';
  for (const b of strOf(insn, 1)) s += String.fromCharCode(b);
  return s;
}

// Ids for a rewrite round start at the header bound, which by spec exceeds
// every id already used — no scanning needed.
function makeIdSource(mod) {
  let next = mod.header.bound;
  return () => {
    const id = next++;
    mod.header.bound = next;
    return id;
  };
}

// Logical-layout section boundaries (SPIR-V §2.4). Only the pre-function
// prefix is scanned: OpExtInst (12) also appears inside function bodies
// (GLSL.std.450 ALU) and must not move the debug/annotation anchors.
function sections(mod) {
  let firstFunc = -1;
  mod.insns.forEach((ins, idx) => {
    if (ins.op === Op.Function && firstFunc < 0) firstFunc = idx;
  });
  const end = firstFunc < 0 ? mod.insns.length : firstFunc;
  let lastDebug = -1, lastAnnot = -1;
  for (let idx = 0; idx < end; idx++) {
    const op = mod.insns[idx].op;
    if ((op >= 3 && op <= 10) || op === 12) lastDebug = idx;
    else if (op === Op.Decorate || op === Op.MemberDecorate) lastAnnot = idx;
  }
  return { lastDebug, lastAnnot, firstFunc };
}
// --- R1: split combined SampledImage uniforms -------------------------------
// A UniformConstant variable of OpTypeSampledImage becomes an image uniform
// (same set/binding) plus a sampler uniform (same set, first free binding).
// Every function-body OpLoad of SampledImage type is replaced in place by
// image-load + sampler-load + OpSampledImage, reusing the old result id so
// all consumers (implicit/explicit LOD, bias, grad, Proj, Dref, Gather) work
// untouched. Returns true when anything changed.
export function splitCombinedSamplers(mod, log) {
  const byId = new Map();
  for (const ins of mod.insns) if (ins.resultId != null) byId.set(ins.resultId, ins);

  const combined = [];
  for (const ins of mod.insns) {
    if (ins.op !== Op.Variable || ins.words[2] !== SC.UniformConstant) continue;
    const ptr = byId.get(ins.words[0]);
    if (!ptr || ptr.op !== Op.TypePointer) continue;
    const sampled = byId.get(ptr.words[2]);
    if (sampled && sampled.op === Op.TypeSampledImage)
      combined.push({ varIns: ins, sampledTy: sampled.resultId, imageTy: sampled.words[1] });
  }
  if (!combined.length) return false;
  const loads = mod.insns.filter(ins => {
    if (ins.op !== Op.Load || !inFunction(mod, ins)) return false;
    const t = byId.get(ins.resultType);
    return t && t.op === Op.TypeSampledImage;
  });
  if (!loads.length) return false;

  const decos = mod.insns.filter(i => i.op === Op.Decorate);
  const setOf = id => decos.find(d => d.words[0] === id && d.words[1] === Dec.DescriptorSet)?.words[2] ?? 0;
  const bindOf = id => decos.find(d => d.words[0] === id && d.words[1] === Dec.Binding)?.words[2] ?? 0;
  const maxBind = new Map();
  for (const d of decos) {
    if (d.words[1] !== Dec.Binding) continue;
    const s = setOf(d.words[0]);
    maxBind.set(s, Math.max(maxBind.get(s) ?? -1, d.words[2]));
  }

  const newId = makeIdSource(mod);
  const addAnnot = [], addType = [];
  const dropIdx = new Set();
  const pairs = new Map();

  const findType = (op, eq) => mod.insns.find(i => i.op === op && eq(i))?.resultId ?? null;
  let samplerTy = findType(Op.TypeSampler, () => true);
  if (samplerTy == null) {
    samplerTy = newId();
    addType.push({ op: Op.TypeSampler, words: [samplerTy], resultType: null, resultId: samplerTy });
  }
  const ptrFor = elem => {
    const hit = findType(Op.TypePointer, i => i.words[1] === SC.UniformConstant && i.words[2] === elem);
    if (hit != null) return hit;
    const id = newId();
    addType.push({ op: Op.TypePointer, words: [id, SC.UniformConstant, elem], resultType: null, resultId: id });
    return id;
  };

  for (const c of combined) {
    const varId = c.varIns.resultId;
    const set = setOf(varId), imgBind = bindOf(varId);
    const sampBind = (maxBind.get(set) ?? imgBind) + 1;
    maxBind.set(set, sampBind);
    const imgVar = newId(), sampVar = newId();
    addType.push({ op: Op.Variable, words: [ptrFor(c.imageTy), imgVar, SC.UniformConstant], resultType: ptrFor(c.imageTy), resultId: imgVar });
    addType.push({ op: Op.Variable, words: [ptrFor(samplerTy), sampVar, SC.UniformConstant], resultType: ptrFor(samplerTy), resultId: sampVar });
    addAnnot.push({ op: Op.Decorate, words: [imgVar, Dec.DescriptorSet, set], resultType: null, resultId: null });
    addAnnot.push({ op: Op.Decorate, words: [imgVar, Dec.Binding, imgBind], resultType: null, resultId: null });
    addAnnot.push({ op: Op.Decorate, words: [sampVar, Dec.DescriptorSet, set], resultType: null, resultId: null });
    addAnnot.push({ op: Op.Decorate, words: [sampVar, Dec.Binding, sampBind], resultType: null, resultId: null });
    pairs.set(varId, { imageVar: imgVar, samplerVar: sampVar, sampledTy: c.sampledTy, imageTy: c.imageTy });
    mod.insns.forEach((ins, i) => {
      if (ins.op === Op.Variable && ins.resultId === varId) dropIdx.add(i);
      if ((ins.op === Op.Decorate || ins.op === Op.Name) && ins.words[0] === varId) dropIdx.add(i);
    });
    const nm = decodeStr(mod.insns.find(i => i.op === Op.Name && i.words[0] === varId) ?? { words: [] });
    if (nm) {
      addType.push({ op: Op.Name, words: [imgVar, ...strWords(nm)], resultType: null, resultId: null });
    }
    // Preserve the guest descriptor source through Naga's WGSL translation.
    // wgsl-utils reads this generated identifier; the new Binding decoration
    // remains the WebGPU slot, while sampling state comes from imgBind.
    addType.push({ op: Op.Name, words: [sampVar, ...strWords(`bw_combined_g${set}_b${imgBind}_v${varId}`)], resultType: null, resultId: null });
    log.push(`spirvfix R1: split combined uniform %${varId} -> image %${imgVar} (set ${set}, binding ${imgBind}) + sampler %${sampVar} (binding ${sampBind})`);
  }

  // SPIR-V 1.4+ lists descriptor globals in OpEntryPoint interfaces.
  // Preserve that interface when the original combined variable is deleted.
  // https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#OpEntryPoint
  for (const ins of mod.insns) {
    if (ins.op !== Op.EntryPoint) continue;
    let start = 2; // execution model, function ID, then NUL-terminated name
    while (start < ins.words.length) {
      const word = ins.words[start++];
      if ([0, 8, 16, 24].some(shift => ((word >>> shift) & 255) === 0)) break;
    }
    ins.words = ins.words.slice(0, start).concat(ins.words.slice(start).flatMap(id => {
      const pair = pairs.get(id);
      return pair ? [pair.imageVar, pair.samplerVar] : [id];
    }));
  }

  const kept = mod.insns.filter((_, i) => !dropIdx.has(i));
  const aPos = (() => { const s = sections({ insns: kept }); return s.lastAnnot + 1; })();
  kept.splice(Math.max(aPos, 0), 0, ...addAnnot);
  const tPos = (() => { const s = sections({ insns: kept }); return s.firstFunc < 0 ? kept.length : s.firstFunc; })();
  kept.splice(tPos, 0, ...addType.filter(i => i.op !== Op.Name));
  const dPos = (() => { const s = sections({ insns: kept }); return s.lastDebug + 1; })();
  kept.splice(Math.max(dPos, 0), 0, ...addType.filter(i => i.op === Op.Name));
  mod.insns = kept;
  byId.clear();
  for (const ins of mod.insns) if (ins.resultId != null) byId.set(ins.resultId, ins);

  let n = 0;
  const first = pairs.values().next().value;
  for (const load of loads) {
    // Load words are [result-type, result-id, pointer, ...]: the pointer is
    // words[2]. Fall back to the first pair when the source is a temporary.
    const pair = pairs.get(load.words[2]) ?? first;
    if (!pair) continue;
    const imgLoad = newId(), sampLoad = newId();
    const idx = mod.insns.indexOf(load);
    if (idx < 0) continue;
    mod.insns.splice(idx, 1,
      { op: Op.Load, words: [pair.imageTy, imgLoad, pair.imageVar], resultType: pair.imageTy, resultId: imgLoad },
      { op: Op.Load, words: [samplerTy, sampLoad, pair.samplerVar], resultType: samplerTy, resultId: sampLoad },
      { op: Op.SampledImage, words: [load.resultType, load.resultId, imgLoad, sampLoad], resultType: load.resultType, resultId: load.resultId });
    byId.set(imgLoad, mod.insns[idx]);
    byId.set(sampLoad, mod.insns[idx + 1]);
    byId.set(load.resultId, mod.insns[idx + 2]);
    n++;
  }
  log.push(`spirvfix R1: rewrote ${n} SampledImage load(s) into OpSampledImage couples`);
  return n > 0;
}

function inFunction(mod, target) {
  let depth = 0;
  for (const ins of mod.insns) {
    if (ins === target) return depth > 0;
    if (ins.op === Op.Function) depth++;
    else if (ins.op === Op.FunctionEnd) depth--;
  }
  return false;
}
// --- R2: fold spec constants -------------------------------------------------
// OpSpecConstant* with SpecConstantOp trees become plain OpConstants holding
// default spec values (the page tier has no specialization API). Scalar and
// vector int/uint/float/bool ALU + select + composites are folded; anything
// else is left as-is and reported. Note: instruction `words` below always
// include the result type+id (see parseSpirv), so e.g. an OpConstant's value
// starts at words[2] and an OpSpecConstantOp's inner opcode is words[2].
function scalarTypeOf(byId, typeId) {
  const t = byId.get(typeId);
  if (!t) return null;
  if (t.op === Op.TypeBool) return { kind: 'bool' };
  if (t.op === Op.TypeInt) return { kind: t.words[2] ? 'sint' : 'uint', width: t.words[1] };
  if (t.op === Op.TypeFloat) return { kind: 'float', width: t.words[1] };
  return null;
}
function constValue(byId, id) {
  const ins = byId.get(id);
  if (!ins) return null;
  if (ins.op === Op.CopyObject) return constValue(byId, ins.words[2]);
  if (ins.op === Op.ConstantTrue || ins.op === Op.SpecConstantTrue)
    return { type: ins.words[0], elems: [{ kind: 'bool', value: true }] };
  if (ins.op === Op.ConstantFalse || ins.op === Op.SpecConstantFalse)
    return { type: ins.words[0], elems: [{ kind: 'bool', value: false }] };
  if (ins.op !== Op.Constant && ins.op !== Op.SpecConstant &&
      ins.op !== Op.ConstantComposite && ins.op !== Op.SpecConstantComposite) return null;
  const t = byId.get(ins.words[0]);
  if (!t) return null;
  if (t.op === Op.TypeVector) {
    const n = t.words[2];
    if (ins.words.length - 2 !== n) return null;
    const elems = [];
    for (const c of ins.words.slice(2)) {
      const v = constValue(byId, c);
      if (!v || v.elems.length !== 1) return null;
      elems.push(v.elems[0]);
    }
    return { type: ins.words[0], elems };
  }
  const s = scalarTypeOf(byId, ins.words[0]);
  if (!s || ins.words.length - 2 < 1) return null;
  return { type: ins.words[0], elems: [{ ...s, value: decodeScalar(s, ins.words.slice(2)) }] };
}
function decodeScalar(s, words) {
  if (s.kind === 'bool') return words[0] !== 0;
  if (s.kind === 'float') {
    if (s.width === 64) {
      const b = new Uint8Array(8); new Uint32Array(b.buffer).set(words.slice(0, 2));
      return new Float64Array(b.buffer)[0];
    }
    const b = new Uint8Array(4); new Uint32Array(b.buffer)[0] = words[0];
    return new Float32Array(b.buffer)[0];
  }
  let v = 0n;
  for (let i = words.length - 1; i >= 0; i--) v = (v << 32n) | BigInt(words[i]);
  if (s.kind === 'sint') {
    const bits = BigInt(s.width);
    if (v >> (bits - 1n)) v -= 1n << bits;
  }
  return v;
}
function encodeScalar(s, value) {
  if (s.kind === 'bool') return [value ? 1 : 0];
  if (s.kind === 'float') {
    if (s.width === 64) {
      const b = new Uint8Array(8); new Float64Array(b.buffer)[0] = value;
      return Array.from(new Uint32Array(b.buffer));
    }
    const b = new Uint8Array(4); new Float32Array(b.buffer)[0] = value;
    return [new Uint32Array(b.buffer)[0]];
  }
  let v = BigInt(value);
  v &= (1n << BigInt(s.width)) - 1n;
  const out = [];
  for (let w = 0; w < s.width / 32; w++) { out.push(Number(v & 0xffffffffn)); v >>= 32n; }
  return out;
}
// Inner-opcode numbers (SPIR-V unified1).
const SOP = {
  SNegate: 126, FNegate: 127, IAdd: 128, FAdd: 129, ISub: 130, FSub: 131,
  IMul: 132, FMul: 133, UDiv: 134, SDiv: 135, FDiv: 136, UMod: 137,
  SRem: 138, SMod: 139, FRem: 140, FMod: 141, IEqual: 170, INotEqual: 171,
  UGreaterThan: 172, SGreaterThan: 173, UGreaterThanEqual: 174,
  SGreaterThanEqual: 175, ULessThan: 176, SLessThan: 177,
  ULessThanEqual: 178, SLessThanEqual: 179, FOrdEqual: 180,
  FUnordEqual: 181, FOrdNotEqual: 182, FOrdLessThan: 184,
  FUnordLessThan: 185, FOrdGreaterThan: 186, FUnordGreaterThan: 187,
  FOrdLessThanEqual: 188, FUnordLessThanEqual: 189,
  FOrdGreaterThanEqual: 190, FUnordGreaterThanEqual: 191,
  ShiftRightLogical: 194, ShiftRightArithmetic: 195, ShiftLeftLogical: 196,
  BitwiseOr: 197, BitwiseXor: 198, BitwiseAnd: 199, Not: 200,
  BitFieldUExtract: 203,
  LogicalEqual: 164, LogicalNotEqual: 165, LogicalOr: 166,
  LogicalAnd: 167, LogicalNot: 168, Select: 169,
};

function evalLane(inner, xs) {
  // One component lane. xs: [{kind, width, value}] (bool: boolean; float:
  // Number; int/uint: BigInt). Returns {kind, width, value} or null.
  const k = xs[0].kind, w = xs[0].width ?? 32;
  const num = x => (x.kind === 'float' ? x.value : Number(x.value));
  const big = x => BigInt(x.kind === 'bool' ? (x.value ? 1 : 0) : x.value);
  const M = (1n << BigInt(w)) - 1n;
  const wrapU = v => ((v % (M + 1n)) + (M + 1n)) % (M + 1n);
  try {
    switch (inner) {
      case SOP.BitFieldUExtract: {
        if (xs.length !== 3) return null;
        const offset = big(xs[1]), count = big(xs[2]);
        if (offset < 0n || count < 0n || offset + count > BigInt(w)) return null;
        return { kind: 'uint', width: w, value: (big(xs[0]) >> offset) & ((1n << count) - 1n) };
      }
      case SOP.Select: {
        if (xs.length !== 3) return null;
        const c = xs[0].kind === 'bool' ? xs[0].value : big(xs[0]) !== 0n;
        const p = c ? xs[1] : xs[2];
        return { kind: p.kind, width: p.width ?? 1, value: p.value };
      }
      case SOP.IAdd: case SOP.FAdd:
        return k === 'float'
          ? { kind: k, width: w, value: num(xs[0]) + num(xs[1]) }
          : { kind: k, width: w, value: wrapU(big(xs[0]) + big(xs[1])) };
      case SOP.ISub: case SOP.FSub:
        return k === 'float'
          ? { kind: k, width: w, value: num(xs[0]) - num(xs[1]) }
          : { kind: k, width: w, value: wrapU(big(xs[0]) - big(xs[1])) };
      case SOP.IMul: case SOP.FMul:
        return k === 'float'
          ? { kind: k, width: w, value: num(xs[0]) * num(xs[1]) }
          : { kind: k, width: w, value: wrapU(big(xs[0]) * big(xs[1])) };
      case SOP.UDiv: {
        if (k !== 'uint' || big(xs[1]) === 0n) return null;
        return { kind: k, width: w, value: wrapU(big(xs[0]) / big(xs[1])) };
      }
      case SOP.SDiv: {
        if (k !== 'sint' || big(xs[1]) === 0n) return null;
        /** @param {bigint} v @returns {bigint} */
        const si = v => { v = wrapU(v); const h = (M + 1n) >> 1n; return v >= h ? v - (M + 1n) : v; };
        return { kind: k, width: w, value: wrapU(si(big(xs[0])) / si(big(xs[1]))) };
      }
      case SOP.FDiv:
        if (num(xs[1]) === 0) return null;
        return { kind: 'float', width: w, value: num(xs[0]) / num(xs[1]) };
      case SOP.UMod: {
        if (k !== 'uint' || big(xs[1]) === 0n) return null;
        return { kind: k, width: w, value: big(xs[0]) % big(xs[1]) };
      }
      case SOP.SRem: {
        if (k !== 'sint' || big(xs[1]) === 0n) return null;
        /** @param {bigint} v @returns {bigint} */
        const si = v => { v = wrapU(v); const h = (M + 1n) >> 1n; return v >= h ? v - (M + 1n) : v; };
        return { kind: k, width: w, value: wrapU(si(big(xs[0])) % si(big(xs[1]))) };
      }
      case SOP.SMod: {
        if (k !== 'sint' || big(xs[1]) === 0n) return null;
        /** @param {bigint} v @returns {bigint} */
        const si = v => { v = wrapU(v); const h = (M + 1n) >> 1n; return v >= h ? v - (M + 1n) : v; };
        const a = si(big(xs[0])), b = si(big(xs[1]));
        let r = a % b;
        if (r !== 0n && (r < 0n) !== (b < 0n)) r += b;
        return { kind: k, width: w, value: wrapU(r) };
      }
      case SOP.FRem:
        return { kind: 'float', width: w, value: num(xs[0]) % num(xs[1]) };
      case SOP.FMod: {
        const a = num(xs[0]), b = num(xs[1]);
        if (b === 0) return null;
        return { kind: 'float', width: w, value: a - b * Math.floor(a / b) };
      }
      case SOP.SNegate:
        if (k === 'float') return null;
        return { kind: k, width: w, value: wrapU(-big(xs[0])) };
      case SOP.FNegate: return { kind: 'float', width: w, value: -num(xs[0]) };
      case SOP.Not:
        if (k === 'float') return null;
        return { kind: k, width: w, value: wrapU(~big(xs[0])) };
      case SOP.ShiftLeftLogical:
        if (k === 'float') return null;
        return { kind: k, width: w, value: wrapU(big(xs[0]) << (big(xs[1]) & 63n)) };
      case SOP.ShiftRightLogical: {
        if (k === 'float') return null;
        const v = wrapU(big(xs[0])) >> (big(xs[1]) & 63n);
        return { kind: k, width: w, value: v };
      }
      case SOP.ShiftRightArithmetic: {
        if (k !== 'sint') return null;
        /** @param {bigint} v @returns {bigint} */
        const si = v => { v = wrapU(v); const h = (M + 1n) >> 1n; return v >= h ? v - (M + 1n) : v; };
        const s = big(xs[1]) & 63n;
        return { kind: k, width: w, value: wrapU(si(big(xs[0])) >> s) };
      }
      case SOP.BitwiseOr:
        if (k === 'float') return null;
        return { kind: k, width: w, value: wrapU(big(xs[0]) | big(xs[1])) };
      case SOP.BitwiseXor:
        if (k === 'float') return null;
        return { kind: k, width: w, value: wrapU(big(xs[0]) ^ big(xs[1])) };
      case SOP.BitwiseAnd:
        if (k === 'float') return null;
        return { kind: k, width: w, value: wrapU(big(xs[0]) & big(xs[1])) };
      case SOP.IEqual: case SOP.LogicalEqual:
        return { kind: 'bool', width: 1, value: big(xs[0]) === big(xs[1]) && num(xs[0]) === num(xs[1]) ? num(xs[0]) === num(xs[1]) : big(xs[0]) === big(xs[1]) };
      case SOP.INotEqual: case SOP.LogicalNotEqual:
        return { kind: 'bool', width: 1, value: !(big(xs[0]) === big(xs[1]) && String(num(xs[0])) === String(num(xs[1]))) ? big(xs[0]) !== big(xs[1]) : num(xs[0]) !== num(xs[1]) };
      case SOP.UGreaterThan: case SOP.SGreaterThan:
        return { kind: 'bool', width: 1, value: k === 'float' ? num(xs[0]) > num(xs[1]) : (k === 'uint' ? wrapU(big(xs[0])) > wrapU(big(xs[1])) : si(big(xs[0])) > si(big(xs[1]))) };
      case SOP.UGreaterThanEqual: case SOP.SGreaterThanEqual:
        return { kind: 'bool', width: 1, value: k === 'float' ? num(xs[0]) >= num(xs[1]) : (k === 'uint' ? wrapU(big(xs[0])) >= wrapU(big(xs[1])) : si(big(xs[0])) >= si(big(xs[1]))) };
      case SOP.ULessThan: case SOP.SLessThan:
        return { kind: 'bool', width: 1, value: k === 'float' ? num(xs[0]) < num(xs[1]) : (k === 'uint' ? wrapU(big(xs[0])) < wrapU(big(xs[1])) : si(big(xs[0])) < si(big(xs[1]))) };
      case SOP.ULessThanEqual: case SOP.SLessThanEqual:
        return { kind: 'bool', width: 1, value: k === 'float' ? num(xs[0]) <= num(xs[1]) : (k === 'uint' ? wrapU(big(xs[0])) <= wrapU(big(xs[1])) : si(big(xs[0])) <= si(big(xs[1]))) };
      case SOP.FOrdEqual: case SOP.FUnordEqual:
        return { kind: 'bool', width: 1, value: num(xs[0]) === num(xs[1]) };
      case SOP.FOrdNotEqual:
        return { kind: 'bool', width: 1, value: num(xs[0]) !== num(xs[1]) && num(xs[0]) === num(xs[0]) && num(xs[1]) === num(xs[1]) };
      case SOP.FOrdLessThan: case SOP.FUnordLessThan:
        return { kind: 'bool', width: 1, value: num(xs[0]) < num(xs[1]) };
      case SOP.FOrdGreaterThan: case SOP.FUnordGreaterThan:
        return { kind: 'bool', width: 1, value: num(xs[0]) > num(xs[1]) };
      case SOP.FOrdLessThanEqual: case SOP.FUnordLessThanEqual:
        return { kind: 'bool', width: 1, value: num(xs[0]) <= num(xs[1]) };
      case SOP.FOrdGreaterThanEqual: case SOP.FUnordGreaterThanEqual:
        return { kind: 'bool', width: 1, value: num(xs[0]) >= num(xs[1]) };
      case SOP.LogicalOr:
        return { kind: 'bool', width: 1, value: !!(xs[0].value || xs[1].value) };
      case SOP.LogicalAnd:
        return { kind: 'bool', width: 1, value: !!(xs[0].value && xs[1].value) };
      case SOP.LogicalNot:
        return { kind: 'bool', width: 1, value: !xs[0].value };
      default: return null;
    }
    function si(v) { v = wrapU(v); const h = (M + 1n) >> 1n; return v >= h ? v - (M + 1n) : v; }
  } catch { return null; }
}
function evalSpecOp(byId, inner, operandIds, resultType) {
  const t = byId.get(resultType);
  const count = (t && t.op === Op.TypeVector) ? t.words[2] : 1;
  const args = operandIds.map(id => constValue(byId, id));
  if (args.some(a => !a)) return null;
  const lanes = [];
  for (let lane = 0; lane < count; lane++) {
    const xs = args.map(a => (a.elems.length === 1 ? a.elems[0] : (a.elems[lane] ?? null)));
    if (xs.some(x => !x)) return null;
    const r = evalLane(inner, xs);
    if (!r) return null;
    lanes.push(r);
  }
  return { elems: lanes };
}
function rebuild(ins, op, resultType, resultId, operands) {
  // operands here are the FULL post-opcode words INCLUDING result type+id
  // (the parseSpirv convention), e.g. [type, id, value...] for OpConstant.
  // For id-only ops (ConstantTrue/False) pass resultType=null and operands=[id].
  const body = [];
  if (resultType) body.push(resultType);
  if (resultId) body.push(resultId);
  body.push(...operands);
  ins.op = op;
  ins.resultType = resultType || null;
  ins.resultId = resultId || null;
  ins.words = body;
}

function emitFoldedConstant(ins, r, cx) {
  // cx: { mod, byId, newId }. Scalar folds rewrite `ins` in place; vector
  // folds materialize one scalar OpConstant per lane just before `ins` (which
  // lives in the constants section, so logical layout stays valid) and rebuild
  // `ins` as an OpConstantComposite over them.
  const t = ins.resultType, id = ins.resultId;
  if (r.elems.length === 1 && r.elems[0].kind === 'bool') {
    // Typed form [type, id] (the SPIR-V grammar's IdResultType + IdResult;
    // naga 30.2.0's spv-in reads it; the bool type is the folded
    // SpecConstantOp's own result type).
    rebuild(ins, r.elems[0].value ? Op.ConstantTrue : Op.ConstantFalse, t, id, []);
    return;
  }
  if (r.elems.length === 1) {
    rebuild(ins, Op.Constant, t, id, encodeScalar(r.elems[0], r.elems[0].value));
    return;
  }
  const vecTy = cx.byId.get(t);
  const compTy = vecTy && vecTy.op === Op.TypeVector ? vecTy.words[1] : null;
  if (compTy == null) throw new Error('spirvfix: vector fold without vector result type');
  const lanes = r.elems.map(lane => {
    const cid = cx.newId();
    let li;
    if (lane.kind === 'bool') {
      li = { op: lane.value ? Op.ConstantTrue : Op.ConstantFalse, words: [compTy, cid], resultType: compTy, resultId: cid };
    } else {
      const w = encodeScalar({ kind: lane.kind, width: lane.width ?? 32 }, lane.value);
      li = { op: Op.Constant, words: [compTy, cid, ...w], resultType: compTy, resultId: cid };
    }
    cx.byId.set(cid, li);
    return li;
  });
  const at = cx.mod.insns.indexOf(ins);
  if (at < 0) throw new Error('spirvfix: fold target not in module');
  cx.mod.insns.splice(at, 0, ...lanes);
  rebuild(ins, Op.ConstantComposite, t, id, lanes.map(l => l.resultId));
}

// A vector-typed composite's constituents must be scalars (SPIR-V §3.28.9):
// when a folded SpecConstantComposite nests vector composites (e.g. v4 made
// of two v2s), flatten them to their scalar lane ids. Struct/array results
// keep nested composites untouched.
function flattenVecConstituents(byId, typeId, ids) {
  const t = byId.get(typeId);
  if (!t || t.op !== Op.TypeVector) return ids;
  const out = [];
  const push = id => {
    const d = byId.get(id);
    if (d && d.op === Op.ConstantComposite) {
      const dt = byId.get(d.resultType);
      if (dt && dt.op === Op.TypeVector) {
        for (const c of d.words.slice(2)) push(c);
        return;
      }
    }
    out.push(id);
  };
  for (const id of ids) push(id);
  return out;
}

export function foldSpecConstants(mod, log, specOverrides) {
  // Build specId -> override value map. Overrides come from the bridge's
  // VkSpecializationInfo capture (constantID -> raw bytes). When present,
  // the override value replaces the SPIR-V default.
  const overrideMap = new Map();
  if (specOverrides) {
    for (const o of specOverrides) {
      // o.value is a Uint8Array; convert to uint32 words (little-endian)
      const words = [];
      for (let i = 0; i < o.value.length; i += 4) {
        let w = 0;
        for (let b = 0; b < 4 && i + b < o.value.length; b++) w |= o.value[i + b] << (8 * b);
        words.push(w >>> 0);
      }
      overrideMap.set(o.constantID, words);
    }
  }
  // Map resultId -> specId from OpDecorate SpecId
  const specIdOf = new Map();
  for (const ins of mod.insns) {
    if (ins.op === 71 && ins.words[1] === 1 && ins.words.length >= 3) { // OpDecorate, SpecId
      specIdOf.set(ins.words[0], ins.words[2]);
    }
  }
  const byId = new Map();
  for (const ins of mod.insns) if (ins.resultId != null) byId.set(ins.resultId, ins);
  if (!mod.insns.some(i => i.op >= Op.SpecConstantTrue && i.op <= Op.SpecConstantOp)) return false;
  const newId = makeIdSource(mod);
  const cx = { mod, byId, newId };
  let folded = 0, converted = 0;
  for (const ins of mod.insns) {
    if (ins.op === Op.SpecConstant) {
      const specId = specIdOf.get(ins.resultId);
      const override = specId !== undefined ? overrideMap.get(specId) : undefined;
      const valueWords = override || ins.words.slice(2);
      rebuild(ins, Op.Constant, ins.resultType, ins.resultId, valueWords);
      if (override) log.push('spirvfix R2: applied specialization override for specId ' + specId);
      converted++;
    } else if (ins.op === Op.SpecConstantTrue) {
      const specId = specIdOf.get(ins.resultId);
      const override = specId !== undefined ? overrideMap.get(specId) : undefined;
      if (override && override[0] === 0) {
        rebuild(ins, Op.ConstantFalse, ins.resultType, ins.resultId, []);
      } else {
        rebuild(ins, Op.ConstantTrue, ins.resultType, ins.resultId, []);
      }
      converted++;
    } else if (ins.op === Op.SpecConstantFalse) {
      const specId = specIdOf.get(ins.resultId);
      const override = specId !== undefined ? overrideMap.get(specId) : undefined;
      if (override && override[0] !== 0) {
        rebuild(ins, Op.ConstantTrue, ins.resultType, ins.resultId, []);
      } else {
        rebuild(ins, Op.ConstantFalse, ins.resultType, ins.resultId, []);
      }
      converted++;
    }
  }
  let progress = true;
  while (progress) {
    progress = false;
    for (const ins of mod.insns) {
      if (ins.op === Op.SpecConstantComposite) {
        const ready = ins.words.slice(2).every(c => {
          const d = byId.get(c);
          return d && (d.op === Op.Constant || d.op === Op.ConstantComposite ||
            d.op === Op.ConstantTrue || d.op === Op.ConstantFalse);
        });
        if (!ready) continue;
        const flat = flattenVecConstituents(byId, ins.resultType, ins.words.slice(2));
        rebuild(ins, Op.ConstantComposite, ins.resultType, ins.resultId, flat);
        folded++; progress = true;
      } else if (ins.op === Op.SpecConstantOp) {
        const inner = ins.words[2], args = ins.words.slice(3);
        if (!args.every(c => constValue(byId, c))) continue;
        let r = null;
        try { r = evalSpecOp(byId, inner, args, ins.resultType); } catch { r = null; }
        if (!r) continue;
        try { emitFoldedConstant(ins, r, cx); } catch { continue; }
        folded++; progress = true;
      }
    }
  }
  const remaining = mod.insns.filter(i => i.op === Op.SpecConstantOp || i.op === Op.SpecConstantComposite);
  if (!remaining.length) {
    mod.insns = mod.insns.filter(ins => !(ins.op === Op.Decorate && ins.words[1] === Dec.SpecId));
  }
  log.push(`spirvfix R2: folded ${folded} op(s), pinned ${converted} plain spec const(s) to defaults` +
    (remaining.length ? `, ${remaining.length} UNSUPPORTED (left as-is)` : ''));
  return true;
}
// WebGPU has no point size or point-coordinate builtin. Drop PointSize stores
// for every topology and substitute the center for PointCoord reads. True
// point sprites need quad expansion in the renderer, which is out of scope.
function stripPointBuiltins(mod, log) {
  const decorations = mod.insns.filter(i => i.op === Op.Decorate && i.words[1] === 11 && [1, 16].includes(i.words[2]));
  if (!decorations.length) return false;
  const byId = new Map(mod.insns.filter(i => i.resultId != null).map(i => [i.resultId, i]));
  const ids = new Set(decorations.map(i => i.words[0]));
  const rewrites = [];
  const pointLoadCount = (() => {
    let count = 0;
    const ids = new Set(decorations.map(i => i.words[0]));
    for (const ins of mod.insns) if (ins.op === Op.Load && ids.has(ins.words[2])) count++;
    return count;
  })();
  const removedStores = new Set();
  const constants = [];
  const constantByKey = new Map();
  const newId = makeIdSource(mod);
  const constant = (typeId, value) => {
    const key = `${typeId}:${value}`;
    if (constantByKey.has(key)) return constantByKey.get(key);
    const type = byId.get(typeId);
    if (!type) throw new Error(`missing point builtin value type %${typeId}`);
    let id, ins;
    if (type.op === Op.TypeFloat && type.words[1] === 32) {
      const bits = value === 0.5 ? 0x3f000000 : 0;
      const found = mod.insns.find(i => i.op === Op.Constant && i.resultType === typeId && i.words[2] === bits);
      if (found) { constantByKey.set(key, found.resultId); return found.resultId; }
      id = newId(); ins = { op: Op.Constant, words: [typeId, id, bits], resultType: typeId, resultId: id };
    } else if (type.op === Op.TypeVector && type.words[2] === 2) {
      const scalarType = byId.get(type.words[1]);
      if (!scalarType || scalarType.op !== Op.TypeFloat || scalarType.words[1] !== 32)
        throw new Error('PointCoord must be vec2<f32>');
      const component = constant(type.words[1], value);
      id = newId(); ins = { op: Op.ConstantComposite, words: [typeId, id, component, component], resultType: typeId, resultId: id };
    } else throw new Error(`unsupported point builtin value type %${typeId}`);
    constants.push(ins); constantByKey.set(key, id); return id;
  };

  for (const deco of decorations) {
    const id = deco.words[0], builtin = deco.words[2];
    const variable = byId.get(id), pointer = variable && byId.get(variable.words[0]);
    const storage = builtin === 1 ? 3 : 1;
    if (!variable || variable.op !== Op.Variable || !pointer || pointer.op !== Op.TypePointer ||
        variable.words[2] !== storage || pointer.words[1] !== storage) {
      log.push(`spirvfix: left point builtin %${id} unchanged (unsupported interface shape)`);
      return false;
    }
    for (const ins of mod.insns) {
      if (ins === variable || ins.op === Op.Decorate || ins.op === 72 || ins.op === Op.Name || ins.op === Op.EntryPoint) continue;
      if (builtin === 1 && ins.op === 62 && ins.words[0] === id) {
        removedStores.add(ins); continue;
      }
      if (ins.op === 61 && ins.words[2] === id) {
        const zero = constant(ins.resultType, builtin === 16 ? 0.5 : 0);
        rewrites.push({ ins, zero }); continue;
      }
      if ([65, 66, 67, 68, 70, 83].includes(ins.op) && ins.words[2] === id) {
        log.push(`spirvfix: left point builtin %${id} unchanged (pointer is used outside direct loads/stores)`);
        return false;
      }
    }
  }

  for (const { ins, zero } of rewrites) {
    ins.op = Op.CopyObject;
    ins.words = [ins.resultType, ins.resultId, zero];
  }
  mod.insns = mod.insns.filter(i => !removedStores.has(i) &&
    !((i.op === Op.Variable || i.op === Op.Decorate || i.op === Op.Name) && ids.has(i.resultId ?? i.words[0])));
  for (const entry of mod.insns.filter(i => i.op === Op.EntryPoint)) {
    let start = 2;
    while (start < entry.words.length) {
      const word = entry.words[start++];
      if ([0, 8, 16, 24].some(shift => ((word >>> shift) & 255) === 0)) break;
    }
    entry.words = entry.words.slice(0, start).concat(entry.words.slice(start).filter(id => !ids.has(id)));
  }
  const firstVariable = mod.insns.findIndex(i => i.op === Op.Variable || i.op === Op.Function);
  mod.insns.splice(firstVariable < 0 ? mod.insns.length : firstVariable, 0, ...constants);
  log.push(`spirvfix: stripped ${decorations.filter(i => i.words[2] === 1).length} PointSize output(s) and replaced ${pointLoadCount} point builtin load(s)`);
  return true;
}

// Demote continues helper execution; Kill terminates it. Lower only a demote
// immediately followed by an acyclic chain of branch-only blocks ending in
// void return: no subsequent derivatives, writes or helper queries can differ.
function lowerTailDemotes(mod, log, protectedKills = new Set()) {
  const entryFunctions = new Set(mod.insns.filter(i => i.op === Op.EntryPoint && i.words[0] === 4).map(i => i.words[1]));
  const demotes = [];
  let functionId = null;
  for (const ins of mod.insns) {
    if (ins.op === Op.Function) functionId = ins.resultId;
    else if (ins.op === Op.FunctionEnd) functionId = null;
    else if (ins.op === 5380 && entryFunctions.has(functionId)) demotes.push(ins);
  }
  if (!demotes.length) return false;
  const blocks = new Map(); let block = null;
  for (const i of mod.insns) {
    if (i.op === Op.Label) { block = []; blocks.set(i.words[0], block); }
    else if (i.op === Op.FunctionEnd) block = null;
    else if (block) block.push(i);
  }
  const pureReturn = (id, seen = new Set()) => {
    if (seen.has(id)) return false;
    seen.add(id);
    const tail = blocks.get(id);
    if (!tail) return false;
    if (tail.length === 1)
      return tail[0].op === 253 || (tail[0].op === 249 && pureReturn(tail[0].words[0], seen));
    // DXVK ends fixed-function alpha testing with an output-only FClamp.
    // Demoted fragments cannot publish that output, and this tail has no
    // derivatives, helper queries or externally observable writes.
    if (tail.length !== 4) return false;
    const [load, clamp, store, ret] = tail;
    const output = mod.insns.find(i => i.op === Op.Variable && i.words[1] === load.words[2]);
    const glsl = mod.insns.find(i => i.op === 11 && i.words[0] === clamp.words[2]
      && new TextDecoder().decode(new Uint8Array(strOf(i, 1))) === 'GLSL.std.450');
    return load.op === Op.Load && output?.words[2] === 3 && clamp.op === 12 && !!glsl
      && clamp.words[3] === 43 && clamp.words[4] === load.words[1]
      && clamp.words[0] === load.words[0] && store.op === Op.Store
      && store.words[0] === load.words[2] && store.words[1] === clamp.words[1]
      && ret.op === 253;
  };
  const tails = [];
  for (const d of demotes) {
    const next = mod.insns[mod.insns.indexOf(d) + 1];
    if (next?.op === 249 && pureReturn(next.words[0])) tails.push({ demote: d, branch: next });
  }
  if (!tails.length) return false;
  for (const { demote, branch } of tails) {
    demote.op = 252; demote.words = [];
    protectedKills.add(demote);
    mod.insns.splice(mod.insns.indexOf(branch), 1); // Kill is the block terminator, replacing branch
  }
  if (!mod.insns.some(i => i.op === 5380 || i.op === 5381)) {
    mod.insns = mod.insns.filter(i => !(i.op === 17 && i.words[0] === 5379) &&
      !(i.op === 10 && new TextDecoder().decode(new Uint8Array(strOf(i, 0))) === 'SPV_EXT_demote_to_helper_invocation'));
  }
  log.push(`spirvfix: lowered ${tails.length} terminal demote(s) with no helper continuation`);
  return true;
}

// Delay mid-shader fragment discard until each entry return so derivatives,
// helper checks and later samples still execute. Non-entry functions share a
// private flag because their function-local state cannot reach the entry tail.
function lowerMidShaderDiscards(mod, log, protectedKills = new Set()) {
  const entryFunctions = new Set(mod.insns.filter(i => i.op === Op.EntryPoint && i.words[0] === 4).map(i => i.words[1]));
  const functions = [];
  let fn = null, block = null;
  for (const ins of mod.insns) {
    if (ins.op === Op.Function) {
      fn = { id: ins.resultId, declaration: ins, blocks: [] };
      functions.push(fn); block = null;
    } else if (ins.op === Op.FunctionEnd) {
      fn = null; block = null;
    } else if (fn && ins.op === Op.Label) {
      block = { id: ins.words[0], instructions: [ins] };
      fn.blocks.push(block);
    } else if (block) block.instructions.push(ins);
  }

  const successors = instructions => {
    const term = instructions.at(-1);
    if (term?.op === 249) return [term.words[0]];
    if (term?.op === 250) return [term.words[1], term.words[2]];
    if (term?.op === 251) return [term.words[1], ...term.words.slice(3).filter((_, i) => i % 2 === 0)];
    return [];
  };
  const regions = new Map();
  for (const fn of functions) {
    const labels = new Map(fn.blocks.map(b => [b.id, b]));
    for (const header of fn.blocks) {
      const merge = header.instructions.find(i => i.op === 247 || i.op === 246);
      if (!merge) continue;
      const loop = merge.op === 246;
      const mergeId = merge.words[0], continueId = loop ? merge.words[1] : null;
      const starts = successors(header.instructions);
      const inside = new Set(), todo = [...starts];
      while (todo.length) {
        const id = todo.pop();
        if (id === mergeId || id === continueId || inside.has(id)) continue;
        const current = labels.get(id);
        if (!current) continue;
        inside.add(id); todo.push(...successors(current.instructions));
      }
      const list = regions.get(fn.id) ?? [];
      list.push({ target: loop ? continueId : mergeId, labels: inside });
      regions.set(fn.id, list);
    }
  }

  const resumeTarget = (fn, block) => {
    const candidates = (regions.get(fn.id) || []).filter(r => r.labels.has(block.id));
    if (candidates.length) return candidates.sort((a, b) => a.labels.size - b.labels.size)[0].target;
    const returns = fn.blocks.filter(b => b.instructions.at(-1)?.op === 253);
    if (returns.length) return returns.at(-1).id;
    return null;
  };
  const sites = [];
  const isHelperReads = mod.insns.filter(i => i.op === 5381);
  for (const f of functions) for (const b of f.blocks) for (const ins of b.instructions) {
    if (protectedKills.has(ins)) continue;
    if (![5380, 252, 4416].includes(ins.op)) continue;
    const entry = entryFunctions.has(f.id);
    // An entry Kill with no structured continuation is already the tail.
    if (entry && ins.op !== 5380 && !(regions.get(f.id) || []).some(r => r.labels.has(b.id))) continue;
    const target = ins.op === 5380 ? null : resumeTarget(f, b);
    if (ins.op !== 5380 && target == null) {
      if (entry) continue; // entry Kill outside structured flow is already the tail
      const returnType = mod.insns.find(i => i.resultId === f.declaration.words[0]);
      if (returnType?.op !== Op.TypeVoid) {
        log.push(`spirvfix: left discard in function %${f.id} unchanged (no void continuation block)`);
        continue;
      }
    }
    sites.push({ fn: f, block: b, ins, entry, target });
  }
  if (!sites.length && !isHelperReads.length) return false;

  const newId = makeIdSource(mod);
  const byId = new Map(mod.insns.filter(i => i.resultId != null).map(i => [i.resultId, i]));
  const synthesizedReturns = new Map();
  for (const site of sites) {
    if (site.entry || site.target != null) continue;
    let target = synthesizedReturns.get(site.fn.id);
    if (target == null) {
      target = newId(); synthesizedReturns.set(site.fn.id, target);
      const end = mod.insns.findIndex(i => i.op === Op.FunctionEnd && mod.insns.indexOf(i) > mod.insns.indexOf(site.fn.declaration));
      mod.insns.splice(end < 0 ? mod.insns.length : end, 0,
        { op: Op.Label, words: [target], resultType: null, resultId: target },
        { op: 253, words: [], resultType: null, resultId: null });
    }
    site.target = target;
  }
  let boolType = mod.insns.find(i => i.op === Op.TypeBool)?.resultId;
  const typeAdds = [];
  let boolTypeAdd = null;
  if (boolType == null) {
    boolType = newId();
    boolTypeAdd = { op: Op.TypeBool, words: [boolType], resultType: null, resultId: boolType };
  }
  const pointerFor = storage => {
    const found = mod.insns.find(i => i.op === Op.TypePointer && i.words[1] === storage && i.words[2] === boolType);
    if (found) return found.resultId;
    const id = newId();
    typeAdds.push({ op: Op.TypePointer, words: [id, storage, boolType], resultType: null, resultId: id });
    return id;
  };
  const privatePointer = pointerFor(6);
  const localPointer = sites.length ? pointerFor(7) : null;
  const falseId = mod.insns.find(i => i.op === Op.ConstantFalse && i.resultType === boolType)?.resultId ?? newId();
  const trueId = mod.insns.find(i => i.op === Op.ConstantTrue && i.resultType === boolType)?.resultId ?? newId();
  const constantAdds = [];
  if (!byId.has(falseId)) constantAdds.push({ op: Op.ConstantFalse, words: [boolType, falseId], resultType: boolType, resultId: falseId });
  if (!byId.has(trueId)) constantAdds.push({ op: Op.ConstantTrue, words: [boolType, trueId], resultType: boolType, resultId: trueId });
  const helperState = newId();
  const privateVariable = { op: Op.Variable, words: [privatePointer, helperState, 6, falseId], resultType: privatePointer, resultId: helperState };
  const localVariables = new Map();
  if (sites.length) for (const id of entryFunctions) localVariables.set(id, newId());

  if (boolTypeAdd) {
    const firstConstant = mod.insns.findIndex(i => [41, 42, 43, 44, 45, 46, 48, 49, 50, 51, 52, 59, 54].includes(i.op));
    mod.insns.splice(firstConstant < 0 ? mod.insns.length : firstConstant, 0, boolTypeAdd);
  }
  const firstVariable = mod.insns.findIndex(i => i.op === Op.Variable || i.op === Op.Function);
  mod.insns.splice(firstVariable < 0 ? mod.insns.length : firstVariable, 0, ...constantAdds);
  const firstGlobalVariable = mod.insns.findIndex(i => i.op === Op.Variable || i.op === Op.Function);
  mod.insns.splice(firstGlobalVariable < 0 ? mod.insns.length : firstGlobalVariable, 0, ...typeAdds);
  const firstFunction = mod.insns.findIndex(i => i.op === Op.Function);
  mod.insns.splice(firstFunction < 0 ? mod.insns.length : firstFunction, 0, privateVariable);

  for (const f of functions) {
    if (!entryFunctions.has(f.id)) continue;
    const localId = localVariables.get(f.id);
    const firstBlock = f.blocks[0];
    if (!firstBlock) continue;
    const labelAt = mod.insns.indexOf(firstBlock.instructions[0]);
    let insertAt = labelAt + 1;
    while (mod.insns[insertAt]?.op === Op.Variable && mod.insns[insertAt].words[2] === SC.Function) insertAt++;
    const setup = [];
    if (localId != null) setup.push(
      { op: Op.Variable, words: [localPointer, localId, SC.Function], resultType: localPointer, resultId: localId },
      { op: Op.Store, words: [localId, falseId], resultType: null, resultId: null });
    setup.push({ op: Op.Store, words: [helperState, falseId], resultType: null, resultId: null });
    mod.insns.splice(insertAt, 0, ...setup);
  }

  for (const site of sites) {
    const localId = site.entry ? localVariables.get(site.fn.id) : null;
    const stores = [
      { op: Op.Store, words: [helperState, trueId], resultType: null, resultId: null },
      ...(localId == null ? [] : [{ op: Op.Store, words: [localId, trueId], resultType: null, resultId: null }]),
    ];
    const index = mod.insns.indexOf(site.ins);
    if (index < 0) continue;
    if (site.ins.op === 5380) mod.insns.splice(index, 1, ...stores);
    else mod.insns.splice(index, 1, ...stores, { op: 249, words: [site.target], resultType: null, resultId: null });
  }

  for (const read of isHelperReads) {
    read.op = Op.Load;
    read.words = [boolType, read.resultId, helperState];
  }

  const returns = mod.insns.filter(i => i.op === 253 && functions.some(f => entryFunctions.has(f.id) && f.blocks.some(b => b.instructions.includes(i))));
  for (const ret of returns) {
    const localId = [...localVariables.entries()].find(([id]) => functions.find(f => f.id === id)?.blocks.some(b => b.instructions.includes(ret)))?.[1];
    if (localId == null) continue;
    const loadLocal = newId(), loadGlobal = newId(), condition = newId(), killLabel = newId(), mergeLabel = newId();
    const at = mod.insns.indexOf(ret);
    mod.insns.splice(at, 1,
      { op: Op.Load, words: [boolType, loadLocal, localId], resultType: boolType, resultId: loadLocal },
      { op: Op.Load, words: [boolType, loadGlobal, helperState], resultType: boolType, resultId: loadGlobal },
      { op: 166, words: [boolType, condition, loadLocal, loadGlobal], resultType: boolType, resultId: condition },
      { op: 247, words: [mergeLabel, 0], resultType: null, resultId: null },
      { op: 250, words: [condition, killLabel, mergeLabel], resultType: null, resultId: null },
      { op: Op.Label, words: [killLabel], resultType: null, resultId: killLabel },
      { op: 252, words: [], resultType: null, resultId: null },
      { op: Op.Label, words: [mergeLabel], resultType: null, resultId: mergeLabel },
      ret);
  }

  if (!mod.insns.some(i => i.op === 5380 || i.op === 5381)) {
    mod.insns = mod.insns.filter(i => !(i.op === 17 && i.words[0] === 5379) &&
      !(i.op === 10 && new TextDecoder().decode(new Uint8Array(strOf(i, 0))) === 'SPV_EXT_demote_to_helper_invocation'));
  }
  log.push(`spirvfix: delayed ${sites.length} mid-shader discard operation(s)${isHelperReads.length ? ` and lowered ${isHelperReads.length} helper query/query(ies)` : ''}`);
  return true;
}

// R5 "strip-unused-draw-parameters": naga 30.2.0 rejects
// OpCapability DrawParameters (4427) with UnsupportedCapability, even when
// DXVK declares it but never uses the draw builtins (BaseVertex 4424,
// BaseInstance 4425, DrawIndex 4426). If the capability is present but none
// of those builtins are decorated, drop the capability. Safe because the
// shader cannot reference what it never declares.
function stripUnusedDrawParameters(mod, log) {
  const hasCap = mod.insns.some(i => i.op === 17 && i.words[0] === 4427);
  if (!hasCap) return false;
  const used = mod.insns.some(i => i.op === Op.Decorate && i.words[1] === 11
    && [4424, 4425, 4426].includes(i.words[2]));
  if (used) return false;
  mod.insns = mod.insns.filter(i => !(i.op === 17 && i.words[0] === 4427));
  log.push('spirvfix: stripped unused OpCapability DrawParameters');
  return true;
}

// DXVK computes SV_VertexID/InstanceID by subtracting BaseVertex/BaseInstance
// from the corresponding WebGPU builtin. WebGPU's vertex_index and
// instance_index already include the draw's base values, so make the
// subtractands zero and keep VertexIndex/InstanceIndex untouched. This means
// SV_InstanceID includes StartInstanceLocation, the one known D3D deviation.
function replaceDrawParameterBuiltins(mod, log) {
  const baseBuiltins = new Map(mod.insns
    .filter(i => i.op === Op.Decorate && i.words[1] === 11 && [4424, 4425, 4426].includes(i.words[2]))
    .map(i => [i.words[0], i.words[2]]));
  if (!baseBuiltins.size) return false;

  const byId = new Map(mod.insns.filter(i => i.resultId != null).map(i => [i.resultId, i]));
  const variables = new Set();
  const loads = [];
  for (const id of baseBuiltins.keys()) {
    const variable = byId.get(id), pointer = variable && byId.get(variable.words[0]);
    if (!variable || variable.op !== Op.Variable || variable.words[2] !== 1 ||
        !pointer || pointer.op !== Op.TypePointer || pointer.words[1] !== 1) {
      log.push(`spirvfix: left DrawParameters builtin %${id} unchanged (unsupported interface shape)`);
      return false;
    }
    variables.add(id);
    for (const ins of mod.insns) {
      if (ins.op === Op.Load && ins.words[2] === id) {
        const scalar = byId.get(ins.resultType);
        if (!inFunction(mod, ins) || ins.resultType !== pointer.words[2] || !scalar ||
            scalar.op !== Op.TypeInt || scalar.words[1] !== 32) {
          log.push(`spirvfix: left DrawParameters builtin %${id} unchanged (expected a direct 32-bit integer load)`);
          return false;
        }
        loads.push(ins);
      } else if (ins.op !== Op.Variable && ins.op !== Op.Decorate && ins.op !== Op.Name && ins.op !== Op.EntryPoint &&
          ins.words.includes(id)) {
        log.push(`spirvfix: left DrawParameters builtin %${id} unchanged (pointer is used outside OpLoad)`);
        return false;
      }
    }
  }

  const newId = makeIdSource(mod);
  const zeroByType = new Map();
  const constants = [];
  for (const load of loads) {
    let zero = zeroByType.get(load.resultType);
    if (zero == null) {
      zero = newId();
      zeroByType.set(load.resultType, zero);
      constants.push({ op: Op.Constant, words: [load.resultType, zero, 0], resultType: load.resultType, resultId: zero });
    }
    load.op = Op.CopyObject;
    load.words = [load.resultType, load.resultId, zero];
  }

  mod.insns = mod.insns.filter(i => !((i.op === Op.Variable && variables.has(i.resultId)) ||
    ((i.op === Op.Decorate || i.op === Op.Name) && variables.has(i.words[0])) ||
    (i.op === 17 && i.words[0] === 4427)));
  for (const entry of mod.insns.filter(i => i.op === Op.EntryPoint)) {
    let start = 2;
    while (start < entry.words.length) {
      const word = entry.words[start++];
      if ([0, 8, 16, 24].some(shift => ((word >>> shift) & 255) === 0)) break;
    }
    entry.words = entry.words.slice(0, start).concat(entry.words.slice(start).filter(id => !variables.has(id)));
  }
  const firstVariable = mod.insns.findIndex(i => i.op === Op.Variable);
  mod.insns.splice(firstVariable < 0 ? sections(mod).firstFunc : firstVariable, 0, ...constants);
  log.push(`spirvfix: replaced ${loads.length} BaseVertex/BaseInstance/DrawIndex load(s) with zero`);
  return true;
}

// R6 "strip-index-decorations": naga 30.2.0 rejects fragment entry points
// carrying OpDecorate Index (32, dual-source blending index) with
// "Entry point main at Fragment is invalid", even on valid SPIR-V
// (spirv-val passes). DXVK emits Index 0 on single-source outputs where it
// is meaningless. Drop the decoration; dual-source blending is not used by
// the D3D11/D3D10 paths here.
function stripIndexDecorations(mod, log) {
  const found = mod.insns.filter(i => i.op === Op.Decorate && i.words[1] === 32);
  if (!found.length) return false;
  mod.insns = mod.insns.filter(i => !(i.op === Op.Decorate && i.words[1] === 32));
  log.push(`spirvfix: stripped ${found.length} OpDecorate Index decoration(s)`);
  return true;
}

// R7 "strip-float-controls": naga 30.2.0 rejects all five SPV_KHR_float_controls
// capabilities (4464-4468) with UnsupportedCapability (each verified against
// the vendored naga). Drop the capabilities, their OpExecutionMode
// instructions (4459-4463), and the SPV_KHR_float_controls extension line;
// without them naga never sees the family. Rendering-safe: these declarations
// only govern denorm flushing/preservation, rounding mode, and NaN/inf
// signed-zero preservation -- FP edge-case behaviour WGSL does not expose.
function stripFloatControls(mod, log) {
  const CAPS = { 4464: 'DenormPreserve', 4465: 'DenormFlushToZero', 4466: 'SignedZeroInfNanPreserve', 4467: 'RoundingModeRTE', 4468: 'RoundingModeRTZ' };
  const MODES = new Set([4459, 4460, 4461, 4462, 4463]);
  const foundCaps = [...new Set(mod.insns.filter(i => i.op === 17 && CAPS[i.words[0]]).map(i => CAPS[i.words[0]]))];
  const modes = mod.insns.filter(i => i.op === Op.ExecutionMode && MODES.has(i.words[1]));
  const exts = mod.insns.filter(i => i.op === Op.ExtInstImport &&
    new TextDecoder().decode(new Uint8Array(strOf(i, 0))) === 'SPV_KHR_float_controls');
  if (!foundCaps.length && !modes.length && !exts.length) return false;
  mod.insns = mod.insns.filter(i => !((i.op === 17 && CAPS[i.words[0]]) ||
    (i.op === Op.ExecutionMode && MODES.has(i.words[1])) ||
    (i.op === Op.ExtInstImport &&
      new TextDecoder().decode(new Uint8Array(strOf(i, 0))) === 'SPV_KHR_float_controls')));
  const what = [foundCaps.length ? 'capabilities ' + foundCaps.join('/') : null,
    modes.length ? modes.length + ' execution mode(s)' : null,
    exts.length ? exts.length + ' OpExtension(s)' : null].filter(Boolean).join(' + ');
  log.push('spirvfix: stripped float-controls declarations (' + what + ')');
  return true;
}

// R8 "strip-unused-physical-storage-buffer": naga rejects OpCapability
// PhysicalStorageBufferAddresses (5346, or 5347 as emitted by vkd3d-shader)
// and the SPV_KHR_physical_storage_buffer OpExtension with
// UnsupportedCapability/UnsupportedExtension. vkd3d-proton emits them even
// for shaders that never touch a physical pointer (no OpConvertUToPtr /
// OpConvertPtrToU, no PhysicalStorageBuffer StorageClass pointer types) —
// observed on D3D12 shaders compiled with a descriptor table present, where
// the whole pipeline gets the PhysicalStorageBuffer64 memory model. When the
// capability is unused, drop it and the extension and rewrite the addressing
// model to Logical (memory model untouched). If physical pointers ARE used,
// leave the module alone (naga cannot translate it; the frame is skipped).
function stripUnusedPhysicalStorageBuffer(mod, log) {
  const hasCap = mod.insns.some(i => i.op === 17 && (i.words[0] === 5346 || i.words[0] === 5347));
  if (!hasCap) return false;
  const usesPsb = mod.insns.some(i =>
    i.op === 120 || i.op === 121 || // OpConvertUToPtr / OpConvertPtrToU
    (i.op === Op.TypePointer && i.words[1] === 5349)); // PhysicalStorageBuffer storage class (operands: [resultId, storageClass, pointee])
  if (usesPsb) {
    log.push('spirvfix: PhysicalStorageBufferAddresses is actually used; not stripping');
    return false;
  }
  const extName = insn => new TextDecoder().decode(new Uint8Array(strOf(insn, 0)));
  const exts = mod.insns.filter(i => i.op === 10 && extName(i) === 'SPV_KHR_physical_storage_buffer');
  let memRewritten = false;
  for (const i of mod.insns) {
    if (i.op === Op.MemoryModel && i.words[0] === 5348) { // PhysicalStorageBuffer64
      i.words[0] = 0; // Logical
      memRewritten = true;
    }
  }
  mod.insns = mod.insns.filter(i => !((i.op === 17 && (i.words[0] === 5346 || i.words[0] === 5347)) ||
    (i.op === 10 && extName(i) === 'SPV_KHR_physical_storage_buffer')));
  const what = ['capability',
    exts.length ? exts.length + ' OpExtension(s)' : null,
    memRewritten ? 'OpMemoryModel->Logical' : null].filter(Boolean).join(' + ');
  log.push('spirvfix: stripped unused PhysicalStorageBufferAddresses (' + what + ')');
  return true;
}

// Vulkan permits alternative image variables at one binding. WebGPU requires
// unique bindings. When captured specialization values select just one branch,
// remove the provably unreachable branch and its unused descriptor variables.
// Dynamic choices remain unchanged and are rejected rather than guessed.
function pruneSpecializedAliases(mod, log) {
  const bindings = new Set();
  let collision = false;
  for (const d of mod.insns.filter(i => i.op === Op.Decorate && i.words[1] === Dec.Binding)) {
    const set = mod.insns.find(i => i.op === Op.Decorate && i.words[0] === d.words[0] && i.words[1] === Dec.DescriptorSet)?.words[2] || 0;
    const key = set + ':' + d.words[2];
    if (bindings.has(key)) collision = true;
    bindings.add(key);
  }
  if (!collision) return false;
  const byId = new Map(mod.insns.filter(i => i.resultId != null).map(i => [i.resultId, i]));
  const constants = new Set();
  const supported = new Set([128, 130, 132, 164, 165, 166, 167, 168, 170, 171,
    172, 173, 174, 175, 176, 177, 178, 179, 194, 195, 196, 197, 198, 199, 200, 203]);
  let progress = true;
  while (progress) {
    progress = false;
    let inFunc = false;
    for (const ins of mod.insns) {
      if (ins.op === Op.Function) inFunc = true;
      if (ins.op === Op.FunctionEnd) inFunc = false;
      if (!inFunc || constants.has(ins)) continue;
      if (ins.op === Op.Select) {
        const cond = constValue(byId, ins.words[2]);
        if (cond?.elems.length !== 1 || cond.elems[0].kind !== 'bool') continue;
        rebuild(ins, Op.CopyObject, ins.resultType, ins.resultId, [ins.words[cond.elems[0].value ? 3 : 4]]);
        progress = true;
      }
      if (ins.op === Op.CopyObject) {
        const value = constValue(byId, ins.words[2]);
        if (!value || value.elems.length !== 1) continue;
        emitFoldedConstant(ins, value, { mod, byId, newId: makeIdSource(mod) });
        constants.add(ins); progress = true;
      } else if (supported.has(ins.op)) {
        const ty = byId.get(ins.resultType);
        if (!ty || (ty.op !== Op.TypeInt && ty.op !== Op.TypeBool)) continue;
        const value = evalSpecOp(byId, ins.op, ins.words.slice(2), ins.resultType);
        if (!value) continue;
        emitFoldedConstant(ins, value, { mod, byId, newId: makeIdSource(mod) });
        constants.add(ins); progress = true;
      }
    }
  }
  // Constants belong before functions in SPIR-V's logical layout.
  mod.insns = mod.insns.filter(i => !constants.has(i));
  mod.insns.splice(sections(mod).firstFunc, 0, ...constants);
  const remove = new Set();
  for (let n = 0; n < mod.insns.length; ++n) {
    const ins = mod.insns[n];
    if (ins.op !== 250 || mod.insns[n - 1]?.op === 246) continue; // Keep loops' structural edges.
    const cond = constValue(byId, ins.words[0]);
    if (cond?.elems.length !== 1 || cond.elems[0].kind !== 'bool') continue;
    const target = ins.words[cond.elems[0].value ? 1 : 2];
    if (mod.insns[n - 1]?.op === 247) remove.add(mod.insns[n - 1]);
    ins.op = 249; ins.words = [target];
  }
  let blocks = null, block = null;
  const functions = [];
  for (const ins of mod.insns) {
    if (ins.op === Op.Function) { blocks = new Map(); functions.push(blocks); }
    if (ins.op === Op.FunctionEnd) { blocks = null; block = null; }
    if (ins.op === Op.Label) { block = []; blocks.set(ins.words[0], block); }
    if (block) block.push(ins);
  }
  for (const blocks of functions) {
    const reachable = new Set(), todo = [blocks.keys().next().value];
    while (todo.length) {
      const id = todo.pop();
      if (reachable.has(id)) continue;
      reachable.add(id);
      for (const ins of blocks.get(id) || []) {
        if (ins.op === 249) todo.push(ins.words[0]);
        if (ins.op === 250) todo.push(ins.words[1], ins.words[2]);
        if (ins.op === 251) todo.push(ins.words[1], ...ins.words.slice(3).filter((_, i) => i % 2 === 0));
        if (ins.op === 246) todo.push(ins.words[0], ins.words[1]);
        if (ins.op === 247 && !remove.has(ins)) todo.push(ins.words[0]);
      }
    }
    for (const [id, instructions] of blocks) {
      if (!reachable.has(id)) { instructions.forEach(i => remove.add(i)); continue; }
      for (const ins of instructions) if (ins.op === 245) {
        const pairs = [];
        for (let k = 2; k < ins.words.length; k += 2)
          if (reachable.has(ins.words[k + 1])) pairs.push(ins.words[k], ins.words[k + 1]);
        if (pairs.length === 2) rebuild(ins, Op.CopyObject, ins.resultType, ins.resultId, [pairs[0]]);
        else ins.words = ins.words.slice(0, 2).concat(pairs);
      }
    }
  }
  mod.insns = mod.insns.filter(i => !remove.has(i));
  const referenced = new Set();
  let inFunc = false;
  for (const ins of mod.insns) {
    if (ins.op === Op.Function) inFunc = true;
    if (ins.op === Op.FunctionEnd) inFunc = false;
    if (inFunc) for (const word of ins.words.slice(ins.resultType ? 2 : 0)) referenced.add(word);
  }
  const unused = new Set(mod.insns.filter(i => i.op === Op.Variable && i.words[2] === SC.UniformConstant
    && !referenced.has(i.resultId)).map(i => i.resultId));
  if (!unused.size) return constants.size > 0 || remove.size > 0;
  mod.insns = mod.insns.filter(i => !(i.op === Op.Variable && unused.has(i.resultId))
    && !((i.op === Op.Name || i.op === Op.Decorate) && unused.has(i.words[0])));
  for (const ins of mod.insns) if (ins.op === Op.EntryPoint) {
    let start = 2;
    while (start < ins.words.length) {
      const word = ins.words[start++];
      if ([0, 8, 16, 24].some(shift => ((word >>> shift) & 255) === 0)) break;
    }
    ins.words = ins.words.slice(0, start).concat(ins.words.slice(start).filter(id => !unused.has(id)));
  }
  log.push(`spirvfix: pruned specialized branches and ${unused.size} unused descriptor alias(es)`);
  return true;
}

// R10 "flat-integer-varyings": naga rejects a Vertex output / Fragment input
// whose Location carries an integer type without the Flat decoration
// ("Entry point main at Vertex is invalid"). DXVK's D3D11 MSAA-resolve meta
// VS writes `int o_instance` plain. Integer varyings can only ever be flat,
// so adding the decoration is semantics-preserving.
function flatIntegerVaryings(mod, log) {
  const models = new Set(mod.insns.filter(i => i.op === Op.EntryPoint).map(i => i.words[0]));
  const byId = new Map(mod.insns.filter(i => i.resultId != null).map(i => [i.resultId, i]));
  const isInt = (typeId, depth = 0) => {
    const t = byId.get(typeId);
    if (!t || depth > 4) return false;
    if (t.op === Op.TypeInt) return true;
    if (t.op === Op.TypeVector || t.op === Op.TypeArray) return isInt(t.words[1], depth + 1);
    return false;
  };
  const decos = new Map();
  for (const i of mod.insns) if (i.op === Op.Decorate) {
    if (!decos.has(i.words[0])) decos.set(i.words[0], new Set());
    decos.get(i.words[0]).add(i.words[1]);
  }
  const add = [];
  for (const v of mod.insns) {
    if (v.op !== Op.Variable) continue;
    const sc = v.words[2];
    if (!((sc === 3 && models.has(0)) || (sc === 1 && models.has(4)))) continue; // VS Output / FS Input
    const d = decos.get(v.resultId);
    if (!d || !d.has(30) /* Location */ || d.has(14) /* Flat */ || d.has(11) /* BuiltIn */) continue;
    const ptr = byId.get(v.words[0]);
    if (!ptr || ptr.op !== Op.TypePointer || !isInt(ptr.words[2])) continue;
    add.push({ op: Op.Decorate, words: [v.resultId, 14] });
  }
  if (!add.length) return false;
  const { lastDebug, lastAnnot } = sections(mod);
  const at = lastAnnot >= 0 ? lastAnnot : lastDebug;
  mod.insns.splice(at + 1, 0, ...add);
  log.push(`spirvfix: added Flat to ${add.length} integer varying(s)`);
  return true;
}

// R11 "fragment-layer-to-zero": WebGPU has no Layer fragment input. DXVK's
// resolve meta pass reads gl_Layer in the FS (fed by its geometry shader,
// which the WebGPU backend does not run). Substitute constant 0 — correct for
// non-array targets — and drop the interface variable. OpCapability Geometry
// is left untouched: naga ignores it once no Geometry-only builtin remains.
function fragmentLayerToZero(mod, log) {
  if (!mod.insns.some(i => i.op === Op.EntryPoint && i.words[0] === 4)) return false;
  const ids = new Set(mod.insns.filter(i => i.op === Op.Decorate && i.words[1] === 11 && i.words[2] === 9).map(i => i.words[0]));
  if (!ids.size) return false;
  const byId = new Map(mod.insns.filter(i => i.resultId != null).map(i => [i.resultId, i]));
  const loads = [];
  for (const id of ids) {
    const v = byId.get(id), ptr = v && byId.get(v.words[0]);
    if (!v || v.op !== Op.Variable || v.words[2] !== 1 || !ptr || ptr.op !== Op.TypePointer) {
      log.push(`spirvfix: left Layer builtin %${id} unchanged (unsupported interface shape)`);
      return false;
    }
    for (const ins of mod.insns) {
      if (ins === v || ins.op === Op.Decorate || ins.op === Op.Name || ins.op === Op.EntryPoint) continue;
      if (ins.op === Op.Load && ins.words[2] === id) { loads.push(ins); continue; }
      // The pointer may only be consumed by loads. Scan only the opcodes
      // that can consume a pointer-typed operand: a raw words.includes(id)
      // over every instruction false-positives on literal operands that
      // numerically equal the id (DXVK copy_color_2d has its Layer variable
      // at id 32 and an OpTypeInt of width 32). In a valid module no other
      // opcode can reference an Input pointer id -- any other use would be
      // a type error the translator rejects anyway.
      if (POINTER_USE_OPS.has(ins.op) && ins.words.includes(id)) {
        log.push(`spirvfix: left Layer builtin %${id} unchanged (pointer used outside OpLoad)`);
        return false;
      }
    }
  }
  const newId = makeIdSource(mod);
  const zeroByType = new Map(), constants = [];
  for (const ld of loads) {
    let z = zeroByType.get(ld.resultType);
    if (z == null) {
      const t = byId.get(ld.resultType);
      if (!t || t.op !== Op.TypeInt || t.words[1] !== 32) {
        log.push('spirvfix: left Layer builtin unchanged (expected a 32-bit integer load)');
        return false;
      }
      z = newId(); zeroByType.set(ld.resultType, z);
      constants.push({ op: Op.Constant, words: [ld.resultType, z, 0], resultType: ld.resultType, resultId: z });
    }
  }
  for (const ld of loads) {
    ld.op = Op.CopyObject;
    ld.words = [ld.resultType, ld.resultId, zeroByType.get(ld.resultType)];
  }
  mod.insns = mod.insns.filter(i => !((i.op === Op.Variable && ids.has(i.resultId)) ||
    ((i.op === Op.Decorate || i.op === Op.Name) && ids.has(i.words[0]))));
  for (const entry of mod.insns.filter(i => i.op === Op.EntryPoint)) {
    let start = 2;
    while (start < entry.words.length) {
      const word = entry.words[start++];
      if ([0, 8, 16, 24].some(shift => ((word >>> shift) & 255) === 0)) break;
    }
    entry.words = entry.words.slice(0, start).concat(entry.words.slice(start).filter(id => !ids.has(id)));
  }
  // The zero constants go at the END of the global section (just before the
  // first function). Splicing before the first OpVariable can place an
  // OpConstant ahead of the OpTypeInt it references in glslang layouts that
  // interleave pointer/image types and variables before the scalar types
  // (DXVK copy_color_1d declares %int after the image variables), which
  // validators reject: the constant's type id is not yet defined there.
  const firstFunc = sections(mod).firstFunc;
  mod.insns.splice(firstFunc < 0 ? mod.insns.length : firstFunc, 0, ...constants);
  log.push(`spirvfix: replaced ${loads.length} fragment Layer load(s) with zero`);
  return true;
}


// --- R12: texel-buffer images -> wrapped 2D sampled images -------------------
// WebGPU has no texel buffers and naga rejects Dim=Buffer images outright
// (UnsupportedCapability(SampledBuffer)) — which is what kills DXVK's HUD
// data view (a uniform texel buffer, R8_UINT x 16384) during Portal device
// creation. A UNIFORM texel buffer (OpTypeImage Dim=Buffer, Sampled=1) is
// emulated as a 2D sampled texture of row width TEXEL_WRAP_WIDTH (8192, the
// guaranteed WebGPU 2D dimension): the page tier materialises each buffer
// view as a 2D texture whose texel (i mod W, i div W) is element i, and this
// rewrite makes the shader speak that language:
//   * Dim Buffer -> 2D on the image type; OpCapability SampledBuffer dropped.
//   * Each UniformConstant sampled-image variable over a converted type is
//     split the way R1 splits combined samplers (image var keeps set/binding,
//     sampler var takes the next free binding with the bw_combined name);
//     OpImageFetch never samples, so that sampler is inert. Runs before R1,
//     so R1 never sees these variables.
//   * Every OpImageFetch on a converted image gets its scalar coordinate
//     widened just before it: bitcast to uint, x = c mod W, y = c div W.
//     Unsigned arithmetic sends negative (out-of-range) coordinates far
//     outside the texture, where textureLoad returns zero — the robust
//     buffer access behaviour the bridge advertises.
// The rewrite refuses (returns false, module untouched) when a value derived
// from a converted image feeds any other image op: OpImageQuerySize would
// need the view's true element count, which a wrapped texture cannot
// express. Storage texel buffers (Sampled=2) are not converted: shader
// writes to a texture copy could never reach guest memory, so those modules
// keep failing loudly at naga, as today.
const TEXEL_WRAP_WIDTH = 8192;
export function texelBuffersTo2D(mod, log) {
  const byId = new Map();
  for (const ins of mod.insns) if (ins.resultId != null) byId.set(ins.resultId, ins);
  const convTy = new Set();
  for (const ins of mod.insns)
    if (ins.op === Op.TypeImage && ins.words[2] === 5 && ins.words[6] === 1) convTy.add(ins.resultId);
  if (!convTy.size) return false;
  const convSampledTy = new Set();
  for (const ins of mod.insns)
    if (ins.op === Op.TypeSampledImage && convTy.has(ins.words[1])) convSampledTy.add(ins.resultId);
  if (!convSampledTy.size) return false;
  const vars = [];
  for (const ins of mod.insns) {
    if (ins.op !== Op.Variable || ins.words[2] !== SC.UniformConstant) continue;
    const ptr = byId.get(ins.words[0]);
    if (ptr && ptr.op === Op.TypePointer && convSampledTy.has(ptr.words[2]))
      vars.push({ varIns: ins, sampledTy: ptr.words[2], imageTy: byId.get(ptr.words[2]).words[1] });
  }
  if (!vars.length) return false;
  const varIds = new Set(vars.map(v => v.varIns.resultId));

  // --- analysis (no mutation yet): trace converted values, vet consumers ---
  const tracedSampled = new Set(), tracedImage = new Set(), fetches = [];
  for (const ins of mod.insns) {
    if ((ins.op === Op.Load || ins.op === Op.FunctionParameter) && convSampledTy.has(ins.resultType)) {
      if (ins.op === Op.Load && !varIds.has(ins.words[2]) && vars.length > 1) {
        log.push('spirvfix R12: sampled-image load from an unknown source; conversion refused');
        return false;
      }
      tracedSampled.add(ins.resultId);
    }
  }
  const refuse = why => { log.push('spirvfix R12: ' + why + '; conversion refused'); return false; };
  for (const ins of mod.insns) {
    const w = ins.words;
    if (ins.op === Op.SampledImage) {
      if (tracedSampled.has(w[2])) tracedSampled.add(ins.resultId);
      else if (tracedImage.has(w[2]) || tracedSampled.has(w[3])) return refuse('OpSampledImage over a converted value');
    } else if (ins.op === Op.Image) {
      if (tracedSampled.has(w[2])) tracedImage.add(ins.resultId);
    } else if (ins.op === Op.CopyObject) {
      if (tracedSampled.has(w[2])) tracedSampled.add(ins.resultId);
      else if (tracedImage.has(w[2])) tracedImage.add(ins.resultId);
    } else if (ins.op === Op.ImageFetch) {
      if (tracedImage.has(w[2]) || tracedSampled.has(w[2])) fetches.push(ins);
    } else {
      const start = ins.resultType != null ? 2 : ins.resultId != null ? 1 : 0;
      for (let k = start; k < w.length; k++)
        if (tracedSampled.has(w[k]) || tracedImage.has(w[k]))
          return refuse('converted image value feeds opcode ' + ins.op);
    }
  }
  for (const f of fetches) {
    const def = byId.get(f.words[3]);
    const t = def && byId.get(def.resultType);
    if (!t || t.op !== Op.TypeInt || t.words[1] !== 32)
      return refuse('OpImageFetch coordinate is not a 32-bit integer scalar');
    if (f.words.length > 4) {
      // Image operands present: only Lod/ConstOffset/Offset/ConstOffsets/
      // MinLod are expressible around the Lod word this rule may add.
      const mask = f.words[4];
      if (mask & ~0x172) return refuse('OpImageFetch carries image operands the conversion cannot extend');
    }
  }

  // --- mutate: Dim flip (Buffer=5 -> 2D=1) + capability drop -----------------
  for (const ins of mod.insns) if (convTy.has(ins.resultId)) ins.words[2] = 1;
  mod.insns = mod.insns.filter(i => !(i.op === Op.Capability && i.words[0] === 46));

  // --- split variables (mirrors R1, restricted to converted pairs) ----------
  const decos = mod.insns.filter(i => i.op === Op.Decorate);
  const setOf = id => decos.find(d => d.words[0] === id && d.words[1] === Dec.DescriptorSet)?.words[2] ?? 0;
  const bindOf = id => decos.find(d => d.words[0] === id && d.words[1] === Dec.Binding)?.words[2] ?? 0;
  const maxBind = new Map();
  for (const d of decos) {
    if (d.words[1] !== Dec.Binding) continue;
    const s = setOf(d.words[0]);
    maxBind.set(s, Math.max(maxBind.get(s) ?? -1, d.words[2]));
  }
  const newId = makeIdSource(mod);
  const findType = (op, eq) => mod.insns.find(i => i.op === op && eq(i))?.resultId ?? null;
  let samplerTy = findType(Op.TypeSampler, () => true);
  const addType = [], addAnnot = [];
  if (samplerTy == null) {
    samplerTy = newId();
    addType.push({ op: Op.TypeSampler, words: [samplerTy], resultType: null, resultId: samplerTy });
  }
  const ptrFor = elem => {
    const hit = findType(Op.TypePointer, i => i.words[1] === SC.UniformConstant && i.words[2] === elem)
      ?? addType.find(i => i.op === Op.TypePointer && i.words[1] === SC.UniformConstant && i.words[2] === elem)?.resultId ?? null;
    if (hit != null) return hit;
    const id = newId();
    addType.push({ op: Op.TypePointer, words: [id, SC.UniformConstant, elem], resultType: null, resultId: id });
    return id;
  };
  const pairs = new Map();
  const dropIdx = new Set();
  for (const c of vars) {
    const varId = c.varIns.resultId;
    const set = setOf(varId), imgBind = bindOf(varId);
    const sampBind = (maxBind.get(set) ?? imgBind) + 1;
    maxBind.set(set, sampBind);
    const imgVar = newId(), sampVar = newId();
    addType.push({ op: Op.Variable, words: [ptrFor(c.imageTy), imgVar, SC.UniformConstant], resultType: ptrFor(c.imageTy), resultId: imgVar });
    addType.push({ op: Op.Variable, words: [ptrFor(samplerTy), sampVar, SC.UniformConstant], resultType: ptrFor(samplerTy), resultId: sampVar });
    addAnnot.push({ op: Op.Decorate, words: [imgVar, Dec.DescriptorSet, set], resultType: null, resultId: null });
    addAnnot.push({ op: Op.Decorate, words: [imgVar, Dec.Binding, imgBind], resultType: null, resultId: null });
    addAnnot.push({ op: Op.Decorate, words: [sampVar, Dec.DescriptorSet, set], resultType: null, resultId: null });
    addAnnot.push({ op: Op.Decorate, words: [sampVar, Dec.Binding, sampBind], resultType: null, resultId: null });
    pairs.set(varId, { imageVar: imgVar, samplerVar: sampVar, sampledTy: c.sampledTy, imageTy: c.imageTy });
    mod.insns.forEach((ins, i) => {
      if (ins.op === Op.Variable && ins.resultId === varId) dropIdx.add(i);
      if ((ins.op === Op.Decorate || ins.op === Op.Name) && ins.words[0] === varId) dropIdx.add(i);
    });
    const nm = decodeStr(mod.insns.find(i => i.op === Op.Name && i.words[0] === varId) ?? { words: [] });
    if (nm) addType.push({ op: Op.Name, words: [imgVar, ...strWords(nm)], resultType: null, resultId: null });
    addType.push({ op: Op.Name, words: [sampVar, ...strWords(`bw_combined_g${set}_b${imgBind}_v${varId}`)], resultType: null, resultId: null });
    log.push(`spirvfix R12: texel-buffer uniform %${varId} -> 2D image %${imgVar} (set ${set}, binding ${imgBind}) + inert sampler %${sampVar} (binding ${sampBind})`);
  }
  for (const ins of mod.insns) {
    if (ins.op !== Op.EntryPoint) continue;
    let start = 2;
    while (start < ins.words.length) {
      const word = ins.words[start++];
      if ([0, 8, 16, 24].some(shift => ((word >>> shift) & 255) === 0)) break;
    }
    ins.words = ins.words.slice(0, start).concat(ins.words.slice(start).flatMap(id => {
      const pair = pairs.get(id);
      return pair ? [pair.imageVar, pair.samplerVar] : [id];
    }));
  }
  // Conversion types/constants (uint, uvec2, wrap width) join addType.
  let uintTy = findType(Op.TypeInt, i => i.words[1] === 32 && i.words[2] === 0);
  if (uintTy == null) {
    uintTy = newId();
    addType.push({ op: Op.TypeInt, words: [uintTy, 32, 0], resultType: null, resultId: uintTy });
  }
  let uvec2Ty = findType(Op.TypeVector, i => i.words[1] === uintTy && i.words[2] === 2)
    ?? addType.find(i => i.op === Op.TypeVector && i.words[1] === uintTy)?.resultId ?? null;
  if (uvec2Ty == null) {
    uvec2Ty = newId();
    addType.push({ op: Op.TypeVector, words: [uvec2Ty, uintTy, 2], resultType: null, resultId: uvec2Ty });
  }
  let wConst = mod.insns.find(i => i.op === Op.Constant && i.resultType === uintTy && i.words[2] === TEXEL_WRAP_WIDTH)?.resultId ?? null;
  if (wConst == null) {
    wConst = newId();
    addType.push({ op: Op.Constant, words: [uintTy, wConst, TEXEL_WRAP_WIDTH], resultType: uintTy, resultId: wConst });
  }
  // A converted fetch needs an explicit Lod image operand (naga's frontend
  // requires it for non-Buffer dimensions; glslang omits it for buffers).
  let sintTy = findType(Op.TypeInt, i => i.words[1] === 32 && i.words[2] === 1);
  if (sintTy == null) {
    sintTy = newId();
    addType.push({ op: Op.TypeInt, words: [sintTy, 32, 1], resultType: null, resultId: sintTy });
  }
  let lodZero = mod.insns.find(i => i.op === Op.Constant && i.resultType === sintTy && i.words[2] === 0)?.resultId ?? null;
  if (lodZero == null) {
    lodZero = newId();
    addType.push({ op: Op.Constant, words: [sintTy, lodZero, 0], resultType: sintTy, resultId: lodZero });
  }
  const kept = mod.insns.filter((_, i) => !dropIdx.has(i));
  const aPos = (() => { const s = sections({ insns: kept }); return s.lastAnnot + 1; })();
  kept.splice(Math.max(aPos, 0), 0, ...addAnnot);
  const tPos = (() => { const s = sections({ insns: kept }); return s.firstFunc < 0 ? kept.length : s.firstFunc; })();
  kept.splice(tPos, 0, ...addType.filter(i => i.op !== Op.Name));
  const dPos = (() => { const s = sections({ insns: kept }); return s.lastDebug + 1; })();
  kept.splice(Math.max(dPos, 0), 0, ...addType.filter(i => i.op === Op.Name));
  mod.insns = kept;
  byId.clear();
  for (const ins of mod.insns) if (ins.resultId != null) byId.set(ins.resultId, ins);

  // --- replace loads with couples (mirrors R1) -------------------------------
  const loads = mod.insns.filter(ins => ins.op === Op.Load && inFunction(mod, ins) && convSampledTy.has(ins.resultType));
  const firstPair = pairs.values().next().value;
  const imgLoadOf = new Map();   // couple result id -> image load id
  let nLoads = 0;
  for (const load of loads) {
    const pair = pairs.get(load.words[2]) ?? firstPair;
    if (!pair) continue;
    const imgLoad = newId(), sampLoad = newId();
    const idx = mod.insns.indexOf(load);
    if (idx < 0) continue;
    mod.insns.splice(idx, 1,
      { op: Op.Load, words: [pair.imageTy, imgLoad, pair.imageVar], resultType: pair.imageTy, resultId: imgLoad },
      { op: Op.Load, words: [samplerTy, sampLoad, pair.samplerVar], resultType: samplerTy, resultId: sampLoad },
      { op: Op.SampledImage, words: [load.resultType, load.resultId, imgLoad, sampLoad], resultType: load.resultType, resultId: load.resultId });
    imgLoadOf.set(load.resultId, imgLoad);
    nLoads++;
  }

  // --- widen fetch coordinates ------------------------------------------------
  const tracedS = new Set(), tracedI = new Set();
  for (const [coupleId, imgLoad] of imgLoadOf) { tracedS.add(coupleId); tracedI.add(imgLoad); }
  let nFetch = 0;
  for (let idx = 0; idx < mod.insns.length; idx++) {
    const ins = mod.insns[idx];
    if (ins.op === Op.SampledImage && tracedI.has(ins.words[2])) tracedS.add(ins.resultId);
    else if (ins.op === Op.Image && tracedS.has(ins.words[2])) tracedI.add(ins.resultId);
    else if (ins.op === Op.CopyObject) {
      if (tracedS.has(ins.words[2])) tracedS.add(ins.resultId);
      else if (tracedI.has(ins.words[2])) tracedI.add(ins.resultId);
    } else if (ins.op === Op.ImageFetch && (tracedI.has(ins.words[2]) || tracedS.has(ins.words[2]))) {
      const coordId = ins.words[3];
      const def = byId.get(coordId) ?? mod.insns.find(i => i.resultId === coordId);
      const t = def && byId.get(def.resultType);
      const seq = [];
      let u = coordId;
      if (t && t.words[2] === 1) {   // signed -> bitcast to uint
        u = newId();
        seq.push({ op: Op.Bitcast, words: [uintTy, u, coordId], resultType: uintTy, resultId: u });
      }
      const x = newId(), y = newId(), vec = newId();
      seq.push({ op: Op.UMod, words: [uintTy, x, u, wConst], resultType: uintTy, resultId: x });
      seq.push({ op: Op.UDiv, words: [uintTy, y, u, wConst], resultType: uintTy, resultId: y });
      seq.push({ op: Op.CompositeConstruct, words: [uvec2Ty, vec, x, y], resultType: uvec2Ty, resultId: vec });
      mod.insns.splice(idx, 0, ...seq);
      ins.words[3] = vec;
      if (ins.words.length === 4) ins.words.push(2, lodZero);
      else if (!(ins.words[4] & 2)) { ins.words[4] |= 2; ins.words.splice(5, 0, lodZero); }
      idx += seq.length;
      nFetch++;
    }
  }
  log.push(`spirvfix R12: rewrote ${nLoads} texel-buffer load(s), widened ${nFetch} fetch coordinate(s) to the ${TEXEL_WRAP_WIDTH}-wide 2D wrap`);
  return true;
}

// --- entry point -------------------------------------------------------------
/** @param {Uint8Array} bytes @param {{rasterTopology?: number, specOverrides?: {constantID: number, value: Uint8Array}[]}} [options] */
// --- R13: uniform-block-to-storage -------------------------------------------
// The bridge advertises VK_KHR_uniform_buffer_standard_layout (the honest
// half of vkd3d's shader-model 6.0 gate): a uniform block may carry std430
// packing. WGSL's storage address space IS std430, byte for byte -- but
// WGSL's uniform address space is not: array strides and struct sizes /
// member offsets there round up to 16, so naga rejects a std430-packed
// uniform block ("The array stride 4 is not a multiple of the required
// alignment 16") even though every byte offset is exactly representable one
// address space over. dxil-spv emits precisely those blocks for D3D12
// constant buffers (a wrapper struct around a tightly-strided array whose
// element type is the shader's widest cbuffer access: float -> stride 4,
// vec2 -> stride 8).
//
// Rewrite: a DescriptorSet-bound Uniform block variable whose decorated
// layout is NOT expressible in WGSL's uniform layout but IS exactly
// expressible in WGSL's storage layout is demoted to a read-only
// StorageBuffer variable (storage class flip on the variable and on every
// pointer type its access chains produce, plus a NonWritable decoration on
// the variable so naga emits var<storage> (read) -- legal in every stage,
// and the page tier derives the bind-group layout entry and the GPU buffer
// usage from the WGSL declaration, so the guest's bytes are bound as a
// storage buffer and read at identical offsets). Blocks whose layout
// matches NEITHER WGSL layout (scalar packing: vec3 arrays at stride 12,
// MatrixStride 12, scalar-packed members) are left untouched -- they keep
// failing loudly at naga, which is the honest outcome for
// scalarBlockLayout = VK_FALSE. Anything the checker cannot model exactly
// (bool members, runtime arrays in a uniform block, spec-constant array
// lengths, a flipped pointer flowing into a function call / store / phi)
// aborts the demotion for that variable: fail closed, module unchanged.
const R13_SC = { Uniform: 2, StorageBuffer: 12 };
// Declaration opcodes above 56 whose operands are decoration targets,
// literals or strings rather than consumed values (Variable 59 is for its
// initializer operand).
const R13_TRACE_SKIP = new Set([59, 71, 72, 73, 74, 328, 330, 331, 332, 5632, 5633]);
const R13_DEC = {
  Block: 2, BufferBlock: 3, RowMajor: 4, ColMajor: 5, ArrayStride: 6,
  MatrixStride: 7, NonWritable: 24, Offset: 35,
};

// WGSL host-shareable layout model, shared by R13 (which rewrites with it)
// and the block-layout audit below (which rejects with it). One model, one
// source of truth: the two consumers can never disagree about what WGSL
// layout computes for a decorated SPIR-V block.
function createLayoutModel(mod) {
  const byId = new Map();
  for (const ins of mod.insns) if (ins.resultId != null) byId.set(ins.resultId, ins);

  // ---- decorations ---------------------------------------------------------
  const typeDeco = new Map();       // typeId -> { arrayStride }
  const structKind = new Map();     // structId -> 'block' | 'bufferblock'
  const memberDeco = new Map();     // `${structId}:${idx}` -> { offset, matrixStride, rowMajor }
  const varDeco = new Map();        // varId -> Set of decorations applied to the variable
  for (const ins of mod.insns) {
    if (ins.op === Op.Decorate) {
      const [target, deco, val] = ins.words;
      if (deco === R13_DEC.ArrayStride) {
        const d = typeDeco.get(target) || {}; d.arrayStride = val; typeDeco.set(target, d);
      } else if (deco === R13_DEC.Block) structKind.set(target, 'block');
      else if (deco === R13_DEC.BufferBlock) structKind.set(target, 'bufferblock');
      else {
        const s = varDeco.get(target) || new Set(); s.add(deco); varDeco.set(target, s);
      }
    } else if (ins.op === Op.MemberDecorate) {
      const [st, idx, deco, val] = ins.words;
      const key = st + ':' + idx;
      const d = memberDeco.get(key) || {};
      if (deco === R13_DEC.Offset) d.offset = val;
      else if (deco === R13_DEC.MatrixStride) d.matrixStride = val;
      else if (deco === R13_DEC.RowMajor) d.rowMajor = true;
      else if (deco === R13_DEC.ColMajor) d.rowMajor = false;
      memberDeco.set(key, d);
    }
  }
  const constVal = (id) => {
    const c = byId.get(id);
    if (!c || c.op !== Op.Constant) return null;
    const t = byId.get(c.words[0]);
    if (!t || t.op !== Op.TypeInt) return null;
    const w = t.words[1];
    const v = w === 64 ? (BigInt(c.words[2] >>> 0) | (BigInt(c.words[3] || 0) << 32n)) : BigInt(c.words[2] >>> 0);
    return v <= 0x7fffffffn ? Number(v) : null;
  };
  const roundUp = (a, v) => (v + a - 1) & ~(a - 1);

  // WGSL host-shareable layout, per address space ('storage' | 'uniform').
  // measure(typeId, space, mDeco, trace?) -> { size, align, ok } | null
  //   ok: every decorated stride / offset inside the type equals the value
  //       this space's WGSL layout computes (so naga's re-layout is a
  //       byte-exact no-op). null: a shape this checker does not model.
  //   size -1 marks an unbounded type (runtime array): layout of its
  //       elements is still fully modelled; only the total size is unknown.
  // mDeco is the enclosing struct member's decorations (matrix stride /
  // majorness live there, not on the type); arrays pass it through, nested
  // structs replace it with their own members' decorations.
  // trace, when given, collects one record per proven inequality:
  // { kind: 'matrix-stride' | 'array-stride' | 'member-offset', space?,
  //   type?, struct?, member?, decorated, computed }. Matrix stride is
  // space-independent (the vector rule in both spaces); the other records
  // carry the space they were computed for.
  function measure(typeId, space, mDeco, trace) {
    const t = byId.get(typeId);
    if (!t) return null;
    switch (t.op) {
      case Op.TypeInt:
      case Op.TypeFloat: {
        const size = t.words[1] / 8;
        return { size, align: size, ok: true };
      }
      case Op.TypeVector: {
        const comp = measure(t.words[1], space, null, trace);
        if (!comp) return null;
        const n = t.words[2];
        return { size: comp.size * n, align: comp.size * (n === 2 ? 2 : 4), ok: comp.ok };
      }
      case Op.TypeMatrix: {
        if (!mDeco || mDeco.matrixStride == null) return null;
        const col = byId.get(t.words[1]);
        if (!col || col.op !== Op.TypeVector) return null;
        const comp = measure(col.words[1], space, null, trace);
        if (!comp) return null;
        const rows = col.words[2], cols = t.words[2];
        // The vectors the buffer actually stores: columns (ColMajor) or
        // rows (RowMajor). Stride rule is the vector-array rule in BOTH
        // spaces (proven: mat2 at MatrixStride 8 converts as var<uniform>).
        const vecSize = mDeco.rowMajor ? comp.size * cols : comp.size * rows;
        const vecAlign = comp.size * ((mDeco.rowMajor ? cols : rows) === 2 ? 2 : 4);
        const vecCount = mDeco.rowMajor ? rows : cols;
        const stride = roundUp(vecAlign, vecSize);
        if (trace && mDeco.matrixStride !== stride)
          trace.push({ kind: 'matrix-stride', type: typeId, decorated: mDeco.matrixStride, computed: stride });
        return { size: stride * vecCount, align: vecAlign, ok: mDeco.matrixStride === stride };
      }
      case Op.TypeArray:
      case Op.TypeRuntimeArray: {
        const deco = typeDeco.get(typeId);
        if (!deco || deco.arrayStride == null) return null;
        const len = t.op === Op.TypeArray ? constVal(t.words[2]) : -1;
        if (len == null) return null;
        const elem = measure(t.words[1], space, mDeco, trace);
        if (!elem || elem.size < 0) return null;
        let stride = roundUp(elem.align, elem.size);
        if (space === 'uniform') stride = roundUp(16, stride);
        const align = space === 'uniform' ? Math.max(16, elem.align) : elem.align;
        if (trace && deco.arrayStride !== stride)
          trace.push({ kind: 'array-stride', space, type: typeId, decorated: deco.arrayStride, computed: stride });
        return { size: len < 0 ? -1 : stride * len, align, ok: elem.ok && deco.arrayStride === stride };
      }
      case Op.TypeStruct: {
        let endU = 0, endS = 0, alignU = 0, alignS = 0, okU = true, okS = true;
        for (let i = 1; i < t.words.length; i++) {
          const mt = t.words[i];
          const md = memberDeco.get(typeId + ':' + (i - 1));
          if (!md || md.offset == null) return null;
          const mu = measure(mt, 'uniform', md, trace), ms = measure(mt, 'storage', md, trace);
          if (!mu || !ms || mu.size < 0 || ms.size < 0) return null;
          const offU = roundUp(mu.align, endU), offS = roundUp(ms.align, endS);
          if (trace && md.offset !== offU)
            trace.push({ kind: 'member-offset', space: 'uniform', struct: typeId, member: i - 1, decorated: md.offset, computed: offU });
          if (trace && md.offset !== offS)
            trace.push({ kind: 'member-offset', space: 'storage', struct: typeId, member: i - 1, decorated: md.offset, computed: offS });
          if (md.offset !== offU || !mu.ok) okU = false;
          if (md.offset !== offS || !ms.ok) okS = false;
          endU = offU + mu.size; endS = offS + ms.size;
          alignU = Math.max(alignU, mu.align); alignS = Math.max(alignS, ms.align);
        }
        if (space === 'uniform')
          return { size: roundUp(Math.max(16, alignU), endU), align: Math.max(16, alignU), ok: okU };
        return { size: roundUp(alignS, endS), align: alignS, ok: okS };
      }
      default:
        return null; // bool, pointer, image, ...: unmodelled
    }
  }
  // For the block struct itself only member checks matter (nothing follows
  // it, so its own size/align never gate conversion).
  function blockVerdict(structId) {
    const t = byId.get(structId);
    if (!t || t.op !== Op.TypeStruct) return null;
    let endU = 0, endS = 0, okU = true, okS = true;
    for (let i = 1; i < t.words.length; i++) {
      const md = memberDeco.get(structId + ':' + (i - 1));
      if (!md || md.offset == null) return null;
      const mu = measure(t.words[i], 'uniform', md), ms = measure(t.words[i], 'storage', md);
      if (!mu || !ms || mu.size < 0 || ms.size < 0) return null;
      if (md.offset !== roundUp(mu.align, endU)) okU = false;
      if (md.offset !== roundUp(ms.align, endS)) okS = false;
      okU = okU && mu.ok; okS = okS && ms.ok;
      endU = roundUp(mu.align, endU) + mu.size;
      endS = roundUp(ms.align, endS) + ms.size;
    }
    return { uniform: okU, storage: okS };
  }
  return { byId, typeDeco, structKind, memberDeco, varDeco, constVal, roundUp, measure, blockVerdict };
}

export function uniformBlocksToStorage(mod, log) {
  const { byId, structKind, varDeco, blockVerdict } = createLayoutModel(mod);

  // ---- candidate variables ---------------------------------------------------
  const candidates = [];
  for (const ins of mod.insns) {
    if (ins.op !== Op.Variable || ins.words[2] !== R13_SC.Uniform) continue;
    const decos = varDeco.get(ins.resultId);
    if (!decos || !decos.has(Dec.DescriptorSet)) continue;
    const ptr = byId.get(ins.words[0]);
    if (!ptr || ptr.op !== Op.TypePointer || ptr.words[1] !== R13_SC.Uniform) continue;
    const structId = ptr.words[2];
    if (structKind.get(structId) !== 'block') continue;
    const verdict = blockVerdict(structId);
    if (!verdict || verdict.uniform || !verdict.storage) continue;
    candidates.push({ varIns: ins, varId: ins.resultId, structId, ptrTypeId: ins.words[0] });
  }
  if (!candidates.length) return false;

  // SPIR-V < 1.3 spells a storage block BufferBlock, not Block: the struct
  // decoration flips with the variable, which is only sound when no other
  // variable keeps the struct.
  const pre13 = mod.header.version < 0x00010300;
  const structUsers = new Map();
  for (const ins of mod.insns) {
    if (ins.op !== Op.Variable) continue;
    const ptr = byId.get(ins.words[0]);
    if (ptr && ptr.op === Op.TypePointer) {
      const s = structUsers.get(ptr.words[2]) || new Set();
      s.add(ins.resultId); structUsers.set(ptr.words[2], s);
    }
  }

  const nextId = makeIdSource(mod);
  const twinOf = new Map();   // pointer type id -> StorageBuffer twin id
  let pendingTwins = [];      // twins created for the candidate being mutated
  const twinPointer = (ptrTypeId) => {
    if (twinOf.has(ptrTypeId)) return twinOf.get(ptrTypeId);
    const src = byId.get(ptrTypeId);
    const id = nextId();
    const ins = { op: Op.TypePointer, words: [id, R13_SC.StorageBuffer, src.words[2]], resultType: null, resultId: id };
    twinOf.set(ptrTypeId, id); pendingTwins.push(ins);
    return id;
  };

  let changed = 0;
  for (const cand of candidates) {
    // ---- forward value trace from the variable (fail closed) ---------------
    const traced = new Set([cand.varId]);
    const typeRewrites = [];  // instructions whose resultType needs the twin
    let abort = false;
    let grew = true;
    while (grew && !abort) {
      grew = false;
      for (const ins of mod.insns) {
        if (ins.resultId != null && traced.has(ins.resultId)) continue;
        // Only executable instructions consume values. Declarations
        // (types, constants, OpEntryPoint interface lists, OpFunction
        // control masks, decorations) also *mention* ids or small literals
        // that can numerically equal an id; scanning them would refuse
        // every demotion. OpVariable (59) is excluded for its initializer.
        if (ins.op < 57 || R13_TRACE_SKIP.has(ins.op)) continue;
        const w = ins.words;
        // first value operand index: after (resultType, resultId) when the
        // instruction has them, else after resultId alone, else 0.
        const start = ins.resultType != null ? 2 : ins.resultId != null ? 1 : 0;
        let hit = -1;
        for (let k = start; k < w.length; k++) if (traced.has(w[k])) { hit = k; break; }
        if (hit < 0) continue;
        if (ins.op === Op.AccessChain || ins.op === Op.InBoundsAccessChain || ins.op === Op.PtrAccessChain || ins.op === Op.CopyObject) {
          // access chains / pointer copies: the base must be the operand in
          // base position (first value operand); anything else is exotic.
          if (hit !== start) { abort = true; break; }
          traced.add(ins.resultId); typeRewrites.push(ins); grew = true;
        } else if (ins.op === Op.Load) {
          // loading a VALUE through the pointer: fine, nothing to rewrite.
        } else {
          abort = true; break; // call, store, phi, select, ...: fail closed
        }
      }
    }
    if (abort) {
      log.push(`spirvfix R13: demotion of uniform block %${cand.varId} refused (pointer value escapes the access-chain trace)`);
      continue;
    }
    if (pre13) {
      const users = structUsers.get(cand.structId) || new Set();
      const others = [...users].filter((u) => u !== cand.varId && !candidates.some((c) => c.varId === u));
      if (others.length) {
        log.push(`spirvfix R13: demotion of uniform block %${cand.varId} refused (pre-1.3 struct shared with another variable)`);
        continue;
      }
    }
    // ---- mutate -------------------------------------------------------------
    pendingTwins = [];
    cand.varIns.words[0] = twinPointer(cand.ptrTypeId);
    cand.varIns.resultType = cand.varIns.words[0];
    cand.varIns.words[2] = R13_SC.StorageBuffer;
    // serializeSpirv emits ins.words verbatim: the result type lives at
    // words[0] AND in ins.resultType; both must move to the twin.
    for (const ins of typeRewrites) { ins.resultType = twinPointer(ins.resultType); ins.words[0] = ins.resultType; }
    // Twins go immediately before the variable that first uses them: its
    // pointee closure is fully defined there in any valid module, however
    // the producer interleaved its sections.
    if (pendingTwins.length) mod.insns.splice(mod.insns.indexOf(cand.varIns), 0, ...pendingTwins);
    // NonWritable on the variable: UBO semantics (the shader cannot write
    // a uniform block) and what makes naga emit var<storage> (read-only)
    // instead of var<storage, read_write> -- read_write storage is illegal
    // in a WebGPU vertex stage, read-only is not.
    const nw = { op: Op.Decorate, words: [cand.varId, R13_DEC.NonWritable], resultType: null, resultId: null };
    const sec = sections(mod);
    mod.insns.splice((sec.lastAnnot >= 0 ? sec.lastAnnot : sec.lastDebug) + 1, 0, nw);
    if (pre13) {
      for (const ins of mod.insns)
        if (ins.op === Op.Decorate && ins.words[0] === cand.structId && ins.words[1] === R13_DEC.Block)
          ins.words[1] = R13_DEC.BufferBlock;
    }
    changed++;
    log.push(`spirvfix R13: uniform block %${cand.varId} (struct %${cand.structId}) demoted to read-only storage (std430 layout is not WGSL-uniform-expressible)`);
  }
  if (!changed) return false;
  return true;
}


// --- Block-layout audit ------------------------------------------------------
// The layout model above decides what R13 may REWRITE; this audit decides
// what the tier may SERVE AT ALL. It runs on the module AFTER applySpirvFix
// (web/shader.mjs, immediately before naga), so every variable's storage
// class is final -- R13's demotions have already happened, and a block is
// judged in exactly the WGSL address space it will be translated into.
//
// Why it exists (measured against the vendored naga, campaign report 4.20):
// naga validates MatrixStride only for matrices that are direct struct
// members, and array strides only against the address space's alignment
// rules -- never for equality with its own computed layout. A block whose
// decorations disagree with WGSL's layout therefore CONVERTS SILENTLY and
// reads the guest's bytes at the wrong offsets whenever the disagreement
// hides inside an array element (array<mat3> at MatrixStride 12/20,
// runtime arrays of mat3) or in a padded array stride (array<f32> at
// ArrayStride 32 in a uniform block, 16 in a storage block -- WGSL reads
// 16 / 4). No error appears anywhere; the frame is just wrong.
//
// Verdict per block variable (Uniform Block, StorageBuffer Block /
// BufferBlock, PushConstant Block; descriptor arrays of blocks included):
//   'ok'      every member is modelled and every decorated Offset /
//             ArrayStride / MatrixStride EQUALS the WGSL value for the
//             variable's final address space -- naga's re-layout is a
//             byte-exact no-op.
//   'reject'  a proven inequality (the trace names the decoration). The
//             module cannot be served: naga would misread it silently or
//             reject it with an unnamed error. web/shader.mjs turns this
//             into a thrown "spirv-block-layout: ..." error, which the
//             shader cache logs loudly and no frame is drawn.
//   'unknown' a shape the layout model does not cover (bool member,
//             spec-constant array length, missing Offset decoration...).
//             No verdict: the module keeps today's outcome. The audit only
//             ever rejects on proof -- it must never be stricter than the
//             tier it guards.
// Push constants are judged under the uniform rules: that is the layout
// naga gives var<immediate> (proven: push-mat3 at MatrixStride 12 fails
// naga with UnsupportedMatrixStride, push scalars convert).
const AUDIT_SC = { Uniform: 2, PushConstant: 9, StorageBuffer: 12 };

function layoutTypeName(model, typeId) {
  const { byId } = model;
  const t = byId.get(typeId);
  if (!t) return '%' + typeId;
  switch (t.op) {
    case Op.TypeBool: return 'bool';
    case Op.TypeInt: return (t.words[2] ? 'i' : 'u') + t.words[1];
    case Op.TypeFloat: return 'f' + t.words[1];
    case Op.TypeVector: return `vec${t.words[2]}<${layoutTypeName(model, t.words[1])}>`;
    case Op.TypeMatrix: {
      const col = byId.get(t.words[1]);
      const rows = col && col.op === Op.TypeVector ? col.words[2] : '?';
      return `mat${t.words[2]}x${rows}<${col ? layoutTypeName(model, col.words[1]) : '?'}>`;
    }
    case Op.TypeArray: return `array<${layoutTypeName(model, t.words[1])}, ${model.constVal(t.words[2]) ?? '?'}>`;
    case Op.TypeRuntimeArray: return `array<${layoutTypeName(model, t.words[1])}>`;
    case Op.TypeStruct: return `struct %${typeId}`;
    default: return `type %${typeId}`;
  }
}

function layoutTraceDetail(model, rec, where) {
  if (rec.kind === 'matrix-stride')
    return `${where}: MatrixStride ${rec.decorated} on ${layoutTypeName(model, rec.type)} does not equal the WGSL matrix stride ${rec.computed}; WGSL would read the stored vectors at stride ${rec.computed} with no error`;
  if (rec.kind === 'array-stride')
    return `${where}: ArrayStride ${rec.decorated} on ${layoutTypeName(model, rec.type)} does not equal the WGSL ${rec.space} array stride ${rec.computed}; WGSL would index the array at stride ${rec.computed} with no error`;
  return `${where}: member Offset ${rec.decorated} (struct %${rec.struct} member ${rec.member}) does not equal the WGSL ${rec.space} offset ${rec.computed}`;
}

// Walk one block struct in ONE address space. Returns { state, detail? }
// with state 'ok' | 'reject' | 'unknown' as documented above.
function auditBlockStruct(model, structId, space) {
  const { byId, memberDeco, measure, roundUp } = model;
  const t = byId.get(structId);
  if (!t || t.op !== Op.TypeStruct) return { state: 'unknown' };
  let end = 0, endKnown = true, sawUnknown = false;
  for (let i = 1; i < t.words.length; i++) {
    const idx = i - 1;
    const md = memberDeco.get(structId + ':' + idx);
    if (!md || md.offset == null) return { state: 'unknown' };
    const trace = [];
    const m = measure(t.words[i], space, md, trace);
    if (!m) { sawUnknown = true; endKnown = false; continue; }
    const rec = trace.find((r) => r.kind === 'matrix-stride' || r.space === space);
    if (rec) return { state: 'reject', detail: layoutTraceDetail(model, rec, `block %${structId} member ${idx}`) };
    if (!m.ok) return { state: 'unknown' }; // inequality the trace cannot name: stay conservative
    if (endKnown) {
      const off = roundUp(m.align, end);
      if (md.offset !== off)
        return { state: 'reject', detail: `block %${structId} member ${idx}: member Offset ${md.offset} does not equal the WGSL ${space} offset ${off}` };
      if (m.size < 0) { // runtime array: modelled, but terminal by spec
        if (i !== t.words.length - 1) return { state: 'unknown' };
        endKnown = false;
      } else end = off + m.size;
    }
  }
  return { state: sawUnknown ? 'unknown' : 'ok' };
}

// Reflect the block variables of a module and judge each one's layout.
// Accepts SPIR-V bytes (the post-spirvfix module in the tier flow).
// Returns null when the module does not parse; otherwise one entry per
// block variable: { varId, structId, set, binding, storageClass, space,
// verdict, detail? }. Never throws.
export function auditBlockLayouts(bytes) {
  let mod;
  try { mod = parseSpirv(bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes)); } catch { return null; }
  try {
    const model = createLayoutModel(mod);
    const { byId, structKind, measure } = model;
    const bindOf = new Map(); // varId -> { set, binding }
    for (const ins of mod.insns) {
      if (ins.op !== Op.Decorate) continue;
      if (ins.words[1] !== Dec.DescriptorSet && ins.words[1] !== Dec.Binding) continue;
      const b = bindOf.get(ins.words[0]) || {};
      if (ins.words[1] === Dec.DescriptorSet) b.set = ins.words[2]; else b.binding = ins.words[2];
      bindOf.set(ins.words[0], b);
    }
    const out = [];
    for (const ins of mod.insns) {
      if (ins.op !== Op.Variable) continue;
      const sc = ins.words[2];
      if (sc !== AUDIT_SC.Uniform && sc !== AUDIT_SC.StorageBuffer && sc !== AUDIT_SC.PushConstant) continue;
      const ptr = byId.get(ins.words[0]);
      if (!ptr || ptr.op !== Op.TypePointer || ptr.words[1] !== sc) continue;
      // Unwrap descriptor arrays of blocks (uniform B b[N]): the array's own
      // stride is judged by measure, the element struct by auditBlockStruct.
      let pointee = ptr.words[2];
      const layers = [];
      let elem = byId.get(pointee);
      while (elem && (elem.op === Op.TypeArray || elem.op === Op.TypeRuntimeArray)) {
        layers.push(pointee);
        pointee = elem.words[1];
        elem = byId.get(pointee);
      }
      const kind = structKind.get(pointee);
      if (sc === AUDIT_SC.Uniform && kind !== 'block') continue;
      if (sc === AUDIT_SC.PushConstant && kind !== 'block') continue;
      if (sc === AUDIT_SC.StorageBuffer && kind !== 'block' && kind !== 'bufferblock') continue;
      const space = sc === AUDIT_SC.StorageBuffer ? 'storage' : 'uniform';
      let verdict = 'ok', detail = null, sawUnknown = false;
      for (const layer of layers) {
        const trace = [];
        const m = measure(layer, space, null, trace);
        if (!m) { sawUnknown = true; continue; }
        const rec = trace.find((r) => r.kind === 'matrix-stride' || r.space === space);
        if (rec) { verdict = 'reject'; detail = layoutTraceDetail(model, rec, `block %${pointee} descriptor-array layer`); break; }
        if (!m.ok) sawUnknown = true;
      }
      if (verdict !== 'reject') {
        const s = auditBlockStruct(model, pointee, space);
        if (s.state === 'reject') { verdict = 'reject'; detail = s.detail; }
        else if (s.state === 'unknown' || sawUnknown) verdict = 'unknown';
      }
      const b = bindOf.get(ins.resultId) || {};
      out.push({ varId: ins.resultId, structId: pointee, set: b.set ?? null, binding: b.binding ?? null, storageClass: sc, space, verdict, ...(detail ? { detail } : {}) });
    }
    return out;
  } catch { return null; }
}

// R14 "storage-image-format": vkd3d typed-UAV codegen (enabled by the
// bridge's STORAGE_READ_WITHOUT_FORMAT format bits) emits storage images
// with Image Format Unknown plus OpCapability StorageImageReadWithoutFormat
// (55). Naga rejects BOTH the capability ("UnsupportedCapability") and an
// Unknown storage format ("InvalidStorageImageWithoutFormat") -- the format
// is a property of the bound VIEW, which the shader cannot know. The page
// tier therefore passes the formats of the views actually bound for this
// draw in options.storageFormats, a "set:binding=spvFormat;..." signature,
// and this rule writes the format into the OpTypeImage before translation.
// The component kind of the injected format must match the image type's
// sampled scalar kind (f/i/u); any mismatch or unbound variable leaves the
// type Unknown and naga fails loudly -- a wrong guess would silently read
// reinterpreted texels. When no Unknown storage format remains, the 55/56
// capability declarations are stripped (naga cannot consume them either).
// Byte-identical passthrough when the option is absent or nothing applies.
const SPV_FORMAT_KIND = {
  // SPIR-V ImageFormat values (spirv-as/dis + vendored naga agree; verified
  // empirically per format name -- do not "fix" these from memory).
  1: 'f', 2: 'f', 3: 'f', 4: 'f', 5: 'f', 6: 'f', 7: 'f', 9: 'f', 10: 'f', 12: 'f',
  13: 'f', 14: 'f', 15: 'f', 16: 'f', 17: 'f', 18: 'f', 19: 'f', 20: 'f',
  21: 'i', 22: 'i', 23: 'i', 24: 'i', 25: 'i', 26: 'i', 27: 'i', 28: 'i', 29: 'i', 41: 'i',
  30: 'u', 31: 'u', 32: 'u', 33: 'u', 35: 'u', 36: 'u', 37: 'u', 38: 'u', 39: 'u', 40: 'u',
};

function scalarKindOf(byId, typeId) {
  const t = byId.get(typeId);
  if (!t) return null;
  if (t.op === 21) return t.words[2] ? 'i' : 'u';        // OpTypeInt: width, signedness
  if (t.op === 22) return 'f';                            // OpTypeFloat
  if (t.op === 23) return scalarKindOf(byId, t.words[1]); // OpTypeVector: component type
  return null;
}

function storageDecorations(mod) {
  const decorations = new Map();
  for (const ins of mod.insns) {
    if (ins.op !== Op.Decorate) continue;
    const d = decorations.get(ins.words[0]) || {};
    if (ins.words[1] === 34) d.set = ins.words[2];      // DescriptorSet
    if (ins.words[1] === 33) d.binding = ins.words[2];  // Binding
    decorations.set(ins.words[0], d);
  }
  return decorations;
}

// Reflect the storage-image variables of a module: one entry per OpVariable
// whose type chain ends at an OpTypeImage with Sampled==2 (a storage image).
// Used by the page tier to build the options.storageFormats signature from
// the views bound for a draw. Returns null when the module does not parse.
export function storageImageVars(bytes) {
  let mod;
  try { mod = parseSpirv(bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes)); } catch { return null; }
  const byId = new Map(mod.insns.filter(i => i.resultId != null).map(i => [i.resultId, i]));
  const decorations = storageDecorations(mod);
  const out = [];
  const arrayVars = new Map(); // varId -> entry, runtime-array vars only
  for (const ins of mod.insns) {
    if (ins.op !== Op.Variable) continue;
    const ptr = byId.get(ins.words[0]);
    if (!ptr || ptr.op !== Op.TypePointer) continue;
    let pointee = byId.get(ptr.words[2]);
    // A runtime-array-wrapped storage image (a descriptor heap before the
    // R15a specialization runs -- the tier builds this signature from the
    // ORIGINAL module bytes) counts at its own set/binding.
    const runtimeArray = !!pointee && pointee.op === Op.TypeRuntimeArray;
    if (runtimeArray) pointee = byId.get(pointee.words[1]);
    const img = pointee;
    if (!img || img.op !== Op.TypeImage || img.words[6] !== 2) continue;
    const d = decorations.get(ins.resultId) || {};
    const entry = { set: d.set ?? 0, binding: d.binding ?? 0, dim: img.words[2], format: img.words[7], kind: scalarKindOf(byId, img.words[1]), typeId: img.resultId };
    // A runtime-array entry additionally carries the array's proven index
    // signature (filled in below): its format is the format of the element
    // the shader actually touches, which is not necessarily element 0.
    if (runtimeArray) { entry.array = true; entry.index = null; arrayVars.set(ins.resultId, entry); }
    out.push(entry);
  }
  if (arrayVars.size) {
    // The same analysis the R15a specialization serves on: an array earns
    // an index only when it is servable with exactly one index signature.
    // Anything else keeps index === null and resolves nothing downstream.
    const { arrays } = analyzeDescriptorArrays(mod);
    for (const rec of arrays) {
      const entry = arrayVars.get(rec.varId);
      if (entry && rec.servable && rec.signatures.length) entry.index = rec.signatures[0].sig;
    }
  }
  return out;
}

function injectStorageImageFormats(mod, log, signature) {
  if (!signature) return false;
  const wanted = new Map();
  for (const part of String(signature).split(';')) {
    const m = /^(\d+):(\d+)=(\d+)$/.exec(part.trim());
    if (m) wanted.set(m[1] + ':' + m[2], Number(m[3]));
  }
  if (!wanted.size) return false;
  const byId = new Map(mod.insns.filter(i => i.resultId != null).map(i => [i.resultId, i]));
  const decorations = storageDecorations(mod);
  const types = new Map();
  for (const ins of mod.insns) {
    if (ins.op !== Op.TypeImage || ins.words[6] !== 2) continue;
    types.set(ins.resultId, { ins, vars: [] });
  }
  if (!types.size) return false;
  for (const ins of mod.insns) {
    if (ins.op !== Op.Variable) continue;
    const ptr = byId.get(ins.words[0]);
    if (!ptr || ptr.op !== Op.TypePointer) continue;
    let pointeeId = ptr.words[2];
    const pointee = byId.get(pointeeId);
    if (pointee && pointee.op === Op.TypeRuntimeArray) pointeeId = pointee.words[1];
    const t = types.get(pointeeId);
    if (!t) continue;
    const d = decorations.get(ins.resultId) || {};
    t.vars.push({ set: d.set ?? 0, binding: d.binding ?? 0 });
  }
  let changed = false;
  for (const t of types.values()) {
    if (t.ins.words[7] !== 0) continue; // already concrete
    let fmt = null, ok = t.vars.length > 0;
    for (const v of t.vars) {
      const f = wanted.get(v.set + ':' + v.binding);
      if (f == null || (fmt != null && f !== fmt)) { ok = false; break; }
      fmt = f;
    }
    if (!ok || fmt == null) continue; // unresolved: stays Unknown, naga fails loudly
    const kind = SPV_FORMAT_KIND[fmt];
    const skind = scalarKindOf(byId, t.ins.words[1]);
    if (!kind || kind !== skind) {
      log.push(`spirvfix: left storage image type %${t.ins.resultId} Unknown (injected format kind ${kind} != sampled scalar kind ${skind})`);
      continue;
    }
    t.ins.words[7] = fmt;
    changed = true;
    log.push(`spirvfix: injected storage image format ${fmt} into type %${t.ins.resultId} from the bound view`);
  }
  const anyUnknown = [...types.values()].some(t => t.ins.words[7] === 0);
  if (!anyUnknown) {
    const before = mod.insns.length;
    mod.insns = mod.insns.filter(i => !(i.op === Op.Capability && (i.words[0] === 55 || i.words[0] === 56)));
    if (mod.insns.length !== before) {
      changed = true;
      log.push('spirvfix: stripped StorageImage{Read,Write}WithoutFormat capabilities (all storage formats concrete)');
    }
  }
  return changed;
}

// --- R15: descriptor addressing (Route A) ------------------------------------
// Two rewrites serving the addressing forms the bridge audit mirror
// (vk64AuditSpirv in vk64bridge.cpp) admits under the SAME classifiers
// (campaign report section 4.21; research report section 4). Both fail
// closed: a module is rewritten only when EVERY addressing use in it
// classifies into the servable subset; otherwise it is left byte-identical
// and the audit's named capability rejection stands.
//
// R15a "descriptor-array-specialize": a descriptor variable whose pointee
// is OpTypeRuntimeArray of a descriptor element type (image / sampler /
// sampled image), where every access chain carries exactly ONE index and
// all index expressions share ONE signature -- {const v} or
// {push@byteOff + add} (a load of the merged push-constant block, possibly
// plus a constant) -- is replaced by ONE scalar clone variable at the same
// set/binding. The clone's OpName encodes the signature
// (bw_arr_g<set>_b<binding>_p<pushOff>_a<add> or ..._c<value>) and the
// page tier resolves the element at bind time from the draw's push block
// (the bw_combined_* precedent: resolution metadata travels in the name;
// the bind-group fingerprint covers the resolved entry). Two distinct
// signatures on one array would need two resources at one binding in a
// single draw -- unservable, left unchanged. Writable storage-image
// arrays (Sampled==2 without NonWritable) specialize the same way: the
// clone copies the array's decorations verbatim, so it stays writable,
// naga emits a write / read_write storage texture, and the tier binds it
// with the shader-declared access through the same bw_arr_* resolution
// (the access is parsed from the WGSL decl, never assumed). That serves
// the shapes that reach translation: compute modules (the bridge audit's
// compute carve-out admits them before the capability checks) and
// graphics modules that only READ through a writable-declared array. A
// graphics module that actually writes a storage image still meets the
// bridge's module-level spirv-storage-image-write rejection, unchanged.
// Stores/atomics through an element POINTER and texel-pointer forms stay
// fail-closed in the per-chain uses analysis: servable image writes go
// through the loaded image object (OpImageWrite), never the pointer.
//
// R15b "root-descriptor-bda": a physical-storage-buffer pointer
// materialized from the push block -- OpConvertUToPtr, or OpBitcast of a
// push-loaded 64-bit value (the form Wine's DXBC compiler + dxbc-spirv
// emit for root descriptors; census of the captured root12 modules,
// section 4.21) -- whose address traces to exactly ONE root slot (push
// byte offset, zero addend) and which is only ever accessed through
// access chains rooted at the materialization, is re-rooted at a
// synthesized StorageBuffer variable named bw_root_p<pushByteOff> in the
// module's heap-array set (else set 0) at binding 4096 + pushByteOff/8.
// The bridge resolves the slot's VA at draw time and streams the target
// range in a V2 ROOT_DESCS record; the tier binds it like any other
// buffer entry. The address may also reach the materialization as a
// v2uint rebuilt by the dxbc-spirv carry idiom (OpIAddCarry over the two
// push-loaded halves; the form root SRV loads take) -- r15ClassifyVec2Addr
// classifies exactly that shape under the same zero-addend rule. WRITES
// through a root pointer (OpStore through a re-rooted chain) ARE served:
// the synthesized variable is read-write, the bridge marks the resolved
// range as GPU-written when the draw is recorded, and the section 4.30
// deferred BUFFER_READBACK path returns the bytes to guest memory before
// any later copy reads them. Atomic RMW through a root pointer is NOT
// served (no guest-visible atomicity model) and keeps the loud
// rejection. Pointer escapes (call
// arguments, stores of the pointer itself, OpConvertPtrToU, phi/select,
// OpPtrAccessChain) fail closed the same way, as do pointees whose
// decorated layout is not byte-exact in WGSL storage layout (the R13
// measure rules, storage space, plus a runtime-array tail member).
const R15_OP = { Extension: 10, TypeImage: 25, TypeSampler: 26, TypeSampledImage: 27, ConvertPtrToU: 117, ConvertUToPtr: 120, Bitcast: 124, IAdd: 128, ISub: 129, IAddCarry: 149, AtomicLoad: 227 };
const R15_PSB = 5349;          // PhysicalStorageBuffer storage class
const R15_PSB_MODEL = 5348;    // PhysicalStorageBuffer64 addressing model
const R15_STORAGE_BUFFER = 12; // StorageBuffer storage class

// Scalar integer constant extraction over the shared constValue (which
// returns {type, elems} objects): a Number, or null.
function r15ConstInt(ctx, id) {
  const v = constValue(ctx.byId, id);
  if (!v || v.elems.length !== 1) return null;
  const e = v.elems[0];
  if (e.kind !== 'sint' && e.kind !== 'uint') return null;
  const n = typeof e.value === 'bigint' ? Number(e.value) : e.value;
  return typeof n === 'number' && Number.isSafeInteger(n) ? n : null;
}

function r15Context(mod) {
  const byId = new Map();
  for (const ins of mod.insns) if (ins.resultId != null) byId.set(ins.resultId, ins);
  const memberDeco = new Map();
  const typeDeco = new Map();
  const varDeco = new Map();
  const structKind = new Map();
  for (const ins of mod.insns) {
    if (ins.op === Op.Decorate) {
      const [target, deco, val] = ins.words;
      if (deco === 6) { const d = typeDeco.get(target) || {}; d.arrayStride = val; typeDeco.set(target, d); }
      else {
        if (deco === 2) structKind.set(target, 'block');
        else if (deco === 3) structKind.set(target, 'bufferblock');
        const d = varDeco.get(target) || { decos: [] };
        if (deco === 34) d.set = val; else if (deco === 33) d.binding = val;
        else if (deco === 24) d.nonWritable = true;
        d.decos.push(ins); varDeco.set(target, d);
      }
    } else if (ins.op === Op.MemberDecorate) {
      const [st, idx, deco, val] = ins.words;
      const key = st + ':' + idx;
      const d = memberDeco.get(key) || {};
      if (deco === 35) d.offset = val; else if (deco === 7) d.matrixStride = val;
      else if (deco === 4) d.rowMajor = true; else if (deco === 5) d.rowMajor = false;
      memberDeco.set(key, d);
    }
  }
  let pushVarId = null, pushStructId = null, multiPush = false;
  for (const ins of mod.insns) {
    if (ins.op !== Op.Variable || ins.words[2] !== 9) continue;
    if (pushVarId != null) { multiPush = true; break; }
    pushVarId = ins.resultId;
    const ptr = byId.get(ins.words[0]);
    pushStructId = ptr && ptr.op === Op.TypePointer ? ptr.words[2] : null;
  }
  if (multiPush) { pushVarId = null; pushStructId = null; }
  let firstFunc = mod.insns.findIndex(i => i.op === Op.Function);
  if (firstFunc < 0) firstFunc = mod.insns.length;
  return { mod, byId, memberDeco, typeDeco, varDeco, structKind, pushVarId, pushStructId, firstFunc };
}

// Byte offset + result pointee type of a pointer value that chains back to
// the push-constant variable, or null. Indices must be constants; struct
// members resolve through Offset decorations, vectors through their scalar
// size, arrays through ArrayStride. Anything else is unmodelled.
function r15ChainOffset(ctx, ptrId) {
  const ins = ctx.byId.get(ptrId);
  if (!ins) return null;
  if (ins.op === Op.Variable) {
    if (ptrId !== ctx.pushVarId || ctx.pushStructId == null) return null;
    return { off: 0, typeId: ctx.pushStructId };
  }
  if (ins.op !== Op.AccessChain && ins.op !== Op.InBoundsAccessChain) return null;
  const base = r15ChainOffset(ctx, ins.words[2]);
  if (!base || base.typeId == null) return null;
  let off = base.off, cur = base.typeId;
  for (let k = 3; k < ins.words.length; k++) {
    const idx = r15ConstInt(ctx, ins.words[k]);
    if (idx == null || idx < 0) return null;
    const t = ctx.byId.get(cur);
    if (!t) return null;
    if (t.op === Op.TypeStruct) {
      if (idx + 1 >= t.words.length) return null;
      const md = ctx.memberDeco.get(cur + ':' + idx);
      if (!md || md.offset == null) return null;
      off += md.offset; cur = t.words[idx + 1];
    } else if (t.op === Op.TypeVector) {
      const comp = ctx.byId.get(t.words[1]);
      if (!comp || (comp.op !== Op.TypeInt && comp.op !== Op.TypeFloat)) return null;
      off += idx * (comp.words[1] / 8); cur = t.words[1];
    } else if (t.op === Op.TypeArray) {
      const d = ctx.typeDeco.get(cur);
      if (!d || d.arrayStride == null) return null;
      off += idx * d.arrayStride; cur = t.words[1];
    } else return null;
  }
  return { off, typeId: cur };
}

// Classify an integer value expression: {kind:'push', byteOff, add} for a
// load of the push block (through chains / copies / bitcasts / constant
// adds and subs), {kind:'const', value} for a pure constant, else null.
function r15ClassifyExpr(ctx, id, depth = 0) {
  if (depth > 8) return null;
  const ins = ctx.byId.get(id);
  if (!ins) return null;
  const cv = r15ConstInt(ctx, id);
  if (cv != null) return { kind: 'const', value: cv };
  switch (ins.op) {
    case Op.Load: {
      const r = r15ChainOffset(ctx, ins.words[2]);
      if (!r) return null;
      const lt = ctx.byId.get(ins.resultType);
      if (!lt) return null;
      let ok = lt.op === Op.TypeInt;
      if (!ok && lt.op === Op.TypeVector) {
        const c = ctx.byId.get(lt.words[1]);
        ok = !!c && c.op === Op.TypeInt && c.words[1] === 32 && lt.words[2] === 2;
      }
      return ok ? { kind: 'push', byteOff: r.off, add: 0 } : null;
    }
    case Op.CopyObject:
      return r15ClassifyExpr(ctx, ins.words[2], depth + 1);
    case R15_OP.Bitcast:
      return r15ClassifyExpr(ctx, ins.words[2], depth + 1);
    case R15_OP.IAdd: case R15_OP.ISub: {
      const a = r15ClassifyExpr(ctx, ins.words[2], depth + 1);
      const b = r15ClassifyExpr(ctx, ins.words[3], depth + 1);
      if (ins.op === R15_OP.IAdd) {
        if (a && a.kind === 'push' && b && b.kind === 'const') return { kind: 'push', byteOff: a.byteOff, add: a.add + b.value };
        if (a && a.kind === 'const' && b && b.kind === 'push') return { kind: 'push', byteOff: b.byteOff, add: b.add + a.value };
        if (a && a.kind === 'const' && b && b.kind === 'const') return { kind: 'const', value: a.value + b.value };
      } else {
        if (a && a.kind === 'push' && b && b.kind === 'const') return { kind: 'push', byteOff: a.byteOff, add: a.add - b.value };
        if (a && a.kind === 'const' && b && b.kind === 'const') return { kind: 'const', value: a.value - b.value };
      }
      return null;
    }
    default:
      return null;
  }
}

// The result id of the OpLoad an address half ultimately extracts from,
// unwrapping OpCopyObject; null for anything else.
function r15BaseLoadId(ctx, id) {
  let cur = id;
  for (let i = 0; i < 8; i++) {
    const s = ctx.byId.get(cur);
    if (!s) return null;
    if (s.op === Op.Load) return s.resultId;
    if (s.op === Op.CopyObject) { cur = s.words[2]; continue; }
    return null;
  }
  return null;
}

// Classify one 32-bit half of a 64-bit address built by the dxbc-spirv
// carry idiom: the half traces to OpCompositeExtract <half> of ONE
// push-loaded v2uint (the root VA), plus constant adds/subs and at most
// one OpIAddCarry -- whose sum feeds the low half and whose carry the
// high half consumes through OpIAdd. Returns { loadId, byteOff, half,
// add, carryId } (add is the constant added to this half, mod 2^32;
// carryId the OpIAddCarry result id this half produces (low) or
// consumes (high), or null), or null for any other shape.
function r15Vec2Half(ctx, id, depth = 0) {
  if (depth > 8) return null;
  const ins = ctx.byId.get(id);
  if (!ins) return null;
  if (ins.op === Op.CopyObject) return r15Vec2Half(ctx, ins.words[2], depth + 1);
  if (ins.op === Op.CompositeExtract && ins.words.length === 4) {
    const half = ins.words[3];
    if (half !== 0 && half !== 1) return null;
    const src = ctx.byId.get(ins.words[2]);
    if (!src) return null;
    if (src.op === Op.Load || src.op === Op.CopyObject) {
      const prov = r15ClassifyExpr(ctx, ins.words[2]);
      if (!prov || prov.kind !== 'push' || prov.add !== 0) return null;
      const lt = ctx.byId.get(src.resultType);
      if (!lt || lt.op !== Op.TypeVector || lt.words[2] !== 2) return null;
      const loadId = r15BaseLoadId(ctx, ins.words[2]);
      if (loadId == null) return null;
      return { loadId, byteOff: prov.byteOff, half, add: 0, carryId: null };
    }
    if (src.op === R15_OP.IAddCarry && half === 0) {
      const aC = r15ConstInt(ctx, src.words[2]), bC = r15ConstInt(ctx, src.words[3]);
      let base = null, c = null;
      if (bC != null && aC == null) { base = r15Vec2Half(ctx, src.words[2], depth + 1); c = bC; }
      else if (aC != null && bC == null) { base = r15Vec2Half(ctx, src.words[3], depth + 1); c = aC; }
      if (!base || base.half !== 0 || base.carryId != null) return null;
      return { ...base, add: (base.add + c) >>> 0, carryId: src.resultId };
    }
    return null;
  }
  if (ins.op === R15_OP.IAdd || ins.op === R15_OP.ISub) {
    const aC = r15ConstInt(ctx, ins.words[2]), bC = r15ConstInt(ctx, ins.words[3]);
    if (bC != null && aC == null) {
      const h = r15Vec2Half(ctx, ins.words[2], depth + 1);
      if (h) return { ...h, add: (h.add + (ins.op === R15_OP.IAdd ? bC : -bC)) >>> 0 };
    }
    if (ins.op === R15_OP.IAdd && aC != null && bC == null) {
      const h = r15Vec2Half(ctx, ins.words[3], depth + 1);
      if (h) return { ...h, add: (h.add + aC) >>> 0 };
    }
    if (ins.op === R15_OP.IAdd && aC == null && bC == null) {
      // OpIAdd(halfExpr, OpCompositeExtract(OpIAddCarry, 1)): the high
      // half consuming the low half's carry, in either operand order.
      for (const [xId, yId] of [[ins.words[2], ins.words[3]], [ins.words[3], ins.words[2]]]) {
        const y = ctx.byId.get(yId);
        if (!y || y.op !== Op.CompositeExtract || y.words.length !== 4 || y.words[3] !== 1) continue;
        const carrySrc = ctx.byId.get(y.words[2]);
        if (!carrySrc || carrySrc.op !== R15_OP.IAddCarry) continue;
        const h = r15Vec2Half(ctx, xId, depth + 1);
        if (h && h.carryId == null) return { ...h, carryId: carrySrc.resultId };
      }
    }
    return null;
  }
  return null;
}

// Classify a 64-bit root address materialized as a v2uint:
// OpCompositeConstruct(loHalf, hiHalf) over one push load, each half
// classified by r15Vec2Half. Returns { kind:'push', byteOff, add,
// addHi, vec2:true } or null. analyzeRootDescriptors serves only the
// zero-addend case, exactly like the scalar form.
function r15ClassifyVec2Addr(ctx, id) {
  const ins = ctx.byId.get(id);
  if (!ins || ins.op !== Op.CompositeConstruct || ins.words.length !== 4) return null;
  const tt = ctx.byId.get(ins.resultType);
  if (!tt || tt.op !== Op.TypeVector || tt.words[2] !== 2) return null;
  const comp = ctx.byId.get(tt.words[1]);
  if (!comp || comp.op !== Op.TypeInt || comp.words[1] !== 32 || comp.words[2] !== 0) return null;
  const lo = r15Vec2Half(ctx, ins.words[2], 0);
  const hi = r15Vec2Half(ctx, ins.words[3], 0);
  if (!lo || !hi) return null;
  if (lo.half !== 0 || hi.half !== 1) return null;
  if (lo.loadId !== hi.loadId || lo.byteOff !== hi.byteOff) return null;
  // The carry out of the low half's addition must be consumed by the
  // high half, and the high half may consume no other carry: both sides
  // must name the same OpIAddCarry (or neither).
  if (lo.carryId !== hi.carryId) return null;
  return { kind: 'push', byteOff: lo.byteOff, add: lo.add, addHi: hi.add, vec2: true };
}

// Classify every use of a pointer value: reads (Load / AtomicLoad through
// it or through chains/copies rooted at it), writes (Store / atomic RMW),
// escapes (anything not positively classified). Also collects the cone of
// access-chain instructions rooted at the pointer.
function r15PointerUses(ctx, rootId) {
  const out = { reads: 0, writes: 0, storeWrites: 0, atomicWrites: 0, escapes: [], cone: [] };
  const seen = new Set();
  const visit = (pid, depth) => {
    if (depth > 16 || seen.has(pid)) return;
    seen.add(pid);
    for (let ii = ctx.firstFunc; ii < ctx.mod.insns.length; ii++) {
      const ins = ctx.mod.insns[ii];
      const start = ins.resultType != null ? 2 : ins.resultId != null ? 1 : 0;
      let hit = -1;
      for (let k = start; k < ins.words.length; k++) if (ins.words[k] === pid) { hit = k; break; }
      if (hit < 0) continue;
      if ((ins.op === Op.AccessChain || ins.op === Op.InBoundsAccessChain) && hit === start) {
        out.cone.push(ins); visit(ins.resultId, depth + 1); continue;
      }
      if (ins.op === Op.CopyObject && hit === start) { visit(ins.resultId, depth + 1); continue; }
      if (ins.op === Op.Load && hit === start) { out.reads++; continue; }
      if (ins.op === Op.Store) {
        if (hit === start) { out.writes++; out.storeWrites++; } else out.escapes.push('pointer stored as a value');
        continue;
      }
      if (ins.op >= 227 && ins.op <= 245 && hit === start) {
        if (ins.op === R15_OP.AtomicLoad) out.reads++; else { out.writes++; out.atomicWrites++; }
        continue;
      }
      out.escapes.push('op' + ins.op);
    }
  };
  visit(rootId, 0);
  return out;
}

// WGSL storage-layout measure (the R13 rules for the storage space, plus a
// runtime-array tail). Returns {size, align, ok, runtime?} or null when the
// shape is unmodelled. ok: every decorated stride/offset equals the value
// WGSL storage layout computes, so naga's re-layout is a byte-exact no-op.
function r15StorageMeasure(ctx, typeId, mDeco, depth = 0) {
  if (depth > 12) return null;
  const t = ctx.byId.get(typeId);
  if (!t) return null;
  const roundUp = (a, v) => (v + a - 1) & ~(a - 1);
  switch (t.op) {
    case Op.TypeInt: case Op.TypeFloat: {
      const size = t.words[1] / 8;
      return { size, align: size, ok: true };
    }
    case Op.TypeVector: {
      const comp = r15StorageMeasure(ctx, t.words[1], null, depth + 1);
      if (!comp || comp.runtime) return null;
      const n = t.words[2];
      return { size: comp.size * n, align: comp.size * (n === 2 ? 2 : 4), ok: comp.ok };
    }
    case Op.TypeMatrix: {
      if (!mDeco || mDeco.matrixStride == null) return null;
      const col = ctx.byId.get(t.words[1]);
      if (!col || col.op !== Op.TypeVector) return null;
      const comp = r15StorageMeasure(ctx, col.words[1], null, depth + 1);
      if (!comp) return null;
      const rows = col.words[2], cols = t.words[2];
      const vecSize = mDeco.rowMajor ? comp.size * cols : comp.size * rows;
      const vecAlign = comp.size * ((mDeco.rowMajor ? cols : rows) === 2 ? 2 : 4);
      const vecCount = mDeco.rowMajor ? rows : cols;
      const stride = roundUp(vecAlign, vecSize);
      return { size: stride * vecCount, align: vecAlign, ok: mDeco.matrixStride === stride };
    }
    case Op.TypeArray: {
      const len = r15ConstInt(ctx, t.words[2]);
      const d = ctx.typeDeco.get(typeId);
      if (len == null || !d || d.arrayStride == null) return null;
      const elem = r15StorageMeasure(ctx, t.words[1], mDeco, depth + 1);
      if (!elem || elem.runtime) return null;
      const stride = roundUp(elem.align, elem.size);
      return { size: stride * len, align: elem.align, ok: elem.ok && d.arrayStride === stride };
    }
    case Op.TypeRuntimeArray: {
      const d = ctx.typeDeco.get(typeId);
      if (!d || d.arrayStride == null) return null;
      const elem = r15StorageMeasure(ctx, t.words[1], mDeco, depth + 1);
      if (!elem || elem.runtime) return null;
      const stride = roundUp(elem.align, elem.size);
      return { size: 0, align: elem.align, ok: elem.ok && d.arrayStride === stride, runtime: true };
    }
    case Op.TypeStruct: {
      let end = 0, align = 0, ok = true;
      for (let i = 1; i < t.words.length; i++) {
        const md = ctx.memberDeco.get(typeId + ':' + (i - 1));
        if (!md || md.offset == null) return null;
        const m = r15StorageMeasure(ctx, t.words[i], md, depth + 1);
        if (!m) return null;
        if (m.runtime && i !== t.words.length - 1) return null;
        const off = roundUp(m.align, end);
        if (md.offset !== off || !m.ok) ok = false;
        end = off + (m.runtime ? 0 : m.size);
        align = Math.max(align, m.align);
      }
      return { size: roundUp(align, end), align, ok };
    }
    default:
      return null;
  }
}

// --- R15 analysis (shared by the rewrites, the census tool and the tests;
// the bridge audit in vk64bridge.cpp mirrors these classifiers) -----------

// Runtime-descriptor-array variables and their access signatures.
export function analyzeDescriptorArrays(mod) {
  const ctx = r15Context(mod);
  const arrays = [];
  for (const ins of mod.insns) {
    if (ins.op !== Op.Variable) continue;
    const ptr = ctx.byId.get(ins.words[0]);
    if (!ptr || ptr.op !== Op.TypePointer) continue;
    const pointee = ctx.byId.get(ptr.words[2]);
    if (!pointee || pointee.op !== Op.TypeRuntimeArray) continue;
    const elem = ctx.byId.get(pointee.words[1]);
    if (!elem) continue;
    const elemKind = elem.op === R15_OP.TypeImage ? 'image'
      : elem.op === R15_OP.TypeSampler ? 'sampler'
      : elem.op === R15_OP.TypeSampledImage ? 'sampled-image' : null;
    const d = ctx.varDeco.get(ins.resultId) || {};
    const rec = {
      varId: ins.resultId, varIns: ins, set: d.set ?? 0, binding: d.binding ?? 0,
      elemTypeId: pointee.words[1], elemKind, sc: ptr.words[1],
      signatures: [], servable: false, reason: null, writable: false,
    };
    if (!elemKind) { rec.reason = 'array element is not a descriptor type'; arrays.push(rec); continue; }
    // Writability only exists for storage images (Sampled==2); sampled
    // images and samplers cannot be stored through at all. A writable
    // storage-image array (no NonWritable decoration) is NOT a rejection:
    // the clone inherits the decorations verbatim and stays writable, so
    // image writes through the element (OpImageWrite on the loaded image
    // object) translate to a storage-texture store on the scalar clone.
    // Recorded on the rec for the census/tests; the per-chain uses
    // analysis below still fails closed on pointer-level stores, atomics
    // and escapes (texel-pointer forms included).
    rec.writable = elemKind === 'image' && elem.words[6] === 2 && !d.nonWritable;
    const sigKeys = new Map();
    let bad = null;
    for (let ii = ctx.firstFunc; ii < mod.insns.length && !bad; ii++) {
      const u = mod.insns[ii];
      const start = u.resultType != null ? 2 : u.resultId != null ? 1 : 0;
      let hit = -1;
      for (let k = start; k < u.words.length; k++) if (u.words[k] === ins.resultId) { hit = k; break; }
      if (hit < 0) continue;
      if ((u.op === Op.AccessChain || u.op === Op.InBoundsAccessChain) && hit === start) {
        if (u.words.length !== start + 2) { bad = 'multi-index array access'; break; }
        const sig = r15ClassifyExpr(ctx, u.words[start + 1]);
        if (!sig) { bad = 'unclassifiable array index expression'; break; }
        const key = sig.kind === 'const' ? 'c' + sig.value : 'p' + sig.byteOff + 'a' + sig.add;
        sigKeys.set(key, sig);
        const cu = r15PointerUses(ctx, u.resultId);
        if (cu.writes > 0) { bad = 'store through an array element'; break; }
        if (cu.escapes.length) { bad = 'array element pointer escape (' + cu.escapes[0] + ')'; break; }
        rec.signatures.push({ sig, chainId: u.resultId });
      } else {
        bad = 'array variable use op' + u.op;
      }
    }
    if (bad) rec.reason = bad;
    else if (!rec.signatures.length) rec.reason = 'no accesses';
    else if (sigKeys.size !== 1) rec.reason = sigKeys.size + ' distinct index signatures';
    else rec.servable = true;
    arrays.push(rec);
  }
  return { ctx, arrays };
}

// Physical-storage-buffer pointer materializations, grouped into root
// slots by push byte offset, with a per-slot servability verdict.
export function analyzeRootDescriptors(mod) {
  const ctx = r15Context(mod);
  const mats = [];
  for (const ins of mod.insns) {
    if (ins.op !== R15_OP.ConvertUToPtr && ins.op !== R15_OP.Bitcast) continue;
    const rt = ctx.byId.get(ins.resultType);
    if (!rt || rt.op !== Op.TypePointer || rt.words[1] !== R15_PSB) continue;
    const provenance = r15ClassifyExpr(ctx, ins.words[2]) || r15ClassifyVec2Addr(ctx, ins.words[2]);
    const uses = r15PointerUses(ctx, ins.resultId);
    mats.push({
      ins, matId: ins.resultId, ptrTypeId: ins.resultType, pointeeId: rt.words[2],
      form: ins.op === R15_OP.Bitcast ? 'bitcast' : 'convert', provenance, uses,
    });
  }
  const out = { ctx, mats, slots: [], moduleServable: false, rejectReason: null, flipStructs: [] };
  if (!mats.length) return out;
  const bySlot = new Map();
  for (const m of mats) {
    if (!m.provenance || m.provenance.kind !== 'push') {
      out.rejectReason = 'pointer %' + m.matId + ' address does not trace to a single push-block load';
      return out;
    }
    if (m.provenance.add !== 0 || (m.provenance.addHi || 0) !== 0) {
      out.rejectReason = 'pointer %' + m.matId + ' carries a nonzero address addend (' +
        (m.provenance.vec2 ? '0x' + (m.provenance.addHi >>> 0).toString(16) + ':' + (m.provenance.add >>> 0).toString(16) : m.provenance.add) + ')';
      return out;
    }
    if (m.provenance.byteOff % 8 !== 0) {
      out.rejectReason = 'root slot push offset ' + m.provenance.byteOff + ' is not 8-byte aligned';
      return out;
    }
    const key = m.provenance.byteOff;
    if (!bySlot.has(key)) bySlot.set(key, { byteOff: key, mats: [], writable: false, atomicWrites: 0, escapes: [], pointees: new Set(), coneTypes: new Set() });
    const s = bySlot.get(key);
    s.mats.push(m);
    if (m.uses.writes > 0) s.writable = true;
    s.atomicWrites += m.uses.atomicWrites || 0;
    s.escapes.push(...m.uses.escapes);
    s.pointees.add(m.pointeeId);
    for (const c of m.uses.cone) s.coneTypes.add(c.resultType);
  }
  out.slots = [...bySlot.values()].sort((a, b) => a.byteOff - b.byteOff);
  // Every PhysicalStorageBuffer pointer type in the module must belong to
  // a servable cone (or be entirely unreferenced, in which case the
  // rewrite deletes it): after the rewrite no PSB type may remain live.
  const matTypes = new Set(mats.map(m => m.ptrTypeId));
  const coneTypes = new Set();
  for (const s of out.slots) for (const t of s.coneTypes) coneTypes.add(t);
  const referenced = new Set();
  for (const ins of mod.insns) for (const w of ins.words) referenced.add(w);
  for (const ins of mod.insns) {
    if (ins.op !== Op.TypePointer || ins.words[1] !== R15_PSB) continue;
    const tid = ins.resultId;
    if (matTypes.has(tid) || coneTypes.has(tid)) continue;
    if (referenced.has(tid) && ins.resultId != null) {
      // referenced by something outside the analyzed cones (its own
      // declaration does not count: resultId is not an operand word)
      let external = false;
      for (const u of mod.insns) {
        if (u === ins) continue;
        if (u.words.includes(tid)) { external = true; break; }
      }
      if (external) {
        out.rejectReason = 'PhysicalStorageBuffer pointer type %' + tid + ' is used outside a root-descriptor cone';
        return out;
      }
    }
  }
  const flip = new Set();
  for (const s of out.slots) {
    if (s.atomicWrites > 0) { out.rejectReason = 'root slot push@' + s.byteOff + ' is written through an atomic operation (atomics not served)'; return out; }
    if (s.escapes.length) { out.rejectReason = 'root slot push@' + s.byteOff + ' pointer escapes (' + s.escapes[0] + ')'; return out; }
    if (s.pointees.size !== 1) { out.rejectReason = 'root slot push@' + s.byteOff + ' has ' + s.pointees.size + ' distinct pointee types'; return out; }
    const pointeeId = [...s.pointees][0];
    s.pointeeId = pointeeId;
    const measure = r15StorageMeasure(ctx, pointeeId, null);
    if (!measure || !measure.ok) { out.rejectReason = 'root slot push@' + s.byteOff + ' pointee layout is not byte-exact in WGSL storage layout'; return out; }
    const pt = ctx.byId.get(pointeeId);
    if (pt && pt.op === Op.TypeStruct) {
      const kind = ctx.structKind.get(pointeeId);
      if (kind !== 'block' && kind !== 'bufferblock') { out.rejectReason = 'root pointee struct %' + pointeeId + ' is neither Block nor BufferBlock'; return out; }
      // The Block -> BufferBlock flip is only sound when no other pointer
      // type or variable reaches the struct in a different storage class.
      for (const u of mod.insns) {
        if (u.op === Op.TypePointer && u.words[2] === pointeeId && u.words[1] !== R15_PSB) {
          out.rejectReason = 'root pointee struct %' + pointeeId + ' is shared with a non-PSB pointer type';
          return out;
        }
      }
      if (kind === 'block') flip.add(pointeeId);
    }
  }
  // The synthesized bindings must not collide with a module-owned
  // binding in the chosen set (the rewrite picks the heap set exactly
  // this way; mirrored in the bridge audit).
  {
    const heapSets = new Set();
    for (const ins of mod.insns) {
      if (ins.op !== Op.Variable) continue;
      const ptr = ctx.byId.get(ins.words[0]);
      if (!ptr || ptr.op !== Op.TypePointer) continue;
      const pointee = ctx.byId.get(ptr.words[2]);
      if (pointee && pointee.op === Op.TypeRuntimeArray) {
        const d = ctx.varDeco.get(ins.resultId) || {};
        heapSets.add(d.set ?? 0);
      }
    }
    const heapSet = heapSets.size === 1 ? [...heapSets][0] : 0;
    const occupied = new Set();
    for (const [id, d] of ctx.varDeco) {
      if (typeof d.set === 'number' && typeof d.binding === 'number') occupied.add(d.set + ':' + d.binding);
    }
    for (const sl of out.slots) {
      const binding = 4096 + sl.byteOff / 8;
      if (occupied.has(heapSet + ':' + binding)) {
        out.rejectReason = 'synthesized root binding ' + binding + ' in set ' + heapSet + ' collides with a module-owned binding';
        return out;
      }
    }
  }
  out.flipStructs = [...flip];
  out.moduleServable = true;
  return out;
}

// --- R15 rewrites -------------------------------------------------------------

function r15EntryPointTail(ins) {
  // Index of the first interface id word in an OpEntryPoint instruction
  // (after execution model, entry id, and the NUL-terminated name).
  let k = 2;
  while (k < ins.words.length) {
    const w = ins.words[k++];
    if ((w & 0xff) === 0 || ((w >>> 8) & 0xff) === 0 || ((w >>> 16) & 0xff) === 0 || ((w >>> 24) & 0xff) === 0) break;
  }
  return k;
}

function r15SpliceAdditions(mod, addAnnot, addTypes, addNames) {
  if (addAnnot.length) {
    const s = sections(mod);
    mod.insns.splice(Math.max(s.lastAnnot + 1, 0), 0, ...addAnnot);
  }
  if (addTypes.length) {
    const s = sections(mod);
    mod.insns.splice(s.firstFunc < 0 ? mod.insns.length : s.firstFunc, 0, ...addTypes);
  }
  if (addNames.length) {
    const s = sections(mod);
    mod.insns.splice(Math.max(s.lastDebug + 1, 0), 0, ...addNames);
  }
}

// Strip the Aligned operand from a Load/Store memory-operand list (the
// re-rooted storage binding carries the alignment contract through the
// audited layout; the operand would otherwise promise an alignment the
// VA's offset inside its buffer does not guarantee).
function r15StripAligned(ins, memIdx) {
  if (ins.words.length <= memIdx) return;
  let mask = ins.words[memIdx];
  if (!(mask & 2)) return;
  const kept = [];
  let src = memIdx + 1;
  const bits = [[1, 0], [2, 1], [4, 0], [8, 1], [16, 1], [32, 0]];
  for (const [bit, nops] of bits) {
    if (!(mask & bit)) continue;
    if (bit !== 2) { kept.push(...ins.words.slice(src, src + nops)); }
    src += nops;
  }
  mask &= ~2;
  if (mask === 0) ins.words.length = memIdx;
  else { ins.words[memIdx] = mask; ins.words.splice(memIdx + 1, ins.words.length - (memIdx + 1), ...kept); }
}

export function specializeDescriptorArrays(mod, log) {
  const { ctx, arrays } = analyzeDescriptorArrays(mod);
  const targets = arrays.filter(a => a.servable);
  for (const a of arrays) {
    if (!a.servable && a.signatures.length) log.push('spirvfix: left descriptor array %' + a.varId + ' unchanged (' + a.reason + ')');
  }
  if (!targets.length) return false;
  const nextId = makeIdSource(mod);
  const addTypes = [], addAnnot = [], addNames = [];
  const drop = new Set();
  for (const a of targets) {
    const sig = a.signatures[0].sig;
    let clonePtrId = null;
    for (const ins of mod.insns) {
      if (ins.op === Op.TypePointer && ins.words[1] === a.sc && ins.words[2] === a.elemTypeId) { clonePtrId = ins.resultId; break; }
    }
    if (clonePtrId == null) {
      clonePtrId = nextId();
      addTypes.push({ op: Op.TypePointer, words: [clonePtrId, a.sc, a.elemTypeId], resultType: null, resultId: clonePtrId });
    }
    const cloneId = nextId();
    addTypes.push({ op: Op.Variable, words: [clonePtrId, cloneId, a.sc], resultType: clonePtrId, resultId: cloneId });
    const suffix = sig.kind === 'const' ? '_c' + sig.value : '_p' + sig.byteOff + '_a' + sig.add;
    addNames.push({ op: Op.Name, words: [cloneId, ...strWords('bw_arr_g' + a.set + '_b' + a.binding + suffix)], resultType: null, resultId: null });
    for (const dins of (ctx.varDeco.get(a.varId) || {}).decos || []) {
      addAnnot.push({ op: Op.Decorate, words: [cloneId, ...dins.words.slice(1)], resultType: null, resultId: null });
    }
    for (const s of a.signatures) {
      const chain = ctx.byId.get(s.chainId);
      chain.op = Op.CopyObject;
      chain.words = [chain.resultType, chain.resultId, cloneId];
    }
    for (const ins of mod.insns) {
      if (ins.op !== Op.EntryPoint) continue;
      const tail = r15EntryPointTail(ins);
      let found = false;
      for (let k = tail; k < ins.words.length; k++) if (ins.words[k] === a.varId) { ins.words[k] = cloneId; found = true; }
      if (!found) ins.words.push(cloneId);
    }
    drop.add(a.varIns);
    for (const dins of (ctx.varDeco.get(a.varId) || {}).decos || []) drop.add(dins);
    log.push('spirvfix: specialized descriptor array %' + a.varId + ' (set ' + a.set + ' binding ' + a.binding + ') to scalar clone %' + cloneId + ' (bw_arr' + suffix + ')' + (a.writable ? ' (writable)' : ''));
  }
  mod.insns = mod.insns.filter(i => !drop.has(i));
  r15SpliceAdditions(mod, addAnnot, addTypes, addNames);
  // Orphan sweep: delete array / runtime-array / pointer TYPE declarations
  // the specialization left unreferenced (references from decorations and
  // names do not count; their decorations die with them). If no runtime
  // array type survives, the RuntimeDescriptorArray capability goes too --
  // the audit mirror admits cap 5302 under exactly this condition.
  let swept = true;
  while (swept) {
    swept = false;
    const live = new Set();
    for (const ins of mod.insns) {
      if (ins.op === Op.Decorate || ins.op === Op.MemberDecorate || ins.op === Op.Name || ins.op === Op.MemberName) continue;
      // Header/directive instructions carry enums, strings, or variable
      // ids -- never type ids -- so their words are not references (a
      // capability NUMBER equal to a type id must not keep the type
      // alive; mirrored in the bridge audit's skip5302 simulation).
      if (ins.op === Op.SourceContinued || ins.op === Op.Source || ins.op === Op.SourceExtension ||
          ins.op === Op.String || ins.op === Op.Extension || ins.op === Op.MemoryModel ||
          ins.op === Op.EntryPoint || ins.op === Op.ExecutionMode || ins.op === Op.Capability) continue;
      // resultType (words[0]) is a use; the resultId word is a definition.
      if (ins.resultType != null) live.add(ins.words[0]);
      const start = ins.resultType != null ? 2 : ins.resultId != null ? 1 : 0;
      for (let k = start; k < ins.words.length; k++) live.add(ins.words[k]);
    }
    const dead = new Set();
    for (const ins of mod.insns) {
      if ((ins.op === Op.TypeArray || ins.op === Op.TypeRuntimeArray || ins.op === Op.TypePointer) && ins.resultId != null && !live.has(ins.resultId)) dead.add(ins.resultId);
    }
    if (dead.size) {
      mod.insns = mod.insns.filter(i => {
        if (i.resultId != null && dead.has(i.resultId) && (i.op === Op.TypeArray || i.op === Op.TypeRuntimeArray || i.op === Op.TypePointer)) return false;
        if ((i.op === Op.Decorate || i.op === Op.MemberDecorate || i.op === Op.Name || i.op === Op.MemberName) && dead.has(i.words[0])) return false;
        return true;
      });
      swept = true;
    }
  }
  if (!mod.insns.some(i => i.op === Op.TypeRuntimeArray)) {
    mod.insns = mod.insns.filter(i => !(i.op === Op.Capability && i.words[0] === 5302));
  }
  return true;
}

export function rewriteRootDescriptors(mod, log) {
  const analysis = analyzeRootDescriptors(mod);
  const { ctx } = analysis;
  if (!analysis.mats.length) return false;
  if (!analysis.moduleServable) {
    log.push('spirvfix: left root-descriptor module unchanged (' + analysis.rejectReason + ')');
    return false;
  }
  // Binding set: the module's heap-array set when all its runtime-array
  // variables share one set, else set 0.
  const heapSets = new Set();
  for (const ins of mod.insns) {
    if (ins.op !== Op.Variable) continue;
    const ptr = ctx.byId.get(ins.words[0]);
    if (!ptr || ptr.op !== Op.TypePointer) continue;
    const pointee = ctx.byId.get(ptr.words[2]);
    if (pointee && pointee.op === Op.TypeRuntimeArray) {
      const d = ctx.varDeco.get(ins.resultId) || {};
      heapSets.add(d.set ?? 0);
    }
  }
  const heapSet = heapSets.size === 1 ? [...heapSets][0] : 0;
  const nextId = makeIdSource(mod);
  const addTypes = [], addAnnot = [], addNames = [];
  const twinOf = new Map();
  const twinPointer = (ptrTypeId) => {
    if (twinOf.has(ptrTypeId)) return twinOf.get(ptrTypeId);
    const src = ctx.byId.get(ptrTypeId);
    const id = nextId();
    addTypes.push({ op: Op.TypePointer, words: [id, R15_STORAGE_BUFFER, src.words[2]], resultType: null, resultId: id });
    twinOf.set(ptrTypeId, id);
    return id;
  };
  const baseOf = new Map(); // old pointer value id -> new base value id
  const newVars = [];
  for (const slot of analysis.slots) {
    const binding = 4096 + slot.byteOff / 8;
    const varId = nextId();
    const ptrId = twinPointer(slot.mats[0].ptrTypeId);
    addTypes.push({ op: Op.Variable, words: [ptrId, varId, R15_STORAGE_BUFFER], resultType: ptrId, resultId: varId });
    addNames.push({ op: Op.Name, words: [varId, ...strWords('bw_root_p' + slot.byteOff)], resultType: null, resultId: null });
    addAnnot.push({ op: Op.Decorate, words: [varId, 34, heapSet], resultType: null, resultId: null });
    addAnnot.push({ op: Op.Decorate, words: [varId, 33, binding], resultType: null, resultId: null });
    if (!slot.writable) addAnnot.push({ op: Op.Decorate, words: [varId, 24], resultType: null, resultId: null });
    for (const m of slot.mats) baseOf.set(m.matId, varId);
    slot.newVarId = varId;
    newVars.push(varId);
    log.push('spirvfix: re-rooted root descriptor push@' + slot.byteOff + ' at set ' + heapSet + ' binding ' + binding + ' (bw_root_p' + slot.byteOff + (slot.writable ? ', writable' : '') + ')');
  }
  // Block -> BufferBlock on the pointee structs, pre-SPIR-V 1.3 only:
  // from 1.3 on, a StorageBuffer variable's struct keeps Block (BufferBlock
  // is rejected by spirv-val for 1.4+ modules), mirroring R13's rule.
  for (const structId of mod.header.version < 0x00010300 ? analysis.flipStructs : []) {
    for (const ins of mod.insns) {
      if (ins.op === Op.Decorate && ins.words[0] === structId && ins.words[1] === 2) ins.words[1] = 3;
    }
  }
  // Re-root chains and direct loads. Instructions are in SSA order, so a
  // single pass rewrites bases before their consumers.
  const rewritten = new Set(baseOf.keys());
  for (const ins of mod.insns) {
    if ((ins.op === Op.AccessChain || ins.op === Op.InBoundsAccessChain) && rewritten.has(ins.words[2])) {
      ins.resultType = twinPointer(ins.resultType);
      ins.words[0] = ins.resultType;
      ins.words[2] = baseOf.get(ins.words[2]);
      baseOf.set(ins.resultId, ins.resultId);
      rewritten.add(ins.resultId);
    } else if (ins.op === Op.CopyObject && rewritten.has(ins.words[2])) {
      ins.resultType = twinPointer(ins.resultType);
      ins.words[0] = ins.resultType;
      ins.words[2] = baseOf.get(ins.words[2]);
      baseOf.set(ins.resultId, ins.resultId);
      rewritten.add(ins.resultId);
    } else if (ins.op === Op.Load && rewritten.has(ins.words[2])) {
      ins.words[2] = baseOf.get(ins.words[2]);
      r15StripAligned(ins, 3);
    } else if (ins.op === Op.Store && rewritten.has(ins.words[0])) {
      // A store through a re-rooted chain needs no base change (the
      // chain was rewritten in place); a store directly on a
      // materialization takes the synthesized variable. Either way the
      // Aligned operand goes, as for loads.
      if (baseOf.has(ins.words[0])) ins.words[0] = baseOf.get(ins.words[0]);
      r15StripAligned(ins, 2);
    }
  }
  // Delete the materializations, then sweep the address-provenance cone
  // (loads / chains / arithmetic that fed only deleted materializations).
  const drop = new Set(analysis.mats.map(m => m.ins));
  const sweepOps = new Set([Op.Load, Op.AccessChain, Op.InBoundsAccessChain, Op.CopyObject, R15_OP.IAdd, R15_OP.ISub, R15_OP.Bitcast, Op.CompositeConstruct, Op.CompositeExtract, R15_OP.IAddCarry]);
  let grew = true;
  while (grew) {
    grew = false;
    const live = new Set();
    for (const ins of mod.insns) {
      if (drop.has(ins)) continue;
      if (ins.resultType != null) live.add(ins.words[0]);
      const start = ins.resultType != null ? 2 : ins.resultId != null ? 1 : 0;
      for (let k = start; k < ins.words.length; k++) live.add(ins.words[k]);
    }
    for (const ins of mod.insns) {
      if (drop.has(ins) || !sweepOps.has(ins.op) || ins.resultId == null) continue;
      if (!live.has(ins.resultId)) { drop.add(ins); grew = true; }
    }
  }
  mod.insns = mod.insns.filter(i => !drop.has(i));
  // No PhysicalStorageBuffer type may survive (analysis proved every one
  // was a cone type, a materialization type, or unreferenced).
  mod.insns = mod.insns.filter(i => !(i.op === Op.TypePointer && i.words[1] === R15_PSB));
  // Strip the capabilities, the extension, and the addressing model.
  mod.insns = mod.insns.filter(i => !(i.op === Op.Capability && (i.words[0] === 5346 || i.words[0] === 5347)));
  mod.insns = mod.insns.filter(i => !(i.op === R15_OP.Extension && new TextDecoder().decode(new Uint8Array(strOf(i, 0))) === 'SPV_KHR_physical_storage_buffer'));
  for (const ins of mod.insns) {
    if (ins.op === Op.MemoryModel && ins.words[0] === R15_PSB_MODEL) ins.words[0] = 0;
  }
  for (const ins of mod.insns) {
    if (ins.op !== Op.EntryPoint) continue;
    for (const v of newVars) ins.words.push(v);
  }
  r15SpliceAdditions(mod, addAnnot, addTypes, addNames);
  return true;
}

// R16 "strip-dead-undefs": dxbc-spirv emits OpUndef for DXIL temp registers
// that end up never read (vkd3d's root-UAV pixel shader carries three dead
// OpUndef %uint in main). naga 30.2.0's validator rejects the whole function
// over them ("Function [0] 'main' is invalid" -- verified against the
// vendored naga on the real d3d12-ps-root-uav module; removing exactly the
// dead undefs makes it translate, and the module still passes spirv-val).
// An OpUndef whose result id has NO consumer anywhere in the module is pure
// dead weight: removing it cannot change any value any instruction reads.
// Scope is fail-closed: only function-local undefs (global OpUndef feeds the
// constant pool, a different naga path), and any undef with even one word
// equal to its id in another instruction is kept (a literal operand that
// happens to equal the id therefore also keeps it -- the module then stays
// untranslated, never mistranslated).
function stripDeadUndefs(mod, log) {
  const OP_UNDEF = 1;
  let inFunc = false;
  const dead = [];
  const candidates = [];
  for (const ins of mod.insns) {
    if (ins.op === Op.Function) inFunc = true;
    else if (ins.op === Op.FunctionEnd) inFunc = false;
    else if (inFunc && ins.op === OP_UNDEF && ins.words.length === 2) candidates.push(ins); // parseSpirv leaves resultId null for OpUndef; its id is words[1]
  }
  if (!candidates.length) return false;
  for (const cand of candidates) {
    let uses = 0;
    for (const ins of mod.insns) {
      if (ins === cand) continue;
      for (const w of ins.words) if (w === cand.words[1]) uses++;
    }
    if (!uses) dead.push(cand);
  }
  if (!dead.length) return false;
  const deadSet = new Set(dead);
  mod.insns = mod.insns.filter(i => !deadSet.has(i));
  log.push('spirvfix: stripped ' + dead.length + ' dead OpUndef instruction(s) (naga rejects functions carrying them)');
  return true;
}

export function applySpirvFix(bytes, { specOverrides, storageFormats } = {}) {
  const u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const log = [], applied = [];
  let mod;
  try {
    mod = parseSpirv(u8);
  } catch (e) {
    return { bytes: u8, applied, log: ['parse failed: ' + (e.message || e)] };
  }
  try {
    if (texelBuffersTo2D(mod, log)) applied.push('texel-buffer-to-2d');
  } catch (e) {
    log.push('R12 failed: ' + (e.message || e));
  }
  try {
    if (splitCombinedSamplers(mod, log)) applied.push('split-combined-sampler');
  } catch (e) {
    log.push('R1 failed: ' + (e.message || e));
  }
  try {
    if (foldSpecConstants(mod, log, specOverrides)) applied.push('fold-spec-constants');
  } catch (e) {
    log.push('R2 failed: ' + (e.message || e));
  }
  try {
    if (stripPointBuiltins(mod, log)) applied.push('strip-point-builtins');
  } catch (e) {
    log.push('R3 failed: ' + (e.message || e));
  }
  const protectedTailDemoteKills = new Set();
  try {
    if (lowerTailDemotes(mod, log, protectedTailDemoteKills)) applied.push('tail-demote-to-kill');
  } catch (e) {
    log.push('R4 failed: ' + (e.message || e));
  }
  try {
    if (lowerMidShaderDiscards(mod, log, protectedTailDemoteKills)) applied.push('lower-mid-shader-discard');
  } catch (e) {
    log.push('R4 mid-shader discard rewrite failed: ' + (e.message || e));
  }
  try {
    if (replaceDrawParameterBuiltins(mod, log)) applied.push('replace-draw-parameters');
  } catch (e) {
    log.push('R5 used DrawParameters rewrite failed: ' + (e.message || e));
  }
  try {
    if (stripUnusedDrawParameters(mod, log)) applied.push('strip-draw-parameters');
  } catch (e) {
    log.push('R5 unused DrawParameters rewrite failed: ' + (e.message || e));
  }
  try {
    if (stripIndexDecorations(mod, log)) applied.push('strip-index-decorations');
  } catch (e) {
    log.push('R6 failed: ' + (e.message || e));
  }
  try {
    if (stripFloatControls(mod, log)) applied.push('strip-float-controls');
  } catch (e) {
    log.push('R7 failed: ' + (e.message || e));
  }
  try {
    if (stripUnusedPhysicalStorageBuffer(mod, log)) applied.push('strip-unused-physical-storage-buffer');
    if (stripDeadUndefs(mod, log)) applied.push('strip-dead-undefs');
  } catch (e) {
    log.push('R8 failed: ' + (e.message || e));
  }
  try {
    if (pruneSpecializedAliases(mod, log)) applied.push('prune-specialized-aliases');
  } catch (e) { log.push('R9 failed: ' + (e.message || e)); }
  try {
    if (flatIntegerVaryings(mod, log)) applied.push('flat-integer-varyings');
  } catch (e) { log.push('R10 failed: ' + (e.message || e)); }
  try {
    if (fragmentLayerToZero(mod, log)) applied.push('fragment-layer-to-zero');
  } catch (e) { log.push('R11 failed: ' + (e.message || e)); }
  try {
    if (specializeDescriptorArrays(mod, log)) applied.push('descriptor-array-specialize');
  } catch (e) { log.push('R15a failed: ' + (e.message || e)); }
  try {
    if (rewriteRootDescriptors(mod, log)) applied.push('root-descriptor-bda');
  } catch (e) { log.push('R15b failed: ' + (e.message || e)); }
  try {
    if (uniformBlocksToStorage(mod, log)) applied.push('uniform-block-to-storage');
  } catch (e) { log.push('R13 failed: ' + (e.message || e)); }
  try {
    if (injectStorageImageFormats(mod, log, storageFormats)) applied.push('storage-image-format');
  } catch (e) { log.push('R14 failed: ' + (e.message || e)); }
  if (!applied.length) return { bytes: u8, applied, log };
  try {
    return { bytes: serializeSpirv(mod), applied, log };
  } catch (e) {
    return { bytes: u8, applied: [], log: [...log, 'serialize failed: ' + (e.message || e)] };
  }
}

export const REWRITES = ['texel-buffer-to-2d', 'split-combined-sampler', 'fold-spec-constants', 'strip-point-builtins', 'tail-demote-to-kill', 'lower-mid-shader-discard', 'replace-draw-parameters', 'strip-draw-parameters', 'strip-index-decorations', 'strip-float-controls', 'strip-unused-physical-storage-buffer', 'prune-specialized-aliases', 'flat-integer-varyings', 'fragment-layer-to-zero', 'uniform-block-to-storage', 'storage-image-format', 'descriptor-array-specialize', 'root-descriptor-bda', 'strip-dead-undefs'];
