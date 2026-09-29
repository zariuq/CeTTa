#!/usr/bin/env python3
"""Print MeTTa output as logical lines: a newline inside a string literal is
written as the escape \\n, so each printed result is one line whatever the
lane's printer does with a string's newlines (PeTTa keeps them, as its
swrite does; HE escapes them)."""
import sys


def main() -> int:
    text = sys.stdin.read()
    out = []
    in_string = False
    escaped = False
    for c in text:
        if in_string:
            if escaped:
                escaped = False
                out.append(c)
            elif c == "\\":
                escaped = True
                out.append(c)
            elif c == '"':
                in_string = False
                out.append(c)
            elif c == "\n":
                out.append("\\n")
            else:
                out.append(c)
        else:
            if c == '"':
                in_string = True
            out.append(c)
    sys.stdout.write("".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
