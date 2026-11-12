#!/usr/bin/env python3
"""Check that Prime's `type:of` and `get-type` answer alike where both read HE-style declarations.

Every judgment of HE's typing corpus is asked both ways in the same space:

    !(let $types (collapse (get-type E))
       (let $of (collapse (type:of &self E)) (Law <id> $types $of)))

The law, with the correspondence of verdicts:

* `type:of` established with types T1 ... Tn: `get-type` answers the same bag, multiplicity
  included, up to the names of type variables;
* `type:of` refuted (`False`): `get-type` answers no type;
* `type:of` undetermined (it stays unevaluated): `get-type` answers `%Undefined%`.

Any other pair is reported with its judgment, for a reason to be named or a repair.

Usage: law_check.py --work DIR [--cetta BINARY] [--json OUT] [--upstream-root DIR]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import corpus  # noqa: E402
import run_routes  # noqa: E402
from sexpr import read_forms, show  # noqa: E402

LAW_LINE = re.compile(r"^\[\(Law ")
VARIABLE = re.compile(r"\$[A-Za-z0-9_'\-]+(?:#[0-9]+)?")


def law_line(ident: str, expression: str) -> str:
    return (f"!(let $types (collapse (get-type {expression})) "
            f"(let $of (collapse (type:of &self {expression})) (Law {ident} $types $of)))")


def open_declared(programme: corpus.Programme) -> list[str]:
    """The names with an open declaration `(: name type)`, `type` mentioning a variable."""
    names = []
    for line in programme.lines:
        for form in read_forms(line) if line.startswith("(:") else []:
            atom = form.atom
            if (not form.bang and isinstance(atom, tuple) and len(atom) == 3
                    and str(atom[0]) == ":" and "$" in show(atom[2])
                    and show(atom[1]) not in names):
                names.append(show(atom[1]))
    return names


def law_programme(programme: corpus.Programme, schemes: bool = False) -> str:
    """The programme with each judgment asked both ways.  With `schemes`, every open declaration
    is marked `(type-scheme name)`, so that Prime reads it as HE does."""
    lines = []
    by_line = {corpus.query_line(j.ident, j.expression): j for j in programme.judgments}
    if schemes:
        lines.extend(f"(type-scheme {name})" for name in open_declared(programme))
    for line in programme.lines:
        judgment = by_line.get(line)
        lines.append(law_line(judgment.ident, judgment.expression) if judgment else line)
    return "\n".join(lines) + "\n"


def normal(text: str) -> str:
    """A type with its variables renamed in order of first occurrence."""
    names: dict[str, str] = {}

    def rename(match: re.Match) -> str:
        return names.setdefault(match.group(0), f"$v{len(names)}")

    return VARIABLE.sub(rename, text)


def elements(atom) -> list[str]:
    return [show(item) for item in atom] if isinstance(atom, tuple) else [show(atom)]


def classify(expression: str, types: list[str], of: list[str]) -> tuple[str, bool]:
    unevaluated = normal(f"(type:of &self {expression})")
    if len(of) == 1 and normal(of[0]) == unevaluated:
        return "undetermined", types == ["%Undefined%"]
    if of == ["False"]:
        return "refuted", types == []
    if not of:
        return "no-answer", False
    return "established", sorted(map(normal, of)) == sorted(map(normal, types))


def check(cetta: Path, programmes: list[corpus.Programme], work: Path,
          schemes: bool = False) -> list[dict]:
    rows = []
    for programme in programmes:
        path = work / f"{programme.name}.law.metta"
        path.write_text(law_programme(programme, schemes))
        output = run_routes.run([str(cetta), "--lang", "prime", str(path)], run_routes.ROOT)
        found = {}
        for line in output.splitlines():
            if not LAW_LINE.match(line):
                continue
            for form in read_forms(line[1:-1]):
                atom = form.atom
                if isinstance(atom, tuple) and len(atom) == 4 and str(atom[0]) == "Law":
                    found[str(atom[1])] = (elements(atom[2]), elements(atom[3]))
        for judgment in programme.judgments:
            if judgment.ident not in found:
                rows.append({"id": judgment.ident, "expression": judgment.expression,
                             "verdict": "missing", "holds": False})
                continue
            types, of = found[judgment.ident]
            verdict, holds = classify(judgment.expression, types, of)
            rows.append({"id": judgment.ident, "expression": judgment.expression,
                         "get-type": types, "type:of": of, "verdict": verdict, "holds": holds})
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--cetta", type=Path, default=run_routes.ROOT / "cetta")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--schemes", action="store_true",
                        help="mark every open declaration (type-scheme name)")
    parser.add_argument("--upstream-root", type=Path,
                        default=run_routes.ROOT.parent / "hyperon-experimental")
    args = parser.parse_args()
    args.probe = False
    args.work.mkdir(parents=True, exist_ok=True)
    rows = check(args.cetta, run_routes.programmes(args), args.work, args.schemes)
    if args.json:
        args.json.write_text(json.dumps(rows, indent=1))
    counts: dict[str, list[int]] = {}
    for row in rows:
        entry = counts.setdefault(row["verdict"], [0, 0])
        entry[0 if row["holds"] else 1] += 1
    for verdict, (holds, fails) in sorted(counts.items()):
        print(f"{verdict:13s} law holds {holds:3d}  differs {fails:3d}")
    for row in rows:
        if not row["holds"]:
            print("DIFFERS", row["id"], row["expression"], "get-type", row.get("get-type"),
                  "type:of", row.get("type:of"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
