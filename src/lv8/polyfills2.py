"""lv8 G3 engine polyfills, tranche 2 (ported from the reference bootstrap).

2A (installed): geometry (DOMRect/DOMPoint/DOMMatrix/DOMQuad), Headers,
DataTransfer family.

2B (installed when the LV8_2B env var is "all"): message channels, offline
WebSocket, geolocation/notification and WebGPU declaration surfaces.

Each tranche is a self-contained IIFE with private helpers; nothing here
clobbers globals the engine (or tranche 1) already provides.
"""

from __future__ import annotations

__all__ = ["POLYFILL_TRANCHE_2A", "POLYFILL_TRANCHE_2B"]


HELPERS = r'''
  const setToStringTag = (object, tag) => Object.defineProperty(
    object, Symbol.toStringTag, { value: tag, configurable: true });
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
  class SimpleEvent {
    constructor(type, options = {}) {
      this.type = String(type);
      this.bubbles = Boolean(options.bubbles);
      this.cancelable = Boolean(options.cancelable);
      this.target = null; this.currentTarget = null; this.eventPhase = 0;
      this.cancelBubble = false; this.cancelImmediate = false; this.defaultPrevented = false;
    }
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
        const soft = (object, key, value) => { try { object[key] = value; } catch (_) {} };
        if (!event.target) soft(event, "target", this);
        const callbacks = [...(listeners.get(String(event.type)) || [])];
        for (const entry of callbacks) {
          soft(event, "currentTarget", this);
          try { entry.callback.call(this, event); } catch (_) {}
          if (entry.once) this.removeEventListener(event.type, entry.callback, { capture: entry.capture });
          if (event.cancelImmediate) break;
        }
        const handler = this["on" + event.type];
        if (!event.cancelImmediate && typeof handler === "function") { try { handler.call(this, event); } catch (_) {} }
        soft(event, "currentTarget", null);
        return !event.defaultPrevented;
      } },
    });
    return target;
  };
'''

GEOMETRY = r'''
  // DOMRect / DOMPointReadOnly / DOMPoint / DOMMatrixReadOnly /
  // DOMMatrix / DOMQuad are native host constructors now (G4 ledger
  // chunk 1; see LV8DomExtras.cpp). This tranche keeps only Headers.
'''

HEADERS = r'''
  // Headers is a native host constructor now (G4 ledger chunk 5; see
  // LV8Net.cpp InstallHeadersGlobal).
'''

DATATRANSFER = r'''
  // DataTransfer / DataTransferItem / DataTransferItemList are native
  // host constructors now (G4 ledger chunk 1; see LV8DomExtras.cpp).
'''

