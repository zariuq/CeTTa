#!/bin/sh
# Run inside a disposable Linux container with a clean source archive.
set -eu
archive=${1:?Usage: ci_linux_portability.sh SOURCE_TAR}
test -f "$archive"
if command -v apk >/dev/null; then
    apk add --no-cache build-base pkgconf python3-dev gmp-dev curl-dev \
        pcre2-dev sqlite-dev bash linux-headers git coreutils findutils
elif command -v dnf >/dev/null; then
    dnf install -y -q gcc make pkgconf-pkg-config python3-devel gmp-devel \
        libcurl-devel pcre2-devel sqlite-devel git findutils diffutils which
elif command -v apt-get >/dev/null; then
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential pkg-config \
        python3-dev libgmp-dev libcurl4-openssl-dev libpcre2-dev libsqlite3-dev git
else
    echo 'Unsupported package manager' >&2
    exit 2
fi
build=${CETTA_PORTABILITY_BUILD_DIR:-/build}
mkdir -p "$build"
tar -xf "$archive" -C "$build"
cd "$build"
cat /etc/os-release
set -- BUILD=python ENABLE_HTTP=1 ENABLE_DURABLE=1 SQLITE_PROVIDER=system \
    ENABLE_JSON_GSLT=1 ENABLE_LIB_PROLOG=0 PYTHON_CONFIG=python3-config
# Use one worker until all generated bootstrap dependencies support parallel builds.
if ! make -j1 "$@" cetta space-service value-worker test-atom-constructor-fields \
    test-space-path-index test-space-service-commit >build.log 2>&1; then
    tail -100 build.log
    exit 1
fi
paths=$(make --no-print-directory -s "$@" \
    --eval 'portability-paths:;@echo $(SPACE_SERVICE_BIN) $(PUBLIC_BIN) $(VALUE_WORKER_BIN)' \
    portability-paths | tail -1)
set -- $paths
CETTA_SPACE_TEST_BACKENDS=native python3 tests/test_space_service.py "$1" "$2" "$3"
python3 tests/test_agent_mailbox_recovery.py "$2" "$1" --lang petta
python3 tests/test_agent_mailbox_recovery.py "$2" "$1" --lang prime
