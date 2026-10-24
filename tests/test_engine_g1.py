"""G1 detection-surface tests: the oracle's fingerprinting / headless-probe
subset re-hosted on the C++ engine facade (``lv8.engine.JSContext``).

Each test mirrors the semantics of the corresponding oracle test in
``tests/test_smoke.py`` (which runs against the reference implementation);
expectations are kept identical. Where the C++ engine is architecturally
different (native host objects, V8 version), the assertion states the
engine-native expectation explicitly.
"""

from __future__ import annotations

import copy
import json
import re

import pytest

from lv8.engine import JSContext
from lv8.errors import NotImplementedYetError


def test_minimal_bom_uses_profile_and_is_not_affected_by_input_mutation():
    env = {
        "navigator": {"userAgent": "lv8-test", "languages": ["zh-CN"]},
        "location": {"href": "https://example.test/a"},
    }
    ctx = JSContext(environment=env)
    # the facade must have deep-copied the input on construction
    env["navigator"]["userAgent"] = "mutated"
    assert ctx.eval(
        "window === self && window === top && window === parent && "
        "document.location === location && "
        "navigator.userAgent"
    ) == "lv8-test"
    assert ctx.eval("navigator.languages[0]") == "zh-CN"
    assert ctx.eval(
        "Object.getOwnPropertyDescriptor(Navigator.prototype, 'userAgent')."
        "get.call(navigator)"
    ) == "lv8-test"
    assert ctx.eval("location.href") == "https://example.test/a"
    assert ctx.eval("document.contains(document)") is True
    assert ctx.eval("document.defaultView === window") is True
    assert ctx.eval("document.referrer") == ""
    assert ctx.eval("document.scrollingElement === document.documentElement") is True
    assert ctx.eval("navigator.doNotTrack") is None
    assert ctx.eval("[document.visibilityState, document.hidden]", to_py=True) == [
        "visible", False
    ]
    # reference ground truth: createEvent rejects every event class with
    # NotSupportedError (the WAF SDK branches on the throw)
    assert ctx.eval(
        "(function(){ try { document.createEvent.call(document, 'Event');"
        " return 'NO-THROW'; } catch (e) { return e.name; } })()"
    ) == "NotSupportedError"
    assert ctx.eval(
        "Object.prototype.toString.call(document.createExpression.call("
        "document, './/main'))"
    ) == "[object XPathExpression]"
    assert ctx.eval(
        "(() => { const el = document.createElement('i'); el.id = 'g1-all'; "
        "document.body.appendChild(el); return document.all.namedItem.call("
        "document.all, 'g1-all') === el && document.all.item.call("
        "document.all, 0) !== null; })()"
    ) is True
    # B1 realm semantics (browsers/reference): a detached iframe exposes no
    # contentWindow; once in the tree it is an independent realm whose
    # intrinsics do not alias the host page's.
    assert ctx.eval(
        "(() => { const frame = document.createElement('iframe'); const d = "
        "frame.contentWindow; document.body.appendChild(frame); const w = "
        "frame.contentWindow; const out = d === null && w !== undefined && "
        "w !== window && w.Array !== window.Array; "
        "document.body.removeChild(frame); return out; })()"
    ) is True
    ctx.page.load("<main id='page-load-cleanup'></main>")
    assert ctx.eval("document.getElementById('page-load-cleanup') !== null") is True
    assert ctx.eval("document.querySelector('[data-lv8-document-location]')") is None
    assert ctx.eval("Image(4, 3).tagName") == "IMG"
    assert ctx.eval("navigator.userAgentData.toJSON().platform") == "Windows"
    ctx.eval("location.assign('https://example.test/b')")
    assert ctx.eval("location.href") == "https://example.test/b"
    # history depth grew with the assign
    assert ctx.eval("history.length") == 2


def test_location_and_history_keep_url_state_and_navigation_in_sync():
    ctx = JSContext(
        environment={"location": {"href": "https://example.test/start"}}
    )
    ctx.eval(
        """
        globalThis.popCount = 0;
        addEventListener('popstate', () => { popCount += 1; });
        history.pushState({step: 1}, '', '/first?tab=a');
        history.replaceState({step: 2}, '', '/first?tab=b');
        location.assign('/second#end');
        history.back();
        """
    )
    assert ctx.eval("location.href") == "https://example.test/first?tab=b"
    assert ctx.eval("location.pathname + location.search") == "/first?tab=b"
    assert ctx.eval("history.state.step") == 2
    assert ctx.eval("history.length") == 3
    assert ctx.eval("popCount") == 1


