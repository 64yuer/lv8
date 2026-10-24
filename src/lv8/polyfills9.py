"""lv8 engine polyfills, tranche 9: navigator plugins/mimeTypes object graph.

The native profile layer surfaces navigator.plugins/mimeTypes as anonymous
array host objects whose items are the raw profile dictionaries.  The
fingerprint batch that feeds the WP encoder (pfb0) walks the deeper graph
instead: branded Plugin/MimeType instances on shared global prototypes,
per-instance identity (each plugin owns its own MimeType objects, the
MimeTypeArray reuses the first plugin's), own index/named descriptor faces,
item/namedItem null boundaries and Symbol.iterator.

Every member below was measured against the reference reference wheel in prod
mode (work/reference-traces/pfb0-surface.json) and is rebuilt here to that shape:

  PluginArray.prototype   length(get)/item/namedItem/refresh + toStringTag
                          + Symbol.iterator, constructor non-enumerable
  MimeTypeArray.prototype length(get)/item/namedItem + toStringTag + iter
  Plugin.prototype        name/filename/description/length(get accessors)
                          item/namedItem + toStringTag + iter
  MimeType.prototype      type/suffixes/description/enabledPlugin(get)
                          + toStringTag, no iterator
  collection instances    own index keys (enum/configurable, readonly) +
                          non-enumerable named keys (PluginArray/Plugin
                          only); item()/namedItem() return null on miss;
                          MimeTypeArray has no own named keys

Per-instance state lives in closure WeakMaps so the branded instances keep
an empty own face like reference (MimeType owns nothing at all).  Runs after the
Navigator.prototype relayout (tranche 8) and re-points whatever accessor
currently serves navigator.plugins/mimeTypes.  Idempotent.
"""

from __future__ import annotations

__all__ = ["POLYFILL_TRANCHE_9"]


