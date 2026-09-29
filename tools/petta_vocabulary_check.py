#!/usr/bin/env python3
"""Check that PeTTa spellings of libraries use only real PeTTa's vocabulary.

Real PeTTa translates an expression as a call when its head is one of its
functions (fun/1) or a special form of its translator; any other head is
data.  A PeTTa spelling that relies on a head CeTTa evaluates but real PeTTa
does not would compute something under CeTTa and nothing under PeTTa.

In every evaluated position of a spelling (equation bodies and top-level
!-forms, outside quote) each head must be one of:

  - real PeTTa's vocabulary (the reference file: functions and forms, and
    lib_he's heads where the spelling imports lib_he);
  - a head the spelling itself, or a file it imports, defines by equations;
  - a native CeTTa registers for the library (__cetta_ prefix);
  - a head CeTTa does not evaluate either, which both read as data.

A head that CeTTa evaluates (a grounded operation or an evaluator form,
read from src/symbol.h) and that is none of the above is reported.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from library_spelling import Symbol, read_forms  # noqa: E402


def read_vocabulary(path: Path) -> tuple[set, set]:
    """Real PeTTa's functions and forms, and the heads lib_he defines."""
    core, lib_he, section = set(), set(), None
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("["):
            section = line
            continue
        (lib_he if section == "[lib_he]" else core).add(line)
    return core, lib_he


# CeTTa evaluator forms that real PeTTa's translator does not have: HE's
# function/return pair and minimal instructions, and unify, which PeTTa
# gains only by importing lib_he.
CETTA_FORMS_OUTSIDE_PETTA = {
    "function", "return", "unify", "evalc", "switch", "if-equal",
    "collapse-bind", "superpose-bind", "decons-atom",
}


def cetta_evaluated_heads(symbol_header: Path) -> set:
    """The heads CeTTa evaluates: its grounded operations and forms."""
    text = symbol_header.read_text(encoding="utf-8")
    spelling = dict(re.findall(r'X\(([A-Za-z_0-9]+),\s*"([^"]+)"\)', text))
    heads = set(CETTA_FORMS_OUTSIDE_PETTA)
    lines = text.splitlines()
    for macro in ("CETTA_STATIC_GROUNDED_SYMBOL_FIELDS",
                  "CETTA_TYPE_PURE_GROUNDED_SYMBOL_FIELDS"):
        start = next((i for i, line in enumerate(lines)
                      if line.startswith(f"#define {macro}(X)")), None)
        if start is None:
            raise SystemExit(f"{symbol_header}: cannot read {macro}")
        body = []
        index = start
        while True:
            body.append(lines[index])
            if not lines[index].rstrip().endswith("\\"):
                break
            index += 1
        for field in re.findall(r"X\(([A-Za-z_0-9]+)\)", "\n".join(body[1:])):
            if field in spelling:
                heads.add(spelling[field])
    return heads


def head_of(form):
    if isinstance(form, list) and form and isinstance(form[0], Symbol):
        return form[0]
    return None


def defined_heads(forms) -> set:
    heads = set()
    for form in forms:
        if (isinstance(form, list) and len(form) == 3 and form[0] == "="
                and head_of(form[1])):
            heads.add(head_of(form[1]))
    return heads


def imports_of(forms) -> list:
    out = []
    for form in forms:
        if isinstance(form, tuple):
            call = form[1]
            if (isinstance(call, list) and len(call) == 3 and call[0] == "import!"
                    and isinstance(call[2], Symbol)):
                out.append(str(call[2]))
    return out


def resolve_import(name: str, root: Path) -> Path | None:
    if name == "lib_he":
        return None  # real PeTTa's own library: part of the vocabulary
    candidates = []
    if "/" in name or name.endswith(".metta"):
        candidates.append(root / name)
    else:
        candidates += [root / "lib" / "petta" / f"{name}.metta",
                       root / "lib" / f"{name}.metta"]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def closure_defined(path: Path, root: Path, seen: set, lib_he: set) -> set:
    """Heads defined by the file and everything it imports; lib_he's heads
    count once some file in the closure imports lib_he."""
    if path in seen:
        return set()
    seen.add(path)
    forms = read_forms(path.read_text(encoding="utf-8"))
    heads = defined_heads(forms)
    for name in imports_of(forms):
        if name == "lib_he":
            heads |= lib_he
            continue
        target = resolve_import(name, root)
        if target is not None:
            heads |= closure_defined(target, root, seen, lib_he)
    return heads


def evaluated_heads(form, found: list, context):
    """Heads of every expression in an evaluated position.  Patterns are
    not evaluated: case branches' patterns, the binders of let, let*,
    chain and match, and unify's two operands."""
    if not isinstance(form, list):
        return
    head = head_of(form)
    if head == "quote":
        return
    if head is None:
        for item in form:
            evaluated_heads(item, found, context)
        return
    found.append((head, context))
    args = form[1:]
    if head == "case" and len(args) == 2:
        evaluated_heads(args[0], found, context)
        if isinstance(args[1], list):
            for branch in args[1]:
                if isinstance(branch, list) and len(branch) == 2:
                    evaluated_heads(branch[1], found, context)
        return
    if head == "let" and len(args) == 3:
        evaluated_heads(args[1], found, context)
        evaluated_heads(args[2], found, context)
        return
    if head == "let*" and len(args) == 2:
        if isinstance(args[0], list):
            for binding in args[0]:
                if isinstance(binding, list) and len(binding) == 2:
                    evaluated_heads(binding[1], found, context)
        evaluated_heads(args[1], found, context)
        return
    if head == "chain" and len(args) == 3:
        evaluated_heads(args[0], found, context)
        evaluated_heads(args[2], found, context)
        return
    if head == "match" and len(args) == 3:
        evaluated_heads(args[0], found, context)
        evaluated_heads(args[2], found, context)
        return
    if head == "unify" and len(args) == 4:
        evaluated_heads(args[2], found, context)
        evaluated_heads(args[3], found, context)
        return
    for item in args:
        evaluated_heads(item, found, context)


def check(path: Path, root: Path, allowed: set, lib_he: set,
          builtins: set) -> list:
    forms = read_forms(path.read_text(encoding="utf-8"))
    defined = closure_defined(path, root, set(), lib_he)
    found = []
    for form in forms:
        if isinstance(form, tuple):
            evaluated_heads(form[1], found, "top-level form")
        elif isinstance(form, list) and len(form) == 3 and form[0] == "=":
            evaluated_heads(form[2], found, f"body of {head_of(form[1])}")
    violations = {}
    for head, context in found:
        if head in allowed or head in defined or head.startswith("__cetta_"):
            continue
        if head in builtins:
            violations.setdefault(head, context)
    return sorted(violations.items())


def main(argv: list[str]) -> int:
    if len(argv) < 4:
        print("usage: petta_vocabulary_check.py VOCABULARY ROOT SPELLING...",
              file=sys.stderr)
        return 2
    vocabulary, lib_he = read_vocabulary(Path(argv[1]))
    root = Path(argv[2]).resolve()
    builtins = cetta_evaluated_heads(root / "src" / "symbol.h")
    failed = 0
    for spelling in argv[3:]:
        path = root / spelling
        violations = check(path, root, vocabulary, lib_he, builtins)
        if violations:
            failed += 1
            print(f"{spelling}: heads real PeTTa does not evaluate:")
            for head, context in violations:
                print(f"  {head} (first in {context})")
        else:
            print(f"{spelling}: real PeTTa vocabulary only")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
