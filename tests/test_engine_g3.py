"""G3 tests: the oracle's storage/cookie subset re-hosted on the C++
engine facade (``lv8.engine``). Expectations mirror the oracle bytecode.
"""

from __future__ import annotations

import pytest

from lv8.engine import JSContext, Page


def test_web_storage_is_context_isolated_with_configured_values():
    # mirrors the oracle's test_context_isolated_web_storage_includes_
    # configured_values: JSContext(localStorage={...}) seeds values and
    # storage is fully per-context
    with JSContext(localStorage={"theme": "dark"}) as ctx:
        assert ctx.eval("localStorage.getItem('theme')") == "dark"
        ctx.eval("localStorage.setItem('count', 3); sessionStorage.token = 'session'")
        # Web Storage stringifies: attribute reads yield '3', not 3
        assert ctx.eval("localStorage.count") == "3"
        assert ctx.eval("sessionStorage.getItem('token')") == "session"
        assert ctx.eval("localStorage.length") == 2
        ctx.eval("localStorage.removeItem('theme')")
        assert ctx.eval("localStorage.getItem('theme')") is None
        # insertion-ordered key(index) with null beyond the end
        assert ctx.eval(
            "[localStorage.key(0), localStorage.key(5)]", to_py=True
        ) == ["count", None]
        # data keys only (length lives on the prototype, like browsers)
        assert ctx.eval("Object.keys(localStorage)", to_py=True) == ["count"]
        assert ctx.eval("'count' in localStorage") is True
        # delete operator routes through the interceptor
        ctx.eval("delete sessionStorage.token")
        assert ctx.eval("sessionStorage.getItem('token')") is None
        assert ctx.eval("sessionStorage.length") == 0

    # a fresh context sees nothing (per-context isolation)
    with JSContext() as other:
        assert other.eval("localStorage.getItem('count')") is None
        assert other.eval("sessionStorage.getItem('token')") is None


def test_web_storage_uses_the_shared_storage_prototype_contract():
    # Measured from the reference wheel: both stores inherit the one observable
    # Storage.prototype, whose member order and arities are fingerprinted by
    # the challenge collector. The backing maps must remain isolated.
    with JSContext() as ctx:
        assert ctx.eval(
            "Object.getPrototypeOf(localStorage) === Storage.prototype && "
            "Object.getPrototypeOf(sessionStorage) === Storage.prototype && "
            "localStorage instanceof Storage && sessionStorage instanceof Storage && "
            "localStorage.constructor === Storage && "
            "Object.getOwnPropertyDescriptor(Storage, 'prototype').writable === false"
        ) is True
        assert ctx.eval(
            "Object.getOwnPropertyNames(Storage.prototype)", to_py=True
        ) == [
            "length", "clear", "getItem", "key", "removeItem", "setItem",
            "constructor",
        ]
        assert ctx.eval(
            "[Storage.prototype.clear.length, Storage.prototype.getItem.length, "
            "Storage.prototype.key.length, Storage.prototype.removeItem.length, "
            "Storage.prototype.setItem.length]", to_py=True
        ) == [0, 1, 1, 1, 2]
        assert ctx.eval(
            "['clear', 'getItem', 'key', 'removeItem', 'setItem'].every(k => "
            "!Object.hasOwn(localStorage, k) && !Object.hasOwn(sessionStorage, k))"
        ) is True
        ctx.eval("localStorage.setItem('local', 1); sessionStorage.setItem('session', 2)")
        assert ctx.eval(
            "[localStorage.getItem('local'), sessionStorage.getItem('local'), "
            "localStorage.getItem('session'), sessionStorage.getItem('session')]",
            to_py=True,
        ) == ["1", None, None, "2"]


def test_canvas_measure_text_uses_ref_fixed_width_stub():
    # The reference wheel's canvas stub ignores font selection and returns eight
    # units per UTF-8 byte. This value is observable in fingerprint code.
    with JSContext() as ctx:
        Page(ctx).load("<html><body></body></html>")
        assert ctx.eval(
            "(() => { const c = document.createElement('canvas'); "
            "const g = c.getContext('2d'); g.font = '14px Arial'; "
            "return g.measureText('probe,fingerprint').width; })()"
        ) == 136


