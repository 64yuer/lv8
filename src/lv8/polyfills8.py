"""lv8 engine polyfills, tranche 8: Navigator.prototype layout parity.

Real Chrome keeps every navigator member on Navigator.prototype; the
instance itself owns nothing (Object.getOwnPropertyDescriptors(
navigator) is {}, Object.keys(navigator) is []).  The proven engine
(reference) reproduces that layout, and the page challenge SDK fingerprints
it: the first dual-engine divergence traces to
Object.getOwnPropertyDescriptors(navigator) seeing lv8's 26 own
properties where reference sees none, which flips the SDK into a different
build path (missing createElement('a')/'iframe' probes, wrong pc=1088
data payload).

The native installer hangs profile members on the instance, and many
subsystems (storage, buckets, credentials, locks, beacon, midi, ...)
attach more afterwards.  Re-laying that out natively would touch every
install site, so this tranche does the parity move in one place, after
native installation completes: snapshot the instance own values, strip
them, and rebuild the Chrome-153 prototype layout (same members, same
order, accessor descriptors for value members and plain function
values for methods, Symbol.toStringTag on the prototype).  Members lv8
has no host for materialize as toStringTag-tagged objects or
native-looking function stubs, matching the shapes reference reports.
Only runs under prod (debug keeps the monitor-visible accessors);
idempotent.
"""

from __future__ import annotations

__all__ = ["POLYFILL_TRANCHE_8"]


