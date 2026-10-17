export function startupMessage(status, lastLog, hasFrame, now = performance.now()) {
  return !hasFrame && lastLog !== null && now - lastLog < 2500 && /^running[.\u2026]*$/i.test(status.trim())
    ? 'Not frozen, still loading' : status;
}
// Sample a tiny image during startup, then stop after the first visible frame.
export function visibleFrame(canvas, probe) {
  if (!canvas?.width || !canvas?.height) return false;
  const context = probe.getContext('2d', { willReadFrequently: true });
  context.clearRect(0, 0, probe.width, probe.height);
  context.drawImage(canvas, 0, 0, probe.width, probe.height);
  const pixels = context.getImageData(0, 0, probe.width, probe.height).data;
  for (let at = 0; at < pixels.length; at += 4) if (pixels[at + 3] && pixels[at] + pixels[at + 1] + pixels[at + 2] > 30) return true;
  return false;
}
