// Load the optional GL-on-WebGPU adapter before guest code starts. The module is
// imported in the Emscripten runtime realm so the GLctx hook and WebGPU context
// share the same worker and OffscreenCanvas.
(function () {
  var module = typeof Module !== 'undefined' ? Module : null;
  var runtimeUrl = module?.locateFile
    ? module.locateFile('boxedwine64.js')
    : 'boxedwine64.js';
  var adapterUrl = null;
  var baseCandidates = [];
  if (globalThis.document?.baseURI) baseCandidates.push(globalThis.document.baseURI);
  if (typeof scriptDirectory === 'string') baseCandidates.push(scriptDirectory);
  if (typeof module?.mainScriptUrlOrBlob === 'string') baseCandidates.push(module.mainScriptUrlOrBlob);
  if (globalThis.location?.href) baseCandidates.push(globalThis.location.href);
  var runtimeAbsoluteUrl = null;
  for (var i = 0; i < baseCandidates.length && !adapterUrl; i++) {
    try {
      runtimeAbsoluteUrl = new URL(runtimeUrl, baseCandidates[i]);
      adapterUrl = new URL('../gl-webgpu.mjs', runtimeAbsoluteUrl).href;
    } catch (_) {}
  }
  var version = runtimeAbsoluteUrl?.searchParams.get('v');
  if (adapterUrl && version) adapterUrl += '?v=' + encodeURIComponent(version);
  // pre-js is evaluated in the pthread worker too. Start loading immediately
  // in every realm, since GL.contexts and GLctx live in the context-owning
  // worker and cannot be populated from the page realm.
  var adapterReady = (adapterUrl ? import(adapterUrl) : Promise.resolve(null)).then(function (adapter) {
    if (!adapter) return null;
    globalThis.bwGlCreateContext = adapter.createWebGPUGLContext;
    if (module) module.bwGlCreateContext = adapter.createWebGPUGLContext;
    return adapter;
  }).catch(function (error) {
    try { console.warn('gl-webgpu: module load failed; keeping WebGL2', error); } catch (_) {}
    return null;
  });
  if (module) {
    var preRun = module.preRun || [];
    if (!Array.isArray(preRun)) preRun = [preRun];
    preRun.unshift(function () {
      var liveModule = typeof Module !== 'undefined' ? Module : module;
      var dependency = 'bwGlWebGPU';
      liveModule.addRunDependency(dependency);
      adapterReady.then(function () { liveModule.removeRunDependency(dependency); });
    });
    module.preRun = preRun;
  }
})();