def test_document_cookie_jar_round_trip_and_deletion():
    # mirrors the oracle cookie semantics that do not need the network
    # bridge: ordered writes, "; "-joined reads, Max-Age=0 removal,
    # same-name overwrite
    ctx = JSContext()
    Page(ctx).load(
        "<body><script>document.cookie = 'session=abc; Path=/'; "
        "document.cookie = 'view=compact; Path=/'</script></body>"
    )
    assert ctx.eval("document.cookie") == "session=abc; view=compact"
    # set-cookie style writes (from fetch later in G3) land in the same jar
    ctx.eval("document.cookie = 'server=session-2; Path=/; Secure'")
    ctx.eval("document.cookie = 'theme=dark; Path=/'")
    assert ctx.eval("document.cookie") == (
        "session=abc; view=compact; server=session-2; theme=dark"
    )
    # Max-Age=0 deletes regardless of the value given
    ctx.eval("document.cookie = 'session=gone; Path=/; Max-Age=0'")
    assert ctx.eval("document.cookie") == (
        "view=compact; server=session-2; theme=dark"
    )
    # writing an existing name overwrites in place
    ctx.eval("document.cookie = 'theme=light; Path=/'")
    assert ctx.eval("document.cookie") == (
        "view=compact; server=session-2; theme=light"
    )


def test_crypto_digest_and_random_values_are_available_offline():
    # mirrors the oracle verbatim: standard SHA vectors through the
    # microtask checkpoint, ranged getRandomValues fill
    with JSContext() as ctx:
        ctx.eval(
            "\n            const bytesToHex = bytes =>"
            " Array.from(new Uint8Array(bytes), byte =>"
            " byte.toString(16).padStart(2, '0')).join('');\n"
            "            crypto.subtle.digest('SHA-256',"
            " new TextEncoder().encode('abc'))\n"
            "              .then(value => { globalThis.sha256 ="
            " bytesToHex(value); });\n"
            "            crypto.subtle.digest({name: 'SHA-1'},"
            " new TextEncoder().encode('abc'))\n"
            "              .then(value => { globalThis.sha1 ="
            " bytesToHex(value); });\n"
            "            globalThis.random ="
            " Array.from(crypto.getRandomValues(new Uint8Array(8)));\n            "
        )
        ctx.drain()
        assert ctx.eval("sha256") == (
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        )
        assert ctx.eval("sha1") == (
            "a9993e364706816aba3e25717850c26c9cd0d89d"
        )
        random_bytes = ctx.eval("random", to_py=True)
        assert len(random_bytes) == 8
        assert all(0 <= value <= 255 for value in random_bytes)
        # browser object shapes
        assert ctx.eval("Object.prototype.toString.call(crypto)") == (
            "[object Crypto]"
        )
        assert ctx.eval("Object.prototype.toString.call(crypto.subtle)") == (
            "[object SubtleCrypto]"
        )
        # unsupported algorithms reject, catchable at the checkpoint
        ctx.eval(
            "crypto.subtle.digest('MD5', new TextEncoder().encode('x'))"
            ".catch(e => { globalThis.err = '' + e; })"
        )
        ctx.drain()
        assert "NotSupportedError" in ctx.eval("err")


def test_crypto_random_uuid_generates_version_four_uuid_strings():
    # mirrors the oracle verbatim: v4 shape and per-call uniqueness
    import re

    with JSContext() as ctx:
        first = ctx.eval("crypto.randomUUID()")
        second = ctx.eval("crypto.randomUUID()")
        assert first != second
        assert re.fullmatch(
            r"[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-"
            r"[0-9a-f]{12}",
            first,
        ), first


def test_text_encoder_encodes_utf8_bytes():
    # supporting surface the crypto oracle leans on
    with JSContext() as ctx:
        assert ctx.eval("new TextEncoder().encoding") == "utf-8"
        assert ctx.eval(
            "[...new TextEncoder().encode('abc')]", to_py=True
        ) == [97, 98, 99]
        # non-ASCII encodes as UTF-8 (two bytes here)
        assert ctx.eval(
            "[...new TextEncoder().encode('é')]", to_py=True
        ) == [195, 169]
        assert ctx.eval("new TextEncoder().encode().length") == 0


