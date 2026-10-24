#!/usr/bin/env bash
# lv8 local build recipe (Linux x64; macOS remains CI-only by decision).
#
# Status: recipe (DESIGN.md G4). The Windows pipeline (.github/workflows/
# ci.yml + native/build/*.cmd) is authoritative; this script mirrors it for
# POSIX runners. GN args come from the fork's own settings.py (single
# source of truth); library paths are routed through LIBRARY_PATH because
# settings.py hardcodes the Windows directory layout.
#
# Stages (all idempotent; each is skipped when its output exists):
#   1. depot_tools + V8 checkout at --v8-tag, gn gen, ninja v8_monolith
#   2. boost_python/filesystem/iostreams/date_time/thread static libs
#   3. lexbor static lib
#   4. _STPyV8.<abi>.so extension (setup.py build_ext --inplace)
#
# Usage:
#   native/build/posix/build-all.sh [--v8-tag 13.1.201.22] \
#       [--boost 1.84.0] [--lexbor v2.3.0] [--python "$(command -v python3)"] \
#       [--dist-dir dist-linux]
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
# this script lives at <repo>/native/build/posix (three levels below the
# project root lv8/)
repo="$(cd "$here/../../.." && pwd)"
stpyv8="$repo/native/stpyv8"

V8_TAG="13.1.201.22"
BOOST_VER="1.84.0"
LEXBOR_TAG="v2.3.0"
PYTHON_BIN="${PYTHON_BIN:-$(command -v python3)}"
DIST_DIR=""

while [ $# -gt 0 ]; do
  case "$1" in
    --v8-tag) V8_TAG="$2"; shift 2 ;;
    --boost) BOOST_VER="$2"; shift 2 ;;
    --lexbor) LEXBOR_TAG="$2"; shift 2 ;;
    --python) PYTHON_BIN="$2"; shift 2 ;;
    --dist-dir) DIST_DIR="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

if command -v nproc >/dev/null 2>&1; then
  JOBS="$(nproc)"
else
  JOBS="$(sysctl -n hw.ncpu)"
fi

log() { printf '[lv8-build] %s\n' "$*"; }

case "$(uname -s)" in
  Linux) GN_DIR="linux"; OUTDIR="x64.release.sample"; TARGET_CPU="x64" ;;
  Darwin) GN_DIR="mac"; OUTDIR="arm64.release.sample"; TARGET_CPU="arm64" ;;
  *) log "unsupported OS: $(uname -s)"; exit 1 ;;
esac
V8_OBJ_DIR="$stpyv8/v8/out.gn/$OUTDIR/obj"
V8_TAG_STAMP="$V8_OBJ_DIR/lv8-v8-tag"

# ---- stage 1: depot_tools + V8 monolith --------------------------------
if [ ! -f "$V8_OBJ_DIR/libv8_monolith.a" ] ||
   [ ! -f "$V8_TAG_STAMP" ] ||
   [ "$(<"$V8_TAG_STAMP")" != "$V8_TAG" ]; then
  log "stage 1: depot_tools + V8 $V8_TAG (30-60 min cold)"
  if [ ! -d "$stpyv8/depot_tools" ]; then
    git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git \
      "$stpyv8/depot_tools"
  fi
  # first-run bootstrap: writes python3_bin_reldr.txt / CIPD toolchain;
  # without it the depot_tools python3 shim shadows the system python3
  (
    cd "$stpyv8/depot_tools" &&
    (./ensure_bootstrap 2>/dev/null || ./update_depot_tools 2>/dev/null || true)
  )
  export PATH="$stpyv8/depot_tools:$PATH"
  # bypass depot_tools vpython (its CIPD venv build fails on macos-14
  # against the chrome-python-ar index); gclient/fetch only need stdlib
  export VPYTHON_BYPASS="/usr/bin/python3"
  if [ ! -f "$stpyv8/v8/.git/config" ]; then
    # Deterministic fetch. `fetch --no-history v8` clones the moving main
    # tip and then tries to check out a sha that may not exist in the
    # shallow clone; clone the pinned tag directly and let gclient handle
    # DEPS only. (Also: git-checkout has never implemented
    # --end-of-options, which gclient emits for dep checkouts — see the
    # shim note in docs/PROGRESS.md; on vanilla git the checkout fails
    # with "pathspec '--end-of-options' did not match".)
    git init "$stpyv8/v8"
    (
      cd "$stpyv8/v8" &&
      git remote add origin https://chromium.googlesource.com/v8/v8.git &&
      git fetch --depth 1 origin "refs/tags/$V8_TAG:refs/tags/$V8_TAG" &&
      git checkout --quiet --detach "refs/tags/$V8_TAG"
    )
    cat > "$stpyv8/.gclient" <<GEOF