def test_history_events_and_location_component_setters_carry_url_and_state():
    ctx = JSContext(
        environment={"location": {"href": "https://example.test/start#one"}}
    )
    ctx.eval(
        """
        globalThis.navigationEvents = [];
        addEventListener('hashchange', event =>
            navigationEvents.push(['hash', event.oldURL, event.newURL]));
        addEventListener('popstate', event =>
            navigationEvents.push(['pop', event.state.step]));
        history.pushState({step: 1}, '', '/first#two');
        location.hash = 'three';
        history.back();
        """
    )
    assert ctx.eval("location.href") == "https://example.test/first#two"
    assert ctx.eval("navigationEvents", to_py=True) == [
        ["hash", "https://example.test/first#two",
         "https://example.test/first#three"],
        ["pop", 1],
        ["hash", "https://example.test/first#three",
         "https://example.test/first#two"],
    ]
    assert ctx.eval("new PopStateEvent('popstate', {state: 3}).state") == 3


def test_browser_host_objects_expose_their_expected_to_string_tags():
    ctx = JSContext()
    for expr, tag in [
        ("navigator", "Navigator"),
        ("navigator.userAgentData", "NavigatorUAData"),
        ("navigator.plugins", "PluginArray"),
        ("navigator.mimeTypes", "MimeTypeArray"),
        ("location", "Location"),
        ("history", "History"),
    ]:
        assert ctx.eval(f"Object.prototype.toString.call({expr})") == (
            f"[object {tag}]"
        )
    # plugin collections behave like arrays (Chrome's default PDF plugin
    # family: 5 plugins, each exposing its 2-mime length)
    assert ctx.eval("navigator.plugins.length") == 5
    assert ctx.eval("navigator.plugins[0].name") == "PDF Viewer"
    assert ctx.eval("navigator.plugins[0].length") == 2
    assert ctx.eval("navigator.plugins.item(0).name") == "PDF Viewer"
    assert ctx.eval("navigator.plugins instanceof PluginArray")


def test_timezone_profile_drives_dst_aware_date_and_intl_observations():
    ctx = JSContext(config={"timezone": "America/New_York"})
    assert (
        ctx.eval("Intl.DateTimeFormat().resolvedOptions().timeZone")
        == "America/New_York"
    )
    observations = ctx.eval(
        """
        (() => {
          const winter = new Date(Date.UTC(2024, 0, 15, 12, 0, 0));
          const summer = new Date(Date.UTC(2024, 6, 15, 12, 0, 0));
          return [
            winter.getTimezoneOffset(),
            summer.getTimezoneOffset(),
            winter.toString(),
            summer.toTimeString(),
            summer.toDateString(),
            summer.toLocaleString('en-US', {timeZoneName: 'short'}),
            new Intl.DateTimeFormat('en-US', {timeZone: 'UTC'})
                .resolvedOptions().timeZone,
            new Date('not-a-date').toString(),
          ];
        })()
        """,
        to_py=True,
    )
    assert observations[0] == 300  # EST, January
    assert observations[1] == 240  # EDT, July
    assert observations[2] == (
        "Mon Jan 15 2024 07:00:00 GMT-0500 (Eastern Standard Time)"
    )
    assert observations[3] == "08:00:00 GMT-0400 (Eastern Daylight Time)"
    assert observations[4] == "Mon Jul 15 2024"
    # Known oracle deviation: the reference (mini-racer 14.4) returns a bare
    # 'EDT' here, but real Chrome returns the full formatted string ending in
    # the short zone name; we follow Chrome per DESIGN "fingerprint parity".
    assert observations[5].endswith("EDT")
    assert observations[6] == "UTC"
    assert observations[7] == "Invalid Date"
    # native appearance of the Date intrinsics is structural in C++
    assert ctx.eval(
        "Function.prototype.toString.call(Date.prototype.getTimezoneOffset)"
    ) == "function getTimezoneOffset() { [native code] }"
    # a second context re-points the ICU default zone
    ctx2 = JSContext(config={"timezone": "Asia/Shanghai"})
    assert ctx2.eval(
        "new Date(Date.UTC(2024, 0, 15, 12, 0, 0)).toString()"
    ) == "Mon Jan 15 2024 20:00:00 GMT+0800 (China Standard Time)"


def test_timezone_profile_rejects_unknown_iana_names():
    with pytest.raises(Exception, match="Mars/Olympus"):
        JSContext(config={"timezone": "Mars/Olympus"})


