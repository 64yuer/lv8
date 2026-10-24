"""reference-measured Window/EventTarget prototype topology."""

from __future__ import annotations

__all__ = ["POLYFILL_WINDOW_EVENT_INTERFACE"]


POLYFILL_WINDOW_EVENT_INTERFACE = r"""
(function () {
  if (typeof window === "undefined" || typeof EventTarget === "undefined") return;
  var eventProto = EventTarget.prototype;
  var windowProto = Object.getPrototypeOf(window);
  if (!eventProto || !windowProto) return;

  // reference exposes these as enumerable EventTarget prototype methods.  The
  // native lv8 callbacks are correct; only their descriptor/layout differed.
  ["addEventListener", "removeEventListener", "dispatchEvent"].forEach(function (name) {
    var descriptor = Object.getOwnPropertyDescriptor(eventProto, name);
    if (!descriptor || typeof descriptor.value !== "function") return;
    try { Object.defineProperty(descriptor.value, "name", {
      value: name, configurable: true
    }); } catch (_) {}
    if (!descriptor.enumerable) {
      Object.defineProperty(eventProto, name, {
        value: descriptor.value, writable: descriptor.writable,
        configurable: descriptor.configurable, enumerable: true
      });
    }
  });

  // The reference chain is window -> Window.prototype -> EventTarget.prototype.
  // Keep existing Window-prototype own fields intact and add only the missing
  // inherited interface layer.
  if (Object.getPrototypeOf(windowProto) !== eventProto) {
    try { Object.setPrototypeOf(windowProto, eventProto); } catch (_) {}
  }

  if (typeof window.Window === "undefined") {
    function Window() { throw new TypeError("Illegal constructor"); }
    try { Window.prototype = windowProto; } catch (_) {}
    try { Object.defineProperty(windowProto, "constructor", {
      value: Window, writable: true, configurable: true, enumerable: false
    }); } catch (_) {}
    try { Object.defineProperty(window, "Window", {
      value: Window, writable: true, configurable: true, enumerable: false
    }); } catch (_) {}
    [ ["PERSISTENT", 1], ["TEMPORARY", 0] ].forEach(function (entry) {
      try { Object.defineProperty(windowProto, entry[0], {
        value: entry[1], enumerable: true
      }); } catch (_) {}
    });
    try { Object.defineProperty(windowProto, Symbol.toStringTag, {
      value: "Window", configurable: true
    }); } catch (_) {}
  }

  // reference's legacy navigator quota handles share DeprecatedStorageQuota:
  // the tag and the two enumerable methods are inherited, while lv8's native
  // handles exposed a WebkitPersistentStorage own tag and own zero-arity
  // methods.  Re-home the native callbacks without replacing their behavior.
  var nav = window.navigator;
  var quotaNames = ["queryUsageAndQuota", "requestQuota"];
  if (nav) ["webkitPersistentStorage", "webkitTemporaryStorage"].forEach(
    function (storageName) {
      var handle = nav[storageName];
      if (!handle || typeof handle !== "object") return;
      // A second pass runs after tranche 8; the first pass already moved the
      // callbacks, so do not replace the populated prototype with an empty
      // one on that idempotent normalization pass.
      var hasOwnQuotaMethod = quotaNames.some(function (name) {
        var descriptor = Object.getOwnPropertyDescriptor(handle, name);
        return descriptor && typeof descriptor.value === "function";
      });
      if (!hasOwnQuotaMethod) return;
      var quotaProto = Object.create(Object.getPrototypeOf(handle));
      try { Object.defineProperty(quotaProto, Symbol.toStringTag, {
        value: "DeprecatedStorageQuota", writable: false,
        enumerable: false, configurable: true
      }); } catch (_) {}
      quotaNames.forEach(function (name) {
        var descriptor = Object.getOwnPropertyDescriptor(handle, name);
        if (!descriptor || typeof descriptor.value !== "function") return;
        try { Object.defineProperty(descriptor.value, "length", {
          value: 1, configurable: true
        }); } catch (_) {}
        try { Object.defineProperty(quotaProto, name, {
          value: descriptor.value, writable: true,
          enumerable: true, configurable: true
        }); } catch (_) {}
        try { delete handle[name]; } catch (_) {}
      });
      // lv8's previous native tag is configurable.  The prototype's tag is
      // then observable by Object.prototype.toString like the reference handle.
      try { delete handle[Symbol.toStringTag]; } catch (_) {}
      try { Object.setPrototypeOf(handle, quotaProto); } catch (_) {}
    });

  // Tranche 8 supplies this legacy constructor, but reference keeps the global
  // non-enumerable.  WAF code performs a raw `for...in window` survey, so
  // retain the existing function and correct only its descriptor.
  var batteryDescriptor = Object.getOwnPropertyDescriptor(window,
    "BatteryManager");
  if (batteryDescriptor && batteryDescriptor.enumerable) {
    batteryDescriptor.enumerable = false;
    try { Object.defineProperty(window, "BatteryManager", batteryDescriptor); }
    catch (_) {}
  }

  // Screen is profile-backed in lv8, so its configured fields originally
  // materialized as enumerable own data properties.  reference keeps this entire
  // IDL face on Screen.prototype.  Capture the per-context profile values
  // once, then re-home them as accessors and remove the observable instance
  // surface.  The object itself is immutable for the life of this offline
  // browsing context, matching the reference contract.
  var screenObject = window.screen;
  var monitorEnabled = !!(window.__lv8__ && window.__lv8__.monitor &&
    window.__lv8__.monitor.enabled);
  if (screenObject && !monitorEnabled) {
    var screenProto = Object.getPrototypeOf(screenObject);
    if (screenProto) {
      ["availHeight", "availLeft", "availTop", "availWidth", "colorDepth",
       "height", "isExtended", "orientation", "pixelDepth", "width"]
        .forEach(function (name) {
          var own = Object.getOwnPropertyDescriptor(screenObject, name);
          var value;
          try { value = screenObject[name]; } catch (_) { value = undefined; }
          if (own) {
            try { delete screenObject[name]; } catch (_) {}
          }
          try { Object.defineProperty(screenProto, name, {
            get: function () { return value; }, enumerable: true,
            configurable: true
          }); } catch (_) {}
        });
      var onchange = null;
      try { onchange = screenObject.onchange; } catch (_) {}
      try { delete screenObject.onchange; } catch (_) {}
      try { Object.defineProperty(screenProto, "onchange", {
        get: function () { return onchange; },
        set: function (value) { onchange = value; },
        enumerable: true, configurable: true
      }); } catch (_) {}
    }
  }
})();
"""
