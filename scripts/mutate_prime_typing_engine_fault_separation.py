#!/usr/bin/env python3

import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()

    text = args.source.read_text(encoding="utf-8")
    old = """           observation->result.kind == CETTA_NIK_RESULT_OUTCOME &&"""
    new = """           true &&"""
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"expected one semantic-status fault guard, found {count}")
    args.destination.write_text(text.replace(old, new, 1), encoding="utf-8")


if __name__ == "__main__":
    main()
