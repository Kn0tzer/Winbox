// census.mjs -- R15 lane census tool (2026-10-27).
// For each .spv argument: capabilities, entry points, descriptor variables
// (set/binding + resolved pointee shape), and -- the design-deciding part --
// a def-use classification of every array-index expression and every
// OpConvertUToPtr address operand:
//   const <n>                    compile-time constant
//   push@<byteOff> (+ consts)    loaded from the push-constant block
//   buffer-load var %id          loaded from a descriptor buffer
//   other:<op>                   anything else (dynamic, unproven)
// Operand layout follows spirvfix parseSpirv: ins.words excludes the opcode
// word; for HAS_TYPE_ID ops words[0]=resultType, words[1]=resultId and real
// operands start at words[2]; for HAS_ID_ONLY ops words[0]=resultId and
// operands start at words[1]; other ops start at words[0].
// Usage: node tools/vkd3d/bda-research/census.mjs file.spv [more.spv ...]
import { readFileSync } from 'node:fs';
import { parseSpirv, analyzeRootDescriptors, analyzeDescriptorArrays } from '../../spirvfix/spirvfix.mjs';

const CAP_NAMES = { 5302: 'RuntimeDescriptorArray', 5306: 'UniformBufferArrayNonUniformIndexing',
  5307: 'SampledImageArrayNonUniformIndexing', 5308: 'StorageBufferArrayNonUniformIndexing',
  5309: 'StorageImageArrayNonUniformIndexing', 5346: 'PhysicalStorageBufferAddressesEXT',
  5347: 'PhysicalStorageBufferAddresses' };
const SC_NAMES = { 0: 'UniformConstant', 1: 'Input', 2: 'Uniform', 3: 'Output', 4: 'Workgroup',
  5: 'CrossWorkgroup', 6: 'Private', 7: 'Function', 8: 'Generic', 9: 'PushConstant',
  10: 'AtomicCounter', 11: 'Image', 12: 'StorageBuffer', 5349: 'PhysicalStorageBuffer' };

