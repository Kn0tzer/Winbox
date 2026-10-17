// Screenshot text is a menu gate; alpha/clear-color differences are not.
export function hasPortalMenu(text) {
  const words = text.toLowerCase().replace(/[^a-z0-9]/g, '');
  return words.includes('newgame') && words.includes('options') &&
    (words.includes('loadgame') || words.includes('quit'));
}

export function summarizeRenderedFrames(samples, startMs, endMs) {
  if (!Number.isFinite(startMs) || !Number.isFinite(endMs) || endMs <= startMs)
    throw new Error('Frame measurement requires a positive time window');
  const frames = samples.filter(s => Number.isFinite(s.start) && Number.isFinite(s.end) &&
    s.end >= s.start && s.start >= startMs && s.end <= endMs).sort((a, b) => a.end - b.end);
  const durationSeconds = (endMs - startMs) / 1000;
  const renderTimes = frames.map(s => s.end - s.start).sort((a, b) => a - b);
  const intervals = frames.slice(1).map((s, i) => s.end - frames[i].end).sort((a, b) => a - b);
  const percentile = (values, p) => values.length ? values[Math.ceil(p * values.length) - 1] : null;
  return {
    durationSeconds, renderedFrames: frames.length, renderedFps: frames.length / durationSeconds,
    renderTimeMs: { mean: renderTimes.length ? renderTimes.reduce((a, b) => a + b, 0) / renderTimes.length : null,
      median: percentile(renderTimes, 0.5), p95: percentile(renderTimes, 0.95) },
    frameIntervalMs: { median: percentile(intervals, 0.5), p95: percentile(intervals, 0.95) },
  };
}
