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
//   R7 "strip-signed-zero-inf-nan-preserve": naga rejects OpCapability
//      SignedZeroInfNanPreserve (4466) with UnsupportedCapability.
//      vkd3d-proton/vkd3d-shader emits it (with OpExecutionMode 4461) for FP
//      control. Rewrite: drop the capability and any OpExecutionMode
//      SignedZeroInfNanPreserve instructions. Safe for rendering: it only
//      governs NaN/inf edge-case preservation.
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
  Variable: 59, Load: 61, Store: 62, AccessChain: 65,
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

// R7 "strip-signed-zero-inf-nan-preserve": naga 30.2.0 rejects OpCapability
// SignedZeroInfNanPreserve (4466) with UnsupportedCapability. vkd3d-proton's
// vkd3d-shader emits it together with OpExecutionMode SignedZeroInfNanPreserve
// (4461) for floating-point control. Drop the capability and any such
// execution-mode instructions; without the capability naga never sees it.
// Rendering-safe: the mode only governs NaN/inf signed-zero preservation,
// irrelevant to the pixel values these shaders compute.
function stripSignedZeroInfNanPreserve(mod, log) {
  const hasCap = mod.insns.some(i => i.op === 17 && i.words[0] === 4466);
  const modes = mod.insns.filter(i => i.op === Op.ExecutionMode && i.words[1] === 4461);
  const exts = mod.insns.filter(i => i.op === Op.ExtInstImport &&
    new TextDecoder().decode(new Uint8Array(strOf(i, 0))) === 'SPV_KHR_float_controls');
  if (!hasCap && !modes.length && !exts.length) return false;
  mod.insns = mod.insns.filter(i => !((i.op === 17 && i.words[0] === 4466) ||
    (i.op === Op.ExecutionMode && i.words[1] === 4461) ||
    (i.op === Op.ExtInstImport &&
      new TextDecoder().decode(new Uint8Array(strOf(i, 0))) === 'SPV_KHR_float_controls')));
  const what = [hasCap ? 'capability' : null,
    modes.length ? modes.length + ' execution mode(s)' : null,
    exts.length ? exts.length + ' OpExtension(s)' : null].filter(Boolean).join(' + ');
  log.push('spirvfix: stripped SignedZeroInfNanPreserve (' + what + ')');
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
    (i.op === Op.TypePointer && i.words[0] === 5349)); // PhysicalStorageBuffer storage class
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
      if (ins.op === Op.Load && ins.words[2] === id) loads.push(ins);
      else if (ins.words.includes(id) && ins.op !== Op.Variable) {
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
  const firstVariable = mod.insns.findIndex(i => i.op === Op.Variable);
  mod.insns.splice(firstVariable < 0 ? sections(mod).firstFunc : firstVariable, 0, ...constants);
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
export function applySpirvFix(bytes, { specOverrides } = {}) {
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
    if (stripSignedZeroInfNanPreserve(mod, log)) applied.push('strip-signed-zero-inf-nan-preserve');
  } catch (e) {
    log.push('R7 failed: ' + (e.message || e));
  }
  try {
    if (stripUnusedPhysicalStorageBuffer(mod, log)) applied.push('strip-unused-physical-storage-buffer');
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
  if (!applied.length) return { bytes: u8, applied, log };
  try {
    return { bytes: serializeSpirv(mod), applied, log };
  } catch (e) {
    return { bytes: u8, applied: [], log: [...log, 'serialize failed: ' + (e.message || e)] };
  }
}

export const REWRITES = ['texel-buffer-to-2d', 'split-combined-sampler', 'fold-spec-constants', 'strip-point-builtins', 'tail-demote-to-kill', 'lower-mid-shader-discard', 'replace-draw-parameters', 'strip-draw-parameters', 'strip-index-decorations', 'strip-signed-zero-inf-nan-preserve', 'strip-unused-physical-storage-buffer', 'prune-specialized-aliases', 'flat-integer-varyings', 'fragment-layer-to-zero'];