function census(path) {
  const bytes = readFileSync(path);
  const mod = parseSpirv(new Uint8Array(bytes));
  const byId = new Map(), types = new Map(), consts = new Map();
  const decos = new Map(), memberDecos = new Map(), names = new Map();
  const vars = [], caps = [], entries = [];
  const pushVars = new Set();
  for (const ins of mod.insns) {
    if (ins.resultId != null) byId.set(ins.resultId, ins);
    switch (ins.op) {
      case 17: caps.push(ins.words[0]); break;
      case 15: entries.push(ins.words[0]); break;
      case 5: { // OpName: [target, string...]
        let s = ''; for (let k = 1; k < ins.words.length; k++) { let w = ins.words[k]; for (let b = 0; b < 4; b++) { const c = w & 0xff; if (!c) break; s += String.fromCharCode(c); w >>>= 8; } }
        names.set(ins.words[0], s); break; }
      case 43: case 50: { // OpConstant / OpSpecConstant: [type, id, value...]
        const t = byId.get(ins.resultType);
        const vw = ins.words.slice(2);
        let v = null;
        if (t && t.op === 21) { const width = t.words[1], signed = t.words[2];
          if (width <= 32) { v = vw[0] | 0; if (!signed) v = vw[0] >>> 0; else if (width < 32) { const m = 1 << width; if (v >= m / 2) v -= m; } }
          else v = ((BigInt(vw[1] || 0) << 32n) | BigInt(vw[0] >>> 0)).toString(); }
        else if (t && t.op === 22) v = 'float';
        consts.set(ins.resultId, v); break; }
      case 28: types.set(ins.resultId, { kind: 'array', elem: ins.words[1], lenId: ins.words[2] }); break;
      case 29: types.set(ins.resultId, { kind: 'runtime-array', elem: ins.words[1] }); break;
      case 30: types.set(ins.resultId, { kind: 'struct', members: ins.words.slice(1) }); break;
      case 32: types.set(ins.resultId, { kind: 'pointer', sc: ins.words[1], pointee: ins.words[2] }); break;
      case 25: types.set(ins.resultId, { kind: 'image', sampledType: ins.words[1], dim: ins.words[2], depth: ins.words[3], arrayed: ins.words[4], ms: ins.words[5], sampled: ins.words[6], format: ins.words[7] }); break;
      case 26: types.set(ins.resultId, { kind: 'sampler' }); break;
      case 27: types.set(ins.resultId, { kind: 'sampled-image', image: ins.words[1] }); break;
      case 21: types.set(ins.resultId, { kind: 'int', width: ins.words[1], signed: ins.words[2] }); break;
      case 22: types.set(ins.resultId, { kind: 'float', width: ins.words[1] }); break;
      case 23: types.set(ins.resultId, { kind: 'vector', elem: ins.words[1], n: ins.words[2] }); break;
      case 24: types.set(ins.resultId, { kind: 'matrix', col: ins.words[1], n: ins.words[2] }); break;
      case 71: { const d = decos.get(ins.words[0]) || new Map(); d.set(ins.words[1], ins.words.slice(2)); decos.set(ins.words[0], d); break; }
      case 72: { const key = ins.words[0] + ':' + ins.words[1]; const d = memberDecos.get(key) || new Map(); d.set(ins.words[2], ins.words.slice(3)); memberDecos.set(key, d); break; }
      case 59: { vars.push({ id: ins.resultId, type: ins.resultType, sc: ins.words[2] }); if (ins.words[2] === 9) pushVars.add(ins.resultId); break; }
    }
  }
  const typeName = (id, depth = 0) => {
    if (depth > 8) return '...';
    const t = types.get(id); if (!t) return '%' + id;
    switch (t.kind) {
      case 'pointer': return 'ptr<' + (SC_NAMES[t.sc] || t.sc) + ', ' + typeName(t.pointee, depth + 1) + '>';
      case 'array': return 'array<' + typeName(t.elem, depth + 1) + ', len=' + (consts.has(t.lenId) ? consts.get(t.lenId) : '%' + t.lenId) + '>';
      case 'runtime-array': return 'runtime-array<' + typeName(t.elem, depth + 1) + '>';
      case 'image': return 'image<dim=' + t.dim + ', sampled=' + t.sampled + ', fmt=' + t.format + '>';
      case 'struct': return 'struct{' + t.members.map(m => typeName(m, depth + 1)).join(', ') + '}';
      case 'vector': return 'vec' + t.n + '<' + typeName(t.elem, depth + 1) + '>';
      case 'matrix': return 'mat' + t.n + '<' + typeName(t.col, depth + 1) + '>';
      case 'int': return (t.signed ? 'i' : 'u') + t.width;
      case 'float': return 'f' + t.width;
      default: return t.kind;
    }
  };
  // Byte offset of a push-constant access chain ([type,id,base,idx...]);
  // indices must be constants; struct via Offset (35), array via
  // ArrayStride (6), vector/matrix by element size. Null when unprovable.
  const pushByteOffset = (chainIns) => {
    let off = 0;
    const varIns = byId.get(chainIns.words[2]);
    if (!varIns) return null;
    const basePtr = types.get(varIns.resultType);
    if (!basePtr || basePtr.kind !== 'pointer') return null;
    let curType = basePtr.pointee;
    for (let k = 3; k < chainIns.words.length; k++) {
      const idxId = chainIns.words[k];
      if (!consts.has(idxId)) return null;
      const idx = Number(consts.get(idxId));
      const t = types.get(curType);
      if (!t) return null;
      if (t.kind === 'struct') {
        const d = memberDecos.get(curType + ':' + idx);
        if (!d || !d.has(35)) return null;
        off += d.get(35)[0]; curType = t.members[idx];
      } else if (t.kind === 'array') {
        const d = decos.get(curType);
        if (!d || !d.has(6)) return null;
        off += idx * d.get(6)[0]; curType = t.elem;
      } else if (t.kind === 'vector') {
        const el = types.get(t.elem); if (!el || (el.kind !== 'int' && el.kind !== 'float')) return null;
        off += idx * (el.width / 8); curType = t.elem;
      } else return null;
    }
    return off;
  };
  const classify = (id, depth = 0) => {
    if (depth > 24) return { kind: 'deep' };
    if (consts.has(id)) return { kind: 'const', value: consts.get(id) };
    const ins = byId.get(id);
    if (!ins) return { kind: 'unknown' };
    switch (ins.op) {
      case 83: case 124: return classify(ins.words[2], depth + 1); // CopyObject / Bitcast
      case 128: case 129: { // IAdd / ISub: [type,id,a,b]
        const a = classify(ins.words[2], depth + 1), b = classify(ins.words[3], depth + 1);
        if (a.kind === 'const' && b.kind === 'const') return { kind: 'const', value: ins.op === 128 ? a.value + b.value : a.value - b.value };
        if (a.kind === 'push' && b.kind === 'const') return { kind: 'push', byteOff: a.byteOff, add: (a.add || 0) + (ins.op === 128 ? Number(b.value) : -Number(b.value)) };
        if (b.kind === 'push' && a.kind === 'const' && ins.op === 128) return { kind: 'push', byteOff: b.byteOff, add: (b.add || 0) + Number(a.value) };
        return { kind: 'mixed', a: a.kind, b: b.kind };
      }
      case 61: { // Load: [type,id,pointer]
        const ptrId = ins.words[2];
        const ptrIns = byId.get(ptrId);
        if (ptrIns && (ptrIns.op === 65 || ptrIns.op === 66)) {
          if (pushVars.has(ptrIns.words[2])) {
            const off = pushByteOffset(ptrIns);
            return off == null ? { kind: 'push-unproven-offset' } : { kind: 'push', byteOff: off };
          }
          const rootIns = byId.get(ptrIns.words[2]);
          if (rootIns && rootIns.op === 59) return { kind: 'var-load', var: ptrIns.words[2], sc: SC_NAMES[rootIns.words[2]] || rootIns.words[2] };
          return { kind: 'chain-load', rootOp: rootIns ? rootIns.op : -1 };
        }
        if (ptrIns && ptrIns.op === 59) {
          if (pushVars.has(ptrId)) return { kind: 'push-whole' };
          return { kind: 'var-load', var: ptrId, sc: SC_NAMES[ptrIns.words[2]] || ptrIns.words[2] };
        }
        return { kind: 'load-other', ptrOp: ptrIns ? ptrIns.op : -1 };
      }
      case 120: return { kind: 'convert-u-to-ptr' };
      case 57: return { kind: 'call' };
      case 55: return { kind: 'param' };
      case 251: return { kind: 'phi' };
      default: return { kind: 'op' + ins.op };
    }
  };
  const out = { file: path, caps: caps.map(c => CAP_NAMES[c] || c), entryModels: entries };
  out.vars = vars.map(v => {
    const d = decos.get(v.id);
    return { id: v.id, name: names.get(v.id) || '', sc: SC_NAMES[v.sc] || v.sc,
      set: d && d.has(34) ? d.get(34)[0] : null, binding: d && d.has(33) ? d.get(33)[0] : null,
      nonWritable: d ? d.has(24) : false, type: typeName(v.type) };
  });
  out.arrayAccesses = [];
  const arrayVarIds = new Set();
  for (const v of vars) {
    const ptr = types.get(v.type); if (!ptr || ptr.kind !== 'pointer') continue;
    const pt = types.get(ptr.pointee);
    if (pt && (pt.kind === 'array' || pt.kind === 'runtime-array')) arrayVarIds.add(v.id);
  }
  for (const ins of mod.insns) {
    if ((ins.op === 65 || ins.op === 66 || ins.op === 67) && arrayVarIds.has(ins.words[2])) {
      const idxPos = ins.op === 67 ? 4 : 3; // PtrAccessChain has an extra element operand
      out.arrayAccesses.push({ var: ins.words[2], index: classify(ins.words[idxPos]) });
    }
  }
  out.ptrConverts = [];
  for (const ins of mod.insns) {
    if (ins.op === 120) out.ptrConverts.push({ op: 'UToPtr', addr: classify(ins.words[2]) });
    if (ins.op === 121) out.ptrConverts.push({ op: 'PtrToU', operand: ins.words[2] });
  }
  out.physicalPointerTypes = [...types.values()].filter(t => t.kind === 'pointer' && t.sc === 5349).length;
  out.hasDescriptorIndexingExt = new TextDecoder().decode(bytes).includes('SPV_EXT_descriptor_indexing');
  // R15 classifiers (shared with spirvfix, mirrored by the bridge audit):
  // root-descriptor pointer materializations and descriptor arrays.
  try {
    const ra = analyzeRootDescriptors(mod);
    out.rootDescriptors = {
      materializations: ra.mats.map(m => ({ id: m.matId, form: m.form,
        provenance: m.provenance, reads: m.uses.reads, writes: m.uses.writes, escapes: m.uses.escapes })),
      slots: ra.slots.map(sl => ({ byteOff: sl.byteOff, writable: sl.writable, pointee: sl.pointeeId ?? null })),
      moduleServable: ra.moduleServable, rejectReason: ra.rejectReason,
    };
    const aa = analyzeDescriptorArrays(mod);
    out.descriptorArrays = aa.arrays.map(a => ({ varId: a.varId, set: a.set, binding: a.binding,
      elemKind: a.elemKind, servable: a.servable, reason: a.reason,
      signatures: a.signatures.map(x => x.sig) }));
  } catch (e) {
    out.r15Error = String((e && e.message) || e);
  }
  return out;
}

for (const f of process.argv.slice(2)) {
  try { console.log(JSON.stringify(census(f), null, 1)); }
  catch (e) { console.log(JSON.stringify({ file: f, error: String(e && e.message || e) })); }
}
