#!/usr/bin/env python3
"""Remove computed-type completion gating to recreate false uniqueness."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print(
            "usage: mutate_prime_delayed_ambiguity.py INPUT OUTPUT",
            file=sys.stderr,
        )
        return 2
    source = Path(sys.argv[1]).read_text()
    marker = """        if (completion != CETTA_EVAL_COMPLETE) {
            *status = HE_NORM_RESOURCE;
            goto fail;
        }
"""
    if source.count(marker) != 1:
        print("normalization completion gate must occur exactly once", file=sys.stderr)
        return 2
    Path(sys.argv[2]).write_text(source.replace(marker, ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