CHANNELS = r'''
  const cloneMessage = value => {
    if (value instanceof ArrayBuffer) return value.slice(0);
    if (ArrayBuffer.isView(value)) return value.constructor.from(value);
    if (value === undefined || typeof value === "function") return value;
    try { return JSON.parse(JSON.stringify(value)); } catch (_) { return value; }
  };
  function initMessagePort(port) {
    installSimpleTarget(port); port._peer = null; port._closed = false; port._started = false;
  }
  class MessagePort {
    // reference parity (M16): the interface constructor is Illegal - internal
    // factories (SharedWorker join, MessageChannel) go through
    // MessagePort.__lv8Create so page-level `new MessagePort()` still
    // refuses exactly like the reference
    constructor() { throw new TypeError("Illegal constructor"); }
    start() { this._started = true; }
    postMessage(message) {
      if (this._closed || !this._peer || this._peer._closed) return;
      const peer = this._peer, data = cloneMessage(message);
      queueMicrotask(() => { try { if (!peer._closed) peer.dispatchEvent(new MessageEvent("message", { data, ports: [] })); } catch (_) {} });
    }
    close() { this._closed = true; }
  }
  Object.defineProperty(MessagePort, "__lv8Create",
    { value: function () {
        const port = Object.create(MessagePort.prototype);
        initMessagePort(port);
        return port;
      }, writable: true, configurable: true });
  class MessageChannel {
    constructor() {}
  }
  // G4.231: the reference engine's MessageChannel is a stub face - the
  // prototype answers port1/port2 with null accessors, so the vendor
  // bundle's setImmediate polyfill (port1.onmessage = k) throws on it
  // exactly as it does under reference.  Mirroring the functional ports here
  // suppressed that throw and flipped the SDK battery's exception branch.
  {
    const nullPort = () => null;
    for (const port of ["port1", "port2"]) {
      Object.defineProperty(MessageChannel.prototype, port,
        { get: nullPort, set: undefined, enumerable: true,
          configurable: true });
    }
  }
  setToStringTag(MessageChannel.prototype, "MessageChannel");
  const broadcastChannels = new Map();
  class BroadcastChannel {
    constructor(name) {
      if (arguments.length === 0) throw new TypeError("Failed to construct 'BroadcastChannel': 1 argument required, but only 0 present.");
      installSimpleTarget(this);
      // strict-safe own shadow: a bare `this.name = ...` throws on the
      // getter-only prototype accessor (G4.330 functional gap)
      Object.defineProperty(this, 'name',
        { value: String(name), writable: true, enumerable: true, configurable: true });
      this._closed = false;
      const channels = broadcastChannels.get(this.name) || new Set();
      channels.add(this); broadcastChannels.set(this.name, channels);
      this._peers = channels;
    }
    postMessage(message) {
      if (this._closed) throw new Error("InvalidStateError");
      const data = cloneMessage(message);
      queueMicrotask(() => {
        for (const channel of this._peers) {
          try { if (channel !== this && !channel._closed) channel.dispatchEvent(new MessageEvent("message", { data })); } catch (_) {}
        }
      });
    }
    close() { this._closed = true; this._peers.delete(this); }
  }
'''

