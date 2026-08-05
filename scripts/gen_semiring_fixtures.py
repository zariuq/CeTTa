#!/usr/bin/env python3
"""Generate bag-law fixtures from lib/gslt_semiring.metta.

The GSLT is the source of law syntax and the query-connective interpretation.
Generation is transactional: every instantiated law is first checked as a
multiset equality against HE, Prime, and PeTTa extended.  No golden is replaced
if a law fails.
"""

from __future__ import annotations

import argparse
import copy
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, Sequence


ROOT = Path(__file__).resolve().parents[1]
SPEC = ROOT / "lib" / "gslt_semiring.metta"
OUT_BASE = ROOT / "tests" / "generated" / "semiring_query_laws"


class SpecError(RuntimeError):
    pass


SExpr = str | list["SExpr"]


def tokens(text: str) -> Iterator[str]:
    i = 0
    while i < len(text):
        ch = text[i]
        if ch.isspace():
            i += 1
            continue
        if ch == ";":
            newline = text.find("\n", i)
            i = len(text) if newline < 0 else newline + 1
            continue
        if ch in "()":
            yield ch
            i += 1
            continue
        if ch == '"':
            start = i
            i += 1
            escaped = False
            while i < len(text):
                if escaped:
                    escaped = False
                elif text[i] == "\\":
                    escaped = True
                elif text[i] == '"':
                    i += 1
                    break
                i += 1
            else:
                raise SpecError("unterminated string literal")
            yield text[start:i]
            continue
        start = i
        while i < len(text) and not text[i].isspace() and text[i] not in "();":
            i += 1
        yield text[start:i]


def parse_forms(text: str) -> list[SExpr]:
    stream = iter(tokens(text))
    pushed: list[str] = []

    def take() -> str:
        if pushed:
            return pushed.pop()
        try:
            return next(stream)
        except StopIteration as exc:
            raise SpecError("unexpected end of input") from exc

    def parse_one(first: str | None = None) -> SExpr:
        token = take() if first is None else first
        if token == ")":
            raise SpecError("unexpected closing parenthesis")
        if token != "(":
            return token
        result: list[SExpr] = []
        while True:
            token = take()
            if token == ")":
                return result
            result.append(parse_one(token))

    forms: list[SExpr] = []
    while True:
        try:
            token = next(stream)
        except StopIteration:
            break
        forms.append(parse_one(token))
    return forms


def sexpr(value: SExpr) -> str:
    if isinstance(value, str):
        return value
    return "(" + " ".join(sexpr(item) for item in value) + ")"


def is_form(value: SExpr, head: str) -> bool:
    return isinstance(value, list) and bool(value) and value[0] == head


@dataclass(frozen=True)
class Law:
    name: str
    lhs: SExpr
    rhs: SExpr


@dataclass(frozen=True)
class Filler:
    name: str
    space: SExpr
    substitutions: dict[str, SExpr]
    template: SExpr


def load_spec() -> tuple[list[Law], dict[str, SExpr]]:
    forms = parse_forms(SPEC.read_text())
    theory = next(
        (form for form in forms
         if isinstance(form, list) and form[:2] == ["gslt", "semiring"]),
        None,
    )
    interp = next(
        (form for form in forms
         if isinstance(form, list) and form[:2] == ["interpretation", "query-connectives"]),
        None,
    )
    if theory is None or interp is None:
        raise SpecError("semiring theory or query-connectives interpretation is missing")

    laws: list[Law] = []
    for entry in theory[2:]:
        if not is_form(entry, "law"):
            continue
        assert isinstance(entry, list)
        if (len(entry) != 4 or not isinstance(entry[1], str) or
                not is_form(entry[2], "=") or entry[3] != ["obs", "bag"]):
            raise SpecError(f"malformed or non-bag law: {sexpr(entry)}")
        equation = entry[2]
        assert isinstance(equation, list)
        if len(equation) != 3:
            raise SpecError(f"law equation is not binary: {sexpr(entry)}")
        laws.append(Law(entry[1], equation[1], equation[2]))

    expected_names = {
        "assoc-otimes", "unit-otimes-left", "unit-otimes-right",
        "assoc-oplus", "comm-oplus", "unit-oplus-left",
        "annihilate-left", "annihilate-right",
        "distrib-left", "distrib-right",
    }
    names = {law.name for law in laws}
    if len(laws) != 10 or names != expected_names:
        raise SpecError(f"expected the ten pinned semiring laws, found {sorted(names)}")

    mapping: dict[str, SExpr] = {}
    for entry in interp[2:]:
        if not is_form(entry, "map"):
            continue
        assert isinstance(entry, list)
        if len(entry) != 3 or not isinstance(entry[1], str):
            raise SpecError(f"malformed interpretation map: {sexpr(entry)}")
        mapping[entry[1]] = entry[2]
    if set(mapping) != {"otimes", "one", "oplus", "zero"}:
        raise SpecError(f"query interpretation is incomplete: {sorted(mapping)}")
    return laws, mapping


