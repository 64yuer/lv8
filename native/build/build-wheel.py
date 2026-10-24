#!/usr/bin/env python3
"""Assemble a platform wheel for lv8.

The pure-Python ``lv8`` wheel produced by pip/build carries no native code.
This helper injects the per-platform ``_STPyV8`` extension, its ``STPyV8.py``
wrapper, the matching ``icudtl.dat`` and the Apache-2.0 license into
``lv8/_native/`` and retags the wheel, e.g.::

    lv8py-0.1.0-cp311-cp311-win_amd64.whl

Run it with the SAME interpreter the extension was built for.  Requires the
``wheel`` package (pip install wheel).  The repository tree never holds
binaries: inputs come from the build steps (build-ext.cmd / build-all.sh).
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import sysconfig
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
STPYV8 = REPO / "native" / "stpyv8"


def stpyv8_version() -> str:
    """The fork's V8 pin (settings.py V8_GIT_TAG_STABLE), matching
    STPyV8.__version__ = JSEngine.version and package-artifact.py."""
    match = re.search(
        r'^V8_GIT_TAG_STABLE\s*=\s*[\'"]([^\'"]+)[\'"]',
        (STPYV8 / "settings.py").read_text(encoding="utf-8"),
        re.MULTILINE,
    )
    if not match:
        raise SystemExit("cannot read V8_GIT_TAG_STABLE from native/stpyv8/settings.py")
    return match.group(1)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--extension",
        type=Path,
        default=None,
        help="built _STPyV8 .so/.pyd (default: auto-discover the one matching "
        "this interpreter's ABI under native/stpyv8/)",
    )
    parser.add_argument("--icu", type=Path, default=None, help="icudtl.dat (default native/stpyv8/stpyv8-icu)")
    parser.add_argument("--out-dir", type=Path, default=REPO / "dist-wheels")
    default_platform = sysconfig.get_platform().replace("-", "_")
    if default_platform == "linux_x86_64":
        # PyPI rejects the bare linux_x86_64 tag; builds target ubuntu-24.04
        # (glibc 2.39), which manylinux_2_39 promises.
        default_platform = "manylinux_2_39_x86_64"
    parser.add_argument(
        "--platform-tag",
        default=default_platform,
        help="wheel platform tag, e.g. win_amd64 / manylinux_2_39_x86_64",
    )
    return parser.parse_args()


def lv8_version() -> str:
    match = re.search(
        r'^version\s*=\s*[\'"]([^\'"]+)[\'"]',
        (REPO / "pyproject.toml").read_text(encoding="utf-8"),
        re.MULTILINE,
    )
    if not match:
        raise SystemExit("cannot read project.version from pyproject.toml")
    return match.group(1)


def discover_extension() -> Path:
    suffix = sysconfig.get_config_var("EXT_SUFFIX")
    matches = sorted(STPYV8.glob(f"_STPyV8*{suffix}"))
    if not matches:
        raise SystemExit(
            f"no _STPyV8*{suffix} under {STPYV8} -- build the extension first "
            "(native/build/build-ext.cmd or build-all.sh)"
        )
    return matches[0]


def main() -> int:
    args = parse_args()
    extension = (args.extension or discover_extension()).resolve()
    icu = (args.icu or (STPYV8 / "stpyv8-icu" / "icudtl.dat")).resolve()
    if not extension.is_file():
        raise SystemExit(f"extension not found: {extension}")
    if not icu.is_file():
        raise SystemExit(f"icudtl.dat not found: {icu}")

    py_tag = f"cp{sys.version_info.major}{sys.version_info.minor}"
    abi_tag = py_tag  # Boost.Python extensions are CPython-ABI specific.
    wheel_tag = f"{py_tag}-{abi_tag}-{args.platform_tag}"

    args.out_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="lv8-wheel-") as tmp:
        tmp = Path(tmp)
        # 1. Pure wheel from the PEP 517 build (src/lv8 only, no native).
        subprocess.run(
            [sys.executable, "-m", "pip", "wheel", "--no-deps", "--no-build-isolation",
             "--wheel-dir", str(tmp / "pure"), str(REPO)],
            check=True,
        )
        pure = next((tmp / "pure").glob("lv8py-*-py3-none-any.whl"))
        # 2. Unpack, inject native payload.
        subprocess.run([sys.executable, "-m", "wheel", "unpack", "--dest", str(tmp / "work"), str(pure)], check=True)
        unpacked = next((tmp / "work").glob("lv8py-*"))
        native_dir = unpacked / "lv8" / "_native"
        native_dir.mkdir()
        shutil.copy2(extension, native_dir / extension.name)
        shutil.copy2(STPYV8 / "STPyV8.py", native_dir / "STPyV8.py")
        shutil.copy2(icu, native_dir / "icudtl.dat")
        shutil.copy2(STPYV8 / "LICENSE.txt", native_dir / "LICENSE.txt")
        (native_dir / "stpyv8-version.txt").write_text(stpyv8_version() + "\n", encoding="utf-8")
        # 3. Retag the wheel metadata.
        dist_info = next(unpacked.glob("lv8py-*.dist-info"))
        wheel_meta = dist_info / "WHEEL"
        text = wheel_meta.read_text(encoding="utf-8")
        text = re.sub(r"^Tag: .*$", f"Tag: {wheel_tag}", text, flags=re.MULTILINE)
        if "Root-Is-Purelib" not in text:
            text += "Root-Is-Purelib: true\n"
        wheel_meta.write_text(text, encoding="utf-8")
        # 4. Pack (regenerates RECORD with the injected files).
        subprocess.run(
            [sys.executable, "-m", "wheel", "pack", str(unpacked), "--dest-dir", str(args.out_dir.resolve())],
            check=True,
        )
    out = args.out_dir.resolve() / f"lv8py-{lv8_version()}-{wheel_tag}.whl"
    if not out.is_file():
        raise SystemExit("wheel pack did not produce the expected file name")
    print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
