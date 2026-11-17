#!/usr/bin/env python3
"""Execute the original runtime argv, with optional --data-file declarations.

Repeat --data-file PATH before -- EXECUTABLE ARGUMENTS. These declarations
belong to the recorder; the guest receives the original command unchanged.
--data-argument INDEX identifies a file argument by its zero-based position
after EXECUTABLE, including when the same file is also source code.
"""
import os
import sys

import gslt_invocation


if __name__ == "__main__":
    arguments = sys.argv[1:]
    data = []
    positions = []
    while arguments[:1] in (["--data-file"], ["--data-argument"]):
        if len(arguments) < 2:
            raise SystemExit(arguments[0] + " requires a value")
        if arguments[0] == "--data-file":
            data.append(arguments[1])
        else:
            try:
                positions.append(int(arguments[1]))
            except ValueError:
                raise SystemExit("--data-argument requires an integer position")
        arguments = arguments[2:]
    if arguments[:1] == ["--"]:
        arguments = arguments[1:]
    elif data or positions:
        raise SystemExit("data declarations require -- before the executable")
    if not arguments:
        raise SystemExit("usage: run_gslt_program.py [--data-file PATH]... [--data-argument INDEX]... -- EXECUTABLE ARGUMENTS...")
    directory = os.environ.get("CETTA_GSLT_RECORD_DIR")
    if directory:
        raise SystemExit(gslt_invocation.main(["record", directory, "1", *arguments],
                                              data_inputs=data, data_arguments=positions))
    os.execvp(arguments[0], arguments)