def test_offline_fetch_reads_resource_and_records_request():
    # mirrors the oracle: relative fetch against the context location,
    # profile-derived client-hint request headers (lowercased, overridable),
    # netLog entry shape + __iv8__ alias identity, resource timings
    with JSContext(location={"href": "https://example.test/"}) as ctx:
        ctx.add_resource(
            "https://example.test/api/profile",
            '{"name":"lv8"}',
            headers={"content-type": "application/json"},
        )
        ctx.eval(
            """
            fetch('/api/profile', {headers: {'X-Trace': 'abc'}})
              .then(response => {
                globalThis.fetchStatus = [response.ok, response.headers.get('content-type')];
                return response.json();
              })
              .then(value => { globalThis.profileName = value.name; });
            """
        )
        # the fetch response is queued (delivered on drain), not inline
        assert ctx.pending_tasks == 1
        assert ctx.drain() == 0
        assert ctx.eval("fetchStatus", to_py=True) == [True, "application/json"]
        assert ctx.eval("profileName") == "lv8"
        assert ctx.eval(
            "performance.getEntriesByType('resource')[0].initiatorType"
        ) == "fetch"
        assert ctx.eval(
            "performance.getEntriesByType('resource')[0].encodedBodySize"
        ) == 14
        entries = ctx.eval("__iv8__.netLog.entries", to_py=True)
        assert entries == [
            {
                "method": "GET",
                "url": "https://example.test/api/profile",
                "headers": {
                    "x-trace": "abc",
                    "sec-ch-ua": '"Google Chrome";v="127", "Not_A Brand";v="8", "Chromium";v="127"',
                    "sec-ch-ua-mobile": "?0",
                    "sec-ch-ua-platform": '"Windows"',
                },
                "cookieHeader": "",
            }
        ]
        # caller headers override profile defaults, case-insensitively
        ctx.eval("fetch('/api/profile', {headers: {'Sec-CH-UA-Platform': 'override'}})")
        assert ctx.eval(
            "__iv8__.netLog.entries[1].headers['sec-ch-ua-platform']"
        ) == "override"
        assert ctx.eval("typeof __lv8__") == "undefined"


def test_document_cookies_are_sent_by_offline_fetch_and_can_be_deleted():
    # mirrors the oracle: cookie jar feeds request Cookie headers and
    # response set-cookie headers mutate the jar
    ctx = JSContext()
    Page(
        ctx,
    ).load(
        html=(
            "<script>document.cookie = 'session=abc; Path=/'; "
            "document.cookie = 'view=compact; Path=/'</script>"
        ),
        baseURL="https://example.test/app/index.html",
    )
    assert ctx.eval("document.cookie") == "session=abc; view=compact"
    ctx.add_resource(
        "https://example.test/api/profile",
        '{"ok":true}',
    )
    ctx.add_resource(
        "https://example.test/api/bootstrap",
        "ready",
        headers=[
            ("set-cookie", "server=session-2; Path=/; Secure"),
            ("set-cookie", "theme=dark; Path=/"),
        ],
    )
    ctx.eval(
        "fetch('/api/profile').then(response => response.json())"
        ".then(value => { globalThis.cookieResult = value.ok; })"
    )
    ctx.drain()
    assert ctx.eval("cookieResult") is True
    # the request carried the document cookies
    assert ctx.eval(
        "__iv8__.netLog.entries[0].cookieHeader"
    ) == "session=abc; view=compact"
    # response set-cookie headers landed in the jar
    ctx.eval("fetch('/api/bootstrap').then(() => { globalThis.bootstrapDone = true; })")
    ctx.drain()
    assert ctx.eval("bootstrapDone") is True
    assert ctx.eval("document.cookie") == (
        "session=abc; view=compact; server=session-2; theme=dark"
    )
    ctx.eval("document.cookie = 'session=gone; Path=/; Max-Age=0'")
    assert ctx.eval("document.cookie") == (
        "view=compact; server=session-2; theme=dark"
    )


