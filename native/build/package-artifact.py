#!/usr/bin/env python3
"""Create a self-describing native lv8 distribution bundle.

The project deliberately distributes the native extension and the matching
ICU data together.  This helper gives every bundle an unambiguous Python ABI
and platform identity and records the inputs used to build it.  It does not
claim that the bundle is a PyPI wheel: the public ``lv8`` package currently
has its own Python packaging lifecycle.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import sys
import sysconfig
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def normalized_platform() -> str:
    system = platform.system().lower()
    machine = platform.machine().lower()
    machine = {"amd64": "x86_64", "x64": "x86_64", "aarch64": "arm64"}.get(
        machine, machine
    )
    systems = {"windows": "win", "darwin": "macos"}
    return f"{systems.get(system, system)}_{machine}"


def setting(stpyv8: Path, name: str, fallback: str) -> str:
    settings = stpyv8 / "settings.py"
    if not settings.is_file():
        return fallback
    match = re.search(rf"^{re.escape(name)}\s*=\s*[\"']([^\"']+)[\"']", settings.read_text(encoding="utf-8"), re.M)
    return match.group(1) if match else fallback


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--extension", type=Path, required=True, help="built _STPyV8 .so/.pyd")
    parser.add_argument("--icu", type=Path, required=True, help="matching icudtl.dat")
    parser.add_argument("--out-dir", type=Path, required=True, help="directory that receives the bundle")
    parser.add_argument("--stpyv8", type=Path, default=Path(__file__).parents[1] / "stpyv8")
    parser.add_argument("--platform-tag", default=None, help="override platform tag, e.g. linux_x86_64")
    parser.add_argument("--v8-tag", default=None)
    parser.add_argument("--boost", default="1.84.0")
    parser.add_argument("--lexbor-revision", default="26a8819b60801301df92d771f12a01dbe8705a7a")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    extension = args.extension.resolve()
    icu = args.icu.resolve()
    if not extension.is_file():
        raise SystemExit(f"extension not found: {extension}")
    if not icu.is_file():
        raise SystemExit(f"ICU data not found: {icu}")

    py_tag = f"cp{sys.version_info.major}{sys.version_info.minor}"
    abi_tag = py_tag  # Boost.Python extensions are CPython-ABI specific, never abi3.
    platform_tag = args.platform_tag or normalized_platform()
    bundle_name = f"lv8-native-{py_tag}-{abi_tag}-{platform_tag}"
    bundle = args.out_dir.resolve() / bundle_name
    package_dir = bundle / "STPyV8"
    icu_dir = bundle / "stpyv8-icu"
    package_dir.mkdir(parents=True, exist_ok=True)
    icu_dir.mkdir(parents=True, exist_ok=True)

    copied_extension = package_dir / extension.name
    copied_icu = icu_dir / "icudtl.dat"
    shutil.copy2(extension, copied_extension)
    shutil.copy2(icu, copied_icu)

    record = {
        "schema": 1,
        "artifact": bundle_name,
        "tags": {"python": py_tag, "abi": abi_tag, "platform": platform_tag},
        "build": {
            "python": sys.version,
            "implementation": sys.implementation.name,
            "soabi": sysconfig.get_config_var("SOABI"),
            "host": {"system": platform.system(), "release": platform.release(), "machine": platform.machine()},
            "v8_tag": args.v8_tag or setting(args.stpyv8, "V8_GIT_TAG_STABLE", "unknown"),
            "boost": args.boost,
            "lexbor_revision": args.lexbor_revision,
            "created_at": datetime.now(timezone.utc).isoformat(),
        },
        "files": {
            str(copied_extension.relative_to(bundle)): {"bytes": copied_extension.stat().st_size, "sha256": sha256(copied_extension)},
            str(copied_icu.relative_to(bundle)): {"bytes": copied_icu.stat().st_size, "sha256": sha256(copied_icu)},
        },
    }
    (bundle / "build-record.json").write_text(
        json.dumps(record, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(bundle)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