WEBSOCKET = r'''
  const wsLog = (socket, method, body) => {
    try {
      const entries = __iv8__.netLog.entries;
      entries.push({ method, url: socket.url, headers: {}, cookieHeader: "", body: body === undefined ? "" : body });
    } catch (_) {}
  };
  // G4.231: the interface-face installers hang getter-only accessors on
  // WebSocket.prototype (url/protocol/readyState/...).  Strict-mode
  // assignment `this.url = ...` in the constructor would walk into them
  // and throw (the reference engine constructs fine), so instance state
  // lives in this WeakMap and the WebIDL attributes are own accessors -
  // the same shape a browser instance carries.
  const wsStates = new WeakMap();
  class WebSocket {
    constructor(url, protocols = []) {
      // scheme validation with Chrome's exact message: the page SDK
      // constructs WebSocket URLs with foreign schemes and records the
      // thrown message as fingerprint text
      const absURL = resolveURL(url, currentHref());
      const schemeMatch = /^[a-zA-Z][a-zA-Z0-9+.-]*:/.exec(String(absURL));
      const scheme = schemeMatch ? schemeMatch[0].slice(0, -1).toLowerCase() : "";
      if (scheme !== "ws" && scheme !== "wss" && scheme !== "http" &&
          scheme !== "https") {
        // reference's captured fingerprint text carries the doubled prefix
        // ("Failed to construct 'WebSocket': Failed to construct
        // 'WebSocket': ...") -- mirror it verbatim (pfb0 plaintext).
        throw new TypeError(
          "Failed to construct 'WebSocket': Failed to construct " +
          "'WebSocket': The URL's scheme must be either 'http', 'https', " +
          "'ws', or 'wss'. '" + scheme + "' is not allowed.");
      }
      installSimpleTarget(this);
      const state = {
        url: absURL,
        protocol: Array.isArray(protocols) ? String(protocols[0] || "") : String(protocols || ""),
        readyState: WebSocket.CONNECTING,
        bufferedAmount: 0,
        binaryType: "blob",
      };
      wsStates.set(this, state);
      const ownAccessor = (name, get, set) => {
        try {
          Object.defineProperty(this, name, set === undefined
            ? { get, enumerable: true, configurable: true }
            : { get, set, enumerable: true, configurable: true });
        } catch (_) {}
      };
      ownAccessor("url", () => state.url);
      ownAccessor("protocol", () => state.protocol);
      ownAccessor("extensions", () => "");
      ownAccessor("binaryType", () => state.binaryType,
        v => { state.binaryType = String(v); });
      ownAccessor("bufferedAmount", () => state.bufferedAmount);
      ownAccessor("readyState", () => state.readyState);
      const lv8Config = (typeof __LV8_CONFIG__ === "object" && __LV8_CONFIG__) || {};
      const configured = ((lv8Config.webSocketMessages || {})[this.url]) || [];
      wsLog(this, "WEBSOCKET");
      queueMicrotask(() => {
        if (state.readyState !== WebSocket.CONNECTING) return;
        state.readyState = WebSocket.OPEN;
        try { this.dispatchEvent(new SimpleEvent("open")); } catch (_) {}
        for (const data of configured) { try { this.dispatchEvent(new MessageEvent("message", { data })); } catch (_) {} }
      });
    }
    send(data) {
      const state = wsStates.get(this);
      if (!state || state.readyState !== WebSocket.OPEN) throw new Error("InvalidStateError");
      const size = typeof data === "string" ? data.length : (data && data.byteLength) || 0;
      state.bufferedAmount += size;
      state.bufferedAmount = 0;
      wsLog(this, "WEBSOCKET_SEND", typeof data === "string" ? data : "[binary]");
    }
    close(code = 1000, reason = "") {
      const state = wsStates.get(this);
      if (!state || state.readyState === WebSocket.CLOSED) return;
      state.readyState = WebSocket.CLOSING;
      queueMicrotask(() => { state.readyState = WebSocket.CLOSED; try { this.dispatchEvent(new CloseEvent("close", { code, reason, wasClean: true })); } catch (_) {} });
    }
  }
  Object.assign(WebSocket, { CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3 });
  Object.assign(WebSocket.prototype, { CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3 });
  setToStringTag(WebSocket.prototype, "WebSocket");
  // G4.231: reference constructs EventSource; the collector-first installer used
  // to answer an illegal-constructor stub here, which flipped the SDK
  // battery's exception branch.  Browser face: own accessors over a state
  // store, constants on class and prototype, on* via the simple target.
  const esStates = new WeakMap();
  class EventSource {
    constructor(url) {
      const absURL = resolveURL(url, currentHref());
      installSimpleTarget(this);
      const state = { url: absURL, readyState: EventSource.CONNECTING,
        withCredentials: false, handlers: {onerror: null, onmessage: null,
                                           onopen: null} };
      esStates.set(this, state);
    }
    close() {
      const state = esStates.get(this);
      if (state) state.readyState = EventSource.CLOSED;
    }
  }
  // G4.231: reference exposes url/readyState/withCredentials as getter-only
  // prototype accessors and the three handlers as prototype accessors with a
  // setter (all enumerable+configurable); the reference surface reports
  // exactly those six members on EventSource.prototype, so they must not be
  // instance-own.
  const esState = (self) => esStates.get(self) || {};
  ["onerror", "onmessage", "onopen"].forEach((name) => {
    try {
      Object.defineProperty(EventSource.prototype, name, {
        get() { const s = esState(this); return (s.handlers || {})[name] || null; },
        set(v) { const s = esState(this); if (s.handlers) s.handlers[name] = v; },
        enumerable: true, configurable: true
      });
    } catch (_) {}
  });
  ["readyState", "url", "withCredentials"].forEach((name) => {
    try {
      Object.defineProperty(EventSource.prototype, name, {
        get() { return esState(this)[name]; },
        enumerable: true, configurable: true
      });
    } catch (_) {}
  });
  Object.assign(EventSource, { CONNECTING: 0, OPEN: 1, CLOSED: 2 });
  Object.assign(EventSource.prototype, { CONNECTING: 0, OPEN: 1, CLOSED: 2 });
  setToStringTag(EventSource.prototype, "EventSource");
'''

