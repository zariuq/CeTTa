"""A reader and printer for MeTTa S-expressions, enough for typing judgments.

Atoms are Python values: a symbol is ``Sym``, a variable is ``Var``, a number is ``int`` or
``float``, a string literal is ``Str``, and an expression is a tuple.  ``read_forms`` keeps
each top-level form's source text and whether it was prefixed with ``!``.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterator, Union


@dataclass(frozen=True)
class Sym:
    name: str

    def __str__(self) -> str:
        return self.name


@dataclass(frozen=True)
class Var:
    name: str

    def __str__(self) -> str:
        return "$" + self.name


@dataclass(frozen=True)
class Str:
    text: str

    def __str__(self) -> str:
        return '"' + self.text.replace("\\", "\\\\").replace('"', '\\"') + '"'


Atom = Union[Sym, Var, Str, int, float, tuple]


@dataclass
class Form:
    bang: bool
    atom: Atom
    text: str


def _tokens(source: str) -> Iterator[tuple[str, int, int]]:
    index = 0
    length = len(source)
    while index < length:
        char = source[index]
        if char.isspace():
            index += 1
        elif char == ";":
            while index < length and source[index] != "\n":
                index += 1
        elif char in "()":
            yield char, index, index + 1
            index += 1
        elif char == '"':
            start = index
            index += 1
            while index < length and source[index] != '"':
                index += 2 if source[index] == "\\" else 1
            index += 1
            yield source[start:index], start, index
        else:
            start = index
            while index < length and not source[index].isspace() and source[index] not in '();"':
                index += 1
            yield source[start:index], start, index


def _leaf(token: str) -> Atom:
    if token.startswith('"'):
        body = token[1:-1]
        return Str(body.replace('\\"', '"').replace("\\\\", "\\"))
    if token.startswith("$"):
        return Var(token[1:])
    try:
        return int(token)
    except ValueError:
        pass
    try:
        if any(c in token for c in ".eE") and token[0] in "-+0123456789":
            return float(token)
    except ValueError:
        pass
    return Sym(token)


def read_forms(source: str) -> list[Form]:
    """Top-level forms of a MeTTa source, with their text and `!` marks."""
    forms: list[Form] = []
    stack: list[list] = []
    bang = False
    start = 0
    for token, begin, end in _tokens(source):
        if not stack and token == "!":
            bang = True
            continue
        if token == "(":
            if not stack:
                start = begin
            stack.append([])
        elif token == ")":
            items = stack.pop()
            atom = tuple(items)
            if stack:
                stack[-1].append(atom)
            else:
                forms.append(Form(bang, atom, source[start:end]))
                bang = False
        else:
            leaf = _leaf(token)
            if stack:
                stack[-1].append(leaf)
            else:
                forms.append(Form(bang, leaf, token))
                bang = False
    return forms


def read_atom(text: str) -> Atom:
    forms = read_forms(text)
    if len(forms) != 1:
        raise ValueError(f"expected one atom in {text!r}")
    return forms[0].atom


def show(atom: Atom) -> str:
    if isinstance(atom, tuple):
        return "(" + " ".join(show(item) for item in atom) + ")"
    if isinstance(atom, float):
        return repr(atom)
    return str(atom)
