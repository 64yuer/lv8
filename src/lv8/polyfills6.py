"""lv8 engine polyfills, tranche 6: systematic browser-parity gap closure.

Adds every host surface found missing during the comprehensive lv8-vs-browser
audit.  These are surfaces that a real browser exposes and that the WAF SDK's
per-load random path selection may touch.  Each is a no-op or minimal
implementation matching browser return-type semantics.
"""

from __future__ import annotations

__all__ = ["POLYFILL_TRANCHE_6"]


POLYFILL_TRANCHE_6 = r"""
(function () {
  "use strict";
  var noop = function () {};
  var retTrue = function () { return true; };
  var retFalse = function () { return false; };
  var retZero = function () { return 0; };
  var retEmptyStr = function () { return ''; };
  var retEmptyArr = function () { return []; };
  var retNull = function () { return null; };

  function ensure(obj, name, fn) {
    if (!obj || typeof obj[name] === 'function') return;
    try {
      Object.defineProperty(obj, name, {
        value: fn, writable: true, configurable: true, enumerable: false,
      });
    } catch (e) {}
  }

  // ---- HTMLInputElement.prototype
  if (typeof HTMLInputElement !== 'undefined' && HTMLInputElement.prototype) {
    var hip = HTMLInputElement.prototype;
    ensure(hip, 'select', noop);
    ensure(hip, 'setSelectionRange', noop);
    ensure(hip, 'setRangeText', noop);
    ensure(hip, 'stepUp', function () {});
    ensure(hip, 'stepDown', function () {});
  }

  // ---- HTMLFormElement.prototype
  if (typeof HTMLFormElement !== 'undefined' && HTMLFormElement.prototype) {
    ensure(HTMLFormElement.prototype, 'reset', noop);
    ensure(HTMLFormElement.prototype, 'requestSubmit', noop);
  }

  // ---- Event.prototype
  if (typeof Event !== 'undefined' && Event.prototype) {
    ensure(Event.prototype, 'composedPath', function () {
      var t = this.target || document;
      return [t];
    });
  }

  // ---- document surfaces
  ensure(document, 'hasFocus', retTrue);
  ensure(document, 'execCommand', retFalse);
  ensure(document, 'queryCommandSupported', retTrue);
  ensure(document, 'queryCommandEnabled', retTrue);
  ensure(document, 'queryCommandState', retFalse);
  ensure(document, 'evaluate', function () {
    return {
      resultType: 4, booleanValue: false, numberValue: NaN,
      stringValue: '', singleNodeValue: null,
      snapshotLength: 0, snapshotItem: retNull,
      iterateNext: retNull, invalidIteratorState: false,
    };
  });
  ensure(document, 'elementFromPoint', function () {
    return document.body && document.body.firstElementChild || null;
  });
  ensure(document, 'elementsFromPoint', retEmptyArr);
  ensure(document, 'getAnimations', retEmptyArr);
  ensure(document, 'scroll', noop);
  ensure(document, 'scrollBy', noop);
  ensure(document, 'scrollTo', noop);

  // ---- XMLHttpRequest.prototype
  if (typeof XMLHttpRequest !== 'undefined' && XMLHttpRequest.prototype) {
    var xp = XMLHttpRequest.prototype;
    ensure(xp, 'abort', noop);
    ensure(xp, 'addEventListener', function (t, f) {
      if (!this._lv8ls) this._lv8ls = {};
      if (!this._lv8ls[t]) this._lv8ls[t] = [];
      this._lv8ls[t].push(f);
    });
    ensure(xp, 'removeEventListener', function (t, f) {
      var ls = this._lv8ls && this._lv8ls[t];
      if (ls) { var ix = ls.indexOf(f); if (ix >= 0) ls.splice(ix, 1); }
    });
    ensure(xp, 'dispatchEvent', function (ev) {
      var ls = this._lv8ls && this._lv8ls[ev && ev.type];
      if (ls) for (var i = 0; i < ls.length; i++) { try { ls[i](ev); } catch (_) {} }
      return true;
    });
  }

  // ---- window surfaces
  ensure(window, 'createImageBitmap', function () {
    return Promise.reject(new Error('NotSupportedError'));
  });
  ensure(window, 'structuredClone', function (v) {
    return JSON.parse(JSON.stringify(v));
  });
  ensure(window, 'reportError', function (e) {
    if (typeof console !== 'undefined' && console.error) console.error(e);
  });
  ensure(window, 'getScreenDetails', function () {
    return Promise.reject(new Error('NotSupportedError'));
  });
  ensure(window, 'queryLocalFonts', function () { return Promise.resolve([]); });

  // ---- Performance.prototype
  if (typeof performance !== 'undefined') {
    ensure(performance, 'setResourceTimingBufferSize', noop);
    ensure(performance, 'addEventListener', noop);
    ensure(performance, 'removeEventListener', noop);
    ensure(performance, 'clearResourceTimings', noop);
    ensure(performance, 'toJSON', function () { return {}; });
    ensure(performance, 'clearMarks', noop);
    ensure(performance, 'clearMeasures', noop);
  }

  // ---- navigator surfaces
  if (typeof navigator !== 'undefined') {
    ensure(navigator, 'vibrate', retFalse);
    ensure(navigator, 'canShare', retFalse);
    ensure(navigator, 'share', function () { return Promise.reject(new Error('NotSupportedError')); });
    ensure(navigator, 'javaEnabled', retFalse);
  }

  // ---- Range.prototype
  if (typeof Range !== 'undefined' && Range.prototype) {
    var rp = Range.prototype;
    ['selectNode', 'selectNodeContents', 'setStart', 'setEnd',
     'setStartBefore', 'setEndBefore', 'setStartAfter', 'setEndAfter',
     'collapse', 'cloneRange', 'detach', 'compareBoundaryPoints',
     'insertNode', 'surroundContents', 'extractContents', 'cloneContents',
     'deleteContents', 'toString'
    ].forEach(function (m) { ensure(rp, m, m === 'toString' ? retEmptyStr : noop); });
    ensure(rp, 'getBoundingClientRect', function () {
      return { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0, x: 0, y: 0,
        toJSON: function () { return this; } };
    });
    ensure(rp, 'getClientRects', retEmptyArr);
  }

  // ---- Text.prototype.splitText
  if (typeof Text !== 'undefined' && Text.prototype) {
    ensure(Text.prototype, 'splitText', function (offset) {
      var data = String(this.data || '');
      var cut = Number(offset) || 0;
      var rest = document.createTextNode(data.slice(cut));
      this.data = data.slice(0, cut);
      if (this.parentNode) this.parentNode.insertBefore(rest, this.nextSibling);
      return rest;
    });
  }

  // ---- CSSStyleDeclaration.prototype.setProperty
  if (typeof CSSStyleDeclaration !== 'undefined' && CSSStyleDeclaration.prototype) {
    ensure(CSSStyleDeclaration.prototype, 'setProperty', function (name, value, priority) {
      this[String(name).toLowerCase()] = value === undefined ? '' :
        String(value) + (priority ? ' !' + priority : '');
    });
    ensure(CSSStyleDeclaration.prototype, 'getPropertyValue', function (name) {
      return this[String(name).toLowerCase()] || '';
    });
    ensure(CSSStyleDeclaration.prototype, 'getPropertyPriority', retEmptyStr);
    ensure(CSSStyleDeclaration.prototype, 'removeProperty', retEmptyStr);
    ensure(CSSStyleDeclaration.prototype, 'item', function (i) { return this[i] || ''; });
    ensure(CSSStyleDeclaration.prototype, 'length', 0);
  }

  // ---- CanvasRenderingContext2D.prototype stubs
  if (typeof CanvasRenderingContext2D !== 'undefined' && CanvasRenderingContext2D.prototype) {
    var ccp = CanvasRenderingContext2D.prototype;
    var canvasMethods = ['fillText', 'strokeText', 'fillRect', 'strokeRect', 'clearRect',
      'beginPath', 'closePath', 'moveTo', 'lineTo', 'arc', 'arcTo', 'ellipse',
      'rect', 'fill', 'stroke', 'save', 'restore', 'translate', 'rotate', 'scale',
      'transform', 'setTransform', 'resetTransform', 'drawImage', 'putImageData',
      'bezierCurveTo', 'quadraticCurveTo', 'createLinearGradient', 'createRadialGradient',
      'createPattern', 'clip', 'setLineDash', 'getLineDash', 'drawFocusIfNeeded'];
    canvasMethods.forEach(function (m) { ensure(ccp, m, noop); });
    ensure(ccp, 'measureText', function () {
      return { width: 0, actualBoundingBoxAscent: 0, actualBoundingBoxDescent: 0,
        actualBoundingBoxLeft: 0, actualBoundingBoxRight: 0,
        fontBoundingBoxAscent: 0, fontBoundingBoxDescent: 0 };
    });
    ensure(ccp, 'getImageData', function (x, y, w, h) {
      var len = (w || 1) * (h || 1) * 4;
      var data = new Uint8ClampedArray(len);
      return { data: data, width: w || 1, height: h || 1 };
    });
    ensure(ccp, 'createImageData', function (w, h) {
      var len = (w || 1) * (h || 1) * 4;
      return { data: new Uint8ClampedArray(len), width: w || 1, height: h || 1 };
    });
  }

  // ---- Element.prototype geometry + misc
  if (typeof Element !== 'undefined' && Element.prototype) {
    var elp = Element.prototype;
    ensure(elp, 'scrollIntoView', noop);
    ensure(elp, 'getBoundingClientRect', function () {
      return { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0,
        x: 0, y: 0, toJSON: function () { return this; } };
    });
    ensure(elp, 'getClientRects', retEmptyArr);
  }

  // ---- window.dialog arguments polyfill
  ensure(window, 'requestAnimationFrame', function (cb) {
    return setTimeout(function () { cb(Date.now()); }, 16);
  });
  ensure(window, 'cancelAnimationFrame', function (id) { clearTimeout(id); });
})();
"""
