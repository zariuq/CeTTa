#!/usr/bin/env python3
"""The PeTTa or Prime spelling of a shared library source.

A shared library is written in base HE.  Its spelling for another dialect
is the same program with the HE-specific forms rewritten, and nothing else
changed.

Native results sealed by function/case (the PeTTa spelling):

  (function (case CALL (((return $v) (return $v)) ($r (return $r)))))
      -> (case CALL (((return $v) $v) ($r $r)))
  (function (case CALL (($r (return $r)))))
      -> CALL

A native answers (return VALUE); HE's function hands VALUE back untouched,
and PeTTa never evaluates a value twice, so the plain case is exact there.

HE's unify of two values is spelled as PeTTa's lib_he defines it, at the
call itself, so the spelling needs no import for it (PeTTa resolves a
nested import against the top-level program's directory, where lib_he is
not):

  (unify A B THEN ELSE) -> (if (= A B) THEN ELSE)

A spelling that still uses a name lib_he defines imports lib_he first.

HE's one step of a call or a variable where the result is computed next
(a binding's value, a body, a branch, a scrutinee):

  (eval X) -> X

PeTTa computes X there once, from values it does not evaluate again; its
eval would compute X's elements too, and a list's elements are data.  An
eval in an argument stays: it is a computation handed over as it is.

A value handed to a native as it is:

  (: N (-> Atom ... Atom))
  (: F (-> ... Atom))
  (= (F ARGS...) (function (chain (eval (N ARGS...)) $r (return $r))))

HE computes F's arguments and hands them to the native N; N's declared
Atom parameters take them as they are, and the declared Atom results keep
HE from interpreting a result again.  PeTTa never evaluates a value again,
so its spelling is the call itself (petta: CALL).  Prime passes an
argument as a delayed computation, which case forces before the native
runs (prime: (function (case CALL (($r (return $r)))))).  In both, the
declarations of F and N are dropped: they state HE's contract, and in
these dialects an Atom type would keep an argument or a body from being
computed.

A library that imports a generated realization names the HE one, relative
to its own file, as HE resolves imports; the PeTTa spelling names the
PeTTa realization from the top-level directory, as PeTTa resolves them:

  !(import! &self ../langdef/he/generated/NAME.metta)
      -> !(import! &self langdef/petta/generated/NAME.metta)

The output file is generated: it carries no comments, and a gate compares
it with this transformation of its source.
"""

from __future__ import annotations

import sys
from pathlib import Path

HE_REALIZATIONS = "../langdef/he/generated/"
PETTA_REALIZATIONS = "langdef/petta/generated/"
DIALECTS = ("petta", "prime")

# ---------------------------------------------------------------- reader --


class Symbol(str):
    """A symbol token, distinct from a string literal."""


def read_forms(text: str) -> list:
    """The top-level forms of a MeTTa source; comments are dropped."""
    forms = []
    i = 0
    n = len(text)

    def read(i: int):
        while True:
            while i < n and text[i] in " \t\r\n":
                i += 1
            if i < n and text[i] == ";":
                while i < n and text[i] != "\n":
                    i += 1
                continue
            break
        if i >= n:
            return None, i
        c = text[i]
        if c == "!" and i + 1 < n and text[i + 1] == "(":
            form, j = read(i + 1)
            return ("!", form), j
        if c == "(":
            items = []
            i += 1
            while True:
                while i < n and text[i] in " \t\r\n":
                    i += 1
                if i < n and text[i] == ";":
                    while i < n and text[i] != "\n":
                        i += 1
                    continue
                if i >= n:
                    raise ValueError("unterminated form")
                if text[i] == ")":
                    return items, i + 1
                item, i = read(i)
                items.append(item)
        if c == ")":
            raise ValueError("unexpected )")
        if c == '"':
            j = i + 1
            out = ['"']
            while j < n:
                if text[j] == "\\":
                    out.append(text[j:j + 2])
                    j += 2
                    continue
                out.append(text[j])
                if text[j] == '"':
                    return out and "".join(out), j + 1
                j += 1
            raise ValueError("unterminated string")
        j = i
        while j < n and text[j] not in " \t\r\n()":
            j += 1
        return Symbol(text[i:j]), j

    while True:
        form, i = read(i)
        if form is None:
            break
        forms.append(form)
    return forms


