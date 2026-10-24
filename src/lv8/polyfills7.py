"""lv8 engine polyfills, tranche 7: window enumerable-surface normalization.

The challenge SDK's bootstrap walks `for (var k in window)` as an
environment inventory.  The proven engine (reference) exposes exactly this
ordered enumerable key set (captured from reference itself); Chrome/WebIDL
semantics keep interface constructors and engine internals
non-enumerable.  This tranche pins lv8's window enumeration to the
same set in the same order: hides everything else, repositions own
props, and materializes the missing members with matching shapes
(null handler slots, BarProp/External objects, native-looking
function stubs).  Idempotent.
"""

from __future__ import annotations

__all__ = ["POLYFILL_TRANCHE_7"]


POLYFILL_TRANCHE_7 = r"""
(function () {
  var WANT = "window|self|name|customElements|status|closed|top|parent|frames|length|opener|frameElement|locationbar|menubar|personalbar|scrollbars|statusbar|toolbar|external|chrome|innerWidth|innerHeight|scrollX|pageXOffset|scrollY|pageYOffset|outerWidth|outerHeight|screenLeft|screenTop|screenX|screenY|alert|confirm|prompt|close|focus|blur|getSelection|getComputedStyle|isSecureContext|reportError|btoa|atob|setTimeout|setInterval|clearTimeout|clearInterval|queueMicrotask|origin|structuredClone|webkitResolveLocalFileSystemURL|webkitRequestFileSystem|queryLocalFonts|devicePixelRatio|clientInformation|styleMedia|trustedTypes|indexedDB|crossOriginIsolated|scheduler|cookieStore|launchQueue|sharedStorage|documentPictureInPicture|speechSynthesis|originAgentCluster|credentialless|fence|requestAnimationFrame|cancelAnimationFrame|webkitRequestAnimationFrame|webkitCancelAnimationFrame|requestIdleCallback|cancelIdleCallback|moveBy|moveTo|resizeBy|resizeTo|scroll|scrollBy|scrollTo|stop|print|open|postMessage|captureEvents|releaseEvents|find|matchMedia|fetch|createImageBitmap|fetchLater|getScreenDetails|showDirectoryPicker|showOpenFilePicker|showSaveFilePicker|onafterprint|onbeforeprint|onbeforeunload|onhashchange|onlanguagechange|onmessage|onmessageerror|onoffline|ononline|onpagehide|onpageshow|onpopstate|onrejectionhandled|onstorage|onunhandledrejection|onunload|onabort|onblur|oncancel|oncanplay|oncanplaythrough|onchange|onclick|onclose|oncontextmenu|oncuechange|ondblclick|ondrag|ondragend|ondragenter|ondragleave|ondragover|ondragstart|ondrop|ondurationchange|onemptied|onended|onerror|onfocus|onformdata|oninput|oninvalid|onkeydown|onkeypress|onkeyup|onload|onloadeddata|onloadedmetadata|onloadstart|onmousedown|onmouseenter|onmouseleave|onmousemove|onmouseout|onmouseover|onmouseup|onmousewheel|onpause|onplay|onplaying|onprogress|onratechange|onreset|onresize|onscroll|onscrollend|onsecuritypolicyviolation|onseeked|onseeking|onselect|onslotchange|onstalled|onsubmit|onsuspend|ontimeupdate|ontoggle|onvolumechange|onwaiting|onwheel|onauxclick|ongotpointercapture|onlostpointercapture|onpointercancel|onpointerdown|onpointerenter|onpointerleave|onpointermove|onpointerout|onpointerover|onpointerrawupdate|onpointerup|onselectstart|onselectionchange|onanimationend|onanimationiteration|onanimationstart|ontransitioncancel|ontransitionend|ontransitionrun|ontransitionstart|onwebkitanimationend|onwebkitanimationiteration|onwebkitanimationstart|onwebkittransitionend|onbeforeinput|onbeforematch|onbeforetoggle|oncontentvisibilityautostatechange|oncontextlost|oncontextrestored|onsearch|onappinstalled|onbeforeinstallprompt|onbeforexrselect|ondevicemotion|ondeviceorientation|ondeviceorientationabsolute|onpageswap|onpagereveal|navigator|location|screen|visualViewport|performance|crypto|caches|navigation|document|history|localStorage|sessionStorage|FederatedCredential|PasswordCredential|IdleDetector|SensorErrorEvent|VideoDecoder|VideoEncoder".split("|");
  var NULLS = "|opener|fence|onafterprint|onbeforeprint|onbeforeunload|onhashchange|onlanguagechange|onmessage|onmessageerror|onoffline|ononline|onpagehide|onpageshow|onpopstate|onrejectionhandled|onstorage|onunhandledrejection|onunload|onabort|onblur|oncancel|oncanplay|oncanplaythrough|onchange|onclick|onclose|oncontextmenu|oncuechange|ondblclick|ondrag|ondragend|ondragenter|ondragleave|ondragover|ondragstart|ondrop|ondurationchange|onemptied|onended|onerror|onfocus|onformdata|oninput|oninvalid|onkeydown|onkeypress|onkeyup|onload|onloadeddata|onloadedmetadata|onloadstart|onmousedown|onmouseenter|onmouseleave|onmousemove|onmouseout|onmouseover|onmouseup|onmousewheel|onpause|onplay|onplaying|onprogress|onratechange|onreset|onresize|onscroll|onscrollend|onsecuritypolicyviolation|onseeked|onseeking|onselect|onslotchange|onstalled|onsubmit|onsuspend|ontimeupdate|ontoggle|onvolumechange|onwaiting|onwheel|onauxclick|ongotpointercapture|onlostpointercapture|onpointercancel|onpointerdown|onpointerenter|onpointerleave|onpointermove|onpointerout|onpointerover|onpointerrawupdate|onpointerup|onselectstart|onselectionchange|onanimationend|onanimationiteration|onanimationstart|ontransitioncancel|ontransitionend|ontransitionrun|ontransitionstart|onwebkitanimationend|onwebkitanimationiteration|onwebkitanimationstart|onwebkittransitionend|onbeforeinput|onbeforematch|onbeforetoggle|oncontentvisibilityautostatechange|oncontextlost|oncontextrestored|onsearch|onappinstalled|onbeforeinstallprompt|onbeforexrselect|ondevicemotion|ondeviceorientation|ondeviceorientationabsolute|onpageswap|onpagereveal|";
  var FNS = "|close|focus|blur|webkitResolveLocalFileSystemURL|webkitRequestFileSystem|webkitRequestAnimationFrame|webkitCancelAnimationFrame|moveBy|moveTo|resizeBy|resizeTo|scroll|scrollBy|scrollTo|stop|print|postMessage|captureEvents|releaseEvents|find|fetchLater|showDirectoryPicker|showOpenFilePicker|showSaveFilePicker|FederatedCredential|IdleDetector|SensorErrorEvent|VideoDecoder|VideoEncoder|";
  var BARS = "|locationbar|menubar|personalbar|scrollbars|statusbar|toolbar|";
  // reference's UI bars are branded instances over a real BarProp.prototype:
  // empty instance face, and the prototype carries the get-only `visible`
  // accessor (true), a non-enumerable constructor and the toStringTag.
  // (pfb0 window: the SDK reads each bar and its .visible value.)
  var BarPropProto = Object.create(Object.prototype);
  try { Object.defineProperty(BarPropProto, Symbol.toStringTag,
    {value: "BarProp", configurable: true}); } catch (e) {}
  try {
    var gvis = function () { return true; };
    try { Object.defineProperty(gvis, "name", {value: "get visible"}); } catch (e) {}
    try { Object.defineProperty(gvis, "length", {value: 0}); } catch (e) {}
    Object.defineProperty(BarPropProto, "visible",
      {get: gvis, configurable: true, enumerable: true});
  } catch (e) {}
  try {
    var BarPropCtor = (typeof window.BarProp === "function")
      ? window.BarProp : function BarProp() {
        throw new TypeError("Illegal constructor"); };
    Object.defineProperty(BarPropProto, "constructor",
      {value: BarPropCtor, writable: true, configurable: true,
       enumerable: false});
  } catch (e) {}
  function makeBar() {
    return Object.create(BarPropProto);
  }
  function tagobj(tag, props) {
    var o = {};
    try { Object.defineProperty(o, Symbol.toStringTag, {value: tag}); } catch (e) {}
    if (props) { for (var p in props) { try { o[p] = props[p]; } catch (e) {} } }
    return o;
  }
  Object.getOwnPropertyNames(window).forEach(function (k) {
    if (WANT.indexOf(k) !== -1) return;
    var d = Object.getOwnPropertyDescriptor(window, k);
    if (!d || !d.enumerable || !d.configurable) return;
    d.enumerable = false;
    try { Object.defineProperty(window, k, d); } catch (e) {}
  });
  WANT.forEach(function (k) {
    var d = Object.getOwnPropertyDescriptor(window, k);
    // G4.231: the reference window carries `document` as a getter-only
    // accessor placed between `navigation` and `history` (own enumerable
    // index 224/225 of the loaded business page).  The native boot value is
    // deliberately created CONFIGURABLE so this pass can relocate it to the
    // reference position; it is then re-defined non-configurable, which is
    // the descriptor reference exposes.  Without this the SDK's window key battery
    // walks 223 keys in a different order and its state-tag offsets diverge.
    if (k === "document" && d && d.get && d.configurable) {
      try {
        delete window[k];
        Object.defineProperty(window, k, {
          get: d.get, set: d.set, enumerable: true, configurable: false
        });
      } catch (e) {}
      return;
    }
    // reference exposes the six UI bars as configurable accessors.  Their setter is
    // intentionally unusual: the first assignment replaces the accessor with
    // an ordinary writable own data property.  Preserve that observable shape
    // rather than normalizing these slots as plain values.
    if (BARS.indexOf("|" + k + "|") !== -1) {
      var prior = d ? (d.get ? d.get.call(window) : d.value) : null;
      var bar = (prior && typeof prior === "object" &&
                 Object.getPrototypeOf(prior) === BarPropProto)
        ? prior : makeBar();
      try {
        Object.defineProperty(window, k, {
          get: function () { return bar; },
          set: function (value) {
            Object.defineProperty(this, k, {value: value, writable: true, enumerable: true, configurable: true});
          },
          enumerable: true,
          configurable: true
        });
      } catch (e) {}
      return;
    }
    if (d) {
      if (!d.configurable) return;
      var nd = {configurable: true, enumerable: true};
      if (d.get || d.set) { nd.get = d.get; nd.set = d.set; }
      else { nd.value = d.value; nd.writable = d.writable !== false; }
      try { delete window[k]; Object.defineProperty(window, k, nd); } catch (e) {}
      return;
    }
    var stub = undefined;
    if (NULLS.indexOf("|" + k + "|") !== -1) stub = null;
    else if (k === "window" || k === "self" || k === "frames") stub = window;
    else if (FNS.indexOf("|" + k + "|") !== -1) {
      // G4.393n: native-method form - the reference wheel's window
      // functions carry no .prototype property; a bound twin is
      // prototype-less while staying callable.  EXCEPTION: the interface
      // constructors at the FNS tail keep a live prototype (the event
      // family and face rows install onto SensorErrorEvent.prototype et al).
      var IFNAMES = "|FederatedCredential|IdleDetector|SensorErrorEvent|VideoDecoder|VideoEncoder|";
      stub = IFNAMES.indexOf("|" + k + "|") !== -1
        ? function () {}
        : function () {}.bind(window);
    }
    else if (k === "external") stub = tagobj("External");
    else if (k === "styleMedia") stub = tagobj("StyleMedia", {type: "screen", matchMedium: function () { return true; }});
    else if (k === "visualViewport") stub = tagobj("VisualViewport", {width: 1920, height: 1080, offsetLeft: 0, offsetTop: 0, pageLeft: 0, pageTop: 0, scale: 1, onresize: null, onscroll: null});
    else if (k === "launchQueue" || k === "sharedStorage" || k === "documentPictureInPicture") stub = {};
    if (stub === undefined) { try { Object.defineProperty(window, k, {value: undefined, writable: true, enumerable: true, configurable: true}); } catch (e) {} return; }
    try { Object.defineProperty(window, k, {value: stub, writable: true, enumerable: true, configurable: true}); } catch (e) {}
  });

})();
"""
