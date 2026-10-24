// web/vk-dynstate.mjs - Vulkan 1.3 / EXT_extended_dynamic_state{,2} dynamic state.
//
// The bridge records every vkCmdSet{PrimitiveTopology,DepthTestEnable,DepthWriteEnable,
// DepthCompareOp,StencilTestEnable,StencilOp,DepthBiasEnable,RasterizerDiscardEnable,
// PrimitiveRestartEnable,DepthBoundsTestEnable} as a RASTER_STATE record (which, value);
// the pipeline's DYN1 tail says which of those states the pipeline takes from commands.
// applyDynamicState() overlays the recorded values on the static pipeline at draw time.
// A state the pipeline did not declare dynamic keeps its static value (Vulkan: the
// command only applies when the pipeline lists the state in pDynamicStates).

export const DYN_ID = Object.freeze({
  cull: 0, front: 1, topology: 2, depthTest: 3, depthWrite: 4, depthOp: 5,
  stencilTest: 6, depthBiasEnable: 7, rasterizerDiscard: 8, primitiveRestart: 9,
  stencilOp: 10, depthBoundsTest: 11,
});
const DYN_NAME = Object.freeze(Object.fromEntries(Object.entries(DYN_ID).map(([k, v]) => [v, k])));
export const DYN_MASK_LIMIT = 1 << 12;

const isBool = v => v === 0 || v === 1;
// Decodes a RASTER_STATE record. Returns {which, value}; throws on an id/value the
// bridge could never have produced (a corrupt or hostile frame).
export function decodeDynState(which, value) {
  const name = DYN_NAME[which];
  if (name === undefined) throw new Error('vk64: v2 invalid raster state');
  switch (name) {
    case 'cull': case 'front':
      if (value > 3) throw new Error('vk64: v2 invalid raster state');
      return { which: name, value };
    case 'topology':
      if (value > 5) throw new Error('vk64: v2 invalid dynamic topology');
      return { which: name, value };
    case 'depthOp':
      if (value > 7) throw new Error('vk64: v2 invalid dynamic depth op');
      return { which: name, value };
    case 'stencilOp': {
      const op = {
        faceMask: value & 3, failOp: (value >>> 4) & 7, passOp: (value >>> 8) & 7,
        depthFailOp: (value >>> 12) & 7, compareOp: (value >>> 16) & 7,
      };
      if (value >>> 19 || !op.faceMask) throw new Error('vk64: v2 invalid dynamic stencil op');
      return { which: name, value: op };
    }
    default:
      if (!isBool(value)) throw new Error('vk64: v2 invalid dynamic ' + name);
      return { which: name, value };
  }
}

const DEFAULT_FACE = Object.freeze({ failOp: 0, passOp: 0, depthFailOp: 0, compareOp: 7, compareMask: 255, writeMask: 255, reference: 0 });

// dyn: Map/object name -> last recorded value. Returns the pipeline to draw with
// (the input object when nothing is overridden).
export function applyDynamicState(pipe, dyn) {
  const mask = pipe.dynMask | 0;
  const has = id => (mask & (1 << id)) !== 0 && dyn[DYN_NAME[id]] !== undefined;
  let out = pipe, copied = false;
  const set = patch => { out = copied ? Object.assign(out, patch) : { ...out, ...patch }; copied = true; };
  if (has(DYN_ID.topology)) set({ topology: dyn.topology });
  if (has(DYN_ID.depthTest)) set({ depthTest: dyn.depthTest });
  if (has(DYN_ID.depthWrite)) set({ depthWrite: dyn.depthWrite });
  if (has(DYN_ID.depthOp)) set({ depthOp: dyn.depthOp });
  if (has(DYN_ID.depthBiasEnable)) set({ depthBiasEnable: dyn.depthBiasEnable });
  if (has(DYN_ID.primitiveRestart)) set({ primitiveRestartEnable: !!dyn.primitiveRestart });
  if (has(DYN_ID.rasterizerDiscard)) set({ rasterizerDiscard: dyn.rasterizerDiscard });
  if (has(DYN_ID.stencilTest) || has(DYN_ID.stencilOp)) {
    const base = out.stencil || { enabled: 0, dynamicMask: 0, front: { ...DEFAULT_FACE }, back: { ...DEFAULT_FACE } };
    const stencil = { ...base, front: { ...base.front }, back: { ...base.back } };
    if (has(DYN_ID.stencilTest)) stencil.enabled = dyn.stencilTest;
    if (has(DYN_ID.stencilOp)) {
      const o = dyn.stencilOp;
      for (const [bit, face] of [[1, stencil.front], [2, stencil.back]])
        if (o.faceMask & bit) { face.failOp = o.failOp; face.passOp = o.passOp; face.depthFailOp = o.depthFailOp; face.compareOp = o.compareOp; }
    }
    set({ stencil });
  }
  return out;
}