POLYFILL_TRANCHE_9 = r"""
(function () {
  if (!window.navigator) return;
  var nav = window.navigator;

  // ---- locate the accessor that currently serves plugins/mimeTypes ----
  function findHolder(obj, key) {
    var o = obj;
    while (o) {
      if (Object.prototype.hasOwnProperty.call(o, key)) return o;
      o = Object.getPrototypeOf(o);
    }
    return null;
  }
  function readItems(host) {
    var items = [];
    if (!host || typeof host !== 'object') return items;
    var n = Number(host.length);
    if (!isFinite(n)) return items;
    for (var i = 0; i < n; i++) {
      var it = host.item ? host.item(i) : host[i];
      if (!it || typeof it !== 'object') continue;
      var snap = {};
      Object.getOwnPropertyNames(it).forEach(function (k) {
        if (/^\d+$/.test(k)) return;
        try { snap[k] = it[k]; } catch (e) { snap[k] = undefined; }
      });
      items.push(snap);
    }
    return items;
  }
  function str(v) { return v === undefined || v === null ? '' : String(v); }

  // Source the raw profile data directly: reading nav.plugins would route
  // through the profile getter and pollute the debug monitor's entry log.
  var prof = (typeof globalThis.__LV8_PROFILE__ === 'object' &&
              globalThis.__LV8_PROFILE__) || null;
  var navProf = (prof && prof.navigator) || null;
  var plugins = (navProf && Array.isArray(navProf.plugins))
    ? navProf.plugins : readItems(nav.plugins);
  var mimes = (navProf && Array.isArray(navProf.mimeTypes))
    ? navProf.mimeTypes : readItems(nav.mimeTypes);
  if (!plugins.length && !mimes.length) return;

  // ---- per-instance state (keeps the branded instances' own face empty)
  var pluginState = new WeakMap();  // plugin -> {values:{name,...}, mimes}
  var mimeState = new WeakMap();    // mime -> {type,suffixes,description,plugin}

  // ---- prototype plumbing (measured shapes) ----
  function protoFn(proto, key, length, fn) {
    var f = function () { return fn.apply(this, arguments); };
    try { Object.defineProperty(f, 'name', {value: key}); } catch (e) {}
    try { Object.defineProperty(f, 'length', {value: length}); } catch (e) {}
    Object.defineProperty(proto, key, {
      value: f, writable: true, configurable: true, enumerable: true
    });
  }
  function protoGetter(proto, key, compute) {
    var g = function () { return compute(this); };
    try { Object.defineProperty(g, 'name', {value: 'get ' + key}); }
    catch (e) {}
    try { Object.defineProperty(g, 'length', {value: 0}); } catch (e) {}
    Object.defineProperty(proto, key,
      {get: g, configurable: true, enumerable: true});
  }
  function protoIterator(proto, values) {
    var it = function () {
      var i = 0, items = values(this);
      return {next: function () {
        return i < items.length
          ? {value: items[i++], done: false}
          : {value: undefined, done: true};
      }};
    };
    try { Object.defineProperty(it, 'name', {value: 'values'}); } catch (e) {}
    try { Object.defineProperty(it, 'length', {value: 0}); } catch (e) {}
    Object.defineProperty(proto, Symbol.iterator, {
      value: it, writable: true, configurable: true, enumerable: false
    });
  }
  // WebIDL item() boundary (reference measured): missing/undefined -> null,
  // '1' -> 1, 0.5 -> 0, negative/out-of-range -> null, never undefined.
  function toIndex(v, len) {
    if (v === undefined) return -1;
    var n = Number(v);
    if (isNaN(n)) return -1;
    if (n < 0 || n >= len) return -1;
    return Math.floor(n);
  }
  function indexOfPluginName(items, wanted) {
    wanted = str(wanted);
    for (var i = 0; i < items.length; i++) {
      var st = pluginState.get(items[i]);
      if (st && str(st.values.name) === wanted) return i;
    }
    return -1;
  }
  function indexOfMime(items, field, wanted) {
    wanted = str(wanted);
    for (var i = 0; i < items.length; i++) {
      var st = mimeState.get(items[i]);
      if (st && str(st[field]) === wanted) return i;
    }
    return -1;
  }

  // ---- Plugin.prototype / MimeType.prototype (members first, then the
  // non-enumerable constructor lands last like reference's own-key order) ----
  var PluginProto = {}, MimeTypeProto = {};
  Object.defineProperty(PluginProto, Symbol.toStringTag,
    {value: 'Plugin', configurable: true});
  Object.defineProperty(MimeTypeProto, Symbol.toStringTag,
    {value: 'MimeType', configurable: true});
  ['name', 'filename', 'description'].forEach(function (k) {
    protoGetter(PluginProto, k, function (self) {
      var st = pluginState.get(self);
      return st ? st.values[k] : undefined;
    });
  });
  protoGetter(PluginProto, 'length', function (self) {
    var st = pluginState.get(self);
    return st ? st.mimes.length : 0;
  });
  protoFn(PluginProto, 'item', 1, function (i) {
    var st = pluginState.get(this);
    if (!st) return null;
    var idx = toIndex(i, st.mimes.length);
    return idx < 0 ? null : st.mimes[idx];
  });
  protoFn(PluginProto, 'namedItem', 1, function (t) {
    var st = pluginState.get(this);
    if (!st) return null;
    var idx = indexOfMime(st.mimes, 'type', t);
    return idx < 0 ? null : st.mimes[idx];
  });
  protoIterator(PluginProto, function (self) {
    var st = pluginState.get(self);
    return st ? st.mimes : [];
  });
  ['type', 'suffixes', 'description'].forEach(function (k) {
    protoGetter(MimeTypeProto, k, function (self) {
      var st = mimeState.get(self);
      return st ? st[k] : undefined;
    });
  });
  protoGetter(MimeTypeProto, 'enabledPlugin', function (self) {
    var st = mimeState.get(self);
    return st ? st.plugin : null;
  });

  function defineCtor(name, proto) {
    var C = function () { throw new TypeError('Illegal constructor'); };
    Object.defineProperty(C, 'name',
      {value: name, writable: true, configurable: true});
    Object.defineProperty(C, 'length',
      {value: 0, writable: true, configurable: true});
    C.prototype = proto;  // writable slot: relink to the member-carrying object
    Object.defineProperty(C.prototype, 'constructor', {
      value: C, writable: true, configurable: true, enumerable: false
    });
    try {
      Object.defineProperty(window, name,
        {value: C, writable: true, configurable: true, enumerable: false});
    } catch (e) {}
    return C;
  }
  var Plugin = defineCtor('Plugin', PluginProto);
  var MimeType = defineCtor('MimeType', MimeTypeProto);

  // ---- build the branded instances ----
  // Each plugin owns fresh MimeType objects; the MimeTypeArray reuses the
  // first plugin's instances (reference: P[0][k] === M[k], P[i][k] !== M[k], i>0).
  var pluginObjects = plugins.map(function (data) {
    var obj = Object.create(PluginProto);
    var built = mimes.map(function (md) {
      var m = Object.create(MimeTypeProto);
      mimeState.set(m, {
        type: str(md.type), suffixes: str(md.suffixes),
        description: str(md.description), plugin: obj
      });
      return m;
    });
    pluginState.set(obj, {
      values: {name: str(data.name), filename: str(data.filename),
               description: str(data.description)},
      mimes: built
    });
    // reference Plugin own face: enumerable readonly index keys, then the mime
    // type as a non-enumerable readonly named key
    built.forEach(function (m, mi) {
      Object.defineProperty(obj, String(mi), {
        value: m, writable: false, enumerable: true, configurable: true
      });
      var named = mimeState.get(m).type;
      if (named) {
        Object.defineProperty(obj, str(named), {
          value: m, writable: false, enumerable: false, configurable: true
        });
      }
    });
    return obj;
  });
  var mimeObjects = pluginObjects.length
    ? pluginState.get(pluginObjects[0]).mimes : [];

  // ---- PluginArray.prototype / MimeTypeArray.prototype ----
  var PluginArrayProto = {}, MimeTypeArrayProto = {};
  Object.defineProperty(PluginArrayProto, Symbol.toStringTag,
    {value: 'PluginArray', configurable: true});
  Object.defineProperty(MimeTypeArrayProto, Symbol.toStringTag,
    {value: 'MimeTypeArray', configurable: true});
  protoGetter(PluginArrayProto, 'length', function () {
    return pluginObjects.length;
  });
  protoFn(PluginArrayProto, 'item', 1, function (i) {
    var idx = toIndex(i, pluginObjects.length);
    return idx < 0 ? null : pluginObjects[idx];
  });
  protoFn(PluginArrayProto, 'namedItem', 1, function (n) {
    var idx = indexOfPluginName(pluginObjects, n);
    return idx < 0 ? null : pluginObjects[idx];
  });
  protoFn(PluginArrayProto, 'refresh', 0, function () { return undefined; });
  protoIterator(PluginArrayProto, function () { return pluginObjects; });
  protoGetter(MimeTypeArrayProto, 'length', function () {
    return mimeObjects.length;
  });
  protoFn(MimeTypeArrayProto, 'item', 1, function (i) {
    var idx = toIndex(i, mimeObjects.length);
    return idx < 0 ? null : mimeObjects[idx];
  });
  protoFn(MimeTypeArrayProto, 'namedItem', 1, function (t) {
    var idx = indexOfMime(mimeObjects, 'type', t);
    return idx < 0 ? null : mimeObjects[idx];
  });
  protoIterator(MimeTypeArrayProto, function () { return mimeObjects; });
  var PluginArray = defineCtor('PluginArray', PluginArrayProto);
  var MimeTypeArray = defineCtor('MimeTypeArray', MimeTypeArrayProto);

  // ---- collection instances: readonly index keys + non-enumerable named
  // keys (PluginArray only; MimeTypeArray owns indexes alone in reference) ----
  var pluginArray = Object.create(PluginArrayProto);
  var mimeTypeTarget = Object.create(MimeTypeArrayProto);
  pluginObjects.forEach(function (item, i) {
    Object.defineProperty(pluginArray, String(i), {
      value: item, writable: false, enumerable: true, configurable: true
    });
    var named = pluginState.get(item).values.name;
    if (named) {
      Object.defineProperty(pluginArray, str(named), {
        value: item, writable: false, enumerable: false, configurable: true
      });
    }
  });
  mimeObjects.forEach(function (item, i) {
    Object.defineProperty(mimeTypeTarget, String(i), {
      value: item, writable: false, enumerable: true, configurable: true
    });
  });
  // reference answers MimeTypeArray named access (mimeTypes['application/pdf'])
  // through an interceptor: Get succeeds but the key never shows up in
  // getOwnPropertyNames/descriptor or for-in.  A minimal Proxy over the
  // index-keyed target reproduces exactly that surface.
  var mimeTypeArray = new Proxy(mimeTypeTarget, {
    get: function (t, key, rx) {
      if (typeof key === 'string' && !/^\d+$/.test(key)) {
        var idx = indexOfMime(mimeObjects, 'type', key);
        if (idx >= 0) return mimeObjects[idx];
      }
      return Reflect.get(t, key, rx);
    }
  });

  // ---- re-point the navigator accessors at the rebuilt graph ----
  ['plugins', 'mimeTypes'].forEach(function (key) {
    var holder = findHolder(nav, key);
    if (!holder) return;
    var value = key === 'plugins' ? pluginArray : mimeTypeArray;
    var g = function () { return value; };
    try { Object.defineProperty(g, 'name', {value: 'get ' + key}); }
    catch (e) {}
    try { Object.defineProperty(g, 'length', {value: 0}); } catch (e) {}
    try {
      Object.defineProperty(holder, key,
        {get: g, configurable: true, enumerable: true});
    } catch (e) {}
  });

  // ---- navigator.storage.estimate: reference answers {usage, quota} in that
  // key order; the native builder emits quota first ----
  try {
    if (nav.storage && typeof nav.storage.estimate === 'function') {
      var origEstimate = nav.storage.estimate;
      var estimate = function () {
        return Promise.resolve(origEstimate.call(nav.storage))
          .then(function (r) {
            return {usage: r.usage, quota: r.quota};
          });
      };
      try { Object.defineProperty(estimate, 'name',
        {value: 'estimate'}); } catch (e) {}
      try { Object.defineProperty(estimate, 'length', {value: 0}); }
      catch (e) {}
      Object.defineProperty(nav.storage, 'estimate',
        {value: estimate, writable: true, configurable: true,
         enumerable: true});
    }
  } catch (e) {}

  // ---- window.Audio: reference has a legal element constructor here; the stub
  // table's thrower kills the SDK's canPlayType probe element.  No element
  // is created at bootstrap (a lazy element class must not be initialized
  // early) -- the constructor defers to createElement per call.
  try {
    {
      var AudioCtor = function Audio(src) {
        var el = document.createElement('audio');
        if (src !== undefined && src !== null && src !== '') {
          try { el.setAttribute('src', String(src)); } catch (e) {}
          try { Object.defineProperty(el, 'src', {
            get: function () { return el.getAttribute('src') || ''; },
            set: function (v) { try { el.setAttribute('src', String(v)); } catch (e2) {} },
            configurable: true, enumerable: true }); } catch (e) {}
        }
        return el;
      };
      Object.defineProperty(AudioCtor, 'name',
        {value: 'Audio', writable: true, configurable: true});
      Object.defineProperty(AudioCtor, 'length',
        {value: 0, writable: true, configurable: true});
      Object.defineProperty(window, 'Audio',
        {value: AudioCtor, writable: true, configurable: true,
         enumerable: false});
    }
  } catch (e) {}

  // ---- SharedArrayBuffer: reference does not expose it (crossOriginIsolated is
  // false); lv8's V8 build answers true -- hide it to match the reference
  // boolean probes.
  try {
    if (typeof SharedArrayBuffer !== 'undefined') {
      delete window.SharedArrayBuffer;
    }
  } catch (e) {}

  // ---- window.console brand: reference renders [object console] (lowercase) ----
  try {
    if (window.console &&
        Object.prototype.toString.call(window.console) ===
        '[object Console]') {
      Object.defineProperty(window.console, Symbol.toStringTag,
        {value: 'console', configurable: true});
    }
  } catch (e) {}

  // ---- navigator.userAgentData rebuild (pfb0 window: UA-CH data feeds the
  // browser fingerprint batch).  reference keeps an empty instance face with the
  // low-entropy values on prototype accessors, and getHighEntropyValues
  // answers the full known-key set (brands greases, fullVersionList,
  // platformVersion, wow64). ----
  try {
    var uadProf = null;
    try {
      var profUad = prof && prof.navigator && prof.navigator.userAgentData;
      if (profUad && typeof profUad === 'object') uadProf = profUad;
    } catch (e) {}
    var uad = uadProf || nav.userAgentData;
    if (uad && typeof uad === 'object') {
      var uadVals = {};
      ['brands', 'mobile', 'platform', 'architecture', 'bitness', 'model',
       'platformVersion', 'uaFullVersion', 'fullVersionList', 'wow64',
       'formFactors'].forEach(function (k) {
        var v;
        try { v = uad[k]; } catch (e) { return; }
        if (v !== undefined) uadVals[k] = v;
      });
      if (uadVals.fullVersionList === undefined && uadVals.brands) {
        uadVals.fullVersionList = uadVals.brands.map(function (b) {
          var ver = String(b && b.version || '');
          return {brand: b && b.brand, version:
                  ver.indexOf('.') < 0 ? ver + '.0.0.0' : ver};
        });
      }
      if (uadVals.platformVersion === undefined) {
        uadVals.platformVersion = '10.0.0';
      }
      if (uadVals.wow64 === undefined) uadVals.wow64 = false;
      // response keys come back in this fixed order, restricted to the
      // requested hints the profile actually knows (reference drops unknowns)
      var HE_ORDER = ['architecture', 'bitness', 'brands', 'fullVersionList',
        'mobile', 'model', 'platform', 'platformVersion', 'uaFullVersion',
        'wow64'];
      var UADProto = Object.create(Object.prototype);
      Object.defineProperty(UADProto, Symbol.toStringTag,
        {value: 'NavigatorUAData', configurable: true});
      ['brands', 'mobile', 'platform'].forEach(function (k) {
        protoGetter(UADProto, k, function () { return uadVals[k]; });
      });
      protoFn(UADProto, 'getHighEntropyValues', 1, function (hints) {
        var wanted = {};
        if (hints && typeof hints.length === 'number') {
          for (var i = 0; i < hints.length; i++) wanted[str(hints[i])] = true;
        }
        var out = {};
        HE_ORDER.forEach(function (k) {
          if (wanted[k] && uadVals[k] !== undefined) out[k] = uadVals[k];
        });
        return Promise.resolve(out);
      });
      protoFn(UADProto, 'toJSON', 0, function () {
        return {brands: uadVals.brands, mobile: uadVals.mobile,
                platform: uadVals.platform};
      });
      var UADCtor = function () {
        throw new TypeError('Illegal constructor');
      };
      try { Object.defineProperty(UADCtor, 'name',
        {value: 'NavigatorUAData', writable: true, configurable: true}); }
      catch (e) {}
      Object.defineProperty(UADProto, 'constructor', {
        value: UADCtor, writable: true, configurable: true, enumerable: false
      });
      var uadHolder = findHolder(nav, 'userAgentData');
      var uadNew = Object.create(UADProto);
      var uadDesc = uadHolder
        ? Object.getOwnPropertyDescriptor(uadHolder, 'userAgentData') : null;
      if (uadDesc && 'value' in uadDesc) {
        // monitor/debug mode keeps the member as an own data property;
        // preserve its flags so the debug install surface is unchanged
        uadDesc.value = uadNew;
        Object.defineProperty(uadHolder, 'userAgentData', uadDesc);
      } else if (uadHolder) {
        var ug = function () { return uadNew; };
        try { Object.defineProperty(ug, 'name',
          {value: 'get userAgentData'}); } catch (e) {}
        Object.defineProperty(uadHolder, 'userAgentData',
          {get: ug, configurable: true, enumerable: true});
      }
    }
  } catch (e) {}
})();
"""