POLYFILL_TRANCHE_8 = r"""
(function () {
  var nav = window.navigator;
  if (!nav) return;
  var own = Object.getOwnPropertyNames(nav);
  if (own.length === 0) return;
  var proto = Object.getPrototypeOf(nav);
  if (!proto) return;
  // Chrome-153 Navigator.prototype members in enumeration order:
  // 'a' = accessor-backed value member, 'v' = own function value.
  var ORDER = ("vendorSub:a|productSub:a|vendor:a|maxTouchPoints:a|scheduling:a" +
    "|userActivation:a|doNotTrack:a|geolocation:a|connection:a|plugins:a" +
    "|mimeTypes:a|pdfViewerEnabled:a|webkitTemporaryStorage:a" +
    "|webkitPersistentStorage:a|windowControlsOverlay:a|hardwareConcurrency:a" +
    "|cookieEnabled:a|appCodeName:a|appName:a|appVersion:a|platform:a" +
    "|product:a|userAgent:a|language:a|languages:a|onLine:a|webdriver:a" +
    "|getGamepads:v|javaEnabled:v|sendBeacon:v|vibrate:v" +
    "|bluetooth:a|storageBuckets:a|clipboard:a|credentials:a|keyboard:a" +
    "|managed:a|mediaDevices:a|storage:a|serviceWorker:a|virtualKeyboard:a" +
    "|wakeLock:a|deviceMemory:a|userAgentData:a|cookieDeprecationLabel:a" +
    "|login:a|ink:a|mediaCapabilities:a|hid:a|locks:a|gpu:a|mediaSession:a" +
    "|permissions:a|presentation:a|usb:a|xr:a|serial:a" +
    "|canShare:v|share:v|clearAppBadge:v|setAppBadge:v|getBattery:v" +
    "|getUserMedia:v|webkitGetUserMedia:v|requestMIDIAccess:v" +
    "|requestMediaKeySystemAccess:v|registerProtocolHandler:v" +
    "|unregisterProtocolHandler:v").split("|");
  function tagobj(tag) {
    var o = {};
    try { Object.defineProperty(o, Symbol.toStringTag, {value: tag}); } catch (e) {}
    return o;
  }
  // interface names the camel-case guess cannot produce
  var TAG_FIX = {
    storage: "StorageManager", storageBuckets: "StorageBucketManager",
    credentials: "CredentialsContainer", serviceWorker: "ServiceWorkerContainer",
    locks: "LockManager", managed: "NavigatorManagedData",
    login: "NavigatorLogin", gpu: "GPU", xr: "XRSystem", usb: "USB", hid: "HID"
  };
  var snap = {};
  own.forEach(function (k) {
    try { snap[k] = nav[k]; } catch (e) { snap[k] = undefined; }
    try { delete nav[k]; } catch (e) {}
  });
  // the native template seeds the prototype with $methods/$async hosts
  // (javaEnabled, getBattery, ...); they are not own properties, so
  // fold them into the snapshot BEFORE the prototype strip below or
  // they come back as empty stubs
  Object.getOwnPropertyNames(proto).forEach(function (k) {
    if (k === "constructor") return;
    if (Object.prototype.hasOwnProperty.call(snap, k)) return;
    var v;
    try { v = nav[k]; } catch (e) { return; }
    if (typeof v === "function") snap[k] = v;
  });
  // the native install seeds the prototype with a few members out of
  // Chrome order (javaEnabled/getBattery via $methods/$async, plus a
  // hand of accessors); strip every configurable member so the ORDER
  // walk below reinserts everything at its Chrome-153 position
  // (constructor stays - WebIDL keeps it non-enumerable, so the
  // for-in surface is unaffected by its position)
  Object.getOwnPropertyNames(proto).forEach(function (k) {
    if (k === "constructor") return;
    var d = Object.getOwnPropertyDescriptor(proto, k);
    if (d && d.configurable) { try { delete proto[k]; } catch (e) {} }
  });
  // native-installed members can share one handler (vibrate/canShare
  // are both retFalse) or arrive unlabeled (gpu is a bare object);
  // wrap/rename instead of mutating the shared original, and tag the
  // bare interface objects so String() renders [object Xxx] like reference.
  // The wrapper factory stays in this bootstrap script: its products
  // keep own keys {length, name} (no prototype/arguments/caller leak)
  // and Function#toString renders the native format with the patched
  // name.
  var leaked = function (fn) {
    var own;
    try { own = Object.getOwnPropertyNames(fn); } catch (e) { return false; }
    return own.indexOf("prototype") >= 0 ||
      own.indexOf("arguments") >= 0 ||
      own.indexOf("caller") >= 0;
  };
  var wrapFn = function (orig, key) {
    var holder = nav;
    var maker = {};
    try {
      Object.defineProperty(maker, key,
        {value: function () { return orig.apply(holder, arguments); },
         writable: true, configurable: true, enumerable: true});
    } catch (e) { return orig; }
    var w = maker[key];
    try { Object.defineProperty(w, "name", {value: key}); } catch (e) {}
    try { Object.defineProperty(w, "length", {value: orig.length}); }
    catch (e) {}
    return w;
  };
  ORDER.forEach(function (entry) {
    var parts = entry.split(":"), k = parts[0], kind = parts[1];
    var have = Object.prototype.hasOwnProperty.call(snap, k);
    var val = have ? snap[k] : undefined;
    if (!have) {
      if (kind === "v") {
        val = function stub() {};
      } else if (k === "doNotTrack") {
        val = null;
      } else {
        val = tagobj(TAG_FIX[k] ||
          (k.charAt(0).toUpperCase() + k.slice(1)));
      }
    }
    if (kind === "v" && typeof val === "function" &&
        ((val.name || "") !== k || leaked(val))) {
      val = wrapFn(val, k);
    }
    if (have && kind === "a" && val && typeof val === "object" &&
        Object.prototype.toString.call(val) === "[object Object]") {
      try {
        Object.defineProperty(val, Symbol.toStringTag,
          {value: TAG_FIX[k] || (k.charAt(0).toUpperCase() + k.slice(1)),
           configurable: true});
      } catch (e) {}
    }
    try { delete nav[k]; } catch (e) {}
    if (kind === "a") {
      var getter = function getter() { return val; };
      try {
        Object.defineProperty(getter, "name", {value: "get " + k});
      } catch (e) {}
      try {
        Object.defineProperty(proto, k,
          {get: getter, configurable: true, enumerable: true});
      } catch (e) {}
    } else {
      try {
        Object.defineProperty(proto, k,
          {value: val, writable: true, configurable: true, enumerable: true});
      } catch (e) {}
    }
  });
  // the native template put toStringTag on the instance; Chrome keeps it
  // on the prototype (so the instance's own-descriptor map stays empty)
  try {
    Object.defineProperty(proto, Symbol.toStringTag,
      {value: "Navigator", configurable: true});
  } catch (e) {}
})();

// ------------------------------------------------ surface-audit value surfaces
// G4.154: reference-measured value fixes that must win over earlier tranches.
(function () {
  // reference has no window.defaultStatus (lv8 exposed "")
  try { delete window.defaultStatus; } catch (e) {}

  // reference's viewport reads 1920x969 on the reference box
  try {
    Object.defineProperty(window, "innerHeight",
      { value: 969, writable: true, configurable: true, enumerable: true });
  } catch (e) {}

  // reference's location answers valueOf (lv8's location misses the chain)
  try {
    if (typeof location.valueOf !== "function") {
      Object.defineProperty(location, "valueOf",
        { value: function valueOf() { return location; },
          writable: true, configurable: true, enumerable: false });
    }
  } catch (e) {}

  // mediaDevices.enumerateDevices exists as a function in reference and answers
  // with the three default placeholder devices on a profile without
  // explicit media hardware (audioinput/audiooutput/videoinput)
  try {
    if (navigator.mediaDevices &&
        typeof navigator.mediaDevices.enumerateDevices !== "function") {
      Object.defineProperty(navigator.mediaDevices, "enumerateDevices", {
        value: function enumerateDevices() {
          return Promise.resolve([
            {deviceId: "", kind: "audioinput", label: "", groupId: ""},
            {deviceId: "", kind: "audiooutput", label: "", groupId: ""},
            {deviceId: "", kind: "videoinput", label: "", groupId: ""}
          ]);
        },
        writable: true, configurable: true, enumerable: true
      });
    }
  } catch (e) {}

  // reference exposes window.BatteryManager as a constructor
  try {
    if (typeof window.BatteryManager === "undefined") {
      window.BatteryManager = function BatteryManager() {
        throw new TypeError("Illegal constructor");
      };
      Object.defineProperty(window.BatteryManager, "name",
        { value: "BatteryManager" });
    }
  } catch (e) {}

  // performance.timing / navigation / memory / eventCounts exist in reference
  // with toJSON() carrying the standard field sets (re-dumped 2026-09-25:
  // JSON.stringify(performance.timing) yields the full milestone object;
  // the old "empty own key set" read was a truncated dump).  Object.keys
  // stays empty because the fields live only behind toJSON, and the
  // offsets mirror the reference engine's natural load profile.
  try {
    if (performance) {
      var t0 = Date.now();
      // reference's timing instance owns nothing (getOwnPropertyNames is empty);
      // the milestones answer through a prototype toJSON and the brand
      // symbol lives on that prototype too
      var timingProto = Object.create(Object.prototype);
      Object.defineProperty(timingProto, Symbol.toStringTag,
        { value: "PerformanceTiming", configurable: true });
      Object.defineProperty(timingProto, "toJSON", {
        value: function () {
          var n = t0;
          return {
            navigationStart: n,
            unloadEventStart: 0, unloadEventEnd: 0,
            redirectStart: 0, redirectEnd: 0,
            fetchStart: n + 6,
            domainLookupStart: n + 14, domainLookupEnd: n + 42,
            connectStart: n + 53, secureConnectionStart: n + 55,
            connectEnd: n + 95,
            requestStart: n + 106, responseStart: n + 224,
            responseEnd: n + 262,
            domLoading: n + 225,
            domInteractive: n + 434,
            domContentLoadedEventStart: n + 486,
            domContentLoadedEventEnd: n + 480,
            domComplete: n + 777,
            loadEventStart: n + 793, loadEventEnd: n + 809
          };
        }, writable: true, configurable: true, enumerable: false
      });
      var timing = Object.create(timingProto);
      Object.defineProperty(performance, "timing",
        { value: timing, writable: true, configurable: true, enumerable: true });
    }
    if (performance) {
      var navigation = {};
      Object.defineProperty(navigation, Symbol.toStringTag,
        { value: "PerformanceNavigation", configurable: true });
      Object.defineProperty(navigation, "toJSON", {
        value: function () { return { type: 0, redirectCount: 0 }; },
        writable: true, configurable: true, enumerable: false
      });
      Object.defineProperty(performance, "navigation",
        { value: navigation, writable: true, configurable: true,
          enumerable: true });
    }
    if (performance) {
      var eventCounts = {};
      Object.defineProperty(eventCounts, Symbol.toStringTag,
        { value: "EventCounts", configurable: true });
      Object.defineProperty(performance, "eventCounts",
        { value: eventCounts, writable: true, configurable: true,
          enumerable: true });
    }
    if (performance) {
      var memory = {};
      Object.defineProperty(memory, Symbol.toStringTag,
        { value: "MemoryInfo", configurable: true });
      Object.defineProperty(performance, "memory",
        { value: memory, writable: true, configurable: true,
          enumerable: true });
    }
  } catch (e) {}
})();

// G4.156: reference exposes a real Performance constructor at page startup.  lv8
// already has a working Performance host object, but its face was split
// between that instance and a private prototype.  Re-home the visible face
// onto one public interface prototype without changing the backing methods.
(function () {
  try {
    if (!window.performance || typeof window.Performance !== "undefined") return;
    var perf = window.performance;
    var legacy = Object.getPrototypeOf(perf);
    function Performance() { throw new TypeError("Illegal constructor"); }
    Object.defineProperty(Performance, "name", {value: "Performance"});
    Object.setPrototypeOf(Performance.prototype, legacy);
    var methods = [
      "clearMarks", "clearMeasures", "clearResourceTimings", "getEntries",
      "getEntriesByName", "getEntriesByType", "mark", "measure", "now",
      "setResourceTimingBufferSize", "toJSON"
    ];
    var values = ["eventCounts", "memory", "navigation",
                  "onresourcetimingbufferfull", "timeOrigin", "timing"];
    methods.forEach(function (name) {
      var fn = perf[name];
      if (typeof fn !== "function" && legacy) fn = legacy[name];
      Object.defineProperty(Performance.prototype, name,
        {value: typeof fn === "function" ? fn : function () {},
         writable: true, configurable: true});
      try { delete perf[name]; } catch (e) {}
    });
    values.forEach(function (name) {
      var value = perf[name];
      Object.defineProperty(Performance.prototype, name,
        {value: value, writable: true, configurable: true});
      try { delete perf[name]; } catch (e) {}
    });
    Object.setPrototypeOf(perf, Performance.prototype);
    Object.defineProperty(window, "Performance",
      {value: Performance, writable: true, configurable: true, enumerable: false});
  } catch (e) {}
})();

// G4.159: native document members were exposed as per-instance own fields.
// reference exposes the same WebIDL face through a public Document prototype.
// Re-home only existing, non-internal descriptors; unsupported APIs stay
// absent until their behavior is measured and implemented.
(function () {
  try {
    if (typeof window.Document !== "undefined" || !window.document) return;
    var doc = window.document;
    var proto = Object.getPrototypeOf(doc);
    if (!proto) return;
    function Document() {
      // G4.349 (Document 专项三步③): thin shell on the standalone
      // prototype - zero-own, [object Document], URL 'about:blank'
      return Object.create(Document.prototype);
    }
    Object.defineProperty(Document, "name", {value: "Document"});
    Document.prototype = proto;
    Object.defineProperty(proto, "constructor",
      {value: Document, writable: true, configurable: true});
    Object.getOwnPropertyNames(doc).forEach(function (name) {
      if (name.indexOf("__lv8") === 0 || name === "__proto__") return;
      var descriptor = Object.getOwnPropertyDescriptor(doc, name);
      if (!descriptor) return;
      try {
        Object.defineProperty(proto, name, descriptor);
        delete doc[name];
      } catch (e) {}
    });
    ["defaultView", "hidden", "implementation", "visibilityState"].forEach(function (name) {
      var value = proto[name];
      try { Object.defineProperty(proto, name, {enumerable: true, configurable: true,
        get: function () { return value; }}); } catch (e) {}
    });
    // Generated from the current reference member-descriptor dump.  The unhosted
    // entries are intentionally inert, but retain their WebIDL descriptor
    // category so enumeration and ``in`` checks follow the reference shape.
    var methods = "adoptNode|append|captureEvents|caretPositionFromPoint|caretRangeFromPoint|clear|createAttribute|createAttributeNS|createCDATASection|createComment|createNSResolver|createNodeIterator|createProcessingInstruction|createTreeWalker|exitFullscreen|exitPictureInPicture|exitPointerLock|getElementsByName|getElementsByTagNameNS|getSelection|importNode|moveBefore|prepend|queryCommandIndeterm|queryCommandValue|releaseEvents|replaceChildren".split("|");
    methods.forEach(function (name) {
      if (name in proto) return;
      Object.defineProperty(proto, name, {value: function () {
        if (name === "getElementsByName") {
          try { return this.querySelectorAll('[name="' + String(arguments[0]).replace(/"/g, "\\\"") + '"]'); } catch (e) { return []; }
        }
        if (name === "getElementsByTagNameNS") {
          try { return this.getElementsByTagName(arguments[1]); } catch (e) { return []; }
        }
        if (name === "getSelection") {
          try { return window.getSelection ? window.getSelection() : null; } catch (e) { return null; }
        }
        return null;
      }, writable: true, configurable: true, enumerable: true});
    });
    var accessorNames = "adoptedStyleSheets|alinkColor|applets|bgColor|childElementCount|compatMode|contentType|embeds|featurePolicy|fgColor|firstElementChild|fonts|fragmentDirective|fullscreen|fullscreenElement|fullscreenEnabled|lastElementChild|linkColor|onabort|onanimationend|onanimationiteration|onanimationstart|onauxclick|onbeforecopy|onbeforecut|onbeforeinput|onbeforematch|onbeforepaste|onbeforetoggle|onbeforexrselect|onblur|oncancel|oncanplay|oncanplaythrough|onchange|onclick|onclose|oncontextlost|oncontextmenu|oncontextrestored|oncopy|oncuechange|oncut|ondblclick|ondrag|ondragend|ondragenter|ondragleave|ondragover|ondragstart|ondrop|ondurationchange|onemptied|onended|onerror|onfocus|onformdata|onfreeze|onfullscreenchange|onfullscreenerror|ongotpointercapture|oninput|oninvalid|onkeydown|onkeypress|onkeyup|onload|onloadeddata|onloadedmetadata|onloadstart|onlostpointercapture|onmousedown|onmouseenter|onmouseleave|onmousemove|onmouseout|onmouseover|onmouseup|onmousewheel|onpaste|onpause|onplay|onplaying|onpointercancel|onpointerdown|onpointerenter|onpointerleave|onpointerlockchange|onpointerlockerror|onpointermove|onpointerout|onpointerover|onpointerrawupdate|onpointerup|onprerenderingchange|onprogress|onratechange|onreadystatechange|onreset|onresize|onresume|onscroll|onscrollend|onsearch|onsecuritypolicyviolation|onseeked|onseeking|onselect|onselectionchange|onselectstart|onslotchange|onstalled|onsubmit|onsuspend|ontimeupdate|ontoggle|ontouchcancel|ontouchend|ontouchmove|ontouchstart|ontransitioncancel|ontransitionend|ontransitionrun|ontransitionstart|onvisibilitychange|onvolumechange|onwaiting|onwebkitanimationend|onwebkitanimationiteration|onwebkitanimationstart|onwebkitfullscreenchange|onwebkitfullscreenerror|onwebkittransitionend|onwheel|pictureInPictureElement|pictureInPictureEnabled|plugins|pointerLockElement|rootElement|styleSheets|timeline|vlinkColor|webkitCurrentFullScreenElement|webkitFullscreenElement|webkitFullscreenEnabled|webkitHidden|webkitIsFullScreen|webkitVisibilityState|xmlEncoding|xmlStandalone|xmlVersion".split("|");
    var slots = new WeakMap();
    accessorNames.forEach(function (name) {
      if (name in proto) return;
      Object.defineProperty(proto, name, {enumerable: true, configurable: true,
        get: function () {
          if (name.indexOf("on") === 0) {
            var bag = slots.get(this); return bag && name in bag ? bag[name] : null;
          }
          return undefined;
        }, set: function (value) {
          if (name.indexOf("on") !== 0) return;
          var bag = slots.get(this); if (!bag) { bag = {}; slots.set(this, bag); }
          bag[name] = value;
        }});
    });
    Object.defineProperty(window, "Document",
      {value: Document, writable: true, configurable: true, enumerable: false});
  } catch (e) {}
})();

// G4.158: the native crypto object already has the reference-compatible methods,
// but its interface constructor was not exported.  Expose that shared
// prototype instead of replacing the entropy implementation.
(function () {
  try {
    if (typeof window.Crypto !== "undefined" || !window.crypto) return;
    var proto = Object.getPrototypeOf(window.crypto);
    var C = proto && proto.constructor;
    if (typeof C !== "function") return;
    if (!("subtle" in proto) && "subtle" in window.crypto) {
      Object.defineProperty(proto, "subtle",
        {value: window.crypto.subtle, writable: true, configurable: true});
      try { delete window.crypto.subtle; } catch (e) {}
    }
    Object.defineProperty(window, "Crypto",
      {value: C, writable: true, configurable: true, enumerable: false});
  } catch (e) {}
})();

// G4.157: collector-first slice of the reference global interface table.  These
// constructors are all read by the frozen challenge/business flow; publish
// their startup face before adding the broad (non-read) remainder.
(function () {
  function install(name, arity, methods, fields, construct) {
    if (typeof window[name] !== "undefined") return;
    var C = construct || function () { throw new TypeError("Illegal constructor"); };
    try { Object.defineProperty(C, "name", {value: name}); } catch (e) {}
    try { Object.defineProperty(C, "length", {value: arity}); } catch (e) {}
    methods.forEach(function (key) {
      Object.defineProperty(C.prototype, key,
        {value: function () {}, writable: true, configurable: true});
    });
    fields.forEach(function (key) {
      Object.defineProperty(C.prototype, key,
        {get: function () { return undefined; }, set: function () {}, configurable: true});
    });
    Object.defineProperty(window, name,
      {value: C, writable: true, configurable: true, enumerable: false});
  }
  function ImageData(dataOrWidth, widthOrHeight, height) {
    var data, width, rows;
    if (dataOrWidth && typeof dataOrWidth.length === "number") {
      data = dataOrWidth;
      width = Number(widthOrHeight) || 0;
      rows = Number(height) || (width ? data.length / (4 * width) : 0);
    } else {
      width = Number(dataOrWidth) || 0;
      rows = Number(widthOrHeight) || 0;
      data = new Uint8ClampedArray(Math.max(0, 4 * width * rows));
    }
    Object.defineProperties(this, {
      data: {value: data, enumerable: true},
      width: {value: width, enumerable: true},
      height: {value: rows, enumerable: true},
      colorSpace: {value: "srgb", enumerable: true}
    });
  }
  install("ImageData", 2, [], ["colorSpace", "data", "height", "width"], ImageData);
  install("Path2D", 0,
    ["addPath", "arc", "arcTo", "bezierCurveTo", "closePath", "ellipse",
     "lineTo", "moveTo", "quadraticCurveTo", "rect"], []);
  install("SourceBuffer", 0, ["abort", "appendBuffer", "remove"],
    ["appendWindowEnd", "appendWindowStart", "buffered", "mode", "onabort",
     "onerror", "onupdate", "onupdateend", "onupdatestart", "timestampOffset", "updating"]);
  install("SVGPatternElement", 0, [],
    ["height", "href", "patternContentUnits", "patternTransform", "patternUnits",
     "preserveAspectRatio", "requiredExtensions", "systemLanguage", "viewBox",
     "width", "x", "y"]);
  install("CDATASection", 0, [], []);
  install("HTMLFrameSetElement", 0, [], ["cols", "rows"]);
  install("OffscreenCanvasRenderingContext2D", 0,
    ["commit", "isContextLost", "reset", "restore", "save"], ["canvas"]);
  install("TextTrackList", 0, ["getTrackById"],
    ["length", "onaddtrack", "onchange", "onremovetrack"]);
  install("ScreenOrientation", 0, ["lock", "unlock"], ["angle", "onchange", "type"]);
  install("PerformancePaintTiming", 0, [], []);
  install("MediaEncryptedEvent", 1, [], ["initData", "initDataType"]);
  install("BeforeInstallPromptEvent", 1, ["prompt"], ["platforms", "userChoice"]);
})();

// G4.155: CanvasMake2D binds created contexts to this constructor's
// prototype.  Register it during bootstrap so the global is visible before
// the first canvas is created (the reference probe enumerates it at page start).
(function () {
  if (typeof window.CanvasRenderingContext2D !== "undefined") return;
  function CanvasRenderingContext2D() {
    throw new TypeError("Illegal constructor");
  }
  try { Object.defineProperty(CanvasRenderingContext2D, "name",
    {value: "CanvasRenderingContext2D"}); } catch (e) {}
  // reference publishes the complete Canvas 2D member list before a canvas is
  // created. Native code replaces the implemented callables on first use;
  // the remaining entries keep this startup surface visible to probes.
  var proto = CanvasRenderingContext2D.prototype;
  var methods = [
    "arc", "arcTo", "beginPath", "bezierCurveTo", "clearRect", "clip",
    "closePath", "createConicGradient", "createImageData",
    "createLinearGradient", "createPattern", "createRadialGradient",
    "drawImage", "ellipse", "fill", "fillRect", "fillText",
    "getImageData", "getLineDash", "isContextLost", "isPointInPath", "measureText",
    "isPointInStroke", "lineTo", "moveTo", "putImageData",
    "quadraticCurveTo", "rect", "reset", "resetTransform", "restore",
    "rotate", "roundRect", "save", "scale", "setLineDash",
    "setTransform", "stroke", "strokeRect", "strokeText", "transform",
    "translate"
  ];
  var properties = [
    "canvas", "direction", "fillStyle", "filter", "font", "fontKerning",
    "fontStretch", "fontVariantCaps", "globalAlpha", "globalCompositeOperation",
    "imageSmoothingEnabled", "imageSmoothingQuality", "letterSpacing", "lineCap",
    "lineDashOffset", "lineJoin", "lineWidth", "miterLimit", "shadowBlur",
    "shadowColor", "shadowOffsetX", "shadowOffsetY", "strokeStyle", "textAlign",
    "textBaseline", "textRendering", "wordSpacing"
  ];
  methods.forEach(function (name) {
    if (!(name in proto)) Object.defineProperty(proto, name,
      {value: function () {}, writable: true, configurable: true});
  });
  properties.forEach(function (name) {
    if (name in proto) return;
    // G4.286: the reference exposes `canvas` as a read-only accessor (the
    // other 2D properties are read-write).
    if (name === "canvas") {
      Object.defineProperty(proto, name,
        {get: function () { return undefined; }, configurable: true});
      return;
    }
    Object.defineProperty(proto, name,
      {get: function () { return undefined; }, set: function () {}, configurable: true});
  });
  try { Object.defineProperty(window, "CanvasRenderingContext2D", {
    value: CanvasRenderingContext2D, writable: true, configurable: true,
    enumerable: false
  }); } catch (e) {}
})();
"""
