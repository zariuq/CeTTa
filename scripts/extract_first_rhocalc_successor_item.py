#!/usr/bin/env python3

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} <expected-file>",
              file=sys.stderr)
        return 2

    text = Path(sys.argv[1]).read_text(encoding="utf-8").strip()
    if not (text.startswith("[") and text.endswith("]")):
        return 0

    body = text[1:-1].strip()
    if not body:
        return 0

    depth = 0
    for i, ch in enumerate(body):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "," and depth == 0:
            print(body[:i].strip())
            return 0

    print(body.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
