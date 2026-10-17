import { VS, EASU_FS, RCAS_FS, SGSR_FS } from './enhancement-shaders.mjs';
import { fitDisplay } from './presentation.mjs';

export function enhancementSize(width, height, pixelRatio = 1, maximum = 4096) {
  const scale = Math.min(Math.max(1, pixelRatio), maximum / Math.max(1, width, height));
  return { width: Math.max(1, Math.round(width * scale)), height: Math.max(1, Math.round(height * scale)) };
}

export function createEnhancer(display, getSource, { mode = 'fsr1', sharpness = 0.5, onError = error => {} } = {}) {
  const canvas = document.createElement('canvas');
  canvas.dataset.enhancement = mode; canvas.hidden = true; canvas.setAttribute('aria-hidden', 'true');
  display.append(canvas);
  const gl = canvas.getContext('webgl2', { alpha: false, antialias: false, depth: false, stencil: false });
  if (!gl) { canvas.remove(); throw Error('Image enhancement needs WebGL2'); }
  const programs = [], textures = [], shaders = [];
  const sourceKinds = new WeakMap();
  let copyCanvas = null, copyContext = null;
  const framebuffer = gl.createFramebuffer(), vertexArray = gl.createVertexArray();
  let request = 0, stopped = false, frames = 0, inputWidth = 0, inputHeight = 0, outputWidth = 0, outputHeight = 0;
  function compile(type, source) {
    const shader = gl.createShader(type); shaders.push(shader);
    gl.shaderSource(shader, source); gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) throw Error(gl.getShaderInfoLog(shader));
    return shader;
  }
  let vertex;
  function program(fragment) {
    const handle = gl.createProgram(); programs.push(handle);
    gl.attachShader(handle, vertex); gl.attachShader(handle, compile(gl.FRAGMENT_SHADER, fragment)); gl.linkProgram(handle);
    if (!gl.getProgramParameter(handle, gl.LINK_STATUS)) throw Error(gl.getProgramInfoLog(handle));
    return handle;
  }
  function texture() {
    const handle = gl.createTexture(); textures.push(handle); gl.bindTexture(gl.TEXTURE_2D, handle);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return handle;
  }
  let upscale, sharpen, input, intermediate;
  function destroy() {
    if (stopped) return;
    stopped = true; cancelAnimationFrame(request);
    canvas.removeEventListener('webglcontextlost', lost);
    for (const handle of programs) gl.deleteProgram(handle);
    for (const handle of textures) gl.deleteTexture(handle);
    for (const handle of shaders) gl.deleteShader(handle);
    gl.deleteFramebuffer(framebuffer); gl.deleteVertexArray(vertexArray); canvas.remove();
    if (copyCanvas) { copyCanvas.width = copyCanvas.height = 0; copyCanvas = null; copyContext = null; }
    gl.getExtension('WEBGL_lose_context')?.loseContext();
  }
  function lost(event) { event.preventDefault(); destroy(); onError(Error('The image enhancement GPU context was lost')); }
  canvas.addEventListener('webglcontextlost', lost);
  try {
    vertex = compile(gl.VERTEX_SHADER, VS);
    if (mode !== 'sharpen') upscale = program(mode === 'sgsr1' ? SGSR_FS : EASU_FS);
    sharpen = program(RCAS_FS); input = texture(); intermediate = texture();
  } catch (error) { destroy(); throw error; }
  function draw(handle, source, target, width, height, uniform, values, flip = 0) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, target); gl.viewport(0, 0, width, height); gl.useProgram(handle);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, source);
    gl.uniform1i(gl.getUniformLocation(handle, 'uTex'), 0);
    gl.uniform4f(gl.getUniformLocation(handle, uniform), values[0], values[1], values[2], values[3]);
    gl.uniform1f(gl.getUniformLocation(handle, 'uSharpness'), Math.max(0, Math.min(1, Number(sharpness) || 0)));
    gl.uniform1f(gl.getUniformLocation(handle, 'uFlip'), flip);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }
  function render() {
    if (stopped || document.hidden) return;
    const source = getSource();
    if (!source?.width || !source?.height) return;
    try {
      const box = fitDisplay(display.clientWidth, display.clientHeight, source.width, source.height);
      const size = enhancementSize(box.width, box.height, devicePixelRatio, Math.min(4096, gl.getParameter(gl.MAX_TEXTURE_SIZE)));
      if (canvas.width !== size.width || canvas.height !== size.height) { canvas.width = size.width; canvas.height = size.height; }
      for (const [name, value] of Object.entries(box)) canvas.style[name] = value + 'px';
      canvas.style.right = 'auto'; canvas.style.bottom = 'auto';
      gl.bindVertexArray(vertexArray); gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, input);
      if (inputWidth !== source.width || inputHeight !== source.height) {
        inputWidth = source.width; inputHeight = source.height;
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, source.width, source.height, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      }
      if (!sourceKinds.has(source)) {
        let webgpu = false;
        try { webgpu = Boolean(source.getContext('webgpu')); } catch {}
        sourceKinds.set(source, webgpu);
      }
      let upload = source;
      if (sourceKinds.get(source)) {
        copyCanvas ||= document.createElement('canvas'); copyContext ||= copyCanvas.getContext('2d', { alpha: false });
        if (copyCanvas.width !== source.width || copyCanvas.height !== source.height) { copyCanvas.width = source.width; copyCanvas.height = source.height; }
        copyContext.drawImage(source, 0, 0); upload = copyCanvas;
      }
      gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, gl.RGBA, gl.UNSIGNED_BYTE, upload);
      const uploadError = gl.getError();
      if (uploadError !== gl.NO_ERROR) throw Error('The program frame could not be copied to the enhancement GPU (' + uploadError + ')');
      const { width, height } = size;
      if (mode !== 'sharpen') {
        if (outputWidth !== width || outputHeight !== height) {
          outputWidth = width; outputHeight = height;
          gl.bindTexture(gl.TEXTURE_2D, intermediate); gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
          gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer); gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, intermediate, 0);
          const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
          if (status !== gl.FRAMEBUFFER_COMPLETE) throw Error('Image enhancement framebuffer is unavailable (' + status + ')');
        }
        draw(upscale, input, framebuffer, width, height, mode === 'sgsr1' ? 'uView' : 'uRes',
          mode === 'sgsr1' ? [1 / source.width, 1 / source.height, source.width, source.height] : [source.width, source.height, width, height]);
        draw(sharpen, intermediate, null, width, height, 'uRes', [width, height, 1 / width, 1 / height], 1);
      } else draw(sharpen, input, null, width, height, 'uRes', [width, height, 1 / source.width, 1 / source.height]);
      if (gl.getError() !== gl.NO_ERROR) throw Error('The GPU could not enhance this program frame');
      canvas.hidden = false; canvas.dataset.frames = String(++frames);
    } catch (error) { destroy(); onError(error); }
  }
  function tick() { render(); if (!stopped) request = requestAnimationFrame(tick); }
  request = requestAnimationFrame(tick);
  return { canvas, render, destroy };
}
