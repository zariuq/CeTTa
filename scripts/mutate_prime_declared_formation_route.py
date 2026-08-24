#!/usr/bin/env python3

from pathlib import Path
import sys


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: mutate_prime_declared_formation_route.py SOURCE DESTINATION"
        )
    source = Path(sys.argv[1])
    destination = Path(sys.argv[2])
    text = source.read_text(encoding="utf-8")
    start = text.index("static Atom *prime_form_judgment(")
    end = text.index(
        "bool cetta_prime_typing_authority_observation_v1_status(", start
    )
    prefix = text[:start]
    formation = text[start:end]
    suffix = text[end:]
    old = (
        "        if (!native_owned)\n"
        "            status = prime_form_declared_regular_type("
    )
    new = (
        "        if (false && !native_owned)\n"
        "            status = prime_form_declared_regular_type("
    )
    count = formation.count(old)
    if count != 1:
        raise SystemExit(
            f"expected one declared formation route, found {count}"
        )
    destination.write_text(
        prefix + formation.replace(old, new, 1) + suffix,
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
