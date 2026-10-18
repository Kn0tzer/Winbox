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
  // Log activity is reported as an observation, never as proof that the program
  // will launch. Long silences are normal during CPU, graphics and asset loading,
  // and Wine's background services can keep logging after the program itself has
  // already failed. Neither case justifies a timeout or a health claim.
  const active = lastLog !== null && now - lastLog < 2500;
  const silentSeconds = lastLog === null ? null : Math.floor((now - lastLog) / 1000);
  const timeSinceStart = now - startedAt;
  let activity = '';
  if (hasFrame || timeSinceStart <= 15000) activity = '';
  else if (active) activity = 'Runtime logs are arriving. Wine background services can keep logging even when the program cannot start, so this does not confirm that it will open.';
  else if (lastLog === null) activity = 'No runtime logs yet. Programs can be quiet while loading assets or preparing graphics; this does not confirm a launch. Show logs has more detail.';
  else if (silentSeconds <= 30) activity = `No new logs for ${silentSeconds} seconds. This can be normal while loading assets or preparing graphics; launch is not confirmed. Show logs has more detail.`;
  else if (silentSeconds <= 120) activity = `No new logs for ${silentSeconds} seconds. Long silences are common while a program loads assets or compiles shaders; this does not confirm a launch or a hang. Show logs has more detail.`;
  else activity = `No new logs for ${silentSeconds} seconds. A program can be quiet while loading assets or compiling shaders, but if this continues with no window or frame, stop it and check Show logs for details.`;
  return { title, detail, activity, elapsed: `Starting for ${elapsed} seconds` };
}
// Only terminal loader diagnostics for the requested executable count here.
// Ordinary fixme/err output, service failures, and a dead initial Unix PID do
// not establish whether a Windows process (or one of its children) can launch.
export function startupFailure(line, exe = '') {
  if (/^(?:Boot failed:|Exception:|Aborted\b)/i.test(line)) return line;
  if (/wine: could not load kernel32\.dll, status/i.test(line)) return 'Wine could not load its Windows system libraries. ' + line;
  const target = exe.replaceAll('\\', '/').split('/').at(-1)?.toLowerCase();
  if (!target || !line.toLowerCase().includes(target)) return null;
  if (/LdrInitializeThunk.*Importing dlls.*failed, status (?:c0000135|c000007b)/i.test(line))
    return 'The program could not load a required DLL. Show logs lists the missing or incompatible library. ' + line;
  if (/wine: (?:failed to open|cannot find|could not load).*: (?:c0000135|c000007b|c000000f)/i.test(line))
    return 'The program could not be opened. Check its files and required libraries in Show logs. ' + line;
  return null;
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