def show(form) -> str:
    if isinstance(form, tuple):
        return "!" + show(form[1])
    if isinstance(form, list):
        return "(" + " ".join(show(item) for item in form) + ")"
    return str(form)


# ------------------------------------------------------------ transform --


def is_variable(form) -> bool:
    return isinstance(form, Symbol) and form.startswith("$")


def is_return_of(form, var) -> bool:
    return (isinstance(form, list) and len(form) == 2 and form[0] == "return"
            and form[1] == var)


def unwrap_function(form):
    """The two sealed-result wrapper shapes, or None."""
    if not (isinstance(form, list) and len(form) == 2 and form[0] == "function"):
        return None
    case = form[1]
    if not (isinstance(case, list) and len(case) == 3 and case[0] == "case"):
        return None
    call, branches = case[1], case[2]
    if not isinstance(branches, list):
        return None
    if (len(branches) == 1 and isinstance(branches[0], list) and len(branches[0]) == 2
            and is_variable(branches[0][0])
            and is_return_of(branches[0][1], branches[0][0])):
        return call
    new_branches = []
    for branch in branches:
        if not (isinstance(branch, list) and len(branch) == 2):
            return None
        pattern, body = branch
        if not (isinstance(body, list) and len(body) == 2 and body[0] == "return"):
            return None
        new_branches.append([pattern, body[1]])
    return [Symbol("case"), call, new_branches]


def value_handoff_call(form):
    """CALL of (function (chain (eval CALL) $r (return $r))), or None."""
    if not (isinstance(form, list) and len(form) == 2 and form[0] == "function"):
        return None
    chain = form[1]
    if not (isinstance(chain, list) and len(chain) == 4 and chain[0] == "chain"):
        return None
    evaluated, var, body = chain[1], chain[2], chain[3]
    if not (isinstance(evaluated, list) and len(evaluated) == 2
            and evaluated[0] == "eval" and is_variable(var)
            and is_return_of(body, var)):
        return None
    return evaluated[1]


def spell_value_handoff(call, dialect: str):
    if dialect == "petta":
        return call
    result = Symbol("$result")
    return [Symbol("function"),
            [Symbol("case"), call, [[result, [Symbol("return"), result]]]]]


def realization_import(form):
    """The PeTTa spelling of an import of a generated HE realization."""
    if (isinstance(form, list) and len(form) == 3 and form[0] == "import!"
            and isinstance(form[2], Symbol) and form[2].startswith(HE_REALIZATIONS)):
        return [form[0], form[1],
                Symbol(PETTA_REALIZATIONS + form[2][len(HE_REALIZATIONS):])]
    return None


# The positions of a form whose result is computed next.
COMPUTED_POSITIONS = {"let": (2, 3), "chain": (1, 3), "if": (1, 2, 3), "=": (2,)}


def one_step(form):
    """X of (eval X) with X a call or a variable, or None."""
    if (isinstance(form, list) and len(form) == 2 and form[0] == "eval"
            and (isinstance(form[1], list) and form[1] or is_variable(form[1]))):
        return form[1]
    return None


def computed_positions(form):
    """The form with HE's one step dropped where its result is computed next,
    or None when there is none to drop."""
    if not (isinstance(form, list) and form and isinstance(form[0], Symbol)):
        return None
    positions = COMPUTED_POSITIONS.get(str(form[0]), ())
    if form[0] == "case" and len(form) == 3 and isinstance(form[2], list):
        new = list(form)
        changed = False
        if one_step(form[1]) is not None:
            new[1] = one_step(form[1])
            changed = True
        branches = []
        for branch in form[2]:
            if (isinstance(branch, list) and len(branch) == 2
                    and one_step(branch[1]) is not None):
                branches.append([branch[0], one_step(branch[1])])
                changed = True
            else:
                branches.append(branch)
        new[2] = branches
        return new if changed else None
    if not positions or len(form) <= max(positions):
        return None
    new = list(form)
    changed = False
    for position in positions:
        inner = one_step(form[position])
        if inner is not None:
            new[position] = inner
            changed = True
    return new if changed else None


