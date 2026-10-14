#!/usr/bin/env python3
"""Every Lean name the C sources and the Prime fixtures cite resolves in the
Lean project.

A verdict, a record or a trace may name the Lean theorem it rests on, a
fixture of tests/prime/trinity may label an answer with the theorem it
matches, a curriculum of tests/prime/scoped may cite the theorem that states
the set face of one of its theorems, a record of tests/prime/demand or
tests/prime/symbols may name the theorem that licenses a decision, and a
program of tests/prime/causal may cite, in its comments, the theorems its
answers are instances of.  Those names are written in the C sources as string
literals, in the trinity and scoped fixtures' expected outputs as quoted
strings, in the demand and symbols fixtures' expected outputs as symbols of the
records, and in the causal fixtures' text, relative to a root below which the
cited packages live:

  Presentation.*, TowerInterpretation.*
      below Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId;
  Trinity.*, DeclarationBased.*
      below Mettapedia.Languages.MeTTa.PrimeCandidates;
  Laboratory.*, EvidenceExamples.*
      below Mettapedia.Cybernetics.DistinctionCalculus;
  DistinctionCalculus.*
      below Mettapedia.Cybernetics;
  GSLT.*
      below Mettapedia;
  Mettapedia.*  (also written Lean:Mettapedia.*)
      in full.

The script collects every such name from the string literals of src/*.c and
src/*.h (comments are skipped), from the quoted strings of
tests/prime/trinity/*.expected and tests/prime/scoped/*.expected, from the
whole text of tests/prime/demand/*.expected and tests/prime/symbols/*.expected,
and from the whole text of the programs and expected outputs of
tests/prime/causal, finds the modules that declare its last component, and writes one Lean file
that imports them and runs `#check` on the full name of each.  A name that
Lean does not know, whose declaring module is not found, or whose module is
not built from its current source fails the check.  A name whose first
component has no root here fails too, so that a new root is added on
purpose.

  check_prime_lean_citations.py --lean-root DIR --file-check COMMAND --out DIR

COMMAND checks one Lean file inside the Lean project and prints what it
prints (for instance `lake env lean`); it runs in DIR of --lean-root with
the generated file's path appended."""

import argparse
import re
import shlex
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

ROOTS = {
    "Presentation": "Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId",
    "TowerInterpretation": "Mettapedia.TypeTheory.Calculi.ParameterizedPiSigmaId",
    "Trinity": "Mettapedia.Languages.MeTTa.PrimeCandidates",
    "DeclarationBased": "Mettapedia.Languages.MeTTa.PrimeCandidates",
    "Laboratory": "Mettapedia.Cybernetics.DistinctionCalculus",
    "EvidenceExamples": "Mettapedia.Cybernetics.DistinctionCalculus",
    "DistinctionCalculus": "Mettapedia.Cybernetics",
    "GSLT": "Mettapedia",
    "Mettapedia": "",
}

NAME = re.compile(
    r"(?<![A-Za-z0-9_.])(?:Lean:)?((?:" + "|".join(ROOTS) + r")(?:\.[A-Za-z0-9_'!?]+)+)")
DECLARATION = (r"^\s*(?:@\[[^\]]*\]\s*)?(?:(?:private|protected|noncomputable|partial|"
               r"nonrec|unsafe)\s+)*(?:theorem|lemma|def|abbrev|structure|inductive|"
               r"instance|class|opaque|axiom)\s+(?:[A-Za-z0-9_'.!?«»]*\.)?{name}"
               r"(?:[^A-Za-z0-9_'!?]|$)|^\s*\|\s*{name}(?:[^A-Za-z0-9_'!?]|$)")


