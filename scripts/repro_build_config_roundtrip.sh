#!/bin/sh
# Round-trip falsifier for toolchain-selection staleness (the A->B->A class).
#
# A configuration switch moves no file mtime, so a timestamp-driven build can
# reuse artifacts compiled under the previous selection.  The one-way switch
# masks the defect: A->B rebuilds (the B marker did not exist yet), but the
# return leg A->B->A finds A's stale marker and reuses B's objects.  The fix
# under test is configuration identity in the artifact address itself
# (BUILD_OBJ_TAG carries the config fingerprint), verified per leg by
# test-python-build-config against the linked binary, never the flags.
#
# Usage:  scripts/repro_build_config_roundtrip.sh <other-python-config>
# e.g.:   scripts/repro_build_config_roundtrip.sh /usr/bin/python3.14-config
#
# Leg 1: default python3-config from PATH  -> gate must pass
# Leg 2: <other-python-config>             -> gate must pass
# Leg 3: default again (THE falsifier)     -> gate must pass, and quickly
#
# Before the object-tag fix, leg 3 failed: the binary still linked leg 2's
# libpython.  The same class applies to any un-fingerprinted toolchain input.
# The resolved SWI-Prolog pkg-config inputs therefore have their own artifact
# fingerprint and linked-library gate as well.

set -eu

OTHER_CONFIG=${1:?usage: $0 <other-python-config (e.g. /usr/bin/python3.14-config)>}
OTHER_EXECUTABLE=${2:-$(printf '%s' "$OTHER_CONFIG" | sed 's/-config$//')}
JOBS=${JOBS:-8}

command -v "$OTHER_CONFIG" >/dev/null 2>&1 || {
    echo "SKIP: $OTHER_CONFIG not available"; exit 0; }
command -v python3-config >/dev/null 2>&1 || {
    echo "SKIP: no default python3-config on PATH"; exit 0; }

echo "== leg 1: default python3-config =="
make -j"$JOBS" BUILD=python test-python-build-config

echo "== leg 2: $OTHER_CONFIG =="
make -j"$JOBS" BUILD=python \
    PYTHON_CONFIG="$OTHER_CONFIG" \
    PYTHON_EXECUTABLE="$OTHER_EXECUTABLE" \
    test-python-build-config

echo "== leg 3 (falsifier): default python3-config again =="
make -j"$JOBS" BUILD=python test-python-build-config

echo "PASS: A->B->A round-trip links the selected Python on every leg"
