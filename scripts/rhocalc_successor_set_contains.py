#!/usr/bin/env python3

import sys
from pathlib import Path


def split_successor_set(text: str) -> list[str]:
    text = text.strip()
    if not (text.startswith("[") and text.endswith("]")):
        return []

    body = text[1:-1].strip()
    if not body:
        return []

    items: list[str] = []
    depth = 0
    start = 0
    for i, ch in enumerate(body):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "," and depth == 0:
            items.append(body[start:i].strip())
            start = i + 1
    items.append(body[start:].strip())
    return items


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} <item>", file=sys.stderr)
        return 2

    target = sys.argv[1].strip()
    successor_set_text = sys.stdin.read()
    return 0 if target in split_successor_set(successor_set_text) else 1


if __name__ == "__main__":
    raise SystemExit(main())