def test_cache_storage_reuses_offline_fetch_responses_inside_context():
    # mirrors the oracle verbatim: caches.open/add/match/keys over the
    # resource table
    with JSContext(location={"href": "https://example.test/"}) as ctx:
        ctx.add_resource("https://example.test/data.txt", "offline payload")
        ctx.eval(
            """
        caches.open('v1')
          .then(cache => cache.add('/data.txt'))
          .then(() => caches.match('/data.txt'))
          .then(response => response.text())
          .then(value => { globalThis.cachedValue = value; return caches.keys(); })
          .then(keys => { globalThis.cacheKeys = keys; });
        """
        )
        ctx.drain()
        assert ctx.eval("cachedValue") == "offline payload"
        assert ctx.eval("cacheKeys", to_py=True) == ["v1"]


def test_async_xhr_reads_resource_and_reports_state_transitions():
    # mirrors the oracle verbatim: state transitions [1,2,3,4], onload
    # payload (status/text/case-insensitive response header), POST + custom
    # request header recorded in netLog
    with JSContext(location={"href": "https://example.test/"}) as ctx:
        ctx.add_resource(
            "https://example.test/api/score",
            "42",
            status=201,
            headers={"x-request-id": "req-1"},
        )
        ctx.eval(
            """
            globalThis.states = [];
            globalThis.xhr = new XMLHttpRequest();
            xhr.onreadystatechange = () => states.push(xhr.readyState);
            xhr.onload = () => globalThis.xhrResult = [xhr.status, xhr.responseText, xhr.getResponseHeader('X-Request-ID')];
            xhr.open('POST', '/api/score');
            xhr.setRequestHeader('X-Client', 'lv8');
            xhr.send('payload');
            """
        )
        assert ctx.drain() == 0
        assert ctx.eval("states", to_py=True) == [1, 2, 3, 4]
        assert ctx.eval("xhrResult", to_py=True) == [201, "42", "req-1"]
        assert ctx.eval("__iv8__.netLog.entries[0].method") == "POST"
        assert ctx.eval(
            "__iv8__.netLog.entries[0].headers['x-client']"
        ) == "lv8"


def test_navigator_send_beacon_records_an_offline_post_without_transport():
    # mirrors the oracle verbatim: boolean result, POST entry with resolved
    # url and profile-derived headers, native method appearance
    with JSContext(location={"href": "https://example.test/"}) as ctx:
        assert ctx.eval(
            "navigator.sendBeacon('/collect', 'event=ready')"
        ) is True
        entry = ctx.eval("__iv8__.netLog.entries[0]", to_py=True)
        assert entry["method"] == "POST"
        assert entry["url"] == "https://example.test/collect"
        assert entry["headers"]["sec-ch-ua-platform"] == '"Windows"'
        assert ctx.eval(
            "Function.prototype.toString.call(navigator.sendBeacon)"
        ) == "function sendBeacon() { [native code] }"


def test_cookie_store_uses_the_context_cookie_jar_asynchronously():
    # mirrors the oracle verbatim: async set/get over the shared jar,
    # onchange events with changed/deleted counts, document.cookie parity
    with JSContext(location={"href": "https://example.test/page"}) as ctx:
        ctx.eval(
            """
            globalThis.cookieChanges = [];
            cookieStore.onchange = event => cookieChanges.push([event.changed.length, event.deleted.length]);
            cookieStore.set({name: 'theme', value: 'dark', path: '/'}).then(() => cookieStore.get('theme')).then(cookie => { globalThis.cookieValue = cookie.value; });
            """
        )
        ctx.drain()
        assert ctx.eval("cookieValue") == "dark"
        assert ctx.eval("document.cookie") == "theme=dark"
        assert ctx.eval("cookieChanges", to_py=True) == [[1, 0]]
        ctx.eval("cookieStore.delete('theme')")
        ctx.drain()
        assert ctx.eval("document.cookie") == ""
        assert ctx.eval("cookieChanges", to_py=True) == [[1, 0], [0, 1]]
        # has/get null paths and getAll (promises settle at checkpoints)
        ctx.eval(
            "cookieStore.has('theme').then(v => { globalThis.hasTheme = v; });"
        )
        ctx.eval(
            "cookieStore.get('theme').then(v => { globalThis.getTheme = v; });"
        )
        ctx.drain()
        assert ctx.eval("hasTheme") is False
        assert ctx.eval("getTheme") is None
        ctx.eval("cookieStore.set('a', '1')")
        ctx.eval(
            "cookieStore.getAll().then(v => { globalThis.all = v; });"
        )
        ctx.drain()
        assert ctx.eval("all", to_py=True) == [
            {"name": "a", "value": "1", "path": "/", "secure": False,
             "sameSite": "lax"}
        ]