def transform(form, dialect: str):
    if isinstance(form, tuple):
        return ("!", transform(form[1], dialect))
    if isinstance(form, list):
        handoff = value_handoff_call(form)
        if handoff is not None:
            return spell_value_handoff(transform(handoff, dialect), dialect)
        if dialect == "petta":
            if len(form) == 5 and form[0] == "unify":
                left, right, then, otherwise = (transform(item, dialect)
                                                for item in form[1:])
                return [Symbol("if"), [Symbol("="), left, right], then, otherwise]
            unwrapped = unwrap_function(form)
            if unwrapped is not None:
                return transform(unwrapped, dialect)
            stepped = computed_positions(form)
            if stepped is not None:
                return [transform(item, dialect) for item in stepped]
            rewritten = realization_import(form)
            if rewritten is not None:
                return rewritten
        return [transform(item, dialect) for item in form]
    return form


def uses(form, name: str) -> bool:
    if isinstance(form, tuple):
        return uses(form[1], name)
    if isinstance(form, list):
        return any(uses(item, name) for item in form)
    return form == name and isinstance(form, Symbol)


def value_handoff_heads(forms) -> set:
    """The heads whose declarations state HE's value-handoff contract:
    every operation all of whose equations hand a value to a native, and
    every native so called (its HE interface)."""
    handoff, other, natives = set(), set(), set()
    for form in forms:
        if not (isinstance(form, list) and len(form) == 3 and form[0] == "="
                and isinstance(form[1], list) and form[1]
                and isinstance(form[1][0], Symbol)):
            continue
        head = form[1][0]
        call = value_handoff_call(form[2])
        if call is None:
            other.add(head)
            continue
        handoff.add(head)
        if isinstance(call, list) and call and isinstance(call[0], Symbol):
            natives.add(call[0])
    return (handoff - other) | natives


def is_declaration_of(form, heads: set) -> bool:
    return (isinstance(form, list) and len(form) == 3 and form[0] == ":"
            and isinstance(form[1], Symbol) and form[1] in heads)


def spelling(source: str, dialect: str) -> str:
    forms = read_forms(source)
    dropped = value_handoff_heads(forms)
    description = {
        "petta": "PeTTa spelling of the library: the same program with the\n"
                 "; HE-only native wrappers spelled as PeTTa calls.",
        "prime": "Prime spelling of the library: the same program with each\n"
                 "; value handoff spelled as a forcing case.",
    }[dialect]
    out = ["; Generated by tools/library_spelling.py from the shared source;",
           "; do not edit.  The " + description, ""]
    spelled = [transform(form, dialect) for form in forms
               if not is_declaration_of(form, dropped)]
    if dialect == "petta" and any(uses(form, name) for form in spelled
                                  for name in lib_he_names()):
        out.append("!(import! &self lib_he)")
    out.extend(show(form) for form in spelled)
    return "\n".join(out) + "\n"


def lib_he_names() -> set:
    """The functions PeTTa's lib_he defines."""
    source = Path(__file__).resolve().parent.parent / "lib" / "petta" / "lib_he.metta"
    names = set()
    for form in read_forms(source.read_text(encoding="utf-8")):
        if (isinstance(form, list) and len(form) == 3 and form[0] == "="
                and isinstance(form[1], list) and form[1]
                and isinstance(form[1][0], Symbol)):
            names.add(form[1][0])
    return names


def main(argv: list[str]) -> int:
    args = [arg for arg in argv[1:] if not arg.startswith("--")]
    options = [arg for arg in argv[1:] if arg.startswith("--")]
    dialect = "petta"
    for option in options:
        if option.startswith("--dialect="):
            dialect = option.split("=", 1)[1]
        elif option != "--check":
            print(f"unknown option {option}", file=sys.stderr)
            return 2
    if len(args) != 2 or dialect not in DIALECTS:
        print("usage: library_spelling.py SOURCE OUTPUT [--dialect=petta|prime] [--check]",
              file=sys.stderr)
        return 2
    source = Path(args[0]).read_text(encoding="utf-8")
    output = Path(args[1])
    text = spelling(source, dialect)
    if "--check" in options:
        current = output.read_text(encoding="utf-8") if output.exists() else ""
        if current != text:
            print(f"{output} is out of date with {args[0]}", file=sys.stderr)
            return 1
        print(f"{output} is up to date")
        return 0
    output.write_text(text, encoding="utf-8")
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