def test_navigator_connection_uses_environment_network_information_profile():
    ctx = JSContext(
        environment={
            "connection": {
                "effectiveType": "3g",
                "downlink": 1.5,
                "rtt": 280,
                "saveData": True,
                "type": "cellular",
            }
        }
    )
    # JSON conversion yields list/dict (the tuple in the oracle is
    # mini-racer specific; JSON-shaped containers are the engine contract)
    # reference-measured (2026-09-26): the profile drives effectiveType/downlink/
    # rtt/saveData, but `type` is not exposed at all ('type' in
    # navigator.connection is false even when the profile supplies it).
    assert ctx.eval(
        "[navigator.connection.effectiveType, navigator.connection.downlink,"
        " navigator.connection.rtt, navigator.connection.saveData,"
        " navigator.connection.type, 'type' in navigator.connection]",
        to_py=True,
    ) == ["3g", 1.5, 280, True, None, False]
    assert ctx.eval("navigator.connection instanceof NetworkInformation")


def test_get_monitor_mode_reports_constructor_mode():
    assert JSContext().get_monitor_mode() == "prod"
    assert JSContext(mode="debug").get_monitor_mode() == "debug"


def test_navigator_battery_uses_the_environment_profile_without_host_hardware():
    ctx = JSContext(
        environment={
            "battery": {
                "charging": False,
                "chargingTime": 120,
                "dischargingTime": 3600,
                "level": 0.42,
            }
        }
    )
    ctx.eval(
        "navigator.getBattery().then(battery => "
        "{ globalThis.batteryInfo = [battery.charging, battery.chargingTime, "
        "battery.dischargingTime, battery.level]; })"
    )
    # settled promises resolve at the per-eval microtask checkpoint
    assert ctx.eval("batteryInfo", to_py=True) == [False, 120, 3600, 0.42]
    assert (
        ctx.eval("Function.prototype.toString.call(navigator.getBattery)")
        == "function getBattery() { [native code] }"
    )
    assert ctx.eval("navigator.getBattery() instanceof Promise")
    # default battery is charging with Infinity remaining
    ctx2 = JSContext()
    ctx2.eval(
        "navigator.getBattery().then(b => "
        "{ globalThis.defaultOk = b.charging === true && "
        "b.dischargingTime === Infinity; })"
    )
    assert ctx2.eval("defaultOk") is True


def test_match_media_is_deterministic_from_environment_screen_and_media_profile():
    ctx = JSContext(
        environment={
            "screen": {"width": 1440, "height": 1080},
            "devicePixelRatio": 2,
            "media": {
                "prefersColorScheme": "dark",
                "prefersReducedMotion": "reduce",
                "hover": "none",
                "pointer": "coarse",
            },
        }
    )
    assert ctx.eval(
        "[matchMedia('(prefers-color-scheme: dark)').matches, "
        "matchMedia('(prefers-reduced-motion: reduce)').matches, "
        "matchMedia('(pointer: coarse)').matches, "
        "matchMedia('(any-pointer: coarse)').matches, "
        "matchMedia('(any-hover: none)').matches, "
        "matchMedia('(color-gamut: srgb)').matches, "
        "matchMedia('(display-mode: browser)').matches, "
        "matchMedia('(hover: hover)').matches]",
        to_py=True,
    ) == [True, True, True, True, True, True, True, False]
    # G4.238 reference parity: the reference wheel reports no-match for every
    # width/height/resolution/orientation feature (host-level A/B in
    # work/_mq_hostab2.py), so these stay False on purpose.
    assert ctx.eval(
        "[matchMedia('(min-width: 1200px)').matches, "
        "matchMedia('(min-resolution: 2dppx)').matches, "
        "matchMedia('(orientation: landscape)').matches, "
        "matchMedia('(any-pointer: fine)').matches]",
        to_py=True,
    ) == [False, False, False, False]
    assert (
        ctx.eval("Function.prototype.toString.call(matchMedia)")
        == "function matchMedia() { [native code] }"
    )
    assert ctx.eval("matchMedia('(min-width: 500px)').media") == (
        "(min-width: 500px)"
    )
    assert ctx.eval(
        "Object.prototype.toString.call(matchMedia('(hover: none)'))"
    ) == "[object MediaQueryList]"


def test_screen_orientation_uses_environment_profile_and_offline_locking():
    ctx = JSContext(
        environment={
            "screen": {
                "orientation": {"type": "portrait-primary", "angle": 90}
            }
        }
    )
    assert ctx.eval(
        "[screen.orientation.type, screen.orientation.angle]",
        to_py=True,
    ) == ["portrait-primary", 90]
    ctx.eval(
        "screen.orientation.lock('landscape-primary').then(() => "
        "{ globalThis.orientationType = screen.orientation.type; })"
    )
    assert ctx.eval("orientationType") == "landscape-primary"
    assert ctx.eval("screen.orientation.angle") == 90
    assert ctx.eval("screen.orientation instanceof ScreenOrientation")


