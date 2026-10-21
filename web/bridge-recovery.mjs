// Keep JS bridge failures inside the host boundary so one invalid WebGPU/GL
// operation cannot unwind the wasm call which delivered it.
const LOG_LIMIT = 512;
const logged = new Set();
let caught = 0;
let loggedCount = 0;
let recoveryEnabled = (() => {
  try { return new URLSearchParams(globalThis.location?.search || '').get('recover') !== '0'; }
  catch { return true; }
})();

function errorText(error) {
  return String(error?.message || error || 'unknown error');
}

export function safeBridgeDefault(method) {
  if (/^(draw|clear|flush|finish|delete|disable|enable|bind|buffer|pixelStore|renderbuffer|shaderSource|compileShader|attachShader|detachShader|linkProgram|useProgram|uniform|vertexAttrib|viewport|scissor|blend|depth|stencil|cull|frontFace|lineWidth|polygonOffset|sample|hint|copy|framebufferTexture)/i.test(method)) return undefined;
  if (/^create/i.test(method) || /^(getParameter|getError|getShaderParameter|getProgramParameter|getBufferParameter|getFramebufferAttachmentParameter|getRenderbufferParameter|getTexParameter|getAttribLocation|getUniformLocation|checkFramebufferStatus)$/i.test(method)) return 0;
  if (/^is/i.test(method)) return false;
  if (/^(getShaderInfoLog|getProgramInfoLog)$/i.test(method)) return '';
  if (/^(getActiveAttrib|getActiveUniform|getExtension)$/i.test(method)) return null;
  if (/^getSupportedExtensions$/i.test(method)) return [];
  return undefined;
}

export function bridgeRecoveryStats() {
  return { enabled: recoveryEnabled, caught, logged: loggedCount };
}

export function setBridgeRecoveryEnabled(enabled) {
  recoveryEnabled = Boolean(enabled);
}

export function reportBridgeFailure(area, error, log = message => console.warn(message)) {
  caught++;
  const message = `${area}: ${errorText(error)}`;
  const key = message;
  if (logged.has(key) || logged.size >= LOG_LIMIT) return false;
  logged.add(key);
  loggedCount++;
  try { log(`bridge-recover: ${message}`); } catch {}
  return true;
}

export function recoverBridgeCall(area, callback, fallback, log) {
  return function (...args) {
    const recover = error => {
      reportBridgeFailure(area, error, log);
      if (!recoveryEnabled) throw error;
      return typeof fallback === 'function' ? fallback(error, args) : fallback;
    };
    try {
      const result = callback.apply(this, args);
      return result && typeof result.then === 'function' ? result.catch(recover) : result;
    } catch (error) {
      return recover(error);
    }
  };
}

export function createRecoveringBridge(target, { name = 'bridge', log } = {}) {
  const methods = new Map();
  return new Proxy(target, {
    get(object, property, receiver) {
      if (property === 'bwRecoverStats') return bridgeRecoveryStats;
      let value;
      try { value = Reflect.get(object, property, receiver); }
      catch (error) {
        reportBridgeFailure(`${name}.${String(property)}`, error, log);
        if (!recoveryEnabled) throw error;
        return safeBridgeDefault(String(property));
      }
      if (typeof value !== 'function') return value;
      const cached = methods.get(property);
      if (cached?.original === value) return cached.wrapped;
      const wrapped = recoverBridgeCall(`${name}.${String(property)}`, value, safeBridgeDefault(String(property)), log);
      methods.set(property, { original: value, wrapped });
      return wrapped;
    },
    set(object, property, value, receiver) {
      try { return Reflect.set(object, property, value, receiver); }
      catch (error) {
        reportBridgeFailure(`${name}.${String(property)}`, error, log);
        if (!recoveryEnabled) throw error;
        return true;
      }
    },
  });
}