def instantiate(value: SExpr, substitutions: dict[str, SExpr],
                mapping: dict[str, SExpr]) -> SExpr:
    if isinstance(value, str):
        if value in substitutions:
            return copy.deepcopy(substitutions[value])
        if value in mapping:
            return copy.deepcopy(mapping[value])
        return value
    if not value:
        return []
    head = value[0]
    if isinstance(head, str) and head in mapping:
        target = copy.deepcopy(mapping[head])
        args = [instantiate(arg, substitutions, mapping) for arg in value[1:]]
        if isinstance(target, str):
            return [target, *args]
        if args:
            if not target:
                raise SpecError(f"cannot apply an empty target for {head}")
            return [*target, *args]
        return target
    return [instantiate(item, substitutions, mapping) for item in value]


SETUP = [
    "!(bind! &semiring-laws (new-space))",
    "!(add-atom &semiring-laws (f a))",
    "!(add-atom &semiring-laws (f a))",
    "!(add-atom &semiring-laws (f b))",
    "!(add-atom &semiring-laws (tag a))",
    "!(add-atom &semiring-laws (tag b))",
    "!(add-atom &semiring-laws (tag b))",
    "!(add-atom &semiring-laws (mark a))",
    "!(add-atom &semiring-laws (mark b))",
    "!(add-atom &semiring-laws (mark c))",
    "!(bind! &semiring-left (new-space))",
    "!(add-atom &semiring-left (f a))",
    "!(add-atom &semiring-left (tag a))",
    "!(add-atom &semiring-left (mark a))",
    "!(bind! &semiring-right (new-space))",
    "!(add-atom &semiring-right (f a))",
    "!(add-atom &semiring-right (f b))",
    "!(add-atom &semiring-right (tag b))",
    "!(add-atom &semiring-right (mark b))",
]


FILLERS = [
    Filler(
        "ground",
        "&semiring-laws",
        {"$a": ["f", "a"], "$b": ["tag", "a"], "$c": ["mark", "a"]},
        "ground-hit",
    ),
    Filler(
        "variable",
        "&semiring-laws",
        {"$a": ["f", "$x"], "$b": ["tag", "$x"], "$c": ["mark", "$x"]},
        ["variable-hit", "$x"],
    ),
    Filler(
        "space-pair",
        ["|", "&semiring-left", "&semiring-right"],
        {"$a": ["f", "$x"], "$b": ["tag", "$x"], "$c": ["mark", "$x"]},
        ["space-pair-hit", "$x"],
    ),
]


LANG_COMMANDS = {
    "he": ["--lang", "he", "--profile", "he-extended"],
    "prime": ["--lang", "prime"],
    "petta": ["--lang", "petta", "--profile", "extended"],
}


def run_query(cetta: Path, language: str, space: SExpr, pattern: SExpr,
              template: SExpr) -> Counter[str]:
    command = [str(cetta), *LANG_COMMANDS[language], "--quiet"]
    for expression in SETUP:
        command.extend(["-e", expression])
    query = (
        f"!(match {sexpr(space)} {sexpr(pattern)} "
        f"(println! (semiring-row {sexpr(template)})))"
    )
    command.extend(["-e", query])
    proc = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                          timeout=30)
    if proc.returncode != 0 or proc.stderr:
        raise RuntimeError(
            f"{language} oracle failed for {sexpr(pattern)}\n"
            f"return={proc.returncode}\nstderr:\n{proc.stderr}")
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    bad = [line for line in lines if "(Error " in line]
    if bad:
        raise RuntimeError(
            f"{language} oracle produced evaluator output instead of answer rows: {bad}")
    prefix = "(semiring-row "
    rows = [line[len(prefix):-1] for line in lines
            if line.startswith(prefix) and line.endswith(")")]
    return Counter(rows)


