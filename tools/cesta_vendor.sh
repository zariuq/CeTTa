#!/bin/sh
# Prepare a new vendored source candidate; promotion follows qualification.
#
#   cesta_vendor.sh REPOSITORY REVISION DESTINATION
#
# Qualify the candidate before promoting it with cesta_vendor.py.
set -eu
exec python3 "$(dirname "$0")/cesta_vendor.py" prepare "$@"
