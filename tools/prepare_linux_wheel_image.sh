#!/bin/sh
set -eu

if command -v apk >/dev/null 2>&1; then
    apk add make gcc musl-dev gmp-dev curl tar gzip
else
    yum install -y make gcc gmp-devel curl tar gzip
fi

# The authoritative Makefile uses grouped output targets.
if printf 'all:\n\t@test -n "$(filter grouped-target,$(.FEATURES))"\n' | make -s -f -; then
    exit 0
fi

build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
cd "$build_dir"
curl --fail --location --silent --show-error \
    https://ftp.gnu.org/gnu/make/make-4.4.1.tar.gz -o make.tar.gz
printf '%s  make.tar.gz\n' \
    dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3 \
    | sha256sum -c -
tar -xzf make.tar.gz
cd make-4.4.1
./configure --prefix=/usr/local
make -j1
make install
/usr/local/bin/make --version