def test_window_orientation_reads_undefined_without_profile_configuration():
    # reference truth (reference-measured): there is no window.orientation member
    # at all; screen.orientation keeps its host defaults
    ctx = JSContext()
    assert ctx.eval("window.orientation", to_py=True) is None
    assert ctx.eval("typeof screen.orientation") == "object"


def test_window_orientation_stays_absent_when_environment_configures_it():
    # configuring screen.orientation (or a legacy window orientation value)
    # must not resurrect the member reference does not have
    ctx = JSContext(
        environment={
            "screen": {
                "orientation": {"type": "portrait-primary", "angle": 90}
            },
            "window": {"orientation": 90},
        }
    )
    assert ctx.eval("window.orientation", to_py=True) is None
    assert ctx.eval("screen.orientation.angle", to_py=True) == 90


def test_navigator_user_agent_data_collections_and_permissions_follow_profile():
    ctx = JSContext(
        environment={
            "navigator": {
                "userAgent": "lv8 Agent/1.0",
                "userAgentData": {
                    "brands": [{"brand": "Lv8", "version": "1"}],
                    "mobile": True,
                    "platform": "Android",
                    "architecture": "arm",
                    "bitness": "64",
                    "model": "lv8-phone",
                },
                "plugins": [
                    {
                        "name": "Lv8 PDF",
                        "filename": "lv8-pdf",
                        "description": "PDF",
                    }
                ],
                "mimeTypes": [
                    {
                        "type": "application/lv8",
                        "description": "Lv8 data",
                        "suffixes": "lv8",
                    }
                ],
            },
            "permissions": {"geolocation": "denied"},
        }
    )
    assert ctx.eval("navigator.userAgent") == "lv8 Agent/1.0"
    assert (
        ctx.eval("navigator.plugins.namedItem('Lv8 PDF').filename")
        == "lv8-pdf"
    )
    assert ctx.eval("navigator.mimeTypes['application/lv8'].suffixes") == "lv8"
    ctx.eval(
        "navigator.userAgentData.getHighEntropyValues("
        "['architecture', 'bitness', 'model'])"
        ".then(value => { globalThis.highEntropy = value; });"
        "navigator.permissions.query({name: 'geolocation'})"
        ".then(value => { globalThis.permissionState = value.state; });"
    )
    assert ctx.eval("highEntropy", to_py=True) == {
        "architecture": "arm",
        "bitness": "64",
        "model": "lv8-phone",
    }
    assert ctx.eval("permissionState") == "denied"
    # unlisted permission states default to 'prompt' like a real browser
    ctx.eval(
        "navigator.permissions.query({name: 'notifications'})"
        ".then(value => { globalThis.notificationsState = value.state; });"
    )
    assert ctx.eval("notificationsState") == "prompt"


