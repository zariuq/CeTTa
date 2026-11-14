#!/bin/sh
# Record exact argv, inputs and import context; preserve the command's result.
exec python3 "$(dirname "$0")/gslt_invocation.py" record "$@"
