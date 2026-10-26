// Install on the same-origin runtime document so browser copy/paste events
// exchange text through the runtime's X selection bridge.
export function connectRuntimeClipboard(document, getModule) {
  document.addEventListener('copy', event => {
    if (!event.clipboardData) return;
    const module = getModule();
    if (typeof module?.ccall !== 'function') return;
    const text = module.ccall('bw64_clipboard_get', 'string', [], []);
    if (typeof text !== 'string') return;
    event.clipboardData.setData('text/plain', text);
    event.preventDefault();
  });

  document.addEventListener('paste', event => {
    if (!event.clipboardData) return;
    const text = event.clipboardData.getData('text/plain');
    if (!text) return;
    const module = getModule();
    if (typeof module?.ccall !== 'function') return;
    module.ccall('bw64_clipboard_set', null, ['string'], [text]);
    event.preventDefault();
  });
}
