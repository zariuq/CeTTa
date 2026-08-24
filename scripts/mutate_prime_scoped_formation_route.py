#!/usr/bin/env python3

import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()

    text = args.source.read_text(encoding="utf-8")
    old = (
        "        status = prime_form_scoped_regular_type(\n"
        "            arena, type, ledger, &detail, &native_owned);"
    )
    new = (
        "        status = prime_form_scoped_regular_type(\n"
        "            arena, type, ledger, &detail, &native_owned);\n"
        "        native_owned = false;\n"
        "        status = PRIME_FORM_UNDETERMINED;"
    )
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"expected one scoped formation route, found {count}")
    args.destination.write_text(text.replace(old, new, 1), encoding="utf-8")


if __name__ == "__main__":
    main()
