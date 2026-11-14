#!/usr/bin/env python3
"""Execute a gate's runtime argv, optionally recording the exact invocation."""
import os
import sys

import gslt_invocation


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: run_gslt_program.py EXECUTABLE ARGUMENTS...")
    directory = os.environ.get("CETTA_GSLT_RECORD_DIR")
    if directory:
        raise SystemExit(gslt_invocation.main(["record", directory, "1", *sys.argv[1:]]))
    os.execvp(sys.argv[1], sys.argv[1:])