def string_literals(text):
    """The contents of the C string literals of `text`, outside comments."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
        elif text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
        elif text[i] == "'":
            i += 1
            while i < n and text[i] != "'":
                i += 2 if text[i] == "\\" else 1
            i += 1
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i + 1:j])
            i = j + 1
        else:
            i += 1
    return out


def cited_names():
    names = {}
    for path in sorted((ROOT / "src").glob("*.[ch]")):
        for literal in string_literals(path.read_text(errors="replace")):
            for match in NAME.finditer(literal):
                names.setdefault(match.group(1), path.name)
    for fixtures in ("tests/prime/trinity", "tests/prime/scoped", "tests/prime/profiles/megalodon_hotg"):
        for path in sorted((ROOT / fixtures).rglob("*.expected")):
            for literal in re.findall(r'"([^"\n]*)"', path.read_text(errors="replace")):
                for match in NAME.finditer(literal):
                    names.setdefault(match.group(1), path.name)
    # A demand or symbol record names its licence as a symbol, not a string.
    for fixtures in ("tests/prime/demand", "tests/prime/symbols"):
        for path in sorted((ROOT / fixtures).rglob("*.expected")):
            for match in NAME.finditer(path.read_text(errors="replace")):
                names.setdefault(match.group(1), path.name)
    # A causal program cites, in its comments, the theorems its answers instance.
    for pattern in ("*.metta", "*.expected"):
        for path in sorted((ROOT / "tests/prime/causal").rglob(pattern)):
            for match in NAME.finditer(path.read_text(errors="replace")):
                names.setdefault(match.group(1), path.name)
    return names


def full_name(name):
    root = ROOTS[name.split(".", 1)[0]]
    return f"{root}.{name}" if root else name


def declaring_modules(lean_root, full):
    """The modules that declare the name's last component, searched in the
    directory of the longest prefix of the name that is one, and in wider
    ones until some module declares it."""
    parts = full.split(".")
    pattern = DECLARATION.format(name=re.escape(parts[-1]))
    for depth in range(len(parts) - 1, 0, -1):
        search = lean_root.joinpath(*parts[:depth])
        if not search.is_dir():
            continue
        done = subprocess.run(
            ["rg", "-l", "--glob", "*.lean", "--glob", "!_archive*", "-e", pattern,
             str(search)], capture_output=True, text=True)
        paths = sorted(Path(line) for line in done.stdout.split())
        if paths:
            return [(".".join(path.relative_to(lean_root).with_suffix("").parts), path)
                    for path in paths]
    return []


def built(lean_root, module, source):
    olean = lean_root / ".lake/build/lib/lean" / Path(*module.split(".")).with_suffix(".olean")
    if not olean.exists():
        return "not built"
    if olean.stat().st_mtime < source.stat().st_mtime:
        return "built from an older source"
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lean-root", required=True)
    parser.add_argument("--file-check", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    lean_root = Path(args.lean_root).resolve()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    failures = []
    names = cited_names()
    imports = {}
    checks = []
    for name, source in sorted(names.items()):
        if name.split(".", 1)[0] not in ROOTS:
            failures.append(f"{name} ({source}): no root for its first component")
            continue
        full = full_name(name)
        modules = declaring_modules(lean_root, full)
        if not modules:
            failures.append(f"{name} ({source}): no module declares {full.split('.')[-1]}")
            continue
        for module, path in modules:
            problem = built(lean_root, module, path)
            if problem:
                failures.append(f"{name} ({source}): module {module} is {problem}")
            imports[module] = path
        checks.append((name, source, full))

    lean_file = out / "PrimeLeanCitations.lean"
    lines = [f"import {module}" for module in sorted(imports)] + [""]
    line_of = {}
    for name, source, full in checks:
        lines.append(f"#check @{full}")
        line_of[len(lines)] = (name, source)
    lean_file.write_text("\n".join(lines) + "\n")

    if not failures:
        done = subprocess.run(
            shlex.split(args.file_check) + [str(lean_file.resolve())],
            cwd=lean_root, capture_output=True, text=True)
        (out / "lean.out").write_text(done.stdout + done.stderr)
        error = re.compile(re.escape(str(lean_file.resolve())) + r":(\d+):\d+: error")
        failed_lines = {int(m.group(1)) for m in error.finditer(done.stdout + done.stderr)}
        for line in sorted(failed_lines):
            name, source = line_of.get(line, (f"line {line}", "the imports"))
            failures.append(f"{name} ({source}): Lean does not resolve it")
        if done.returncode != 0 and not failed_lines:
            failures.append(f"the Lean check exited {done.returncode} without a located error")

    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        print(f"Lean citations: {len(failures)} of {len(names)} names fail")
        return 1
    print(f"PASS: the {len(checks)} Lean names the C sources and the Prime fixtures cite resolve "
          f"({len(imports)} modules imported)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
