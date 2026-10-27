// vk_gs_passthrough.h -- geometry-stage disposition analysis for the V2
// Vulkan bridge (see vk64bridge.cpp, "geometry-stage").
//
// WebGPU has no geometry stage, so a guest geometry shader can never be
// translated. Most geometry modules must therefore fail loudly at draw
// time. One shape is provably exempt: the layer-passthrough geometry
// shader. DXVK's meta passes (copy / blit / resolve) route a fullscreen
// triangle to a render-target layer through a geometry shader that does
// exactly three things per input vertex: copy each input varying to the
// corresponding output, copy the built-in Position through, and write
// BuiltIn Layer from an input (the instance index). It computes nothing,
// emits each input primitive exactly once, and has no side effects.
//
// For such a module, omitting the stage reproduces the guest-visible
// result whenever every fragment input arrives from the same vertex
// output with or without the stage in between (checked per pipeline by
// stripServeOk): the only information the stage adds is the Layer value,
// and the bridge's dynamic-rendering disposition already collapses
// layered rendering to layer 0 (loudly, "layered-rendering"), which is
// also where the fragment tier reads Layer from (spirvfix R11 rewrites
// fragment Layer loads to 0). Draws under any OTHER geometry shader are
// dropped at submit ("geometry-stage-draw").
//
// The analysis is deliberately conservative: any construct it does not
// understand makes it answer false. False positives in intermediate
// checks only ever push toward "not passthrough", never toward serving.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vkgs {