def test_navigator_storage_estimate_and_persistence_follow_environment_profile():
    # mirrors the oracle verbatim: profile-driven estimate/persisted/persist
    # plus the StorageManager instance shape
    with JSContext(
        storage={"quota": 2048, "usage": 128, "persistent": True}
    ) as ctx:
        ctx.eval(
            """
            Promise.all([navigator.storage.estimate(), navigator.storage.persisted(), navigator.storage.persist()])
              .then(values => { globalThis.storageInfo = [values[0].quota, values[0].usage, values[1], values[2]]; });
            """
        )
        ctx.drain()
        assert ctx.eval("storageInfo", to_py=True) == [2048, 128, True, True]
        assert ctx.eval(
            "navigator.storage instanceof StorageManager"
        ) is True


def test_storage_buckets_are_context_local_with_isolated_cache_namespaces():
    # mirrors the oracle verbatim: bucket open with durability, isolated
    # per-bucket cache namespace (put/match over new Response), keys/delete
    with JSContext() as ctx:
        ctx.eval(
            """
            navigator.storageBuckets.open('assets', {durability: 'strict'}).then(async bucket => {
              const cache = await bucket.caches.open('v1'); await cache.put('/logo', new Response('cached'));
              globalThis.bucketInfo = [bucket instanceof StorageBucket, bucket.name, bucket.durability, await (await cache.match('/logo')).text(), await navigator.storageBuckets.keys()];
            });
            """
        )
        ctx.drain()
        assert ctx.eval("bucketInfo", to_py=True) == [
            True,
            "assets",
            "strict",
            "cached",
            ["assets"],
        ]
        ctx.eval(
            "navigator.storageBuckets.delete('assets')"
            ".then(value => { globalThis.bucketDeleted = value; })"
        )
        ctx.drain()
        assert ctx.eval("bucketDeleted") is True
    # buckets are context-local: a fresh context sees none
    with JSContext() as other:
        other.eval(
            "navigator.storageBuckets.keys().then(v => { globalThis.k = v; });"
        )
        other.drain()
        assert other.eval("k", to_py=True) == []


def test_navigator_credentials_stores_and_gets_context_local_credentials():
    # mirrors the oracle verbatim: PasswordCredential round trip through
    # navigator.credentials store/get
    with JSContext() as ctx:
        ctx.eval(
            """
            const credential = new PasswordCredential({id: 'alice', password: 'secret', name: 'Alice'});
            navigator.credentials.store(credential).then(() => navigator.credentials.get({password: true})).then(value => { globalThis.credentialInfo = [value.id, value.type, value.name, value.password]; });
            """
        )
        ctx.drain()
        assert ctx.eval("credentialInfo", to_py=True) == [
            "alice",
            "password",
            "Alice",
            "secret",
        ]
        assert ctx.eval(
            "navigator.credentials instanceof CredentialsContainer"
        ) is True


def test_service_worker_media_devices_and_webrtc_stay_offline_and_async():
    # mirrors the oracle verbatim: registration scope/scriptURL/unregister,
    # deterministic getUserMedia track kinds, webrtc offer state machine
    with JSContext(
        location={"href": "https://example.test/app/"},
        permissions={"camera": "granted", "microphone": "granted"},
    ) as ctx:
        ctx.eval(
            """
            navigator.serviceWorker.register('/sw.js').then(registration => {
              globalThis.swInfo = [registration.scope, registration.active.scriptURL];
              return registration.unregister();
            }).then(value => { globalThis.unregistered = value; });
            navigator.mediaDevices.getUserMedia({audio: true, video: true}).then(stream => { globalThis.trackKinds = stream.getTracks().map(track => track.kind); });
            const peer = new RTCPeerConnection();
            const channel = peer.createDataChannel('chat'); channel.send('ping');
            peer.createOffer().then(offer => peer.setLocalDescription(offer)).then(() => { globalThis.rtcInfo = [peer.localDescription.type, channel.readyState, peer.signalingState]; });
            """
        )
        ctx.drain()
        assert ctx.eval("swInfo", to_py=True) == [
            "https://example.test/app/",
            "https://example.test/sw.js",
        ]
        assert ctx.eval("unregistered") is True
        assert ctx.eval("trackKinds", to_py=True) == ["audio", "video"]
        assert ctx.eval("rtcInfo", to_py=True) == [
            "offer",
            "open",
            "have-local-offer",
        ]


