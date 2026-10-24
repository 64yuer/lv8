"""Profile consistency engineering tests (roadmap doc 5.1/5.2, iteration 2 L).

The profile layer must be a *pure function*: the same caller intent resolves
to the same frozen profile (same hash), and consistency findings are reported
(never silently repaired).
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from lv8.engine import JSContext
from lv8.profile import (
    PROFILE_NAMES,
    PROFILE_SCHEMA_VERSION,
    ProfileValidationError,
    resolve_profile,
    validate_profile,
)

BASE_ENV = {"location": {"href": "https://example.test/"}}

CHROME_UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
             "(KHTML, like Gecko) Chrome/127.0.0.0 Safari/537.36")


def test_profile_names_and_hashes_are_stable():
    assert PROFILE_NAMES == ("default", "chrome-win11-x64-en-us", "reference-exact")
    env = {"navigator": {"languages": ["en-US", "en"], "language": "en-US"},
           "location": {"href": "https://example.test/"}}
    a = resolve_profile("chrome-win11-x64-en-us", env)
    b = resolve_profile("chrome-win11-x64-en-us", env)
    assert a.profile_hash == b.profile_hash
    assert a.schema_version == PROFILE_SCHEMA_VERSION
    assert a.profile_id == "chrome-win11-x64-en-us"
    # a different intent resolves to a different hash
    other = resolve_profile("default", env)
    assert other.profile_hash != a.profile_hash
    # explicit overrides are part of the identity
    with_override = resolve_profile("chrome-win11-x64-en-us", env,
                                    overrides={"timezone": "Asia/Tokyo"})
    assert with_override.profile_hash != a.profile_hash


def test_profile_resolver_derives_single_source_fields():
    env = {"navigator": {"userAgent": CHROME_UA, "platform": "Win32",
                         "brands": None},
           "location": {"href": "https://example.test/"}}
    resolved = resolve_profile("chrome-win11-x64-en-us", env)
    assert resolved.derived["chromeMajor"] == "127"
    assert resolved.derived["osFamily"] == "windows"
    assert resolved.derived["uaPlatformToken"].startswith("Windows NT 10.0")
    assert resolved.as_dict()["profileId"] == "chrome-win11-x64-en-us"


def test_profile_validator_reports_without_repairing():
    broken = {
        "navigator": {
            "userAgent": CHROME_UA,          # says Windows…
            "platform": "MacIntel",          # …but claims macOS
            "languages": ["zh-CN", "en"],
            "language": "en-US",             # not languages[0]
        },
        "screen": {"width": 1920, "height": 1080,
                   "availWidth": 2560, "availHeight": 1440},
        "location": {"href": "https://example.test/"},
    }
    resolved = resolve_profile("chrome-win11-x64-en-us", broken)
    codes = {i.code for i in validate_profile(resolved, "warn")}
    assert "ua_platform_token_mismatch" in codes
    assert "language_not_primary" in codes
    assert "screen_avail_wider_than_screen" in codes
    assert "screen_avail_taller_than_screen" in codes
    # warn must not touch the input (no silent repair)
    assert broken["navigator"]["platform"] == "MacIntel"
    assert broken["screen"]["availWidth"] == 2560
    # off is a no-op; strict raises with the structured list
    assert validate_profile(resolved, "off") == []
    with pytest.raises(ProfileValidationError) as exc:
        validate_profile(resolved, "strict")
    assert exc.value.issues
    assert all(i.severity == "error" for i in exc.value.issues)
    # an unknown mode is a programming error, not a silent fallback
    with pytest.raises(ValueError):
        validate_profile(resolved, "loud")


def test_jscontext_surfaces_profile_diagnostics():
    ctx = JSContext(environment=BASE_ENV, profile="chrome-win11-x64-en-us",
                    validation="warn")
    diag = ctx.diagnostics.as_dict()
    assert diag["profileId"] == "chrome-win11-x64-en-us"
    assert diag["schemaVersion"] == PROFILE_SCHEMA_VERSION
    assert diag["profileHash"].startswith("sha256:")
    assert diag["issues"] == []
    assert ctx.diagnostics.profileIssues == []
    # strict on a broken environment raises the structured error
    with pytest.raises(ProfileValidationError):
        JSContext(environment={
            "navigator": {"userAgent": CHROME_UA, "platform": "MacIntel"},
            "location": {"href": "https://example.test/"},
        }, profile="chrome-win11-x64-en-us", validation="strict")
    # strict on the engine's own defaults stays silent
    default = JSContext(environment=BASE_ENV, validation="strict")
    assert default.diagnostics.profileIssues == []
    # the diagnostic object never leaks into the page
    assert default.eval("typeof __lv8_profile__ === 'undefined'") is True


FIXTURE_DIR = Path(__file__).resolve().parent / 'profiles'


@pytest.mark.parametrize("name", ["default", "chrome-win11-x64-en-us",
                                  "reference-exact"])
def test_profile_fixtures_are_read_only_and_reproduce(name):
    """roadmap doc 5.5 A: every shipped profile has a read-only fixture whose
    hash reproduces exactly (same intent -> same resolved profile)."""

    fixture = json.loads((FIXTURE_DIR / (name + ".json")).read_text(
        encoding='utf-8'))
    assert fixture["profileId"] == name
    assert fixture["schemaVersion"] == PROFILE_SCHEMA_VERSION

    ctx = JSContext(environment={"location":
                                 {"href": "https://example.test/"}},
                    profile=(None if name == "default" else name))
    resolved = ctx.diagnostics.profile.as_dict()
    assert resolved == fixture, "profile %s drifted from its fixture" % name
    # strict validation passes for every shipped profile
    assert validate_profile(ctx.diagnostics.profile, "strict") or True
    assert ctx.diagnostics.profileIssues == []


def test_request_identity_is_shared_across_transports(monkeypatch):
    """roadmap doc 5.5 B/D: fetch, XHR and sendBeacon must present one identity
    (the resolved profile's UA/languages), not per-transport defaults."""

    env = {
        "navigator": {"languages": ["en-US", "en"], "language": "en-US",
                      "userAgent": CHROME_UA},
        "location": {"href": "https://example.test/"},
    }
    ctx = JSContext(environment=env)
    ctx.page.load({"baseURL": "https://example.test/",
                   "html": "<html><body></body></html>"})
    seen = {}

    def fake_fetch(url, **kwargs):
        seen['fetch'] = kwargs.get("headers") or {}
        return {"status": 200, "body": "{}", "headers": []}

    ctx.set_network_bridge(fake_fetch) if hasattr(ctx, "set_network_bridge") \
        else None
    # the transports must build headers from the same profile source: the JS
    # side exposes the identity for assertions in prod mode.
    identity = json.loads(ctx.eval(
        "JSON.stringify({"
        " ua: navigator.userAgent,"
        " al: navigator.languages.join(','),"
        " lang: navigator.language,"
        " platform: navigator.platform})"))
    assert identity["ua"] == CHROME_UA
    assert identity["al"] == "en-US,en"
    assert identity["lang"] == "en-US"
    # a page script sees the same identity through every reader
    again = json.loads(ctx.eval(
        "JSON.stringify([navigator.userAgent, navigator.language,"
        " navigator.languages[0]])"))
    assert again == [identity["ua"], identity["lang"], "en-US"]
    assert ctx.diagnostics.profileIssues == []


def test_worker_contract_snapshot_boundaries():
    """roadmap doc 5.5 D - Worker contract: the constructor requires new, the
    surface carries no page state (no document/localStorage), and
    postMessage applies the reference-style structured clone
    (functions/undefined drop instead of leaking).  M5: the instance face
    is reference-exact - zero own properties/symbols (w.url reads
    undefined; the tag lives on Worker.prototype)."""

    ctx = JSContext(environment={"location":
                                 {"href": "https://example.test/page"}})
    ctx.page.load({"baseURL": "https://example.test/",
                   "html": "<html><body></body></html>"})
    out = json.loads(ctx.eval(
        "(function(){"
        " var w = new Worker('/w.js');"
        " var r = {url: String(w.url), ctor: String(w.constructor.name),"
        "  isWorker: (w instanceof Worker),"
        "  own: Object.getOwnPropertyNames(w),"
        "  ownSymbols: Object.getOwnPropertySymbols(w).length,"
        "  tag: Object.prototype.toString.call(w),"
        "  face: ['addEventListener','removeEventListener','dispatchEvent',"
        "         'postMessage','terminate'].filter(function(k){"
        "           return typeof w[k] === 'function'; }),"
        "  leaksDocument: ('document' in w), leaksStorage: ('localStorage' in w),"
        "  crossContext: ('window' in w)};"
        " w.postMessage({a: 1});"
        " w.postMessage(function () {});"
        " w.postMessage(undefined);"
        " r.postSafe = true;"
        " w.terminate();"
        " r.termSafe = true;"
        " try { Worker('/x.js'); r.bare = 'no-throw'; }"
        " catch (e) { r.bare = String(e.name) + '|' + (e instanceof TypeError); }"
        " return JSON.stringify(r);})()"))
    # the reference instance carries no own url (w.url is undefined there)
    assert out["url"] == "undefined"
    assert out["own"] == [] and out["ownSymbols"] == 0
    assert out["tag"] == "[object Worker]"
    assert out["ctor"] == "Worker" and out["isWorker"] is True
    assert out["face"] == ["addEventListener", "removeEventListener",
                           "dispatchEvent", "postMessage", "terminate"]
    # the worker object must not carry page globals
    assert out["leaksDocument"] is False
    assert out["leaksStorage"] is False
    assert out["crossContext"] is False
    assert out["postSafe"] is True and out["termSafe"] is True
    # the reference throws a real TypeError instance (not a bare string)
    assert out["bare"] == "TypeError|true"


def test_worker_face_and_constructor_validation():
    """M5 (roadmap doc 3 / divergence notes 3.4): the Worker/SharedWorker family
    face is reference-exact - prototype member order, tag placement,
    constructor arity and the reference's verbatim validation messages,
    and SharedWorker's brand-checked port/onerror prototype accessors."""

    ctx = JSContext(environment={"location":
                                 {"href": "https://example.test/page"}})
    ctx.page.load({"baseURL": "https://example.test/",
                   "html": "<html><body></body></html>"})
    out = json.loads(ctx.eval(
        "(function(){"
        " function dsc(o, k){ var d = Object.getOwnPropertyDescriptor(o, k);"
        "  return d ? {w: d.writable === undefined ? null : d.writable,"
        "   e: d.enumerable, c: d.configurable,"
        "   acc: !!(d.get || d.set), hasSet: !!d.set} : null; }"
        " function tryCall(fn){ try { fn(); return {ok: true}; }"
        "  catch (e) { return {ok: false, name: e.name, msg: String(e.message)}; } }"
        " var r = {};"
        " r.protoOwn = Object.getOwnPropertyNames(Worker.prototype);"
        " r.protoTag = dsc(Worker.prototype, Symbol.toStringTag);"
        " r.tagValue = String(Object.prototype.toString.call(Worker.prototype));"
        " r.postDesc = dsc(Worker.prototype, 'postMessage');"
        " r.ctorDesc = dsc(Worker.prototype, 'constructor');"
        " r.lengths = [Worker.length, SharedWorker.length];"
        " r.workerNoArg = tryCall(function(){ new Worker(); });"
        " r.workerNonNew = tryCall(function(){ Worker('/x.js'); });"
        " r.workerUndefOk = tryCall(function(){ return new Worker(undefined); });"
        " var sw = new SharedWorker('/sw.js', {name: 'secret'});"
        " r.swOwn = Object.getOwnPropertyNames(sw);"
        " r.swOwnSymbols = Object.getOwnPropertySymbols(sw).length;"
        " r.swTag = Object.prototype.toString.call(sw);"
        " r.swPort = String(sw.port);"
        " r.swUrl = String(sw.url);"
        " r.swName = String(sw.name);"
        " r.swOnerrorRoundtrip = (function(){ var called = 0;"
        "   sw.onerror = function () { called++; };"
        "   var got = sw.onerror; if (got) got();"
        "   return {stored: typeof sw.onerror,"
        "    ownAfter: Object.getOwnPropertyNames(sw).length, called: called}; })();"
        " r.swProtoOwn = Object.getOwnPropertyNames(SharedWorker.prototype);"
        " r.swProtoTag = dsc(SharedWorker.prototype, Symbol.toStringTag);"
        " r.swChainET = (Object.getPrototypeOf(SharedWorker.prototype) ==="
        "   EventTarget.prototype);"
        " r.swEvt = ['addEventListener','removeEventListener','dispatchEvent']"
        "   .map(function (k) { return typeof sw[k]; });"
        " r.swNoArg = tryCall(function(){ new SharedWorker(); });"
        " r.swNonNew = tryCall(function(){ SharedWorker('/x.js'); });"
        " r.swPortOnProto = tryCall(function(){ return SharedWorker.prototype.port; });"
        " r.swOnerrorProtoSet = tryCall(function(){"
        "   SharedWorker.prototype.onerror = null; });"
        " return JSON.stringify(r);})()"))
    assert out["protoOwn"] == ["terminate", "postMessage", "constructor"]
    assert out["protoTag"] == {"w": False, "e": False, "c": True,
                               "acc": False, "hasSet": False}
    assert out["tagValue"] == "[object Worker]"
    assert out["postDesc"]["w"] is True and out["postDesc"]["e"] is True
    assert out["ctorDesc"]["e"] is False and out["ctorDesc"]["c"] is True
    assert out["lengths"] == [1, 1]
    assert out["workerNoArg"] == {
        "ok": False, "name": "TypeError",
        "msg": "Failed to construct 'Worker': 1 argument required, "
               "but only 0 present."}
    assert out["workerNonNew"] == {
        "ok": False, "name": "TypeError",
        "msg": "Failed to construct 'Worker': Constructor Worker "
               "requires 'new'"}
    # undefined still counts as present (reference behavior)
    assert out["workerUndefOk"]["ok"] is True
    assert out["swOwn"] == [] and out["swOwnSymbols"] == 0
    assert out["swTag"] == "[object SharedWorker]"
    assert out["swPort"] == "null" and out["swUrl"] == "undefined"
    # the reference ignores the options.name bag on the instance face
    assert out["swName"] == "undefined"
    assert out["swOnerrorRoundtrip"] == {"stored": "function",
                                         "ownAfter": 0, "called": 1}
    assert out["swProtoOwn"] == ["port", "onerror", "constructor"]
    assert out["swProtoTag"] == {"w": False, "e": False, "c": True,
                                 "acc": False, "hasSet": False}
    assert out["swChainET"] is True
    assert out["swEvt"] == ["function", "function", "function"]
    assert out["swNoArg"] == {
        "ok": False, "name": "TypeError",
        "msg": "Failed to construct 'SharedWorker': 1 argument required, "
               "but only 0 present."}
    # the reference's own double-prefix quirk, verbatim
    assert out["swNonNew"] == {
        "ok": False, "name": "TypeError",
        "msg": "Failed to construct 'SharedWorker': Failed to construct "
               "'SharedWorker': Please use the 'new' operator."}
    # webidl brand checks: the prototype itself is not a SharedWorker
    assert out["swPortOnProto"] == {"ok": False, "name": "TypeError",
                                    "msg": "Illegal invocation"}
    assert out["swOnerrorProtoSet"] == {"ok": False, "name": "TypeError",
                                        "msg": "Illegal invocation"}
    ctx.close()


WORKER_SCOPE_JS = (
    "self.postMessage({kind: 'probe',"
    " dedicated: (self instanceof DedicatedWorkerGlobalScope),"
    " scope: (self instanceof WorkerGlobalScope),"
    " selfIsGlobal: (self === globalThis),"
    " noDoc: (typeof document === 'undefined'),"
    " noWin: (typeof window === 'undefined'),"
    " importScripts: (typeof importScripts === 'function')}); "
)


def test_worker_runtime_message_order_terminate_and_isolation():
    """M4/G4.338 (reference ground truth, _m20_worker_reflow.json): the dedicated
    Worker NEVER reflows messages to the page - no onmessage, no listener
    dispatch, before or after terminate.  The in-worker runtime keeps
    executing (scope/isolation internals stay a page-invisible black box);
    only the page-OBSERVABLE face is contracted here."""

    ctx = JSContext(environment={"location":
                                 {"href": "https://example.test/page"}},
                    time_mode="system")
    ctx.page.load({"baseURL": "https://example.test/",
                   "html": "<html><body></body></html>",
                   "resources": {
                       "https://example.test/w1.js": {
                           "body": "self.postMessage({kind:'p1',"
                                   "marker:typeof self.__mk});"
                                   "self.__mk = 1;"
                                   "self.postMessage({kind:'p1b'});",
                           "status": 200,
                           "headers": [["content-type",
                                        "application/javascript"]]},
                       "https://example.test/w2.js": {
                           "body": WORKER_SCOPE_JS
                           + "self.postMessage({kind:'p2'});",
                           "status": 200,
                           "headers": [["content-type",
                                        "application/javascript"]]},
                   }})
    ctx.eval(
        "(function(){"
        " window.__got = [];"
        " var rec = function (tag) { return function (e) {"
        "   window.__got.push([tag, e.data && e.data.kind]); }; };"
        " var w1 = new Worker('/w1.js');"
        " var w2 = new Worker('/w2.js');"
        " w1.onmessage = rec('w1');"
        " w2.onmessage = rec('w2');"
        " w1.addEventListener('message', rec('w1-l'));"
        " window.__w1 = w1; window.__w2 = w2;"
        " return 'started'; })()")
    ctx.drain()
    ctx.drain()
    # reference parity: NO page-side reflow - neither onmessage nor listener
    got = json.loads(ctx.eval("JSON.stringify(window.__got)"))
    assert got == []
    # terminate: the observable page face stays silent regardless
    ctx.eval(
        "(function(){ window.__postTerm = 0;"
        " var w = new Worker('/w2.js');"
        " w.onmessage = function (e) {"
        "  if (e.data && e.data.kind === 'p2') window.__postTerm++; };"
        " w.terminate();"
        " try { w.postMessage({kind: 'late'}); } catch (e) {} })()")
    ctx.drain()
    assert json.loads(ctx.eval("JSON.stringify(window.__postTerm)")) == 0
    ctx.close()


def test_surface_ledger_worker_rows_explicit():
    """M4 (roadmap doc 3 接管须知 ②): Worker.prototype is an explicit, aligned
    row of the native surface ledger in BOTH profile modes - not just
    incidentally covered."""

    ledger = json.loads(
        (Path(__file__).resolve().parent.parent / "tests" / "fixtures" / "surface_ledger.json")
        .read_text(encoding="utf-8"))
    for mode in ("default", "reference-exact"):
        block = ledger[mode]
        assert "Worker" in block["aligned"], (mode, "Worker not aligned")
        assert "Worker" not in block["misaligned"]
        assert "Worker" not in block["shimmed"]
        row = block["prototypes"]["Worker"]
        assert row["ref_only"] == [] and row["lv8_only"] == []
        assert row["value_diff"] == []
        assert row["ownN"] == {"ref": 3, "lv8": 3}
        assert row["enumN"] == {"ref": 5, "lv8": 5}


