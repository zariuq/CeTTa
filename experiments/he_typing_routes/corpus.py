"""The typing judgments of HE's own corpus, as one judgment programme per source.

A judgment programme keeps a source's top-level atoms in order (declarations, subtype
declarations, equations and facts) and replaces every typing assertion with a query that
reports the judgment's types under a tag:

    !(let $types (collapse (get-type E)) (Judgment <id> $types))

so that every route answers the same judgments in the same contexts.  Typing assertions are
`get-type` assertions and assertions whose expected result is a `BadArgType` error.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from sexpr import Form, Str, Sym, read_forms, show

UPSTREAM_SCRIPTS = ("b5_types_prelim", "d1_gadt", "d2_higherfunc", "d3_deptypes",
                    "d4_type_prop", "d5_auto_types")

ASSERTIONS = {"assertEqual", "assertEqualToResult", "assertAlphaEqual",
              "assertAlphaEqualToResult"}


@dataclass
class Judgment:
    ident: str
    source: str
    expression: str
    expected: str
    kind: str


@dataclass
class Programme:
    name: str
    lines: list[str] = field(default_factory=list)
    judgments: list[Judgment] = field(default_factory=list)

    def text(self) -> str:
        return "\n".join(self.lines) + "\n"


def _is_sym(atom, name: str) -> bool:
    return isinstance(atom, Sym) and atom.name == name


def _bad_arg_error(expected) -> bool:
    """The expected result `((Error E (BadArgType ...)))`."""
    if not (isinstance(expected, tuple) and len(expected) == 1 and isinstance(expected[0], tuple)):
        return False
    error = expected[0]
    return (len(error) == 3 and _is_sym(error[0], "Error") and isinstance(error[2], tuple)
            and len(error[2]) > 0 and _is_sym(error[2][0], "BadArgType"))


def query_line(ident: str, expression: str) -> str:
    return f"!(let $types (collapse (get-type {expression})) (Judgment {ident} $types))"


def programme_from_forms(name: str, forms: list[Form], prefix: str) -> Programme:
    programme = Programme(name)
    programme.lines.append(f"; Typing judgments of {name}, one tagged query per assertion.")
    counter = 0
    for form in forms:
        atom = form.atom
        if not form.bang:
            programme.lines.append(form.text)
            continue
        if isinstance(atom, tuple) and atom and _is_sym(atom[0], "pragma!"):
            programme.lines.append("!" + form.text)
            continue
        if isinstance(atom, tuple) and len(atom) == 2 and _is_sym(atom[0], "get-type"):
            counter += 1
            ident = f"{prefix}{counter:02d}"
            programme.judgments.append(Judgment(ident, name, show(atom[1]), "", "get-type"))
            programme.lines.append(query_line(ident, show(atom[1])))
            continue
        if not (isinstance(atom, tuple) and len(atom) == 3 and isinstance(atom[0], Sym)
                and atom[0].name in ASSERTIONS):
            continue
        subject, expected = atom[1], atom[2]
        if isinstance(subject, tuple) and len(subject) == 2 and _is_sym(subject[0], "get-type"):
            expression, kind = subject[1], "get-type"
        elif _bad_arg_error(expected):
            expression, kind = subject, "bad-arg"
        else:
            continue
        counter += 1
        ident = f"{prefix}{counter:02d}"
        programme.judgments.append(Judgment(ident, name, show(expression), show(expected), kind))
        programme.lines.append(query_line(ident, show(expression)))
    return programme


def upstream_programmes(upstream_root: Path) -> list[Programme]:
    scripts = upstream_root / "python" / "tests" / "scripts"
    programmes = []
    for index, name in enumerate(UPSTREAM_SCRIPTS):
        source = (scripts / f"{name}.metta").read_text()
        programmes.append(programme_from_forms(name, read_forms(source), f"u{index}."))
    return programmes


def file_programme(name: str, path: Path, prefix: str) -> Programme:
    return programme_from_forms(name, read_forms(path.read_text()), prefix)