namespace detail {

// SPIR-V constants (unified1 grammar; values cross-checked against
// tools/dxvk/src/include/spirv/include/spirv/unified1/spirv.h).
enum : uint32_t {
    kOpExtInstImport = 11, kOpEntryPoint = 15, kOpExecutionMode = 16,
    kOpTypeInt = 21, kOpTypeArray = 28, kOpTypeRuntimeArray = 29,
    kOpTypeStruct = 30, kOpTypePointer = 32,
    kOpConstant = 43, kOpConstantNull = 46,
    kOpFunction = 54, kOpFunctionEnd = 56, kOpVariable = 59,
    kOpLoad = 61, kOpStore = 62, kOpAccessChain = 65, kOpCopyObject = 83,
    kOpIAdd = 128, kOpULessThan = 176, kOpSLessThan = 177,
    kOpEmitVertex = 218, kOpEndPrimitive = 219,
    kOpLoopMerge = 246, kOpSelectionMerge = 247, kOpLabel = 248,
    kOpBranch = 249, kOpBranchConditional = 250, kOpReturn = 253,
    kOpDecorate = 71, kOpMemberDecorate = 72,
    kModelGeometry = 3,
    kDecoBuiltIn = 11, kDecoLocation = 30,
    kBuiltinPosition = 0, kBuiltinPointSize = 1, kBuiltinClipDistance = 3,
    kBuiltinCullDistance = 4, kBuiltinLayer = 9, kBuiltinViewportIndex = 10,
    kModeInvocations = 0, kModeInputPoints = 19, kModeInputLines = 20,
    kModeInputLinesAdjacency = 21, kModeTriangles = 22,
    kModeInputTrianglesAdjacency = 23, kModeOutputVertices = 26,
    kModeOutputPoints = 27, kModeOutputLineStrip = 28,
    kModeOutputTriangleStrip = 29,
    kSCInput = 1, kSCOutput = 3, kSCFunction = 7,
};

struct Ins {
    uint32_t op;
    const uint32_t* w;   // operands (after the opcode word)
    uint32_t n;          // operand count
};

// An interface key: kind 0 = user Location, kind 1 = BuiltIn.
using Key = std::pair<uint32_t, uint32_t>;

struct Module {
    std::vector<Ins> insns;
    // result id -> instruction index in insns (only for instruction kinds
    // whose layout this analysis reads individually; see defOf use sites)
    std::unordered_map<uint32_t, size_t> defOf;
    std::unordered_map<uint32_t, uint32_t> constVal;      // OpConstant/Null id -> value (32-bit only)
    std::unordered_set<uint32_t> constUnknown;            // constants whose value is not a plain u32
    struct PtrInfo { uint32_t sc = 0, pointee = 0; };
    std::unordered_map<uint32_t, PtrInfo> ptrInfo;        // pointer type id -> {storage class, pointee type}
    std::unordered_map<uint32_t, std::vector<uint32_t>> structMembers; // struct type id -> member type ids
    std::unordered_map<uint32_t, uint32_t> arrayElem;     // array type id -> element type id
    struct VarInfo { uint32_t ptrType = 0, sc = 0; };
    std::unordered_map<uint32_t, VarInfo> vars;           // variable id -> {pointer type, storage class}
    std::unordered_map<uint32_t, uint32_t> varBuiltin;    // variable id -> BuiltIn
    std::unordered_map<uint32_t, uint32_t> varLocation;   // variable id -> Location
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> memberBuiltin;  // (struct type, member) -> BuiltIn
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> memberLocation; // (struct type, member) -> Location
};

inline bool parse(const uint32_t* w, size_t nw, Module& m) {
    if (!w || nw < 5 || w[0] != 0x07230203u) return false;
    for (size_t i = 5; i < nw;) {
        const uint32_t word = w[i];
        const uint32_t len = word >> 16;
        if (!len || i + len > nw) return false;
        m.insns.push_back({word & 0xFFFFu, w + i + 1, len - 1});
        i += len;
    }
    for (size_t idx = 0; idx < m.insns.size(); ++idx) {
        const Ins& in = m.insns[idx];
        const uint32_t* o = in.w;
        switch (in.op) {
        case kOpConstant:
            if (in.n >= 3) {
                m.defOf[o[1]] = idx;
                if (in.n == 3) m.constVal[o[1]] = o[2];
                else m.constUnknown.insert(o[1]);
            }
            break;
        case kOpConstantNull:
            if (in.n >= 2) { m.defOf[o[1]] = idx; m.constVal[o[1]] = 0; }
            break;
        case kOpTypePointer:
            if (in.n >= 3) m.ptrInfo[o[0]] = {o[1], o[2]};
            break;
        case kOpTypeStruct:
            if (in.n >= 1) {
                auto& v = m.structMembers[o[0]];
                for (uint32_t k = 1; k < in.n; ++k) v.push_back(o[k]);
            }
            break;
        case kOpTypeArray: case kOpTypeRuntimeArray:
            if (in.n >= 2) m.arrayElem[o[0]] = o[1];
            break;
        case kOpVariable:
            if (in.n >= 3) {
                m.defOf[o[1]] = idx;
                m.vars[o[1]] = {o[0], o[2]};
            }
            break;
        case kOpLoad: case kOpCopyObject: case kOpAccessChain:
        case kOpIAdd: case kOpULessThan: case kOpSLessThan:
            if (in.n >= 2) m.defOf[o[1]] = idx;
            break;
        case kOpDecorate:
            if (in.n >= 3 && o[1] == kDecoBuiltIn) m.varBuiltin[o[0]] = o[2];
            if (in.n >= 3 && o[1] == kDecoLocation) m.varLocation[o[0]] = o[2];
            break;
        case kOpMemberDecorate:
            if (in.n >= 4 && o[2] == kDecoBuiltIn) m.memberBuiltin[{o[0], o[1]}] = o[3];
            if (in.n >= 4 && o[2] == kDecoLocation) m.memberLocation[{o[0], o[1]}] = o[3];
            break;
        default: break;
        }
    }
    return true;
}

// Pointee type of a variable, unwrapping one array layer if present.
inline uint32_t pointeeStruct(const Module& m, uint32_t var) {
    auto v = m.vars.find(var);
    if (v == m.vars.end()) return 0;
    auto p = m.ptrInfo.find(v->second.ptrType);
    if (p == m.ptrInfo.end()) return 0;
    uint32_t t = p->second.pointee;
    auto a = m.arrayElem.find(t);
    if (a != m.arrayElem.end()) t = a->second;
    return m.structMembers.count(t) ? t : 0;
}
inline bool pointeeIsArray(const Module& m, uint32_t var) {
    auto v = m.vars.find(var);
    if (v == m.vars.end()) return false;
    auto p = m.ptrInfo.find(v->second.ptrType);
    if (p == m.ptrInfo.end()) return false;
    return m.arrayElem.count(p->second.pointee) != 0;
}

// Interface key of (variable, member path): builtin wins, else location.
// memberPath is the constant index path below the variable (for block
// members: the single member index; empty for standalone variables).
inline bool ifaceKey(const Module& m, uint32_t var, const std::vector<uint32_t>& memberPath, Key& out) {
    if (!memberPath.empty()) {
        const uint32_t st = pointeeStruct(m, var);
        if (st) {
            auto b = m.memberBuiltin.find({st, memberPath[0]});
            if (b != m.memberBuiltin.end()) { out = {1, b->second}; return true; }
            auto l = m.memberLocation.find({st, memberPath[0]});
            if (l != m.memberLocation.end()) { out = {0, l->second}; return true; }
        }
        return false;
    }
    auto b = m.varBuiltin.find(var);
    if (b != m.varBuiltin.end()) { out = {1, b->second}; return true; }
    auto l = m.varLocation.find(var);
    if (l != m.varLocation.end()) { out = {0, l->second}; return true; }
    return false;
}

enum class Prov { Unknown, InputChain, InputDirect, Const, FuncLoad };
struct Value {
    Prov prov = Prov::Unknown;
    uint32_t var = 0;                    // InputChain/InputDirect: source variable; FuncLoad: function variable
    std::vector<uint32_t> memberPath;    // InputChain: constant indices below the per-vertex index
    uint32_t constValue = 0;             // Const
};

// Resolve an AccessChain chain to (base variable, index operand ids).
inline bool resolveChain(const Module& m, uint32_t ptrId, uint32_t& baseVar, std::vector<uint32_t>& idxIds) {
    auto d = m.defOf.find(ptrId);
    if (d == m.defOf.end()) return false;
    const Ins& in = m.insns[d->second];
    if (in.op == kOpVariable) { baseVar = ptrId; idxIds.clear(); return true; }
    if (in.op != kOpAccessChain || in.n < 3) return false;
    if (!resolveChain(m, in.w[2], baseVar, idxIds)) return false;
    for (uint32_t k = 3; k < in.n; ++k) idxIds.push_back(in.w[k]);
    return true;
}

inline Value provenance(const Module& m, uint32_t id, int depth = 0);

inline Value loadProvenance(const Module& m, const Ins& load) {
    // OpLoad operands: [result type, result id, pointer, ...]
    uint32_t baseVar = 0;
    std::vector<uint32_t> idx;
    if (!resolveChain(m, load.w[2], baseVar, idx)) return {};
    auto v = m.vars.find(baseVar);
    if (v == m.vars.end()) return {};
    if (v->second.sc == kSCFunction) {
        if (!idx.empty()) return {};
        return {Prov::FuncLoad, baseVar, {}, 0};
    }
    if (v->second.sc != kSCInput) return {};
    if (idx.empty()) {
        if (pointeeIsArray(m, baseVar)) return {};   // whole-array load: cannot pair per-vertex
        return {Prov::InputDirect, baseVar, {}, 0};
    }
    Value out{Prov::InputChain, baseVar, {}, 0};
    // First index of an array-typed input is the per-vertex index (checked
    // by the caller); the remaining indices form the constant member path.
    const size_t start = pointeeIsArray(m, baseVar) ? 1 : 0;
    for (size_t k = start; k < idx.size(); ++k) {
        auto c = m.constVal.find(idx[k]);
        if (c == m.constVal.end()) return {};
        out.memberPath.push_back(c->second);
    }
    out.var = baseVar;
    return out;
}

inline Value provenance(const Module& m, uint32_t id, int depth) {
    if (depth > 8) return {};
    auto c = m.constVal.find(id);
    if (c != m.constVal.end()) return {Prov::Const, 0, {}, c->second};
    if (m.constUnknown.count(id)) return {};
    auto d = m.defOf.find(id);
    if (d == m.defOf.end()) return {};
    const Ins& in = m.insns[d->second];
    if (in.op == kOpLoad) return loadProvenance(m, in);
    if (in.op == kOpCopyObject && in.n >= 3) return provenance(m, in.w[2], depth + 1);
    return {};
}

struct GeomAnalysis {
    bool shape = false;
    // Output interface key -> input interface key, for passthrough stores.
    std::map<Key, Key> pairing;
    std::unordered_set<uint32_t> pairingConflict;  // output keys (encoded) fed from two different inputs
};

inline uint64_t encKey(const Key& k) { return (uint64_t(k.first) << 32) | k.second; }

inline GeomAnalysis analyzeGeom(const uint32_t* w, size_t nw) {
    GeomAnalysis result;
    Module m;
    if (!parse(w, nw, m)) return result;

    // ---- entry point + execution modes -----------------------------------
    uint32_t entryFunc = 0;
    size_t entryCount = 0;
    for (const Ins& in : m.insns) {
        if (in.op != kOpEntryPoint || in.n < 2) continue;
        ++entryCount;
        if (in.w[0] == kModelGeometry) entryFunc = in.w[1];
    }
    if (entryCount != 1 || !entryFunc) return result;
    uint32_t nIn = 0, nOut = 0;
    bool haveIn = false, haveOut = false, haveOutPrim = false;
    for (const Ins& in : m.insns) {
        if (in.op != kOpExecutionMode || in.n < 2 || in.w[0] != entryFunc) continue;
        const uint32_t mode = in.w[1], param = in.n >= 3 ? in.w[2] : 0;
        switch (mode) {
        case kModeInputPoints: nIn = 1; haveIn = true; break;
        case kModeInputLines: nIn = 2; haveIn = true; break;
        case kModeInputLinesAdjacency: nIn = 4; haveIn = true; break;
        case kModeTriangles: nIn = 3; haveIn = true; break;
        case kModeInputTrianglesAdjacency: nIn = 6; haveIn = true; break;
        case kModeOutputVertices: nOut = param; haveOut = true; break;
        case kModeOutputPoints: case kModeOutputLineStrip: case kModeOutputTriangleStrip:
            haveOutPrim = true; break;
        case kModeInvocations: if (param != 1) return result; break;
        default: break;
        }
    }
    // No amplification and no reduction: one output vertex per input vertex.
    if (!haveIn || !haveOut || !haveOutPrim || nOut != nIn || nIn == 0) return result;

    // ---- the single function ---------------------------------------------
    size_t fnBegin = SIZE_MAX, fnEnd = SIZE_MAX;
    size_t fnCount = 0;
    for (size_t i = 0; i < m.insns.size(); ++i) {
        if (m.insns[i].op == kOpFunction) {
            ++fnCount;
            if (m.insns[i].n >= 2 && m.insns[i].w[1] == entryFunc) fnBegin = i;
        }
        if (m.insns[i].op == kOpFunctionEnd && fnBegin != SIZE_MAX && fnEnd == SIZE_MAX) fnEnd = i;
    }
    if (fnCount != 1 || fnBegin == SIZE_MAX || fnEnd == SIZE_MAX) return result;

    // ---- opcode census + whitelist ----------------------------------------
    auto allowed = [](uint32_t op) {
        switch (op) {
        case kOpVariable: case kOpLoad: case kOpStore: case kOpAccessChain:
        case kOpCopyObject: case kOpIAdd: case kOpULessThan: case kOpSLessThan:
        case kOpEmitVertex: case kOpEndPrimitive: case kOpLoopMerge:
        case kOpLabel: case kOpBranch: case kOpBranchConditional: case kOpReturn:
            return true;
        default: return false;
        }
    };
    size_t emitIdx = SIZE_MAX, endPrimIdx = SIZE_MAX, loopIdx = SIZE_MAX, mergeIdx = SIZE_MAX;
    size_t emitCount = 0, endPrimCount = 0, loopCount = 0, retCount = 0;
    uint32_t mergeLabel = 0;
    for (size_t i = fnBegin + 1; i < fnEnd; ++i) {
        const Ins& in = m.insns[i];
        if (!allowed(in.op)) return result;
        switch (in.op) {
        case kOpEmitVertex: ++emitCount; emitIdx = i; break;
        case kOpEndPrimitive: ++endPrimCount; endPrimIdx = i; break;
        case kOpLoopMerge:
            ++loopCount; loopIdx = i;
            if (in.n >= 1) mergeLabel = in.w[0];
            break;
        case kOpReturn: ++retCount; break;
        default: break;
        }
    }
    if (emitCount != 1 || endPrimCount != 1 || loopCount != 1 || retCount != 1) return result;
    for (size_t i = fnBegin + 1; i < fnEnd; ++i)
        if (m.insns[i].op == kOpLabel && m.insns[i].n >= 1 && m.insns[i].w[0] == mergeLabel) mergeIdx = i;
    if (mergeIdx == SIZE_MAX || !(loopIdx < emitIdx && emitIdx < mergeIdx)) return result;
    if (!(mergeIdx < endPrimIdx)) return result;

    const size_t bodyBegin = fnBegin + 1, bodyEnd = fnEnd;
    auto inLoop = [&](size_t i) { return loopIdx < i && i < mergeIdx; };

    // ---- consumer map (ids consumed by whitelisted data instructions) -----
    // Branch/merge instructions are skipped: their operands are labels and
    // literal control words, never data values. A numeric collision with a
    // literal operand elsewhere can only add a phantom consumer, which
    // makes the checks below stricter, never weaker.
    std::unordered_multimap<uint32_t, size_t> consumers;
    for (size_t i = bodyBegin; i < bodyEnd; ++i) {
        const Ins& in = m.insns[i];
        switch (in.op) {
        case kOpLoad: case kOpStore: case kOpCopyObject: case kOpAccessChain:
        case kOpIAdd: case kOpULessThan: case kOpSLessThan:
            // Operand 1 of the result-bearing ops is the instruction's own
            // result id: never a consumption. (OpStore has no result; its
            // operands are all uses.)
            for (uint32_t k = 0; k < in.n; ++k) {
                if (in.op != kOpStore && k == 1) continue;
                consumers.emplace(in.w[k], i);
            }
            break;
        default: break;
        }
    }
    auto consumersOf = [&](uint32_t id) {
        std::vector<size_t> out;
        auto range = consumers.equal_range(id);
        for (auto it = range.first; it != range.second; ++it) out.push_back(it->second);
        return out;
    };

    // ---- loop counter: compare + step + init ------------------------------
    uint32_t counterVar = 0;
    size_t cmpIdx = SIZE_MAX;
    for (size_t i = bodyBegin; i < bodyEnd; ++i) {
        const Ins& in = m.insns[i];
        if (in.op != kOpULessThan && in.op != kOpSLessThan) continue;
        if (cmpIdx != SIZE_MAX || in.n < 4) return result;      // exactly one compare
        cmpIdx = i;
        const Value a = provenance(m, in.w[2]);
        const Value b = provenance(m, in.w[3]);
        if (a.prov != Prov::FuncLoad || b.prov != Prov::Const || b.constValue != nIn) return result;
        counterVar = a.var;
        // The compare result may only feed the loop's branch.
        bool feedsBranch = false;
        for (size_t j = bodyBegin; j < bodyEnd; ++j)
            if (m.insns[j].op == kOpBranchConditional && m.insns[j].n >= 1 && m.insns[j].w[0] == in.w[1]) feedsBranch = true;
        if (!feedsBranch) return result;
    }
    if (cmpIdx == SIZE_MAX || !counterVar) return result;

    bool counterInit = false, counterStep = false;
    for (size_t i = bodyBegin; i < bodyEnd; ++i) {
        const Ins& in = m.insns[i];
        if (in.op == kOpIAdd) {
            if (in.n < 4) return result;
            const Value a = provenance(m, in.w[2]);
            const Value b = provenance(m, in.w[3]);
            const bool stepShape = (a.prov == Prov::FuncLoad && a.var == counterVar && b.prov == Prov::Const && b.constValue == 1) ||
                                   (b.prov == Prov::FuncLoad && b.var == counterVar && a.prov == Prov::Const && a.constValue == 1);
            if (!stepShape) return result;                       // no other arithmetic exists
            auto uses = consumersOf(in.w[1]);
            if (uses.size() != 1) return result;
            const Ins& use = m.insns[uses[0]];
            if (use.op != kOpStore || use.n < 1) return result;
            uint32_t baseVar = 0; std::vector<uint32_t> idx;
            if (!resolveChain(m, use.w[0], baseVar, idx) || baseVar != counterVar || !idx.empty()) return result;
            if (!inLoop(uses[0])) return result;
            counterStep = true;
        }
    }
    if (!counterStep) return result;

    // ---- per-instruction data rules ---------------------------------------
    size_t outputStores = 0;
    for (size_t i = bodyBegin; i < bodyEnd; ++i) {
        const Ins& in = m.insns[i];
        if (in.op == kOpAccessChain) {
            if (in.n < 3) return result;
            uint32_t baseVar = 0; std::vector<uint32_t> idx;
            if (!resolveChain(m, in.w[1], baseVar, idx)) return result;
            auto v = m.vars.find(baseVar);
            if (v == m.vars.end()) return result;
            const uint32_t sc = v->second.sc;
            if (sc != kSCInput && sc != kSCOutput && sc != kSCFunction) return result;
            const bool arrayIn = sc == kSCInput && pointeeIsArray(m, baseVar);
            for (size_t k = 0; k < idx.size(); ++k) {
                const Value iv = provenance(m, idx[k]);
                if (arrayIn && k == 0) {
                    // The per-vertex index must be exactly the loop counter.
                    if (iv.prov != Prov::FuncLoad || iv.var != counterVar) return result;
                } else if (iv.prov != Prov::Const) {
                    return result;
                }
            }
        }
        if (in.op == kOpLoad) {
            if (in.n < 3) return result;
            uint32_t baseVar = 0; std::vector<uint32_t> idx;
            if (!resolveChain(m, in.w[2], baseVar, idx)) return result;
            auto v = m.vars.find(baseVar);
            if (v == m.vars.end()) return result;
            if (v->second.sc != kSCInput && v->second.sc != kSCFunction) return result;
            if (v->second.sc == kSCInput && !inLoop(i)) return result;   // per-vertex data read inside the emit loop only
            // A load result may only be copied or stored (the counter load
            // may additionally feed the compare / the step).
            const Value pv = loadProvenance(m, in);
            for (size_t u : consumersOf(in.w[1])) {
                const Ins& use = m.insns[u];
                if (use.op == kOpStore || use.op == kOpCopyObject) continue;
                if (pv.prov == Prov::FuncLoad && (use.op == kOpAccessChain ||
                    use.op == kOpULessThan || use.op == kOpSLessThan || use.op == kOpIAdd)) continue;
                return result;
            }
        }
        if (in.op == kOpStore) {
            if (in.n < 2) return result;
            uint32_t baseVar = 0; std::vector<uint32_t> idx;
            if (!resolveChain(m, in.w[0], baseVar, idx)) return result;
            auto v = m.vars.find(baseVar);
            if (v == m.vars.end()) return result;
            const Value val = provenance(m, in.w[1]);
            if (v->second.sc == kSCFunction) {
                if (baseVar != counterVar || !idx.empty()) return result;
                if (val.prov == Prov::Const && val.constValue == 0) {
                    if (i < loopIdx) counterInit = true;
                    continue;
                }
                // The step store was validated with its IAdd above.
                auto d = m.defOf.find(in.w[1]);
                if (d != m.defOf.end() && m.insns[d->second].op == kOpIAdd) continue;
                return result;
            }
            if (v->second.sc != kSCOutput) return result;      // no other side effects
            if (!inLoop(i)) return result;                      // outputs written inside the emit loop only
            ++outputStores;
            // Member path of the store target (indices below the variable).
            std::vector<uint32_t> memberPath;
            {
                const size_t start = 0;
                for (size_t k = start; k < idx.size(); ++k) {
                    auto c = m.constVal.find(idx[k]);
                    if (c == m.constVal.end()) return result;
                    memberPath.push_back(c->second);
                }
            }
            Key outKey;
            if (!ifaceKey(m, baseVar, memberPath, outKey)) return result;
            if (outKey.first == 1 && outKey.second == kBuiltinLayer) {
                // Layer may come from any input read or be the constant 0
                // (which is exactly the strip disposition).
                if (val.prov == Prov::InputChain || val.prov == Prov::InputDirect) continue;
                if (val.prov == Prov::Const && val.constValue == 0) continue;
                return result;
            }
            if (outKey.first == 1 && outKey.second == kBuiltinViewportIndex) return result;
            if (outKey.first == 1 && outKey.second != kBuiltinPosition &&
                outKey.second != kBuiltinPointSize && outKey.second != kBuiltinClipDistance &&
                outKey.second != kBuiltinCullDistance) return result;
            // Everything else must be a pure forward of an input value.
            if (val.prov != Prov::InputChain && val.prov != Prov::InputDirect) return result;
            Key inKey;
            if (!ifaceKey(m, val.var, val.memberPath, inKey)) return result;
            auto prev = result.pairing.find(outKey);
            if (prev != result.pairing.end() && prev->second != inKey) {
                result.pairingConflict.insert(encKey(outKey));
            } else {
                result.pairing[outKey] = inKey;
            }
        }
    }
    if (!counterInit || outputStores == 0) return result;
    result.shape = true;
    return result;
}

// True when the module declares at least one Fragment entry point.
inline bool hasFragmentEntry(const uint32_t* w, size_t nw) {
    Module m;
    if (!parse(w, nw, m)) return false;
    for (const Ins& in : m.insns)
        if (in.op == kOpEntryPoint && in.n >= 1 && in.w[0] == 4) return true;   // 4 = Fragment
    return false;
}

// Fragment-side consumption: the interface keys a fragment module reads
// that are fed by the vertex/geometry chain (user locations, plus the
// Clip/CullDistance builtins). Other fragment builtins (FragCoord,
// SampleId, ...) are generated locally and do not cross the stage chain.
inline std::vector<Key> fragmentChainInputs(const uint32_t* w, size_t nw) {
    std::vector<Key> out;
    Module m;
    if (!parse(w, nw, m)) return out;
    std::unordered_set<uint32_t> iface;   // variable ids in a Fragment entry point's interface
    bool anyFrag = false;
    for (const Ins& in : m.insns) {
        if (in.op != kOpEntryPoint || in.n < 2 || in.w[0] != 4) continue;   // 4 = Fragment
        anyFrag = true;
        // Interface ids follow the packed name words; names are NUL-
        // terminated, so interface ids start after the first word that
        // contains a zero byte at or after operand 2.
        size_t start = 2;
        while (start < in.n) {
            const uint32_t word = in.w[start++];
            if (((word & 0xFFu) == 0) || ((word & 0xFF00u) == 0) || ((word & 0xFF0000u) == 0) || ((word & 0xFF000000u) == 0)) break;
        }
        for (size_t k = start; k < in.n; ++k) iface.insert(in.w[k]);
    }
    if (!anyFrag) return out;
    std::unordered_set<uint64_t> seen;
    for (uint32_t var : iface) {
        auto v = m.vars.find(var);
        if (v == m.vars.end() || v->second.sc != kSCInput) continue;
        const uint32_t st = pointeeStruct(m, var);
        if (st) {
            auto sm = m.structMembers.find(st);
            if (sm == m.structMembers.end()) continue;
            for (uint32_t mem = 0; mem < sm->second.size(); ++mem) {
                Key key;
                if (!ifaceKey(m, var, {mem}, key)) continue;
                if (key.first == 0 || (key.first == 1 && (key.second == kBuiltinClipDistance || key.second == kBuiltinCullDistance)))
                    if (seen.insert(encKey(key)).second) out.push_back(key);
            }
        } else {
            Key key;
            if (!ifaceKey(m, var, {}, key)) continue;
            if (key.first == 0 || (key.first == 1 && (key.second == kBuiltinClipDistance || key.second == kBuiltinCullDistance)))
                if (seen.insert(encKey(key)).second) out.push_back(key);
        }
    }
    return out;
}

} // namespace detail

