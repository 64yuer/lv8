"""lv8 G3 engine polyfills, tranche 4: control-surface helpers
(wrapNative/input dispatch) and the element-patch registries.

LockManager, WebMIDI, Request/Response extras, perf family, idle
callbacks, HMAC crypto, observers, getComputedStyle and the canvas
family moved to native C++ (G4 ledger); their JS definitions and
install lines are removed."""

from __future__ import annotations

__all__ = ["POLYFILL_TRANCHE_4"]


POLYFILL_TRANCHE_4 = r"""
(function () {
  "use strict";
  const setToStringTag = (object, tag) => Object.defineProperty(
    object, Symbol.toStringTag, { value: tag, configurable: true });

  // ------------------------------------------------ control surface
  // native-appearance registry backing Function.prototype.toString
  const nativeNames = new WeakMap();
  const wrapNative = (fn, name = undefined) => {
    if (typeof fn !== "function") throw new TypeError("fn must be a function");
    nativeNames.set(fn, String(name === undefined ? fn.name : name));
    return fn;
  };
  if (!Function.prototype.toString._lv8NativePatch) {
    const originalToString = Function.prototype.toString;
    const patchedToString = function toString() {
      const name = nativeNames.get(this);
      if (name !== undefined) {
        return "function " + name + "() { [native code] }";
      }
      const raw = originalToString.call(this);
      // surface audit G4.153: C++ API functions render "function () { [native
      // code] }" even though SetClassName filled the name property; the
      // reference (and Chrome) show the member name.  Rewrite only the
      // exact anonymous-native shape - real JS sources and bound functions
      // ("bound X") must pass through untouched.
      if (raw === "function () { [native code] }") {
        let n = "";
        try { n = String(this.name || ""); } catch (_) { n = ""; }
        if (n && String(n).indexOf("bound ") !== 0) {
          return "function " + n + "() { [native code] }";
        }
      }
      return raw;
    };
    Object.defineProperty(Function.prototype, "toString", {
      value: patchedToString, writable: true, configurable: true,
    });
    nativeNames.set(Function.prototype.toString, "toString");
    Function.prototype.toString._lv8NativePatch = true;
  }
  try {
    __lv8__.wrapNative = wrapNative;
    if (__lv8__.eventLoop) {
      if (!__lv8__.eventLoop.setDateAdvanceStep) {
        __lv8__.eventLoop.setDateAdvanceStep = function () {};
      }
    }
  } catch (_) {}

  // surface audit G4.153: the logical-clock Date (tranche 3) replaces the
  // global in every time mode and leaves Date.now anonymous (no own
  // name) and Date.length at 0; the reference shows
  // "function now() { [native code] }" and arity 7.
  try {
    if (typeof Date.now === "function" && Date.now.name === "") {
      Object.defineProperty(Date.now, "name", { value: "now" });
      wrapNative(Date.now, "now");
    }
    if (Date.length === 0) {
      Object.defineProperty(Date, "length",
        { value: 7, writable: false, configurable: true });
    }
    wrapNative(Date, "Date");
  } catch (_) {}

  // lv8's native Date.prototype.toGMTString returns a bare "H:MM:SS AM"
  // string; the reference returns the full GMT form (toUTCString).  The
  // challenge SDK builds cookie expires attributes through it, and a
  // malformed value flips the cookie-jar divergences seen live (enable_
  // vs WP chain shape).
  try {
    var __gmtProbe = new Date(86400000);
    if (String(__gmtProbe.toGMTString()) !== String(__gmtProbe.toUTCString())) {
      var __toGMT = function toGMTString() {
        return this.toUTCString();
      };
      Object.defineProperty(__toGMT, "length", { value: 0 });
      wrapNative(__toGMT, "toGMTString");
      Object.defineProperty(Date.prototype, "toGMTString", {
        value: __toGMT, writable: true, configurable: true,
      });
    }
  } catch (_) {}

  // trusted input dispatch (events inherit the native Event family, so
  // the engine's element dispatch handles them)
  const dispatchInputEvent = (Constructor, typeOrOptions, options = {}) => {
    const values = typeof typeOrOptions === "string"
      ? { ...options, type: typeOrOptions }
      : { ...(typeOrOptions || {}) };
    const target = typeof values.target === "string"
      ? document.querySelector(values.target)
      : values.target || document.activeElement || document.body;
    if (!target || !target.dispatchEvent) {
      throw new TypeError("input target must be a Node or selector");
    }
    const event = new Constructor(values.type || "", {
      bubbles: true, cancelable: true, ...values,
    });
    event.isTrusted = true;
    return target.dispatchEvent(event);
  };
  try {
    __lv8__.input = {
      dispatchMouseEvent: (t, o) => dispatchInputEvent(MouseEvent, t, o),
      dispatchPointerEvent: (t, o) => dispatchInputEvent(PointerEvent, t, o),
      dispatchKeyboardEvent: (t, o) => dispatchInputEvent(KeyboardEvent, t, o),
      dispatchInputEvent: (t, o) => dispatchInputEvent(InputEvent, t, o),
    };
  } catch (_) {}

  const parseDeclarations = body => {
    const map = new Map();
    for (const item of String(body).split(";")) {
      const idx = item.indexOf(":");
      if (idx === -1) continue;
      const name = item.slice(0, idx).trim().toLowerCase();
      const value = item.slice(idx + 1).trim();
      if (name && value) map.set(name, value);
    }
    return map;
  };
  // ------------------------------------------------ ImageBitmap / offscreen extras
  // ImageBitmap is a native host constructor now (G4 ledger chunk 8;
  // see LV8Net.cpp).

  const previous = (globalThis.__lv8ElementPatches =
    globalThis.__lv8ElementPatches || {});
  const previousAccessors = (globalThis.__lv8ElementAccessors =
    globalThis.__lv8ElementAccessors || {});

  const previousDocs = (globalThis.__lv8DocumentPatches =
    globalThis.__lv8DocumentPatches || {});
  // G4.308: the per-document style-sheet cache used to live on the global
  // face under Symbol.for("lv8SheetCache") - an own symbol the reference's
  // symbol scans observe (read-order 132531).  Closure-scoped instead; the
  // own-symbol pass also deletes any stale global slot defensively.
  const lv8SheetCacheMap = new WeakMap();
  // "get <name>" keys install as accessor properties on the document
  // surface audit G4.153: the reference answers the document metadata and
  // collection surfaces with these exact shapes - collections read back
  // null (not empty HTMLCollections), metadata fields carry fixed
  // values, and baseURI mirrors location.href.
  previousDocs["get styleSheets"] = function () {
    // surface audit G4.153: reference answers document.styleSheets with null when
    // the document has no <style> nodes.  Cache the built sheets so
    // insertRule mutations stay visible to getComputedStyle (oracle
    // test_stylesheet_rules_feed_basic_computed_style_values).  The cache
    // is closure-scoped (G4.308): a Symbol registry key on the global is
    // visible to the reference's own-symbol scans.
    const sheetCache = lv8SheetCacheMap;
    if (sheetCache.has(this)) return sheetCache.get(this);
    const styleNodes = this.getElementsByTagName
      ? this.getElementsByTagName("style") : [];
    const cached = styleNodes.length ? buildStyleSheets(this) : null;
    try { sheetCache.set(this, cached); } catch (_) {}
    return cached;
  };
  previousDocs["get compatMode"] = function () { return "CSS1Compat"; };
  previousDocs["get contentType"] = function () { return "text/html"; };
  previousDocs["get baseURI"] = function () {
    return String(document.location && document.location.href ||
      window.location && window.location.href || "about:blank");
  };
  previousDocs["get lastModified"] = function () {
    return "01/01/1970 00:00:00";
  };
  previousDocs["get activeElement"] = function () { return null; };
  // surface audit G4.153b: reference answers document.links with null even when the
  // document contains <a href> elements - the accessor is simply null
  // there, so it is mirrored unconditionally (forms/images/scripts stay
  // "null only when empty": reference was measured null on empty documents and
  // never measured non-empty).
  previousDocs["get links"] = function () { return null; };
  previousDocs["get applets"] = function () { return null; };
  previousDocs["get embeds"] = function () { return null; };
  previousDocs["get plugins"] = function () { return null; };
  previousDocs["get fullscreenElement"] = function () { return null; };
  previousDocs["get xmlEncoding"] = function () { return null; };

  const buildStyleSheets = document => {
    const sheets = [];
    const styleNodes = document.getElementsByTagName("style");
    for (let styleIndex = 0; styleIndex < styleNodes.length; styleIndex += 1) {
      const style = styleNodes[styleIndex];
      const text = style.textContent || "";
      const rules = [];
      const sheet = {
        cssRules: rules,
        ownerNode: style,
        insertRule(ruleText, index = undefined) {
          const raw = String(ruleText);
          const idx = raw.indexOf("{");
          const close = raw.indexOf("}");
          const selector = (idx === -1 ? raw : raw.slice(0, idx)).trim();
          const body = idx === -1 ? "" : raw.slice(idx + 1, close === -1 ? undefined : close).trim();
          const rule = {
            selectorText: selector,
            _body: body,
            style: {
              getPropertyValue: name => parseDeclarations(body).get(String(name).toLowerCase()) || "",
              length: [...parseDeclarations(body).keys()].length,
            },
            cssText: selector + " { " + body + "; }",
          };
          const at = index === undefined ? rules.length : Number(index);
          rules.splice(at, 0, rule);
          return at;
        },
        deleteRule(index) { rules.splice(Number(index), 1); },
      };
      for (const rule of text.split("}")) {
        const brace = rule.indexOf("{");
        if (brace === -1) continue;
        sheet.insertRule(rule.slice(0, brace).trim() + " {" + rule.slice(brace + 1).trim() + "}");
      }
      sheets.push(sheet);
    }
    sheets.item = index => sheets[Number(index)] || null;
    return sheets;
  };


  // ------------------------------------------------ mutation observers
  // notifyMutation delegates to the native delivery engine (G4 ledger
  // chunk 7d-2; observers register through the native class).
  const notifyMutation = (target, record) => {
    const bridge = globalThis.__iv8__ && globalThis.__iv8__.__lv8_internal__;
    // The reference prod host exposes MutationObserver but does not queue
    // records for these JS DOM mutators (verified for childList and
    // attributes).  Extra callbacks alter the challenge's token schedule.
    if (!(globalThis.__iv8__ && globalThis.__iv8__.monitor &&
          globalThis.__iv8__.monitor.enabled)) return;
    if (bridge && typeof bridge.notifyMutation === "function") {
      bridge.notifyMutation(target, record);
    }
  };
  const nativeElementMethod = name => {
    const bridge = globalThis.__iv8__ && globalThis.__iv8__.__lv8_internal__;
    return bridge && bridge.elementOriginals && bridge.elementOriginals[name];
  };
  const wrapMutationNode = node => {
    if (!node || node.__lv8MutationWrapped) return;
    // The reference engine's observe() never touches the target's own
    // property face; instance wrapping is only worth its fingerprint cost
    // when a monitor is actually consuming the records.
    if (!(globalThis.__iv8__ && globalThis.__iv8__.monitor &&
          globalThis.__iv8__.monitor.enabled)) return;
    try { node.__lv8MutationWrapped = true; } catch (_) { return; }
    for (const name of ["setAttribute", "removeAttribute", "appendChild", "removeChild"]) {
      const original = nativeElementMethod(name) || node[name];
      if (typeof original !== "function") continue;
      node[name] = function (...args) {
        const oldValue = name === "setAttribute" && this.getAttribute
            ? this.getAttribute(args[0]) : null;
        const result = original.apply(this, args);
        try {
          if (name === "appendChild") {
            wrapMutationSubtree(args[0]);
            notifyMutation(this, { type: "childList", target: this, attributeName: null, oldValue: null, addedNodes: [args[0]], removedNodes: [] });
          } else if (name === "removeChild") {
            notifyMutation(this, { type: "childList", target: this, attributeName: null, oldValue: null, addedNodes: [], removedNodes: [args[0]] });
          } else {
            notifyMutation(this, {
              type: "attributes", target: this, attributeName: String(args[0]),
              oldValue: oldValue === null || oldValue === undefined ? null : String(oldValue),
              addedNodes: [], removedNodes: [],
            });
          }
        } catch (_) {}
        return result;
      };
    }
  };
  const wrapMutationSubtree = node => {
    if (!node || node.__lv8MutationWrapped) return;
    wrapMutationNode(node);
    const children = node.children;
    const count = children ? children.length : 0;
    for (let index = 0; index < count; index += 1) wrapMutationSubtree(children[index]);
  };
  globalThis.__lv8_wrapMutationSubtree = wrapMutationSubtree;


  const wrapMutator = (name, recordFor) => {
    const originalPatch = previous[name];
    previous[name] = function (...args) {
      const original = nativeElementMethod(name);
      const result = original ? original.apply(this, args) : undefined;
      try { notifyMutation(this, recordFor(this, args)); } catch (_) {}
      return result;
    };
  };
  previous.__lv8_origSetAttribute = previous.__lv8_origSetAttribute || null;
  previous.setAttribute = function (name, value) {
    const original = nativeElementMethod("setAttribute");
    const oldValue = this.getAttribute ? this.getAttribute(name) : null;
    const result = original ? original.call(this, name, value) : undefined;
    try {
      notifyMutation(this, {
        type: "attributes", target: this, attributeName: String(name),
        oldValue: oldValue === null || oldValue === undefined ? null : String(oldValue),
        addedNodes: [], removedNodes: [],
      });
    } catch (_) {}
    return result;
  };
  previous.removeAttribute = function (name) {
    const original = nativeElementMethod("removeAttribute");
    const oldValue = this.getAttribute ? this.getAttribute(name) : null;
    const result = original ? original.call(this, name) : undefined;
    try {
      notifyMutation(this, {
        type: "attributes", target: this, attributeName: String(name),
        oldValue: oldValue === null || oldValue === undefined ? null : String(oldValue),
        addedNodes: [], removedNodes: [],
      });
    } catch (_) {}
    return result;
  };
  previous.appendChild = function (node) {
    const original = nativeElementMethod("appendChild");
    const result = original ? original.call(this, node) : undefined;
    try {
      notifyMutation(this, { type: "childList", target: this, attributeName: null, oldValue: null, addedNodes: [node], removedNodes: [] });
    } catch (_) {}
    return result;
  };
  previous.removeChild = function (node) {
    const original = nativeElementMethod("removeChild");
    const result = original ? original.call(this, node) : undefined;
    try {
      notifyMutation(this, { type: "childList", target: this, attributeName: null, oldValue: null, addedNodes: [], removedNodes: [node] });
    } catch (_) {}
    return result;
  };
})();
"""
