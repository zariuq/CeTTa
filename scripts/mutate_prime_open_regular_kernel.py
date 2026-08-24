#!/usr/bin/env python3

import pathlib
import sys


RULES = {
    "disable-sigma": "prime-regular-synth-sigma",
    "disable-id": "prime-regular-synth-id",
    "disable-pair": "prime-regular-check-pair",
    "disable-fst": "prime-regular-synth-fst",
    "disable-snd": "prime-regular-synth-snd",
    "disable-refl": "prime-regular-synth-refl",
    "disable-fst-beta": "prime-regular-normalize-fst-pair",
    "disable-snd-beta": "prime-regular-normalize-snd-pair",
    "disable-level-maximum": "prime-regular-level-normalize-maximum",
    "disable-universe-synthesis": "prime-regular-synth-u1",
    "disable-universe-join": "prime-regular-sort-join-explicit-u1",
    "disable-cumulative-promotion": "prime-regular-assignable-cumulative",
    "disable-embedded-sort-conversion":
        "prime-regular-convertible-u1-explicit",
}


def remove_rule(text: str, rule_name: str) -> str:
    marker = f"    (rule {rule_name}\n"
    start = text.find(marker)
    if start < 0 or text.find(marker, start + 1) >= 0:
        raise ValueError(f"rule anchor is absent or ambiguous: {rule_name}")
    depth = 0
    end = start
    while end < len(text):
        char = text[end]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                end += 1
                if end < len(text) and text[end] == "\n":
                    end += 1
                return text[:start] + text[end:]
        end += 1
    raise ValueError(f"unterminated rule: {rule_name}")


def main() -> int:
    if len(sys.argv) != 4 or sys.argv[1] not in RULES:
        choices = "|".join(RULES)
        print(
            f"usage: {sys.argv[0]} {choices} SOURCE OUTPUT",
            file=sys.stderr,
        )
        return 2
    source = pathlib.Path(sys.argv[2])
    output = pathlib.Path(sys.argv[3])
    try:
        mutated = remove_rule(source.read_text(), RULES[sys.argv[1]])
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    output.write_text(mutated)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
