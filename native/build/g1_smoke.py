# G1 smoke: exercises the new native surfaces (navigation, events, timers,
# console, locale). Validation-only script; not part of the Python test suite.
# Run: python g1_smoke.py   (from native/build or native/stpyv8)
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'stpyv8'))

import STPyV8

FAILURES = []

def check(name, actual, expected):
    ok = actual == expected
    print(("PASS" if ok else "FAIL"), name, "->", repr(actual),
          ("" if ok else " expected " + repr(expected)))
    if not ok:
        FAILURES.append(name)

PROFILE = """{
  "navigator": {
    "userAgent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
    "language": "en-US",
    "languages": ["en-US", "en"],
    "hardwareConcurrency": 8,
    "deviceMemory": 8,
    "maxTouchPoints": 0,
    "platform": "Win32",
    "$methods": {"javaEnabled": false, "pdfViewerEnabled": true}
  },
  "screen": {
    "width": 1920, "height": 1080,
    "availWidth": 1920, "availHeight": 1040,
    "colorDepth": 24, "pixelDepth": 24,
    "orientation": {"type": "landscape-primary", "angle": 0}
  },
  "location": {"href": "https://example.test/start"}
}"""

with STPyV8.JSContext() as ctx:
    ctx.install_lv8(PROFILE)

    # ---- profile host objects -------------------------------------------
    check("navigator.userAgent",
          ctx.eval('navigator.userAgent').startswith("Mozilla/5.0"), True)
    check("navigator identity", ctx.eval('navigator.userAgent === navigator.userAgent'), True)
    check("languages array", ctx.eval('navigator.languages.join("-")'), "en-US-en")
    check("nested screen.orientation.type",
          ctx.eval('screen.orientation.type'), "landscape-primary")
    check("toStringTag", ctx.eval('Object.prototype.toString.call(navigator)'),
          "[object Navigator]")
    check("toStringTag screen", ctx.eval('''Object.prototype.toString.call(screen.orientation)'''),
          "[object ScreenOrientation]")
    check("getter native look",
          ctx.eval('Function.prototype.toString.call(Object.getOwnPropertyDescriptor(navigator, "userAgent").get)'),
          "function get userAgent() { [native code] }")
    check("$methods returns", ctx.eval('navigator.javaEnabled()'), False)
    check("$methods native look",
          ctx.eval('Function.prototype.toString.call(navigator.javaEnabled)'),
          "function javaEnabled() { [native code] }")
    check("$methods not own prop",
          ctx.eval('Object.keys(navigator).includes("javaEnabled")'), False)

    # ---- navigation oracle sequence 1 ------------------------------------
    ctx.eval("""
        globalThis.popCount = 0;
        addEventListener('popstate', () => { popCount += 1; });
        history.pushState({step: 1}, '', '/first?tab=a');
        history.replaceState({step: 2}, '', '/first?tab=b');
        location.assign('/second#end');
        history.back();
    """)
    check("nav1 href", ctx.eval('location.href'), "https://example.test/first?tab=b")
    check("nav1 path+search", ctx.eval('location.pathname + location.search'), "/first?tab=b")
    check("nav1 state", ctx.eval('history.state.step'), 2)
    check("nav1 length", ctx.eval('history.length'), 3)
    check("nav1 popCount", ctx.eval('popCount'), 1)

    # ---- navigation oracle sequence 2 ------------------------------------
    check("PopStateEvent init", ctx.eval("new PopStateEvent('popstate', {state: 3}).state"), 3)
    check("PopStateEvent type", ctx.eval("new PopStateEvent('popstate', {state: 3}).type"), "popstate")
    check("Event toStringTag", ctx.eval("Object.prototype.toString.call(new HashChangeEvent('hashchange'))"),
          "[object HashChangeEvent]")

    with STPyV8.JSContext() as ctx2:
        ctx2.install_lv8('{"location": {"href": "https://example.test/start#one"}}')
        ctx2.eval("""
            globalThis.navigationEvents = [];
            addEventListener('hashchange', event => navigationEvents.push(['hash', event.oldURL, event.newURL]));
            addEventListener('popstate', event => navigationEvents.push(['pop', event.state.step]));
            history.pushState({step: 1}, '', '/first#two');
            location.hash = 'three';
            history.back();
        """)
        check("nav2 href", ctx2.eval('location.href'), "https://example.test/first#two")
        import json as _json
        check("nav2 events", _json.loads(ctx2.eval('JSON.stringify(navigationEvents)')), [
            ['hash', 'https://example.test/first#two', 'https://example.test/first#three'],
            ['pop', 1],
            ['hash', 'https://example.test/first#three', 'https://example.test/first#two'],
        ])
        check("nav2 components", ctx2.eval('location.protocol+"|"+location.host+"|"+location.port+"|"+location.origin'),
              "https:|example.test||https://example.test")

    # ---- console ----------------------------------------------------------
    check("console.log native look",
          ctx.eval('Function.prototype.toString.call(console.log)'),
          "function log() { [native code] }")
    check("console keys clean", ctx.eval('Object.keys(console).length'), 0)
    ctx.eval('console.log("g1-smoke", 42, {a: 1})')  # stdout side effect

    # ---- timers (logical clock) -------------------------------------------
    ctx.eval('globalThis.count = 0; setTimeout(() => { count += 2 }, 25)')
    ctx.run_timers(24)
    check("timer not yet due", ctx.eval('count'), 0)
    ctx.run_timers(1)
    check("timer fired", ctx.eval('count'), 2)

    ctx.eval('globalThis.ticks = 0; const iv = setInterval(() => { ticks += 1 }, 10)')
    ctx.run_timers(35)
    check("interval fired 3x", ctx.eval('ticks'), 3)
    ctx.eval('clearInterval(iv)')
    ctx.run_timers(100)
    check("interval cleared", ctx.eval('ticks'), 3)
    check("timeout id numeric", isinstance(ctx.eval('setTimeout(() => {}, 5)'), int), True)
    ctx.run_timers(100)

    # ---- event target ------------------------------------------------------
    ctx.eval("""
        globalThis.seen = [];
        const handler = e => seen.push(e.detail);
        addEventListener('ping', handler);
        dispatchEvent(new CustomEvent('ping', {detail: 7}));
        removeEventListener('ping', handler);
        dispatchEvent(new CustomEvent('ping', {detail: 8}));
    """)
    check("custom event dispatch+remove", ctx.eval('JSON.stringify(seen)'), "[7]")

    # ---- Intl locale -------------------------------------------------------
    ctx.set_locale("de-DE")
    check("intl locale", ctx.eval('Intl.DateTimeFormat().resolvedOptions().locale'), "de-DE")
    ctx.set_locale("en-US")
    check("intl locale restored", ctx.eval('Intl.DateTimeFormat().resolvedOptions().locale'), "en-US")

    # ---- timezone still works ----------------------------------------------
    ctx.set_timezone("America/New_York")
    check("tz override", ctx.eval('Intl.DateTimeFormat().resolvedOptions().timeZone'), "America/New_York")

print()
if FAILURES:
    print("FAILED:", FAILURES)
    sys.exit(1)
print("G1 SMOKE: ALL OK")
