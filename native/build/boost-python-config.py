#!/usr/bin/env python3
"""Print a Boost.Build ``using python`` stanza for the running CPython ABI."""

from __future__ import annotations

import sys
import sysconfig
from pathlib import Path


def jam_path(value: str) -> str:
    return Path(value).as_posix().replace('"', r'\"')


def main() -> None:
    base_executable = getattr(sys, "_base_executable", None) or sys.executable
    base_prefix = getattr(sys, "base_prefix", None) or sys.prefix
    include = sysconfig.get_path("include") or str(Path(base_prefix) / "include")
    libraries = sysconfig.get_config_var("LIBDIR") or str(Path(base_prefix) / "libs")
    version = f"{sys.version_info.major}.{sys.version_info.minor}"
    print(
        f'using python : {version} : "{jam_path(base_executable)}" : '
        f'"{jam_path(include)}" : "{jam_path(libraries)}" ;'
    )


if __name__ == "__main__":
    main()