solutions = [
  { "name": "v8",
    "url": "https://chromium.googlesource.com/v8/v8.git@$V8_TAG",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {},
    "safesync_url": "",
  },
]
target_os = []
GEOF
  fi
  (
    cd "$stpyv8/v8" &&
    # A shallow checkout only has the tag it was originally created with.
    # Fetch the requested tag explicitly so --v8-tag always changes the
    # source tree rather than silently reusing a monolith from another tag.
    git fetch --depth 1 origin "refs/tags/$V8_TAG:refs/tags/$V8_TAG" &&
    git checkout --quiet --detach "refs/tags/$V8_TAG" &&
    gclient sync -D -j"$JOBS"
  )
  # GN args must match the fork's settings.py exactly (single source of
  # truth); the hook scripts run through $PYTHON_BIN.
  gn_args="$("$PYTHON_BIN" -c \
    "import sys; sys.path.insert(0, r'$stpyv8'); import settings; print(settings.GN_ARGS)")"
  log "gn gen with args: $gn_args"
  log "gn gen with args: $gn_args"
  # depot_tools' gn wrapper auto-provisions the binary (buildtools/<os>/gn
  # is only populated by the sync hooks and may be missing)
  (cd "$stpyv8/v8" &&
    gn gen "out.gn/$OUTDIR" \
      --script-executable="$PYTHON_BIN" \
      --args="$gn_args")
  (cd "$stpyv8/v8" && ninja -C "out.gn/$OUTDIR" v8_monolith)
  printf '%s\n' "$V8_TAG" > "$V8_TAG_STAMP"
  log "stage 1 done: $V8_OBJ_DIR/libv8_monolith.a"
else
  log "stage 1: cached V8 $V8_TAG monolith found"
fi

# ---- stage 2: boost static libs ----------------------------------------
boost_ver_short="$(echo "$BOOST_VER" | tr . _)"
boost_dir="$repo/native/boost/boost_$boost_ver_short"
if [ ! -d "$boost_dir" ]; then
  log "stage 2: downloading boost $BOOST_VER"
  mkdir -p "$(dirname "$boost_dir")"
  curl -L -o "/tmp/boost_${boost_ver_short}.tar.gz" \
    "https://archives.boost.io/release/$BOOST_VER/source/boost_${boost_ver_short}.tar.gz"
  tar -xzf "/tmp/boost_${boost_ver_short}.tar.gz" -C "$repo/native/boost"
fi
pyabi="$("$PYTHON_BIN" -c 'import sys; print(sys.version_info[0] * 100 + sys.version_info[1])')"
# Boost.Build derives Python headers from the interpreter's installation
# prefix, rather than sysconfig.  A venv therefore points it at an empty
# <venv>/include directory.  Build Boost against the venv's base executable
# (same CPython ABI), while keeping PYTHON_BIN for the extension and tests.
boost_python_bin="$("$PYTHON_BIN" -c 'import sys; print(getattr(sys, "_base_executable", None) or sys.executable)')"
if ! ls "$boost_dir/stage/lib/libboost_python$pyabi"* >/dev/null 2>&1; then
  log "stage 2: b2 stage for python $pyabi via $boost_python_bin (10-20 min)"
  (
    cd "$boost_dir" &&
    ./bootstrap.sh --with-python="$boost_python_bin" &&
    ./b2 stage -j"$JOBS" link=static runtime-link=static cxxflags=-fPIC \
      --with-system --with-python --with-filesystem --with-iostreams \
      --with-date_time --with-thread
  )
