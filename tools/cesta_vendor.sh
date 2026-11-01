#!/bin/sh
# Prepare a new vendored source candidate; promotion follows qualification.
#
#   cesta_vendor.sh REPOSITORY REVISION DESTINATION
#
# make cesta-update runs the build gates before promoting the candidate.
set -eu
exec python3 "$(dirname "$0")/cesta_vendor.py" prepare "$@"
