#!/usr/bin/env python3
"""Smoke-test an installed lv8 wheel.

Runs against the installed ``lv8`` package (no repo on sys.path): imports the
engine from the wheel-bundled ``lv8/_native`` directory, provisions ICU data
into the user folder on first run, and exercises the README quick-start
surface.  Exits non-zero on any failure; prints SMOKE_OK on success.
"""

import lv8


def main() -> int:
    assert lv8.__version__ == "0.1.0", lv8.__version__

    with lv8.JSContext(
        environment={"location": {"href": "https://example.test/"}},
        config={"timezone": "America/New_York"},
    ) as ctx:
        ua = ctx.eval("navigator.userAgent")
        assert ua.startswith("Mozilla/5.0"), ua
        assert ctx.eval("location.href") == "https://example.test/"
        assert ctx.eval("typeof document.createElement('div').appendChild") == "function"
        assert ctx.eval("Intl.DateTimeFormat().resolvedOptions().timeZone") == "America/New_York"
        assert ctx.eval("Intl.DateTimeFormat().resolvedOptions().locale") == "en-US"

    # Pool sanity: the quick-start keeps working across pooled contexts.
    from lv8 import ContextPool

    with ContextPool(size=2) as pool:
        assert pool.map(lambda ctx, _: ctx.eval("1 + 1"), range(4)) == [2, 2, 2, 2]

    print("installed at:", lv8.__file__)
    print("SMOKE_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