def test_plugin_mime_graph_matches_ref_measured_surface():
    # pfb0 oracle: the whole navigator.plugins/mimeTypes object graph as
    # measured on the reference reference wheel (work/reference-traces/pfb0-surface.json)
    ctx = JSContext()
    assert ctx.eval("navigator.plugins instanceof PluginArray") is True
    assert ctx.eval("navigator.mimeTypes instanceof MimeTypeArray") is True
    assert ctx.eval(
        "Object.getPrototypeOf(navigator.plugins) === PluginArray.prototype"
    ) is True
    assert ctx.eval(
        "Object.getPrototypeOf(navigator.plugins[0]) === Plugin.prototype"
    ) is True
    assert ctx.eval(
        "Object.getPrototypeOf(navigator.mimeTypes[0]) === MimeType.prototype"
    ) is True
    # brands and constructor back-links
    assert ctx.eval(
        "Object.prototype.toString.call(navigator.plugins[0])"
    ) == "[object Plugin]"
    assert ctx.eval(
        "Object.prototype.toString.call(navigator.mimeTypes[0])"
    ) == "[object MimeType]"
    assert ctx.eval("navigator.plugins[0].constructor === Plugin") is True
    assert ctx.eval("navigator.plugins.constructor === PluginArray") is True
    # collection own face: readonly enumerable index keys, non-enumerable
    # named keys, no own length; MimeTypeArray owns indexes alone
    assert ctx.eval(
        "JSON.stringify(Object.getOwnPropertyNames(navigator.plugins))"
    ) == (
        '["0","1","2","3","4","PDF Viewer","Chrome PDF Viewer",'
        '"Chromium PDF Viewer","Microsoft Edge PDF Viewer",'
        '"WebKit built-in PDF"]'
    )
    assert ctx.eval(
        "JSON.stringify(Object.getOwnPropertyNames(navigator.mimeTypes))"
    ) == '["0","1"]'
    assert ctx.eval(
        "JSON.stringify(Object.keys(navigator.plugins))"
    ) == '["0","1","2","3","4"]'
    assert ctx.eval(
        "JSON.stringify(Object.getOwnPropertyDescriptor("
        "navigator.plugins, 0))"
    ) == (
        '{"value":{"0":{},"1":{}},"writable":false,'
        '"enumerable":true,"configurable":true}'
    )
    assert ctx.eval(
        "JSON.stringify(Object.getOwnPropertyDescriptor("
        "navigator.plugins, 'PDF Viewer'))"
    ) == (
        '{"value":{"0":{},"1":{}},"writable":false,'
        '"enumerable":false,"configurable":true}'
    )
    # Plugin own face: mime indexes + non-enumerable mime-type named keys
    assert ctx.eval(
        "JSON.stringify(Object.getOwnPropertyNames(navigator.plugins[0]))"
    ) == '["0","1","application/pdf","text/pdf"]'
    # MimeType own face is empty; values live behind prototype accessors
    assert ctx.eval(
        "JSON.stringify(Object.getOwnPropertyNames(navigator.mimeTypes[0]))"
    ) == "[]"
    assert ctx.eval(
        "JSON.stringify(Object.getOwnPropertyNames(MimeType.prototype))"
    ) == ('["type","suffixes","description","enabledPlugin",'
          '"constructor"]')
    assert ctx.eval("navigator.mimeTypes[0].type") == "application/pdf"
    assert ctx.eval("navigator.mimeTypes[1].type") == "text/pdf"
    assert ctx.eval("navigator.mimeTypes[0].suffixes") == "pdf"
    # prototype members: accessor length, item/namedItem/refresh arities,
    # enumerable; constructor non-enumerable and last
    assert ctx.eval(
        "JSON.stringify(Object.getOwnPropertyNames(PluginArray.prototype))"
    ) == '["length","item","namedItem","refresh","constructor"]'
    assert ctx.eval(
        "JSON.stringify(Object.getOwnPropertyNames(MimeTypeArray.prototype))"
    ) == '["length","item","namedItem","constructor"]'
    assert ctx.eval(
        "JSON.stringify(Object.getOwnPropertyNames(Plugin.prototype))"
    ) == ('["name","filename","description","length","item","namedItem",'
          '"constructor"]')
    assert ctx.eval(
        "Object.getOwnPropertyDescriptor(PluginArray.prototype, "
        "'length').set === undefined"
    ) is True
    assert ctx.eval(
        "Object.getOwnPropertyDescriptor(PluginArray.prototype, "
        "'length').get.name"
    ) == "get length"
    assert ctx.eval(
        "Object.getOwnPropertyDescriptor(PluginArray.prototype, "
        "'item').value.length"
    ) == 1
    assert ctx.eval("typeof PluginArray.prototype.refresh") == "function"
    assert ctx.eval("typeof MimeTypeArray.prototype.refresh") == "undefined"
    # MimeType is not iterable; the other three prototypes are
    assert ctx.eval("typeof Plugin.prototype[Symbol.iterator]") == "function"
    assert ctx.eval(
        "typeof MimeTypeArray.prototype[Symbol.iterator]"
    ) == "function"
    assert ctx.eval(
        "typeof MimeType.prototype[Symbol.iterator]"
    ) == "undefined"
    assert ctx.eval("Array.from(navigator.plugins).length") == 5
    assert ctx.eval("Array.from(navigator.mimeTypes).length") == 2
    assert ctx.eval("Array.from(navigator.plugins[0]).length") == 2
    # identity graph: plugin 0 shares its mimes with the MimeTypeArray,
    # every other plugin carries fresh branded instances of the same types
    assert ctx.eval(
        "navigator.plugins[0][0] === navigator.mimeTypes[0]"
    ) is True
    assert ctx.eval(
        "navigator.plugins[3][0] === navigator.mimeTypes[0]"
    ) is False
    assert ctx.eval(
        "navigator.plugins[3][0].type === navigator.mimeTypes[0].type"
    ) is True
    assert ctx.eval(
        "navigator.mimeTypes[0].enabledPlugin === navigator.plugins[0]"
    ) is True
    assert ctx.eval(
        "navigator.plugins[4][0].enabledPlugin === navigator.plugins[4]"
    ) is True
    # reads are identity-stable and item/namedItem return the same objects
    assert ctx.eval(
        "navigator.plugins[1] === navigator.plugins.item(1)"
    ) is True
    assert ctx.eval(
        "navigator.plugins[1] === "
        "navigator.plugins.namedItem('Chrome PDF Viewer')"
    ) is True
    assert ctx.eval(
        "navigator.plugins[0].item(1) === navigator.mimeTypes[1]"
    ) is True
    assert ctx.eval(
        "navigator.plugins[0].namedItem('text/pdf') === "
        "navigator.mimeTypes[1]"
    ) is True
    # boundaries: misses return null, index access returns undefined,
    # fractional indexes truncate like WebIDL ToIndex
    assert ctx.eval("navigator.plugins.item(99)") is None
    assert ctx.eval("navigator.plugins.item()") is None
    assert ctx.eval("navigator.plugins.item(-1)") is None
    assert ctx.eval(
        "navigator.plugins.item(0.5) === navigator.plugins[0]"
    ) is True
    assert ctx.eval("navigator.plugins.namedItem('nope')") is None
    assert ctx.eval("navigator.mimeTypes.namedItem('x/y')") is None
    assert ctx.eval("navigator.plugins[0].item(9)") is None
    assert ctx.eval("String(navigator.plugins[99])") == "undefined"
    # MimeTypeArray named access is interceptor-shaped: Get answers,
    # descriptors/enumeration stay blind
    assert ctx.eval(
        "navigator.mimeTypes['application/pdf'] === navigator.mimeTypes[0]"
    ) is True
    assert ctx.eval(
        "Object.getOwnPropertyDescriptor(navigator.mimeTypes, "
        "'application/pdf')"
    ) is None
    assert ctx.eval(
        "Object.keys(navigator.mimeTypes).includes('application/pdf')"
    ) is False
    # constructors are native-shaped: empty, throwing; own names match
    # reference's native constructor list exactly (V8 legacy pills included)
    assert ctx.eval("PluginArray.length") == 0
    assert ctx.eval(
        "JSON.stringify(Object.getOwnPropertyNames(Plugin))"
    ) == '["length","name","arguments","caller","prototype"]'
    assert ctx.eval(
        "(function () { try { new Plugin(); return 'no-throw'; } "
        "catch (e) { return e.constructor.name + ':' + e.message; } })()"
    ) == "TypeError:Illegal constructor"
    # env-provided profiles flow through the same branded graph
    ctx2 = JSContext(environment={
        "navigator": {
            "plugins": [{"name": "Lv8 PDF", "filename": "lv8-pdf",
                         "description": "PDF", "length": 1}],
            "mimeTypes": [{"type": "application/lv8",
                           "description": "Lv8 data", "suffixes": "lv8"}],
        },
    })
    assert ctx2.eval(
        "Object.prototype.toString.call(navigator.plugins[0])"
    ) == "[object Plugin]"
    assert ctx2.eval(
        "navigator.plugins.namedItem('Lv8 PDF').filename"
    ) == "lv8-pdf"
    assert ctx2.eval(
        "navigator.mimeTypes['application/lv8'].suffixes"
    ) == "lv8"
    assert ctx2.eval(
        "navigator.plugins[0].item(0) === navigator.mimeTypes[0]"
    ) is True
    assert ctx2.eval(
        "navigator.mimeTypes[0].enabledPlugin === navigator.plugins[0]"
    ) is True