GEO_NOTIFY = r'''
  const lv8Profile = (typeof __LV8_PROFILE__ === "object" && __LV8_PROFILE__) || {};
  const permissionState = name => {
    const permissions = lv8Profile.permissions || {};
    const direct = lv8Profile[name];
    if (direct === "granted" || direct === "denied" || direct === "prompt") return direct;
    return permissions[name] || "prompt";
  };
  const geolocation = {
    getCurrentPosition(success, error = undefined) {
      queueMicrotask(() => {
        const position = lv8Profile.geolocation;
        if (permissionState("geolocation") === "granted" && position) {
          if (typeof success === "function") success({
            coords: {
              latitude: Number(position.latitude), longitude: Number(position.longitude), accuracy: Number(position.accuracy || 0),
              altitude: position.altitude === undefined ? null : position.altitude,
              altitudeAccuracy: position.altitudeAccuracy === undefined ? null : position.altitudeAccuracy,
              heading: position.heading === undefined ? null : position.heading,
              speed: position.speed === undefined ? null : position.speed,
            },
            timestamp: Date.now(),
          });
        } else if (typeof error === "function") error({
          code: permissionState("geolocation") === "denied" ? 1 : 2,
          message: "Geolocation unavailable in this context",
        });
      });
    },
    watchPosition(success, error = undefined) {
      this.getCurrentPosition(success, error);
      return 1;
    },
    clearWatch() {},
  };
  const notificationPermission = () => {
    const state = permissionState("notifications");
    return state === "prompt" ? "default" : state;
  };
  class Notification {
    constructor(title, options = {}) {
      if (arguments.length === 0) throw new TypeError("Failed to construct 'Notification': 1 argument required, but only 0 present.");
      installSimpleTarget(this);
      this.title = String(title); this.body = String(options.body || ""); this.tag = String(options.tag || ""); this.data = options.data === undefined ? null : options.data;
      this.silent = Boolean(options.silent); this.requireInteraction = Boolean(options.requireInteraction); this.closed = false;
      // SimpleEvent lives in another IIFE; a bare event object is enough
      const plainEvent = type => ({ type, target: null, currentTarget: null, defaultPrevented: false, cancelable: false, bubbles: false });
      queueMicrotask(() => this.dispatchEvent(plainEvent("show")));
    }
    close() {
      if (!this.closed) {
        this.closed = true;
        this.dispatchEvent({ type: "close", target: null, currentTarget: null, defaultPrevented: false, cancelable: false, bubbles: false });
      }
    }
    static requestPermission() { return Promise.resolve(notificationPermission()); }
  }
  Object.defineProperty(Notification, "permission", { get: notificationPermission });
'''