def test_navigator_clipboard_is_an_async_context_local_memory_clipboard():
    # mirrors the oracle verbatim: seeded read, write, re-read, native look
    with JSContext(clipboard_text="seed") as ctx:
        ctx.eval(
            """
            navigator.clipboard.readText().then(value => { globalThis.initialClipboard = value; return navigator.clipboard.writeText('updated'); })
              .then(() => navigator.clipboard.readText()).then(value => { globalThis.finalClipboard = value; });
            """
        )
        ctx.drain()
        assert ctx.eval("initialClipboard") == "seed"
        assert ctx.eval("finalClipboard") == "updated"
        assert ctx.eval(
            "Function.prototype.toString.call(navigator.clipboard.readText)"
        ) == "function readText() { [native code] }"


def test_page_load_consumes_offline_external_scripts_and_logs_request():
    # mirrors the oracle verbatim: src scripts are pulled from the
    # resource table, executed, and logged with a blank header placeholder
    with JSContext() as ctx:
        ctx.add_resource(
            "https://example.test/static/app.js",
            "globalThis.fromExternal = 42",
        )
        Page(ctx).load(
            html="<body><script src='/static/app.js'></script></body>",
            baseURL="https://example.test/index.html",
        )
        assert ctx.eval("fromExternal") == 42
        assert ctx.eval("__iv8__.netLog.entries", to_py=True) == [
            {
                "method": "GET",
                "url": "https://example.test/static/app.js",
                "headers": {},
                "cookieHeader": "",
            }
        ]


def test_page_headers_set_cookies_and_current_script_tracks_execution():
    # mirrors the oracle verbatim: load-time set-cookie headers,
    # document.URL/documentURI identity, currentScript during execution
    # (null after), and the control-surface help() doc
    with JSContext() as ctx:
        Page(ctx).load(
            html=(
                "<script>globalThis.currentScriptSource = "
                "document.currentScript.textContent</script>"
            ),
            baseURL="https://example.test/index.html",
            headers=[("set-cookie", "seed=one; Path=/")],
        )
        assert ctx.eval("document.cookie") == "seed=one"
        assert ctx.eval("document.URL") == "https://example.test/index.html"
        assert ctx.eval("document.documentURI") == (
            "https://example.test/index.html"
        )
        assert ctx.eval("currentScriptSource") == (
            "globalThis.currentScriptSource = "
            "document.currentScript.textContent"
        )
        assert ctx.eval("document.currentScript") is None
        assert ctx.eval("__iv8__.help().eventLoop") == (
            "advance/sleep/drain control logical time"
        )


def test_indexeddb_open_upgrade_put_and_get_are_context_local():
    # mirrors the oracle verbatim: open/upgrade/onsuccess chain,
    # keyPath records, databases() listing
    with JSContext() as ctx:
        ctx.eval(
            """
            globalThis.idbEvents = [];
            const open = indexedDB.open('profiles', 1);
            open.onupgradeneeded = event => {
              event.target.result.createObjectStore('users', {keyPath: 'id'});
              idbEvents.push('upgrade');
            };
            open.onsuccess = event => {
              const database = event.target.result;
              const transaction = database.transaction('users', 'readwrite');
              const store = transaction.objectStore('users');
              const put = store.put({id: 7, name: 'lv8'});
              put.onsuccess = () => {
                const get = store.get(7);
                get.onsuccess = result => { globalThis.idbValue = result.target.result; idbEvents.push('get'); };
              };
            };
            """
        )
        ctx.drain()
        assert ctx.eval("idbEvents", to_py=True) == ["upgrade", "get"]
        assert ctx.eval("idbValue", to_py=True) == {"id": 7, "name": "lv8"}
        ctx.eval(
            "indexedDB.databases().then(values =>"
            " { globalThis.idbDatabases = values; })"
        )
        ctx.drain()
        assert ctx.eval("idbDatabases", to_py=True) == [
            {"name": "profiles", "version": 1}
        ]


