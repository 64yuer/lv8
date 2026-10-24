"""lv8 G3 engine polyfills, tranche 5: canonical borrowable prototype surfaces.

Browsers expose DOM methods on interface prototypes (EventTarget.prototype.
addEventListener, Element.prototype.matches, ...); the WAF SDK grabs exactly
such borrowed natives into its VM data table at bootstrap.  Surfaces that
lived only on instances or on the shared element patch registry left those
grabs undefined, which starved the challenge VM's lazy data slots and crashed
the signature callback on undefined['call'] (the page-signature root cause).

Existence-first: every definition is guarded, native implementations win.
"""

from __future__ import annotations

__all__ = ["POLYFILL_TRANCHE_5"]


POLYFILL_TRANCHE_5 = r"""
(function () {
  "use strict";
  const ensure = (obj, name, fn) => {
    if (!obj || typeof obj[name] === "function") return false;
    Object.defineProperty(obj, name, {
      value: fn, writable: true, configurable: true, enumerable: false,
    });
    return true;
  };

  // ---------------- EventTarget.prototype: canonical event borrows
  if (typeof EventTarget !== "undefined") {
    const listeners = new WeakMap();
    const mapOf = (target) => {
      let m = listeners.get(target);
      if (!m) { m = new Map(); listeners.set(target, m); }
      return m;
    };
    const etp = EventTarget.prototype;
    ensure(etp, "addEventListener", function (type, callback) {
      if (typeof callback !== "function") return;
      const key = String(type);
      const m = mapOf(this);
      const list = m.get(key) || [];
      if (!list.includes(callback)) list.push(callback);
      m.set(key, list);
    });
    ensure(etp, "removeEventListener", function (type, callback) {
      const m = listeners.get(this);
      if (!m) return;
      const list = m.get(String(type));
      if (!list) return;
      const index = list.indexOf(callback);
      if (index !== -1) list.splice(index, 1);
    });
    ensure(etp, "dispatchEvent", function (event) {
      const m = listeners.get(this);
      const list = m ? (m.get(String(event && event.type)) || []) : [];
      for (const callback of [...list]) {
        try { callback.call(this, event); } catch (_) {}
      }
      return true;
    });
    // M5: the reference lists the constructor AFTER the event trio on
    // EventTarget.prototype (observable through gOPN/for-in on every
    // event-target chain, e.g. Worker/SharedWorker).
    try {
      const ctorDesc = Object.getOwnPropertyDescriptor(etp, "constructor");
      if (ctorDesc && ctorDesc.configurable) {
        delete etp.constructor;
        Object.defineProperty(etp, "constructor", ctorDesc);
      }
    } catch (_) {}
  }

  // ---------------- Event.prototype: control methods
  if (typeof Event !== "undefined") {
    const ep = Event.prototype;
    ensure(ep, "preventDefault", function () {
      try { this.defaultPrevented = true; } catch (_) {}
    });
    ensure(ep, "stopPropagation", function () {
      try { this.cancelBubble = true; } catch (_) {}
    });
    ensure(ep, "stopImmediatePropagation", function () {
      try { this.cancelBubble = true; } catch (_) {}
    });
  }

  // ---------------- Element.prototype: selector/geometry borrows
  if (typeof Element !== "undefined" && Element.prototype) {
    const elp = Element.prototype;
    ensure(elp, "matches", function (selector) {
      try {
        const doc = this.ownerDocument || document;
        const list = doc.querySelectorAll(selector);
        for (let i = 0; i < list.length; i++) {
          if (list[i] === this) return true;
        }
      } catch (_) {}
      return false;
    });
    ensure(elp, "webkitMatchesSelector", elp.matches);
    ensure(elp, "closest", function (selector) {
      let node = this;
      while (node && node.nodeType === 1) {
        if (Element.prototype.matches.call(node, selector)) return node;
        node = node.parentElement || null;
      }
      return null;
    });
    ensure(elp, "scrollIntoView", function () {});
    // geometry implementations registered for the shared element surface in
    // tranche 3 sink to Element.prototype so borrows resolve on either
    const patches = globalThis.__lv8ElementPatches || {};
    for (const name of ["getBoundingClientRect", "getClientRects"]) {
      const fn = patches[name];
      if (typeof fn === "function") ensure(elp, name, fn);
    }
  }

  // ---------------- HTMLFormElement / HTMLInputElement
  if (typeof HTMLFormElement !== "undefined") {
    ensure(HTMLFormElement.prototype, "reset", function () {
      try {
        const elements = this.elements || [];
        for (const field of elements) {
          if (field && "value" in field) field.value = "";
        }
      } catch (_) {}
    });
  }
  if (typeof HTMLInputElement !== "undefined") {
    ensure(HTMLInputElement.prototype, "select", function () {});
    ensure(HTMLInputElement.prototype, "setSelectionRange", function () {});
  }

  // ---------------- document surfaces
  if (typeof document !== "undefined") {
    ensure(document, "hasFocus", function () { return true; });
    ensure(document, "execCommand", function () { return false; });
    ensure(document, "queryCommandSupported", function () { return true; });
    ensure(document, "queryCommandEnabled", function () { return true; });
    ensure(document, "evaluate", function () {
      return {
        resultType: 4, booleanValue: false, numberValue: NaN,
        stringValue: "", singleNodeValue: null,
        snapshotLength: 0, snapshotItem: () => null,
        iterateNext: () => null, invalidIteratorState: false,
      };
    });
  }

  // ---------------- XMLHttpRequest.prototype
  if (typeof XMLHttpRequest !== "undefined") {
    ensure(XMLHttpRequest.prototype, "overrideMimeType", function () {});
    ensure(XMLHttpRequest.prototype, "setRequestHeader", function () {});
    // surface audit G4.153: the native shell ships no state constants and no
    // event-handler slots; the reference reads DONE=4 through instances
    // and has on* handler properties holding null.
    const xhrConsts = { UNSENT: 0, OPENED: 1, HEADERS_RECEIVED: 2,
      LOADING: 3, DONE: 4 };
    for (const name of Object.keys(xhrConsts)) {
      if (!(name in XMLHttpRequest)) {
        try {
          Object.defineProperty(XMLHttpRequest, name,
            { value: xhrConsts[name], writable: true, configurable: true });
        } catch (_) {}
      }
      if (!(name in XMLHttpRequest.prototype)) {
        try {
          Object.defineProperty(XMLHttpRequest.prototype, name,
            { value: xhrConsts[name], writable: true, configurable: true });
        } catch (_) {}
      }
    }
    const xhrHandlers = ["onreadystatechange", "onloadstart", "onprogress",
      "onabort", "onerror", "onload", "ontimeout", "onloadend"];
    for (const handler of xhrHandlers) {
      if (!(handler in XMLHttpRequest.prototype)) {
        try {
          Object.defineProperty(XMLHttpRequest.prototype, handler,
            { value: null, writable: true, configurable: true });
        } catch (_) {}
      }
    }
  }

  // ---------------- misc prototype stubs
  if (typeof Range !== "undefined") {
    const rp = Range.prototype;
    for (const name of ["selectNode", "selectNodeContents", "setStart",
      "setEnd", "setStartBefore", "setEndBefore", "setStartAfter",
      "setEndAfter", "collapse", "cloneRange", "selectNodeContents"]) {
      ensure(rp, name, function () {});
    }
    ensure(rp, "getBoundingClientRect", function () {
      return { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0,
        x: 0, y: 0 };
    });
  }
  if (typeof Text !== "undefined") {
    ensure(Text.prototype, "splitText", function (offset) {
      const data = String(this.data || "");
      const cut = Number(offset) || 0;
      const rest = document.createTextNode(data.slice(cut));
      this.data = data.slice(0, cut);
      if (this.parentNode) this.parentNode.insertBefore(rest, this.nextSibling);
      return rest;
    });
  }
  if (typeof window.CSSStyleDeclaration !== "undefined") {
    ensure(window.CSSStyleDeclaration.prototype, "setProperty",
      function (name, value, priority) {
        this[String(name).toLowerCase()] = value === undefined ? "" :
          String(value) + (priority ? " !" + priority : "");
      });
    ensure(window.CSSStyleDeclaration.prototype, "getPropertyValue",
      function (name) { return this[String(name).toLowerCase()] || ""; });
  }
  if (typeof window.CanvasRenderingContext2D !== "undefined") {
    const ccp = window.CanvasRenderingContext2D.prototype;
    for (const name of ["fillText", "strokeText", "measureText",
      "fillRect", "strokeRect", "clearRect", "beginPath", "closePath",
      "moveTo", "lineTo", "arc", "fill", "stroke", "save", "restore",
      "translate", "rotate", "scale", "drawImage", "getImageData",
      "putImageData", "createLinearGradient", "createRadialGradient"]) {
      ensure(ccp, name, function () {
        if (name === "measureText") {
          return { width: 0, actualBoundingBoxAscent: 0,
            actualBoundingBoxDescent: 0 };
        }
        if (name === "createLinearGradient" ||
            name === "createRadialGradient") {
          return { addColorStop: function () {} };
        }
        if (name === "getImageData") {
          return { data: new Uint8ClampedArray(4), width: 1, height: 1 };
        }
      });
    }
  }
})();
"""