# --------------------------------------------------------------------------
# facade-specific behavior (beyond the oracle subset)


def test_logical_clock_semantics_match_reference():
    ctx = JSContext()
    assert ctx.now == 0
    ctx.eval("globalThis.count = 0; setTimeout(() => { count += 2 }, 25)")
    assert ctx.pending_tasks == 1
    assert ctx.advance(24) == 0
    assert ctx.eval("count") == 0
    assert ctx.advance(1) == 1
    assert ctx.eval("count") == 2
    assert ctx.pending_tasks == 0
    ctx.eval("globalThis.ticks = 0; const iv = setInterval(() => { ticks++ }, 10)")
    assert ctx.advance(35) == 3
    assert ctx.eval("ticks") == 3
    ctx.eval("clearInterval(iv)")
    assert ctx.advance(100) == 0
    assert ctx.eval("ticks") == 3


def test_to_py_round_trip_and_json_conversion():
    ctx = JSContext()
    assert ctx.eval("1 + 1", to_py=True) == 2
    assert ctx.eval("[1, 2, 3]", to_py=True) == [1, 2, 3]
    assert ctx.eval("({a: {b: 'c'}})", to_py=True) == {"a": {"b": "c"}}
    assert ctx.eval("undefined", to_py=True) is None


def test_environment_property_is_merged_and_copied():
    env = {"navigator": {"userAgent": "ua-x"}}
    ctx = JSContext(environment=env)
    merged = ctx.environment
    assert merged["navigator"]["userAgent"] == "ua-x"
    assert merged["navigator"]["platform"] == "Win32"  # default preserved
    assert merged["screen"]["width"] == 1920
    # mutation of the returned snapshot must not leak into the context
    merged["navigator"]["userAgent"] = "mutated"
    assert ctx.environment["navigator"]["userAgent"] == "ua-x"
    assert ctx.eval("navigator.userAgent") == "ua-x"