def expected_expr(bag: Counter[str]) -> str:
    items: list[str] = []
    for item in sorted(bag):
        items.extend([item] * bag[item])
    return "(" + " ".join(items) + ")"


def build_fixture(cetta: Path) -> tuple[str, str]:
    laws, mapping = load_spec()
    checks: list[tuple[str, SExpr, SExpr, SExpr, SExpr, Counter[str]]] = []

    for law in laws:
        for filler in FILLERS:
            lhs = instantiate(law.lhs, filler.substitutions, mapping)
            rhs = instantiate(law.rhs, filler.substitutions, mapping)
            by_language: dict[str, tuple[Counter[str], Counter[str]]] = {}
            for language in LANG_COMMANDS:
                left = run_query(cetta, language, filler.space, lhs, filler.template)
                right = run_query(cetta, language, filler.space, rhs, filler.template)
                if left != right:
                    raise RuntimeError(
                        f"semiring law {law.name}/{filler.name} fails in {language}\n"
                        f"lhs {sexpr(lhs)} => {left}\n"
                        f"rhs {sexpr(rhs)} => {right}")
                by_language[language] = (left, right)
            if any(by_language["he"][0] != by_language[name][0]
                   for name in ("prime", "petta")):
                raise RuntimeError(
                    f"cross-profile parity fails for {law.name}/{filler.name}: "
                    f"{by_language}")
            if not law.name.startswith("annihilate-") and not by_language["he"][0]:
                raise RuntimeError(
                    f"semiring law {law.name}/{filler.name} passed vacuously")
            checks.append((law.name, filler.name, filler.space, lhs, rhs,
                           by_language["he"][0]))

    lines = [
        "; Generated by scripts/gen_semiring_fixtures.py from lib/gslt_semiring.metta.",
        "; Each assertion compares answer bags; declaration order is not observed.",
        "",
        *SETUP,
        "",
    ]
    for law_name, filler_name, space, lhs, rhs, bag in checks:
        filler = next(item for item in FILLERS if item.name == filler_name)
        expected = expected_expr(bag)
        lines.extend([
            f"; {law_name} / {filler_name}",
            "!(assertEqualToResult",
            f"  (match {sexpr(space)} {sexpr(lhs)} {sexpr(filler.template)})",
            f"  {expected})",
            "!(assertEqualToResult",
            f"  (match {sexpr(space)} {sexpr(rhs)} {sexpr(filler.template)})",
            f"  {expected})",
        ])
    metta = "\n".join(lines) + "\n"

    scratch_dir = ROOT / "runtime" / "semiring-fixtures"
    scratch_dir.mkdir(parents=True, exist_ok=True)
    scratch = scratch_dir / "semiring_query_laws.metta"
    scratch.write_text(metta)
    outputs: dict[str, str] = {}
    try:
        for language, args in LANG_COMMANDS.items():
            proc = subprocess.run([str(cetta), *args, str(scratch)], cwd=ROOT,
                                  text=True, capture_output=True, timeout=60)
            if proc.returncode != 0 or proc.stderr or "(Error " in proc.stdout:
                raise RuntimeError(
                    f"generated fixture fails in {language}\nreturn={proc.returncode}\n"
                    f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}")
            outputs[language] = proc.stdout
    finally:
        scratch.unlink(missing_ok=True)
    if outputs["he"] != outputs["prime"]:
        raise RuntimeError("generated fixture is not byte-identical in HE and Prime")
    return metta, outputs["he"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, default=ROOT / "cetta")
    args = parser.parse_args()
    cetta = args.cetta.resolve()
    if not cetta.is_file():
        raise SystemExit(f"oracle binary not found: {cetta}")

    try:
        metta, expected = build_fixture(cetta)
    except (SpecError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1

    OUT_BASE.parent.mkdir(parents=True, exist_ok=True)
    OUT_BASE.with_suffix(".metta").write_text(metta)
    OUT_BASE.with_suffix(".expected").write_text(expected)
    sync = subprocess.run([str(ROOT / "scripts" / "sync_test_manifest.py"), "--write"],
                          cwd=ROOT)
    if sync.returncode != 0:
        return sync.returncode
    print(f"PASS: generated {OUT_BASE.with_suffix('.metta').relative_to(ROOT)}")
    print("PASS: 10 laws x 3 fillers, bag-equal in HE, Prime, and PeTTa extended")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