// True iff the module is a geometry shader of the layer-passthrough shape
// described at the top of this file.
inline bool isLayerPassthroughGeom(const uint32_t* w, size_t nw) {
    return detail::analyzeGeom(w, nw).shape;
}

// True iff serving the pipeline (vertex, geometry, fragment) with the
// geometry stage omitted reproduces the guest-visible result, modulo the
// tier's documented layer-0 collapse of layered rendering: the geometry
// module is a layer-passthrough AND every fragment input that crosses the
// stage chain is forwarded from the identically-keyed vertex output.
inline bool stripServeOk(const uint32_t* gsW, size_t gsN, const uint32_t* fsW, size_t fsN) {
    const detail::GeomAnalysis g = detail::analyzeGeom(gsW, gsN);
    if (!g.shape) return false;
    // A module with no Fragment entry point is not a fragment stage at all;
    // without this check its (empty) chain-input set would pass vacuously.
    if (!detail::hasFragmentEntry(fsW, fsN)) return false;
    for (const detail::Key& key : detail::fragmentChainInputs(fsW, fsN)) {
        if (g.pairingConflict.count(detail::encKey(key))) return false;
        auto it = g.pairing.find(key);
        if (it == g.pairing.end() || it->second != key) return false;
    }
    return true;
}

} // namespace vkgs