def test_file_system_access_handles_are_context_local_and_writable():
    # mirrors the oracle verbatim: directory/file handles, positioned
    # writes, text readback and path resolution
    with JSContext() as ctx:
        ctx.eval(
            """
            navigator.storage.getDirectory().then(async root => {
              const folder = await root.getDirectoryHandle('data', {create: true}); const file = await folder.getFileHandle('note.txt', {create: true});
              const writer = await file.createWritable(); await writer.write('hello'); await writer.write({type: 'write', position: 5, data: ' lv8'}); await writer.close();
              const saved = await file.getFile(); globalThis.fileSystemInfo = [root instanceof FileSystemDirectoryHandle, file instanceof FileSystemFileHandle, await saved.text(), await root.resolve(file), await root.resolve(folder)];
            });
            """
        )
        ctx.drain()
        assert ctx.eval("fileSystemInfo", to_py=True) == [
            True,
            True,
            "hello lv8",
            ["data", "note.txt"],
            ["data"],
        ]


def test_web_animations_follow_context_logical_time_and_finish_events():
    # G4.222 reference-parity re-measure: the reference exposes the Animation /
    # KeyframeEffect constructors and document.getAnimations, but carries
    # NO animate/getAnimations on element instances or prototypes (the
    # d8y2 read-order A/B showed the former own-property attach diverged
    # from reference, where gOPN(element) stays empty).  The logical-clock
    # binding remains observable through the Animation constructor.
    with JSContext() as ctx:
        ctx.page.load(html="<div id='box'></div>")
        ctx.eval(
            """
            globalThis.animationFace = [
              typeof Animation, typeof KeyframeEffect,
              typeof document.getElementById('box').animate,
              typeof document.getElementById('box').getAnimations,
              typeof document.getAnimations,
              Object.getOwnPropertyNames(
                  document.getElementById('box')).length
            ];
            const keyframeEffect = new KeyframeEffect(
                document.getElementById('box'),
                [{opacity: 0}, {opacity: 1}]);
            const animation = new Animation(
                keyframeEffect, {duration: 25});
            animation.onfinish = () => { globalThis.animationFinish = [animation.playState, animation.currentTime, keyframeEffect.getKeyframes().length]; };
            // G4.317 reference parity (M11): the face-table effect accessor
            // answers null on a zero-own instance (reference ground truth:
            // `new Animation(x).effect` is null and `instanceof` false);
            // the effect object itself still drives getKeyframes()
            globalThis.animationStart = [animation instanceof Animation,
                                         animation.effect === null,
                                         animation.playState];
            """
        )
        assert ctx.eval("animationFace", to_py=True) == [
            "function", "function", "undefined", "undefined", "function", 0]
        assert ctx.eval("animationStart", to_py=True) == [True, True, "running"]
        ctx.advance(25)
        assert ctx.eval("animationFinish", to_py=True) == [
            "finished", 25, 2]


def test_javascript_error_joins_package_error_hierarchy():
    # oracle parity: raw JS throws surface as lv8.JavaScriptError
    import lv8
    with JSContext() as ctx:
        with pytest.raises(lv8.JavaScriptError):
            ctx.eval("throw new Error('bad input')")


def test_multi_context_timezones_are_isolated_on_reentry():
    # G4: the ICU default zone is process-wide state; each context must
    # re-apply its own override when entering the native context (and an
    # unconfigured context must restore the machine zone), so interleaved
    # use never leaks one context's zone into another.
    from lv8.engine import JSContext as _JSContext  # explicit alias for clarity

    probe = ("[new Date(0).getTimezoneOffset(),"
             "Intl.DateTimeFormat().resolvedOptions().timeZone]")
    ny = _JSContext(config={"timezone": "America/New_York"})
    tokyo = _JSContext(config={"timezone": "Asia/Tokyo"})
    try:
        assert ny.eval(probe, to_py=True) == [300, "America/New_York"]
        assert tokyo.eval(probe, to_py=True) == [-540, "Asia/Tokyo"]
        # interleaved: no leakage of the most recently configured zone
        assert ny.eval(probe, to_py=True) == [300, "America/New_York"]
        assert tokyo.eval(probe, to_py=True) == [-540, "Asia/Tokyo"]
        # an unconfigured context follows the machine zone, not the last
        # configured one
        with _JSContext() as plain:
            plain_zone = plain.eval(
                "Intl.DateTimeFormat().resolvedOptions().timeZone")
        assert plain_zone not in ("America/New_York", "Asia/Tokyo")
        assert ny.eval(probe, to_py=True) == [300, "America/New_York"]
    finally:
        ny.close()
        tokyo.close()


