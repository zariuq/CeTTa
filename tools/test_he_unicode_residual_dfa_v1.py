#!/usr/bin/env python3
"""Certify the finite residual presentation of ratified HE Unicode escapes."""

from __future__ import annotations

from dataclasses import replace
from hashlib import sha256
from itertools import product
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import build_he_reader_gslt_v1 as builder  # noqa: E402


EXPECTED_RELATION_DIGEST = (
    "8f9246efc0040f3d90a5b61efecd2703c39a2cb8b43a77c52e28206808d63756"
)


class GateFailure(RuntimeError):
    pass


def relation_digest(sequences: list[tuple[str, ...]]) -> str:
    payload = "".join("\t".join(sequence) + "\n" for sequence in sequences)
    return sha256(payload.encode("utf-8")).hexdigest()


def residual_accepts(
    states: tuple[builder.UnicodeResidualState, ...],
    text: str,
) -> bool:
    state_id = 0
    for character in text:
        codepoint = ord(character)
        matches = [
            transition
            for transition in states[state_id].transitions
            if codepoint
            in builder.UNICODE_HEX_CLASS_POINTS[transition.class_name]
        ]
        if len(matches) > 1:
            raise GateFailure("Unicode residual transitions overlap at runtime")
        if not matches:
            return False
        state_id = matches[0].target
    return states[state_id].accepting


def authority_accepts(text: str) -> bool:
    if (
        not 1 <= len(text) <= 6
        or any(ord(character) not in builder.HEX for character in text)
    ):
        return False
    value = int(text, 16) if text else 0
    return value <= 0x10FFFF and not 0xD800 <= value <= 0xDFFF


def authority_category(codepoint: int) -> tuple[bool, bool, bool, bool]:
    digit = int(chr(codepoint), 16)
    return digit == 0, digit == 1, digit == 13, digit <= 7


def abstract_alphabet(
    sequences: list[tuple[str, ...]],
) -> tuple[str, ...]:
    names = sorted({name for sequence in sequences for name in sequence})
    groups: dict[tuple[bool, ...], list[int]] = {}
    for codepoint in sorted(builder.HEX):
        signature = tuple(
            codepoint in builder.UNICODE_HEX_CLASS_POINTS[name]
            for name in names
        )
        groups.setdefault(signature, []).append(codepoint)
    if len(groups) != 5:
        raise GateFailure("Unicode source classes no longer induce five atoms")
    for points in groups.values():
        categories = {authority_category(codepoint) for codepoint in points}
        if len(categories) != 1:
            raise GateFailure(
                "Unicode class partition does not refine authority boundaries"
            )
    return tuple(chr(points[0]) for points in groups.values())


def expect_invalid(
    sequences: list[tuple[str, ...]],
    states: tuple[builder.UnicodeResidualState, ...],
) -> None:
    try:
        builder.validate_unicode_residual_dfa(sequences, states)
    except ValueError:
        return
    raise GateFailure("Unicode residual mutation survived validation")


def main() -> int:
    sequences = builder.unicode_valid_class_sequences()
    states = builder.unicode_residual_dfa(sequences)
    digest = relation_digest(sequences)
    if digest != EXPECTED_RELATION_DIGEST:
        raise GateFailure(f"Unicode source relation changed: {digest}")
    if len(sequences) != 30 or len(states) != 17:
        raise GateFailure("Unicode residual relation dimensions changed")
    transition_len = sum(len(state.transitions) for state in states)
    if transition_len != 28:
        raise GateFailure("Unicode residual transition count changed")

    alphabet = abstract_alphabet(sequences)
    quotient_cases = 0
    for length in range(8):
        for characters in product(alphabet, repeat=length):
            text = "".join(characters)
            if residual_accepts(states, text) != authority_accepts(text):
                raise GateFailure(
                    f"Unicode residual quotient differs on {text!r}"
                )
            quotient_cases += 1
    if quotient_cases != 97656:
        raise GateFailure("Unicode residual quotient coverage changed")

    acceptance_mutation = list(states)
    acceptance_mutation[0] = replace(
        acceptance_mutation[0],
        accepting=not acceptance_mutation[0].accepting,
    )
    expect_invalid(sequences, tuple(acceptance_mutation))

    coverage_mutation = list(states)
    coverage_mutation[0] = replace(
        coverage_mutation[0],
        transitions=coverage_mutation[0].transitions[:-1],
    )
    expect_invalid(sequences, tuple(coverage_mutation))

    target_mutation = list(states)
    first = target_mutation[0].transitions[0]
    target_mutation[0] = replace(
        target_mutation[0],
        transitions=(replace(first, target=0),)
        + target_mutation[0].transitions[1:],
    )
    expect_invalid(sequences, tuple(target_mutation))

    print(
        "(HEUnicodeResidualDFAV1Summary "
        f"{len(sequences)} {len(states)} {transition_len} "
        f"{len(alphabet)} {quotient_cases} 3 {digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
