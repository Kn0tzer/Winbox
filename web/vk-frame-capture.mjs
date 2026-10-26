// One bounded, opt-in capture of a complete guest presentation. Frame IDs are
// guest submission numbers; page render counters are never used as identity.
export function captureValue(value, budget = { left: 64 * 1024 * 1024, omitted: 0 }) {
  if (typeof value === 'bigint') return { $u64: value.toString() };
  if (ArrayBuffer.isView(value)) {
    const bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    if (bytes.length > budget.left) { budget.omitted += bytes.length; return { $omittedBytes: bytes.length }; }
    budget.left -= bytes.length;
    let binary = '';
    for (let i = 0; i < bytes.length; i += 16384) binary += String.fromCharCode(...bytes.subarray(i, i + 16384));
    return { $bytes: btoa(binary), byteLength: bytes.length };
  }
  if (value instanceof Map) return { $map: [...value].map(([k, v]) => [captureValue(k, budget), captureValue(v, budget)]) };
  if (Array.isArray(value)) return value.map(v => captureValue(v, budget));
  if (value && typeof value === 'object') return Object.fromEntries(Object.entries(value).map(([k, v]) => [k, captureValue(v, budget)]));
  return value;
}

export function createVkFrameCapture({ maxBytes = 64 * 1024 * 1024, isBoundary = () => true } = {}) {
  let wanted, active = false, chunks = [], chunkBytes = 0, omittedWireBytes = 0, report = null, issues = [];
  const owner = new WeakMap();
  function arm(frame = null) {
    if (frame !== null && (!Number.isSafeInteger(frame) || frame < 0)) throw new Error('capture frame must be a nonnegative guest submission number');
    if (active || report?.status === 'rendering') throw new Error('capture already armed or rendering');
    issues = isBoundary() ? [] : ['Capture armed after the presentation prefix was received'];
    wanted = frame; active = true; chunks = []; chunkBytes = 0; omittedWireBytes = 0; report = null;
  }
  function wire(bytes, flags) {
    if (!active) return;
    if (chunkBytes + bytes.length > maxBytes) { omittedWireBytes += bytes.length; return; }
    chunks.push({ bytes: bytes.slice(), flags }); chunkBytes += bytes.length;
  }
  function batch(frame, guestPresent) {
    if (!active) return false;
    const submissions = frame.submissions || [frame];
    if (wanted !== null && !submissions.some(f => f.frameNo === wanted)) {
      if (submissions.some(f => f.frameNo > wanted)) {
        report = { version: 1, status: 'error', requestedFrame: wanted, submissionIds: submissions.map(f => f.frameNo), error: 'Requested guest submission was not transported before a later presentation', omittedWireBytes };
        active = false;
      }
      chunks = []; chunkBytes = 0; omittedWireBytes = 0; issues = [];
      return false;
    }
    active = false;
    const budget = { left: maxBytes, omitted: 0 };
    report = { version: 1, status: 'rendering', requestedFrame: wanted,
      issues: issues.slice(), submissionIds: submissions.map(f => f.frameNo), guestPresent: guestPresent || null,
      wire: captureValue(chunks, budget), decoded: captureValue(frame, budget),
      omittedWireBytes, omittedStateBytes: budget.omitted, images: [], outcomes: [] };
    chunks = [];
    report.stateBytes = report.dataBytes = maxBytes - budget.left;
    owner.set(frame, report);
    for (const f of submissions) owner.set(f, report);
    return true;
  }
  function image(frame, meta, bytes, reserved = false) {
    const r = owner.get(frame);
    if (!r) return;
    const budget = { left: reserved ? bytes?.byteLength || 0 : Math.max(0, maxBytes - r.dataBytes), omitted: 0 };
    r.images.push({ submission: frame.frameNo, attempt: r.attempt || 0, ...meta, data: bytes ? captureValue(bytes, budget) : null });
    r.imageBytes = (r.imageBytes || 0) + (bytes?.byteLength || 0) - budget.omitted;
    if (!reserved) r.dataBytes += (bytes?.byteLength || 0) - budget.omitted;
    r.omittedImageBytes = (r.omittedImageBytes || 0) + budget.omitted;
  }
  return { arm, wire, batch, image,
    incomplete(reason) { if (active) issues.push(String(reason)); },
    fail(error, initializing = false) {
      if (!active && !(initializing && report?.status === 'rendering')) return;
      active = false;
      if (!report) { const budget = { left: maxBytes, omitted: 0 }; report = { version: 1, requestedFrame: wanted, wire: captureValue(chunks, budget), omittedWireBytes, omittedStateBytes: budget.omitted }; }
      report.status = 'error'; report.error = String(error); chunks = [];
    },
    reserve(frame, byteLength) {
      const r = owner.get(frame);
      if (!r || byteLength > maxBytes - r.dataBytes) return false;
      r.dataBytes += byteLength;
      return true;
    },
    beginAttempt(frame, attempt) {
      const r = owner.get(frame);
      if (!r || r.status !== 'rendering') return;
      r.attempt = attempt; r.images = []; r.outcomes = []; r.dataBytes = r.stateBytes;
      r.imageBytes = 0; r.omittedImageBytes = 0;
    },
    failedAttempt(frame, error) {
      const r = owner.get(frame);
      if (r) (r.failedAttempts ||= []).push({ attempt: r.attempt || 0, error: String(error) });
    },
    selected: frame => owner.get(frame)?.status === 'rendering',
    outcome(frame, result) { owner.get(frame)?.outcomes.push({ submission: frame.frameNo, ...result }); },
    finish(frame, error) { if (owner.get(frame) === report && report?.status === 'rendering') { const reason = error || report.issues?.join('; '); report.status = reason ? 'error' : 'complete'; if (reason) report.error = String(reason); report.completeEvidence = !reason && !!report.guestPresent && report.wire.length > 0 && !(report.omittedWireBytes || report.omittedStateBytes || report.omittedImageBytes) && report.images.every(i => !i.skipped && !i.error) && report.images.some(i => i.stage === 'canvas') && report.images.some(i => i.stage === 'present-source'); } },
    get report() { return report; },
    get armed() { return active; },
  };
}