def test_environment_consistency_warnings():
    ctx = JSContext(
        environment={
            "navigator": {
                "userAgent": "Mozilla/5.0 (X11; Linux x86_64) Safari/537.36",
            }
        }
    )
    warnings = ctx.validate_environment()
    assert any("userAgentData.platform reports Windows" in w for w in warnings)
    assert any("no Chrome/ token" in w for w in warnings)
    assert ctx.environment_warnings == warnings
    # consistent profile stays quiet
    ok = JSContext()
    assert ok.validate_environment() == []


def test_get_defaults_returns_supported_paths():
    # oracle semantics: get_defaults() is a flat dotted-key map
    defaults = JSContext.get_defaults()
    assert defaults["navigator.userAgent"].startswith("Mozilla/5.0")
    assert defaults["config.clipboardText"] == ""
    assert defaults["config.animationFrameInterval"] == pytest.approx(
        1000.0 / 60
    )
    assert defaults["config.strictEnvironment"] is False
    assert defaults["config.webSocketMessages"] == {}
    assert defaults["window.devicePixelRatio"] == 1
    # mutation of the returned map must not affect the next call
    defaults["config.clipboardText"] = "changed"
    assert JSContext.get_defaults()["config.clipboardText"] == ""


def test_v8_version_reports_engine():
    ctx = JSContext()
    assert re.fullmatch(r"13\.\d+\.\d+\.\d+", ctx.v8_version)


def test_closed_context_rejects_use():
    ctx = JSContext()
    ctx.close()
    assert ctx.closed
    with pytest.raises(Exception):
        ctx.eval("1")
    assert JSContext(config={"timezone": "America/New_York"}).eval("1") == 1


def test_g3_surfaces_rejected_until_implemented():
    # add_resource/expose/set_network_bridge landed across G3.2-G3.11;
    # the custom js_api alias stays gated
    ctx = JSContext()
    with pytest.raises(NotImplementedYetError):
        JSContext(js_api="__custom__")


def test_debug_mode_records_browser_api_reads_calls_and_respects_ignore_paths():
    ctx = JSContext(mode="debug", ignore_apis=["navigator.language"])
    # ignored paths are hidden at install time: no value, no own key
    assert ctx.eval("navigator.language") is None
    assert ctx.eval('Object.keys(navigator).includes("language")') is False
    ctx.eval(
        "navigator.userAgent; navigator.language; "
        "location.assign('/next')"
    )
    assert ctx.eval("__iv8__.monitor.enabled") is True
    assert ctx.eval("typeof __lv8__") == "undefined"
    entries = ctx.eval("__iv8__.monitor.entries", to_py=True)
    assert [e["path"] for e in entries] == [
        "navigator.userAgent",
        "location.assign",
    ]
    assert [e["operation"] for e in entries] == ["get", "call"]
    ctx.eval("__iv8__.monitor.clear()")
    assert ctx.eval("__iv8__.monitor.entries.length") == 0


def test_prod_mode_does_not_record_monitor_entries():
    ctx = JSContext()
    assert ctx.eval("__iv8__.monitor.enabled") is False
    ctx.eval("navigator.userAgent; location.href")
    assert ctx.eval("__iv8__.monitor.entries.length") == 0


def test_console_surface_matches_browser_shape_and_g1_recording():
    ctx = JSContext()
    # reference-measured appearance: [object console] (lowercase brand, pfb0
    # window surface), no own keys, native methods
    assert ctx.eval("Object.prototype.toString.call(console)") == (
        "[object console]"
    )
    assert ctx.eval("Object.keys(console).length") == 0
    assert ctx.eval("console.log.toString().includes('[native code]')") is True
    methods = [
        "log", "info", "warn", "error", "debug", "trace", "dir", "dirxml",
        "table", "group", "groupCollapsed", "groupEnd", "time", "timeLog",
        "timeEnd", "count", "countReset", "assert", "clear",
    ]
    assert ctx.eval(
        "[" + ",".join(f"typeof console.{m} === 'function'" for m in methods) + "]",
        to_py=True,
    ) == [True] * len(methods)
    # count keeps per-label state; assert with a truthy condition is silent
    ctx.eval("console.count('c'); console.count('c'); console.assert(true, 'no')")
    # a failed assertion writes the message without the condition argument
    ctx.eval("console.assert(false, 'bad', 42)")  # -> stderr-like stdout line