fi

# ---- stage 3: lexbor static lib ----------------------------------------
lexbor_dir="$repo/native/lexbor"
# Pinned upstream commit (master is ahead of the v2.3.0 tag and carries
# lxb_css_selector_list_destroy_memory, which lv8's HTML/CSS layer calls).
# Pinning keeps the Linux build bit-identical to the Windows reference tree.
LEXBOR_REV="26a8819b60801301df92d771f12a01dbe8705a7a"
if [ ! -d "$lexbor_dir" ]; then
  log "stage 3: cloning lexbor at $LEXBOR_REV"
  git init "$lexbor_dir"
  (
    cd "$lexbor_dir" &&
    git remote add origin https://github.com/lexbor/lexbor &&
    git fetch --depth 1 origin "$LEXBOR_REV" &&
    git checkout --quiet --detach "$LEXBOR_REV"
  )
fi
if [ ! -f "$lexbor_dir/build/liblexbor_static.a" ]; then
  log "stage 3: cmake lexbor"
  cmake -S "$lexbor_dir" -B "$lexbor_dir/build" \
    -DLEXBOR_BUILD_STATIC=ON -DLEXBOR_BUILD_SHARED=OFF \
    -DLEXBOR_BUILD_TESTS=OFF -DLEXBOR_BUILD_EXAMPLES=OFF
  cmake --build "$lexbor_dir/build" -j"$JOBS"
fi

# ---- stage 4: extension -------------------------------------------------
log "stage 4: build_ext --inplace (python $pyabi)"
# settings.py hardcodes the Windows directory layout for its library
# dirs; LIBRARY_PATH routes the POSIX linker to the real static libs
# (nonexistent entries are ignored by the linker).
export LIBRARY_PATH="$boost_dir/stage/lib:$lexbor_dir/build:$V8_OBJ_DIR${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LEXBOR_HOME="$lexbor_dir"
export BOOST_ROOT="$boost_dir"
export LV8_PYTHON="$PYTHON_BIN"
# --force: build_ext skips objects newer than their sources, so a flag-only
# change (e.g. the per-file -fno-rtti split for Platform.cpp in setup.py)
# would silently reuse stale objects otherwise.
(cd "$stpyv8" && "$PYTHON_BIN" setup.py build_ext --inplace --force)

# ICU data: V8 reads icudtl.dat at context init; the fork bundles it in
# stpyv8-icu/. Refresh from the V8 checkout if the engine reports a data
# version mismatch (see native/BUILD.md).
if [ ! -f "$stpyv8/stpyv8-icu/icudtl.dat" ] &&
   [ -f "$stpyv8/v8/out.gn/$OUTDIR/icudtl.dat" ]; then
  mkdir -p "$stpyv8/stpyv8-icu"
  cp "$stpyv8/v8/out.gn/$OUTDIR/icudtl.dat" "$stpyv8/stpyv8-icu/"
  log "refreshed stpyv8-icu/icudtl.dat from the V8 build"
fi

log "all stages done -- run: $PYTHON_BIN -m pytest -q (from $repo)"

# Bundle the extension and matching ICU data with an explicit platform/ABI
# identity.  The raw inplace file remains available for development; this is
# the distributable, traceable output.
if [ -z "$DIST_DIR" ]; then
  DIST_DIR="$repo/dist-linux"
fi
ext_suffix="$($PYTHON_BIN -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX"))')"
"$PYTHON_BIN" "$repo/native/build/package-artifact.py" \
  --extension "$stpyv8/_STPyV8$ext_suffix" \
  --icu "$stpyv8/stpyv8-icu/icudtl.dat" \
  --out-dir "$DIST_DIR" \
  --stpyv8 "$stpyv8" \
  --v8-tag "$V8_TAG" \
  --boost "$BOOST_VER" \
  --lexbor-revision "$LEXBOR_REV"
