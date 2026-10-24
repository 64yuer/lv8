"""lv8 G3 engine polyfills (JS-in-context, injected at install time).

The reference implementation (``native/reference`` bytecode) runs a JS
bootstrap on plain V8; these tranches port its self-contained sections onto
the C++ engine so the oracle's utility surfaces behave byte-identically.
Only globals the engine does not already provide are defined. Each tranche
is idempotent (guards with typeof checks) so re-injection is a no-op.
"""

from __future__ import annotations

__all__ = ["POLYFILL_TRANCHE_1"]


POLYFILL_TRANCHE_1 = r"""
(function () {
  "use strict";
  // ------------------------------------------------ shared helpers
  const setToStringTag = (object, tag) => Object.defineProperty(
    object, Symbol.toStringTag, { value: tag, configurable: true });

  // live base URL: resolved against the engine's location host object
  const currentHref = () => {
    try { return (typeof location !== "undefined" && location.href) || "about:blank"; }
    catch (_) { return "about:blank"; }
  };

  const resolveURL = (url, baseURL) => {
    const value = String(url);
    if (/^[a-z][a-z0-9+.-]*:/i.test(value)) return value;
    const base = String(baseURL);
    const baseMatch = base.match(/^([a-z][a-z0-9+.-]*:\/\/[^/?#]+)([^?#]*)/i);
    const origin = baseMatch && baseMatch[1];
    if (!origin) return value;
    if (value.startsWith("//")) return base.match(/^[a-z][a-z0-9+.-]*:/i)[0] + value;
    const split = value.match(/^([^?#]*)([\s\S]*)$/), path = (split && split[1]) || "", suffix = split && split[2] || "";
    const basePath = (baseMatch && baseMatch[2]) || "/";
    if (!path) return origin + basePath + suffix;
    const candidate = path.startsWith("/") ? path : basePath.replace(/\/[^/]*$/, "/") + path;
    const normalized = [];
    for (const part of candidate.split("/")) {
      if (!part || part === ".") continue;
      if (part === "..") normalized.pop(); else normalized.push(part);
    }
    return origin + "/" + normalized.join("/") + suffix;
  };

  const base64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const btoa = input => {
    const value = String(input);
    let output = "";
    for (let index = 0; index < value.length; index += 3) {
      const first = value.charCodeAt(index), second = value.charCodeAt(index + 1), third = value.charCodeAt(index + 2);
      if (first > 255 || second > 255 || third > 255) throw new Error("InvalidCharacterError");
      output += base64Alphabet[first >> 2] + base64Alphabet[((first & 3) << 4) | (second >> 4)] +
        (Number.isNaN(second) ? "=" : base64Alphabet[((second & 15) << 2) | (third >> 6)]) +
        (Number.isNaN(third) ? "=" : base64Alphabet[third & 63]);
    }
    return output;
  };
  const atob = input => {
    const value = String(input).replace(/\s/g, "");
    if (value.length % 4 === 1 || /[^A-Za-z0-9+/=]/.test(value)) throw new Error("InvalidCharacterError");
    let output = "";
    for (let index = 0; index < value.length; index += 4) {
      const a = base64Alphabet.indexOf(value[index]), b = base64Alphabet.indexOf(value[index + 1]);
      const c = value[index + 2] === "=" ? -1 : base64Alphabet.indexOf(value[index + 2]);
      const d = value[index + 3] === "=" ? -1 : base64Alphabet.indexOf(value[index + 3]);
      if (a < 0 || b < 0 || c < -1 || d < -1) throw new Error("InvalidCharacterError");
      output += String.fromCharCode((a << 2) | (b >> 4));
      if (c >= 0) output += String.fromCharCode(((b & 15) << 4) | (c >> 2));
      if (d >= 0) output += String.fromCharCode(((c & 3) << 6) | d);
    }
    return output;
  };

  const bytesFrom = input => {
    if (input instanceof ArrayBuffer) return new Uint8Array(input);
    if (ArrayBuffer.isView(input)) return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
    throw new TypeError("data must be an ArrayBuffer or ArrayBuffer view");
  };
  const arrayBuffer = bytes => bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
  const utf8Encode = value => {
    const encoded = unescape(encodeURIComponent(String(value)));
    return Uint8Array.from(encoded, character => character.charCodeAt(0));
  };
  const normalizeMimeType = value => {
    const type = String(value || "");
    return /^[\x20-\x7e]*$/.test(type) ? type.toLowerCase() : "";
  };

  if (typeof globalThis.btoa !== "function") Object.defineProperty(globalThis, "btoa", { value: btoa, writable: true, enumerable: true, configurable: true });
  if (typeof globalThis.atob !== "function") Object.defineProperty(globalThis, "atob", { value: atob, writable: true, enumerable: true, configurable: true });

  if (typeof globalThis.TextDecoder !== "function") {
    class TextDecoder {
      decode(input) {
        const bytes = bytesFrom(input === undefined ? new Uint8Array() : input);
        let value = "";
        for (const byte of bytes) value += String.fromCharCode(byte);
        return decodeURIComponent(escape(value));
      }
    }
    setToStringTag(TextDecoder.prototype, "TextDecoder");
    Object.defineProperty(globalThis, "TextDecoder", { value: TextDecoder, writable: true, enumerable: true, configurable: true });
  }

  // ------------------------------------------------ JS-side event targets
  // Compact event plumbing for polyfilled hosts (FileReader, signals...).
  // It does not touch the engine's native Event constructor or DOM
  // dispatch; a private SimpleEvent class keeps the two worlds apart.
  class SimpleEvent {
    constructor(type) { this.type = String(type); this.target = null; this.currentTarget = null; this.eventPhase = 0; this.bubbles = false; this.cancelBubble = false; this.cancelImmediate = false; this.defaultPrevented = false; }
    preventDefault() { this.defaultPrevented = true; }
    stopPropagation() { this.cancelBubble = true; }
    stopImmediatePropagation() { this.cancelBubble = this.cancelImmediate = true; }
  }
  const installSimpleTarget = target => {
    const listeners = new Map();
    Object.defineProperties(target, {
      addEventListener: { value(type, callback, options = undefined) {
        if (typeof callback !== "function") return;
        const capture = options === true || Boolean(typeof options === "object" && options && options.capture);
        const once = Boolean(typeof options === "object" && options && options.once);
        const callbacks = listeners.get(String(type)) || [];
        if (callbacks.some(entry => entry.callback === callback && entry.capture === capture)) return;
        callbacks.push({ callback, capture, once });
        listeners.set(String(type), callbacks);
      } },
      removeEventListener: { value(type, callback, options = undefined) {
        const callbacks = listeners.get(String(type));
        if (!callbacks) return;
        const capture = options === true || Boolean(typeof options === "object" && options && options.capture);
        const index = callbacks.findIndex(entry => entry.callback === callback && entry.capture === capture);
        if (index !== -1) callbacks.splice(index, 1);
      } },
      dispatchEvent: { value(event) {
        event.target = event.target || this;
        event.currentTarget = this;
        const callbacks = [...(listeners.get(String(event.type)) || [])];
        for (const entry of callbacks) {
          try { entry.callback.call(this, event); } catch (_) {}
          if (entry.once) this.removeEventListener(event.type, entry.callback, { capture: entry.capture });
          if (event.cancelImmediate) break;
        }
        const handler = this["on" + event.type];
        if (!event.cancelImmediate && typeof handler === "function") { try { handler.call(this, event); } catch (_) {} }
        event.currentTarget = null;
        return !event.defaultPrevented;
      } },
    });
    return target;
  };

  // ------------------------------------------------ Blob / File / FileReader
  // Blob, File, FileList and FileReader are native host constructors
  // now (G4 ledger chunks 4/6; see LV8Net.cpp). FileReader reads via
  // the native __lv8_blob_bytes(blob) -> Uint8Array helper.
  // ------------------------------------------------ URL / URLSearchParams
  // URL and URLSearchParams are native host constructors now (G4 ledger
  // chunk 4; see LV8Net.cpp). The object-URL registry below attaches to
  // the native URL constructor.
  // object URL registry (shared with createObjectURL when present)
  // reference's URL surface has NO createObjectURL/revokeObjectURL/_objectURLs
  // (pfb0 plaintext probes typeof URL.createObjectURL) -- the registry is
  // closure-private; the native blob: fetch branch reads it through
  // __lv8_blob_lookup only.
  const objectURLs = new Map();
  // sync lookup for the native fetch blob: branch (latin-1 style text)
  globalThis.__lv8_blob_lookup = function (url) {
    const blob = objectURLs.get(String(url));
    if (!blob) return null;
    const bridge = globalThis.__iv8__ && globalThis.__iv8__.__lv8_internal__;
    if (!bridge || typeof bridge.blobBytes !== "function") return null;
    const bytes = bridge.blobBytes(blob);
    let value = "";
    for (const byte of bytes) value += String.fromCharCode(byte);
    return decodeURIComponent(escape(value));
  };

  // URLPattern is a native host constructor now (G4 ledger chunk 4;
  // see LV8Net.cpp).

  // ------------------------------------------------ AbortController / AbortSignal
  // G4.285: the reference keeps aborted/reason/onabort as *prototype*
  // accessors and instances thin; the state lives in a private store.
  const __abortState = new WeakMap();
  function initAbortSignal(signal) {
    installSimpleTarget(signal);
    __abortState.set(signal, { aborted: false, reason: undefined, onabort: null });
  }
  const makeAbortSignal = () => {
    const signal = Object.create(AbortSignal.prototype);
    initAbortSignal(signal);
    return signal;
  };
  class AbortSignal {
    // reference parity (M16): the interface constructor is Illegal - instances
    // come from AbortController / the static factories via makeAbortSignal
    constructor() { throw new TypeError("Illegal constructor"); }
    get aborted() { return __abortState.get(this).aborted; }
    get reason() { return __abortState.get(this).reason; }
    get onabort() { return __abortState.get(this).onabort; }
    set onabort(handler) {
      const state = __abortState.get(this);
      if (state.onabort) this.removeEventListener && this.removeEventListener("abort", state.onabort);
      state.onabort = typeof handler === "function" ? handler : null;
      if (state.onabort) this.addEventListener && this.addEventListener("abort", state.onabort);
    }
    throwIfAborted() { if (this.aborted) throw this.reason || Object.assign(new Error("Operation aborted"), { name: "AbortError" }); }
    static abort(reason = undefined) { const controller = new AbortController(); controller.abort(reason); return controller.signal; }
    static timeout(milliseconds) {
      const controller = new AbortController();
      setTimeout(() => { const error = new Error("Signal timed out"); error.name = "TimeoutError"; controller.abort(error); }, Number(milliseconds));
      return controller.signal;
    }
    static any(signals) {
      if (!signals || typeof signals[Symbol.iterator] !== "function") throw new TypeError("signals must be iterable");
      const controller = new AbortController();
      for (const signal of signals) {
        if (!(signal instanceof AbortSignal)) throw new TypeError("signals must contain AbortSignal values");
        if (signal.aborted) { controller.abort(signal.reason); break; }
        signal.addEventListener("abort", () => controller.abort(signal.reason), { once: true });
      }
      return controller.signal;
    }
  }
  setToStringTag(AbortSignal.prototype, "AbortSignal");
  // M6: the reference exposes `signal` as a getter-only prototype accessor
  // (enumerable + configurable); the constructor must NOT assign it (class
  // bodies are strict - the assignment threw).  State lives in a WeakMap.
  const __acSignals = new WeakMap();
  class AbortController {
    constructor() { __acSignals.set(this, makeAbortSignal()); }
    abort(reason = undefined) {
      const state = __abortState.get(this.signal);
      if (state.aborted) return;
      state.aborted = true;
      state.reason = reason instanceof Error ? reason : Object.assign(new Error(String(reason || "Operation aborted")), { name: "AbortError" });
      this.signal.dispatchEvent(new SimpleEvent("abort"));
    }
  }
  setToStringTag(AbortController.prototype, "AbortController");
  Object.defineProperty(AbortController.prototype, "signal", {
    get() { const s = __acSignals.get(this); return s === undefined ? null : s; },
    enumerable: true, configurable: true,
  });

  // ------------------------------------------------ global installation
  const globals = [
    ["AbortSignal", AbortSignal], ["AbortController", AbortController],
  ];
  for (const [name, value] of globals) {
    if (typeof globalThis[name] === "undefined") {
      Object.defineProperty(globalThis, name, {
        value, writable: true, enumerable: true, configurable: true,
      });
    }
  }
})();
"""
