#!/usr/bin/env python3

import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()

    text = args.source.read_text(encoding="utf-8")
    old = "if (!ok || !substituted)"
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"expected one beta-substitution failure site, found {count}")
    text = text.replace(
        old, "if (true)", 1)
    args.destination.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