WEBGPU = r'''
  const readonlyObject = values => {
    const object = {};
    for (const [key, value] of Object.entries(values || {})) Object.defineProperty(object, key, { value, writable: false, enumerable: true, configurable: true });
    return object;
  };
  const promiseTask = task => new Promise((resolve, reject) => queueMicrotask(() => {
    try { resolve(task()); } catch (error) { reject(error); }
  }));
  function initGPUBuffer(buffer, descriptor) {
    buffer.size = Number(descriptor.size || 0); buffer.usage = Number(descriptor.usage || 0); buffer.mapState = descriptor.mappedAtCreation ? "mapped" : "unmapped"; buffer._data = new ArrayBuffer(buffer.size);
  }
  class GPUBuffer {
    // reference parity (M17): the interface constructor is Illegal - instances
    // only come from GPUDevice.createBuffer() via the internal factory
    constructor(descriptor = {}) { throw new TypeError("Illegal constructor"); }
    getMappedRange(offset = 0, size = this.size - Number(offset)) { if (this.mapState !== "mapped") throw new Error("InvalidStateError"); return this._data.slice(Number(offset), Number(offset) + Number(size)); }
    mapAsync() { return promiseTask(() => { this.mapState = "mapped"; }); }
    unmap() { this.mapState = "unmapped"; }
    destroy() { this.mapState = "destroyed"; }
  }
  class GPUQueue { submit() {} writeBuffer() {} onSubmittedWorkDone() { return promiseTask(() => undefined); } }
  function initGPUDevice(device, adapter) {
    const own = (k, v) => Object.defineProperty(device, k,
      { value: v, writable: true, enumerable: true, configurable: true });
    const astate = adapter && adapter.features ? adapter : null;
    own('adapter', adapter);
    own('features', new Set(astate ? adapter.features : []));
    own('limits', { ...(astate ? adapter.limits : {}) });
    own('queue', new GPUQueue());
    own('lost', new Promise(() => {}));
  }
  class GPUDevice {
    // reference parity (M14): the interface constructor is Illegal - instances
    // only come from adapter.requestDevice() via the internal factory
    constructor(adapter) { throw new TypeError("Illegal constructor"); }
    createBuffer(descriptor) { const b = Object.create(GPUBuffer.prototype); initGPUBuffer(b, descriptor); return b; }
    createTexture(descriptor = {}) { return { ...descriptor, createView: () => ({}) }; }
    createSampler(descriptor = {}) { return { ...descriptor }; }
    createShaderModule(descriptor = {}) { return { code: String(descriptor.code || ""), getCompilationInfo: () => promiseTask(() => ({ messages: [] })) }; }
    destroy() {}
  }
  function initGPUAdapter(adapter, options) {
    // G4.330: strict-safe own shadows (a bare assignment throws on the
    // getter-only prototype accessors)
    const own = (k, v) => Object.defineProperty(adapter, k,
      { value: v, writable: true, enumerable: true, configurable: true });
    own('options', options);
    own('features', new Set((lv8Profile.webgpu && lv8Profile.webgpu.features) || []));
    own('limits', { ...((lv8Profile.webgpu && lv8Profile.webgpu.limits) || {}) });
    const webgpu = lv8Profile.webgpu || {};
    own('info', readonlyObject({ vendor: webgpu.vendor || "", architecture: webgpu.architecture || "", device: webgpu.device || "", description: webgpu.description || "" }));
  }
  class GPUAdapter {
    // reference parity (M14): Illegal constructor - instances only come from
    // navigator.gpu.requestAdapter() via the internal factory
    constructor(options = {}) { throw new TypeError("Illegal constructor"); }
    requestAdapterInfo() { return promiseTask(() => this.info); }
    requestDevice() { return promiseTask(() => { const d = Object.create(GPUDevice.prototype); initGPUDevice(d, this); return d; }); }
  }
'''

TRANCHE_2A_SRC = ("(function () {\n  \"use strict\";\n" + HELPERS + GEOMETRY
                  + HEADERS + DATATRANSFER + r'''
  // ------------------------------------------------ installation (2A)
  const globals2a = [];

  for (const [name, value] of globals2a) {
    if (typeof globalThis[name] === "undefined") {
      Object.defineProperty(globalThis, name, {
        value, writable: true, enumerable: true, configurable: true,
      });
    }
  }
})();
''')

TRANCHE_2B_SRC = ("(function () {\n  \"use strict\";\n" + HELPERS + CHANNELS
                  + WEBSOCKET + GEO_NOTIFY + WEBGPU + r'''
  // ------------------------------------------------ installation (2B)
  const globals2b = [
    ["MessagePort", MessagePort], ["MessageChannel", MessageChannel], ["BroadcastChannel", BroadcastChannel],
    ["WebSocket", WebSocket], ["EventSource", EventSource], ["Notification", Notification],
    ["GPUAdapter", GPUAdapter], ["GPUDevice", GPUDevice], ["GPUBuffer", GPUBuffer],
  ];
  for (const [name, value] of globals2b) {
    if (typeof globalThis[name] === "undefined") {
      Object.defineProperty(globalThis, name, {
        value, writable: true, enumerable: true, configurable: true,
      });
    }
  }
  try {
    if (typeof navigator !== "undefined") {
      if (!navigator.geolocation) navigator.geolocation = geolocation;
      if (!navigator.gpu) {
        // G4.332 (reference ground truth, /tmp-free M19-family probe): the
        // reference's requestAdapter() ALWAYS resolves null - the whole
        // adapter/device chain is absent there (no GPUAdapter/GPUDevice
        // instances are ever produced)
        Object.defineProperty(navigator, "gpu", {
          value: { requestAdapter: (options = {}) => promiseTask(() => null) },
          configurable: true,
        });
      }
    }
  } catch (_) {}
})();
''')

POLYFILL_TRANCHE_2A = TRANCHE_2A_SRC
POLYFILL_TRANCHE_2B = TRANCHE_2B_SRC
