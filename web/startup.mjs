export function startupProgress(status, lastLog, hasFrame, now = performance.now(), startedAt = 0, firstMapped = false) {
  const elapsed = Math.max(0, Math.floor((now - startedAt) / 1000));
  let title = 'Starting the Windows environment';
  let detail = status || 'Loading the runtime and program files…';
  if (/download|fetch|wasm|WebAssembly/i.test(status)) title = 'Loading the runtime';
  if (/rootfs|prefix|filesystem/i.test(status)) title = 'Preparing Windows files';
  if (/boot|launching|session/i.test(status)) title = 'Starting your program';
  if (/^running[.\u2026]*$/i.test(status.trim())) {
    title = firstMapped ? 'Waiting for the first frame' : 'Starting your program';
    detail = firstMapped ? 'The program opened a window. Waiting for it to draw its first frame.'
      : 'Wine is starting the program and its background services. Waiting for a window and the first frame.';
  }
  const active = lastLog !== null && now - lastLog < 2500;
  const activity = hasFrame || now - startedAt <= 15000 ? '' : active
    ? 'Not frozen, still loading. The program is still sending logs.'
    : lastLog === null ? 'No program logs yet. You can inspect Show logs or restart.'
      : `No new logs for ${Math.floor((now - lastLog) / 1000)} seconds. You can inspect Show logs or restart.`;
  return { title, detail, activity, elapsed: `Starting for ${elapsed} seconds` };
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
