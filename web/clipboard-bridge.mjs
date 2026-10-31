// Install on the same-origin runtime document so browser copy/paste events
// exchange text through the runtime's X selection bridge.
//
// A guest call that throws (runtime mid-teardown, export missing) must not
// escape the event listener as an uncaught page error, and must not fail
// silently either: the first failure per direction logs one BW64-FAIL line
// and the event is left to the browser default.
const FAILED = Symbol('clipboard-bridge-failed');
const reported = new Set();
function bridgeCall(direction, invoke) {
  try { return invoke(); } catch (error) {
    if (!reported.has(direction)) {
      reported.add(direction);
      console.warn(`BW64-FAIL clipboard-bridge op=${direction} error=${error?.message || error}; event left to the browser default (further failures in this direction are not logged)`);
    }
    return FAILED;
  }
}
export function connectRuntimeClipboard(document, getModule) {
  document.addEventListener('copy', event => {
    if (!event.clipboardData) return;
    const module = getModule();
    if (typeof module?.ccall !== 'function') return;
    const text = bridgeCall('get', () => module.ccall('bw64_clipboard_get', 'string', [], []));
    if (text === FAILED || typeof text !== 'string') return;
    event.clipboardData.setData('text/plain', text);
    event.preventDefault();
  });

  document.addEventListener('paste', event => {
    if (!event.clipboardData) return;
    const text = event.clipboardData.getData('text/plain');
    if (!text) return;
    const module = getModule();
    if (typeof module?.ccall !== 'function') return;
    if (bridgeCall('set', () => module.ccall('bw64_clipboard_set', null, ['string'], [text])) === FAILED) return;
    event.preventDefault();
  });
}