def test_context_pool_accepts_shared_environment_and_config():
    # G4: ContextPool integrates with the engine facade's environment/
    # config plumbing so a shared per-pool profile needs no hand-written
    # factory, and timezone config applies per context on entry.
    from lv8 import ContextPool

    with ContextPool(size=2, environment={"navigator": {"hardwareConcurrency": 3}},
                     config={"timezone": "Asia/Tokyo"}) as pool:
        def probe_js(ctx, _item=None):
            return ctx.eval(
                "[navigator.hardwareConcurrency,"
                "Intl.DateTimeFormat().resolvedOptions().timeZone]",
                to_py=True)
        assert pool.run(probe_js) == [3, "Asia/Tokyo"]
        results = pool.map(probe_js, range(4))
        assert results == [[3, "Asia/Tokyo"]] * 4

    with pytest.raises(ValueError):
        ContextPool(size=1, context_factory=lambda: None, environment={})


def test_window_bars_start_as_ref_accessors_then_assignment_replaces_them():
    # reference exposes these browser UI slots as accessors, but assigning to one
    # deliberately materializes an ordinary writable window own property.
    with JSContext() as ctx:
        assert ctx.eval(
            "['locationbar','menubar','personalbar','scrollbars','statusbar','toolbar']"
            ".every(k => { const d = Object.getOwnPropertyDescriptor(window, k);"
            " return d.enumerable && d.configurable && !!d.get && !!d.set &&"
            " !Object.hasOwn(d, 'writable') && Object.prototype.toString.call(window[k]) === '[object BarProp]' &&"
            " Object.getOwnPropertyNames(window[k]).length === 0; })"
        ) is True
        assert ctx.eval(
            "window.locationbar = {assigned: 1}; const d = "
            "Object.getOwnPropertyDescriptor(window, 'locationbar');"
            "d.value.assigned === 1 && d.writable === true && !d.get && !d.set"
        ) is True


def test_webgl_factory_results_use_ref_branded_prototypes_and_precision():
    # The challenge builds and links a tiny shader program.  reference does not
    # return generic marker objects: these results have their public WebGL
    # prototypes, no string own keys, and the precision fields are accessors.
    with JSContext() as ctx:
        assert ctx.eval(
            "const gl = document.createElement('canvas').getContext('webgl');"
            "const program = gl.createProgram();"
            "const shader = gl.createShader(gl.VERTEX_SHADER);"
            "const uniform = gl.getUniformLocation(program, 'uniformOffset');"
            "const fp = gl.getShaderPrecisionFormat(gl.VERTEX_SHADER, gl.HIGH_FLOAT);"
            "const ip = gl.getShaderPrecisionFormat(gl.FRAGMENT_SHADER, gl.HIGH_INT);"
            "['WebGLProgram','WebGLShader','WebGLUniformLocation'].every((n, i) => {"
            " const value = [program, shader, uniform][i];"
            " return Object.prototype.toString.call(value) === '[object ' + n + ']' &&"
            " Object.getOwnPropertyNames(value).length === 0 &&"
            " Object.getPrototypeOf(value) === window[n].prototype; }) &&"
            "gl.getAttribLocation(program, 'attrVertex') === 0 &&"
            "Object.prototype.toString.call(fp) === '[object WebGLShaderPrecisionFormat]' &&"
            "Object.getOwnPropertyNames(fp).length === 0 &&"
            "Object.getPrototypeOf(fp) === WebGLShaderPrecisionFormat.prototype &&"
            "[fp.rangeMin, fp.rangeMax, fp.precision].join('/') === '127/127/23' &&"
            "[ip.rangeMin, ip.rangeMax, ip.precision].join('/') === '31/30/0' &&"
            "['rangeMin','rangeMax','precision'].every(k => { const d = "
            "Object.getOwnPropertyDescriptor(WebGLShaderPrecisionFormat.prototype, k);"
            " return d.enumerable && d.configurable && !!d.get && !d.set; })"
        ) is True
