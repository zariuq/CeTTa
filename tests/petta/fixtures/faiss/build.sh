#!/bin/sh
set -eu

fixture_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cp "$fixture_dir/faiss.pl" ./faiss.pl
cp "$fixture_dir/embed.pl" ./embed.pl
cp "$fixture_dir/lib_faiss.metta" ./lib_faiss.metta
