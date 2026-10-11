"""Observe result bags without changing atoms, lists or effect streams."""

from collections import Counter

from petta_corpus_manifest import alpha_canonicalize_atom_text


def split_top_level_commas(payload: str) -> list[str]:
    """Split host result framing, preserving every character inside an atom."""
    if not payload.strip():
        return []
    parts = []
    stack = []
    start = 0
    in_string = False
    escaped = False
    for index, character in enumerate(payload):
        if in_string:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_string = False
        elif character == '"':
            in_string = True
        elif character in "([":
            stack.append(character)
        elif character in ")]":
            if not stack or stack.pop() != ("(" if character == ")" else "["):
                raise ValueError("Unbalanced result atom")
        elif character == "," and not stack:
            part = payload[start:index].strip()
            if not part:
                raise ValueError("Empty result occurrence")
            parts.append(part)
            start = index + 1
    if in_string or stack:
        raise ValueError("Unterminated result atom")
    tail = payload[start:].strip()
    if not tail:
        raise ValueError("Empty result occurrence")
    return [*parts, tail]


def answer_bag(frontier: str, *, alpha_variables: bool = False) -> Counter[str]:
    """Only the outer answer collection is unordered; duplicates remain."""
    line = frontier.strip()
    if not (line.startswith("[") and line.endswith("]")):
        raise ValueError("Expected a bracketed result frontier")
    atoms = split_top_level_commas(line[1:-1])
    if alpha_variables:
        atoms = [alpha_canonicalize_atom_text(atom) for atom in atoms]
    return Counter(atoms)


def output_observation(output: str) -> list[tuple[str, object]]:
    """Keep query boundaries and non-answer output in their original order."""
    return [("answers", answer_bag(line)) if line.startswith("[") else
            ("output", line) for line in output.splitlines()]


def main() -> int:
    """Compare stdin with an expected output file under the same contract."""
    import argparse
    from pathlib import Path
    import sys

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("expected", type=Path)
    args = parser.parse_args()
    try:
        return int(output_observation(sys.stdin.read()) !=
            output_observation(args.expected.read_text(encoding="utf-8")))
    except (OSError, ValueError) as error:
        print(f"Invalid result observation: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