def test_async_rejection_propagation_details():
    ctx = JSContext()
    # native rejected promise handlers run with the reason at the checkpoint
    ctx.eval(
        "Promise.reject('a').catch(e => { globalThis.caught = 'caught:' + e; })"
    )
    assert ctx.eval("caught") == "caught:a"
    # host-object rejected promises propagate through .catch
    ctx.eval(
        "screen.orientation.lock('sideways')"
        ".catch(e => { globalThis.lockErr = '' + e; })"
    )
    assert ctx.eval("lockErr") == (
        "TypeError: unknown orientation type: sideways"
    )
    # ...and through await + try/catch
    ctx.eval(
        "(async function(){ try { await screen.orientation.lock('bad'); "
        "globalThis.path = 'resolved'; } catch (e) { "
        "globalThis.path = 'rejected'; } })()"
    )
    assert ctx.eval("path") == "rejected"
    # fulfilled host promises do not trigger rejection handlers
    ctx.eval(
        "navigator.getBattery().then(v => { globalThis.okV = v.charging === true; })"
    )
    assert ctx.eval("okV") is True


def test_g4277_ref_exact_profile_hides_chrome_supersets():
    """Iteration 2 H (roadmap doc 2.3): profile="reference-exact" presents the
    reference-exact prototype faces - the Chrome-style Navigator (37),
    Screen (2) and CSSStyleDeclaration (425) extras are hidden - while the
    default profile keeps them (the SDK-facing member counts measured on the
    reference wheel are 32 / 10 / 10 respectively)."""
    page = {"baseURL": "https://example.test/", "html": "<html><body></body></html>"}
    js = (
        "(function(){function n(p){try{return Object.getOwnPropertyNames(p).length;}"
        "catch(e){return -1;}}"
        "return JSON.stringify([n(Navigator.prototype),n(Screen.prototype),"
        "n(CSSStyleDeclaration.prototype),"
        "typeof Navigator.prototype.userAgentData,"
        "typeof CSSStyleDeclaration.prototype.color]);})()"
    )

    default = JSContext(environment={"location": {"href": "https://example.test/"}})
    default.page.load(page)
    nav, screen, css, uad, color = json.loads(default.eval(js))
    assert (nav, screen, css) == (69, 12, 435)
    assert (uad, color) == ("object", "string")

    exact = JSContext(environment={"location": {"href": "https://example.test/"}},
                      profile="reference-exact")
    exact.page.load(page)
    nav, screen, css, uad, color = json.loads(exact.eval(js))
    assert (nav, screen, css) == (32, 10, 10)
    assert (uad, color) == ("undefined", "undefined")

    # the profile name is validated - unknown names are rejected loudly
    with pytest.raises(ValueError):
        JSContext(environment={"location": {"href": "https://example.test/"}},
                  profile="chrome-1999")


def test_g4278_event_family_shares_event_prototype():
    """The event family inherits from one Event.prototype (reference shape):
    every *Event global's prototype chains into it and instances stay thin."""
    ctx = JSContext(environment={"location": {"href": "https://example.test/"}})
    ctx.page.load({"baseURL": "https://example.test/",
                   "html": "<html><body></body></html>"})
    out = json.loads(ctx.eval(
        "(function(){"
        " function chain(p){var n=0;while(p){if(p===Event.prototype)return n;"
        "p=Object.getPrototypeOf(p);n++;}return -1;}"
        " var names=Object.getOwnPropertyNames(window),linked=0,total=0;"
        " for(var i=0;i<names.length;i++){var n=names[i];"
        "  if(!/Event$/.test(n))continue;var F;"
        "  try{F=window[n];}catch(e){continue;}"
        "  if(typeof F!=='function'||!F.prototype)continue;total++;"
        "  if(chain(F.prototype)>=0)linked++;}"
        " var e=new ErrorEvent('x');"
        " return JSON.stringify([total,linked,Object.getOwnPropertyNames(e).length,"
        "Object.getOwnPropertyNames(Object.getPrototypeOf(e)).indexOf('message')>=0]);"
        "})()"
    ))
    total, linked, own_n, has_message = out
    assert total == 84 and linked == total
    assert has_message is True
    assert own_n <= 8  # instance accessors remain the compatibility path
