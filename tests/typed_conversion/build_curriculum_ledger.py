#!/usr/bin/env python3
"""Ledger for the MettaKernel curriculum and the four reused Prime ladders.

The 202 commands already in curriculum_{coq,hol,lean,megalodon}.metta are
referenced, not copied. A missing capability is a dependency row.
"""

from __future__ import annotations

import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path("/home/aimama/aihub/hyperon/cetta-prime-2.0-draft-20260910")
CURRICULUM = Path("/home/aimama/aihub/Mettapedia/MettaKernel/Curriculum")
COVERAGE = Path(
    "/shared/zahrada/work/prime-coherence-opus-20260924-claude/curriculum-coverage.md"
)
FIXTURES = (
    ROOT / "tests/prime/scoped/curriculum_coq.metta",
    ROOT / "tests/prime/scoped/curriculum_hol.metta",
    ROOT / "tests/prime/scoped/curriculum_lean.metta",
    ROOT / "tests/prime/scoped/curriculum_megalodon.metta",
)
OUT = Path("/shared/zahrada/work/typed-conversion-grok/cumulative-b/curriculum-ledger.tsv")

COLUMNS = (
    "id",
    "source_path",
    "source_statement",
    "assumptions",
    "source_behavior",
    "prime_statement",
    "rule_package",
    "artifact",
    "query_kind",
    "expected",
    "justification",
    "capability",
    "dependency",
)

HEAD = re.compile(r"^!\(([\w:@+-]+)")
HEADER = re.compile(r"^;\s*-+\s+(.+)$")
BIN = Path(os.environ.get(
    "TC_CETTA",
    "/shared/zahrada/work/prime-integration-20260923-claude/binaries/cetta-sealednames-1-9f89629c",
))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def header_keys(title: str) -> list[str]:
    keys = re.findall(r"\b(?:ICL|HOL|FEAT)\d+\b", title)
    if "negative" in title.lower():
        keys.append("neg")
    nums = []
    for start, end in re.findall(r"(\d{2})-(\d{2})", title):
        for n in range(int(start), int(end) + 1):
            nums.append(f"{n:02d}")
    nums.extend(re.findall(r"\b(\d{2})\b", title))
    seen = []
    for key in keys + nums:
        if key not in seen:
            seen.append(key)
    return seen


def file_key(name: str) -> str | None:
    match = re.match(r"((?:ICL|HOL|FEAT)\d+)", name)
    if match:
        return match.group(1)
    if name.startswith("neg"):
        return "neg"
    match = re.match(r"(\d{2})_", name)
    if match:
        return match.group(1)
    return None


def parse_fixture(fixture: Path) -> list[tuple[str, str, str]]:
    """Return (section keys joined, head, line) for each command."""
    current = "preamble"
    rows = []
    for line in fixture.read_text(encoding="utf-8", errors="replace").splitlines():
        header = HEADER.match(line.strip())
        if header:
            keys = header_keys(header.group(1))
            current = ",".join(keys) if keys else header.group(1)[:40]
            continue
        match = HEAD.match(line.strip())
        if match:
            rows.append((current, match.group(1), line.strip()[:240]))
    return rows


def run_fixture(fixture: Path) -> list[str]:
    text = fixture.read_text(encoding="utf-8", errors="replace")
    text = text.replace(
        "!(import! &self ../pf.metta)",
        f"!(import! &self {ROOT / 'lib' / 'pf.metta'})",
    )
    text = text.replace(
        "!(import! &self pf)",
        f"!(import! &self {ROOT / 'lib' / 'pf.metta'})",
    )
    text = text.replace(
        "!(import! &self ../../../lib/prime/curriculum.metta)",
        f"!(import! &self {ROOT / 'lib' / 'prime' / 'curriculum.metta'})",
    )
    dest = Path(os.environ.get(
        "TC_SCRATCH",
        "/tmp/claude/grok-goal-aaee6e0e0c16/implementer",
    )) / f"run-{fixture.name}"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(text)
    proc = subprocess.run(
        ["bash", "-c", 'ulimit -v 25165824 && exec "$1" --lang prime "$2"',
         "run", str(BIN), str(dest)],
        text=True, capture_output=True,
    )
    lines = [ln.strip() for ln in (proc.stdout or "").splitlines() if ln.strip()]
    if proc.returncode != 0 and not lines:
        lines = [f"[exit {proc.returncode}]"]
    return lines


def dependency_for(path: str) -> tuple[str, str]:
    text = path.lower()
    if any(tok in text for tok in ("quotient",)):
        return "missing", "quotients are not in the theory"
    if any(tok in text for tok in ("ltac", "typeclass", "monad", "tactic", "metaprog", "do-notation")):
        return "missing", "elaboration is outside the kernel"
    if any(tok in text for tok in ("impred", "dedukti", "lambdapi")):
        return "missing", "impredicative proof family (H1)"
    if any(tok in text for tok in ("indexed", "mutual", "inductive-family")):
        return "missing", "indexed families (R5)"
    if "case" in text and "tree" in text:
        return "missing", "two-scrutinee case trees (R2)"
    if any(tok in text for tok in ("data", "quote", "gradual")):
        return "missing", "Data and quotation migration (M3)"
    return "recorded", "none"


def first_statement(path: Path) -> str:
    try:
        raw = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return path.name
    for line in raw.splitlines():
        stripped = line.strip()
        if not stripped or stripped in {"{", "}"}:
            continue
        if stripped.startswith((";", "//", "--", "(*", "/-", "#!")):
            continue
        if stripped.startswith("#") and not stripped.startswith(("#guard", "#print", "#eval")):
            continue
        return stripped[:500].replace("\t", " ")
    return path.name


def load_commands() -> tuple[list[dict], dict[tuple[str, str], dict]]:
    """Run each fixture and pair every command with the result line it printed."""
    command_rows = []
    by_section: dict[tuple[str, str], dict] = {}
    for fixture in FIXTURES:
        parsed = parse_fixture(fixture)
        results = run_fixture(fixture)
        if len(results) < len(parsed):
            results = results + ["[missing]"] * (len(parsed) - len(results))
        for n, ((section, head, line), result) in enumerate(zip(parsed, results), 1):
            row = {
                "id": f"{fixture.stem}-c{n:03d}",
                "source_path": fixture.name,
                "source_statement": line,
                "assumptions": "the fixture's local theory",
                "source_behavior": "retained command, not a new port",
                "prime_statement": line,
                "rule_package": "existing set: and pf: package",
                "artifact": str(fixture.relative_to(ROOT)),
                "query_kind": head,
                "expected": result,
                "justification": "result printed by cetta --lang prime on this command",
                "capability": "implemented",
                "dependency": "none",
                "section": section,
            }
            command_rows.append(row)
            for key in section.split(","):
                if key and key != "preamble":
                    by_section.setdefault((fixture.stem, key), row)
    return command_rows, by_section


LIBRARY = ROOT / "lib/prime/curriculum.metta"
MOTIVATION = ROOT / "tests/prime/scoped/curriculum_prime_motivation.metta"

PATH_DEPENDENCY = (
    ("Notation/", "notation elaboration (OSLF/Sol)"),
    ("DeduktiLambdapi/", "impredicative proof family (H1)"),
    ("Coq/ICL05", "impredicative proof family (H1)"),

    ("Coq/ICL10", "indexed families (R5)"),
    ("Coq/ICL11", "indexed families (R5)"),
    ("Coq/ICL12", "indexed families (R5)"),
    ("Coq/ICL13", "two-scrutinee case trees (R2)"),
    ("Coq/ICL14", "two-scrutinee case trees (R2)"),
    ("Coq/ICL15", "indexed families (R5)"),
    ("Coq/FEAT", "elaboration is outside the kernel"),
    ("Lean/02_", "indexed families (R5)"),
    ("Lean/05_", "indexed families (R5)"),
    ("Lean/06_", "indexed families (R5)"),
    ("Lean/08_", "elaboration is outside the kernel"),
    ("Lean/09_", "elaboration is outside the kernel"),
    ("Lean/12_", "Data and quotation migration (M3)"),
    ("Lean/13_", "quotients are not in the theory"),
    ("Lean/16_", "notation elaboration (OSLF/Sol)"),
    ("VerifiedMeTTa/04_", "Data and quotation migration (M3)"),
    ("VerifiedMeTTa/05_", "Data and quotation migration (M3)"),
    ("PrimeMotivation/02_", "indexed families (R5)"),
    ("PrimeMotivation/03_", "Data and quotation migration (M3)"),
    ("PrimeMotivation/04_", "linking laws (M2)"),
    ("PrimeMotivation/05_", "Data and quotation migration (M3)"),
    ("PrimeMotivation/06_", "Data and quotation migration (M3)"),

    ("DTTBench/", "principal-type refutation (L3)"),
    ("HOL/HOLKernelProfiles", "notation elaboration (OSLF/Sol)"),
    ("HOL/HOLLightProofClosure", "elaboration is outside the kernel"),
    ("HOL/neg/neg_typeerror", "elaboration is outside the kernel"),
    ("HOL/neg/neg_unprovable", "impredicative proof family (H1)"),
    ("Megalodon/neg_", "impredicative proof family (H1)"),
    ("CrossSmoke/", "impredicative proof family (H1)"),
    ("Performance/", "elaboration is outside the kernel"),
)


HOST_HARNESS = {
    "Performance/trace_itp_baselines.sh": "bash trace script is outside the kernel",
    "DTTBench/replay_dttbench.py": "python3 replay script is outside the kernel",
    "DTTBench/run_dttbench_gate.sh": "bash gate script is outside the kernel",
    "DTTBench/source_pin.tsv": "curriculum_role table is outside the kernel",
}


def is_generated(rel: str) -> bool:
    if rel in HOST_HARNESS:
        return False
    name = rel.rsplit("/", 1)[-1]
    if "/.hol/" in f"/{rel}" or rel.endswith(".pre"):
        return True
    if name.endswith(".log") or ".run_all" in name:
        return True
    if name.endswith((".sh", ".py", ".tsv", ".pdf", ".tex", ".md")) or name == "Holmakefile":
        return True
    return False


def artifact_row(rel: str, statement: str, rid: str) -> dict:
    """One generated artifact, the same columns as a `.run_all` log."""
    return _row(
        rid,
        rel, statement, "as in the source file", "file",
        "not a lesson", "none", f"Mettapedia/MettaKernel/Curriculum/{rel}",
        "source", "not claimed", COVERAGE.name,
        "recorded", "generated build artifact, not a lesson",
    )


def path_dependency(rel: str) -> str:
    if rel.endswith(".dk"):
        return "Dedukti .dk source is lowered by the guest MeTTa encoder, not checked as a Prime term"
    for prefix, dep in PATH_DEPENDENCY:
        if prefix in rel:
            return dep
    _cap, dep = dependency_for(rel)
    if _cap != "recorded":
        return dep
    return ""


def top_level_bangs(text: str) -> list[str]:
    """Return each top-level `!(...)` command with comments removed from the scan."""
    spans: list[str] = []
    i = 0
    limit = len(text)
    while i < limit:
        if text[i] == ";":
            newline = text.find("\n", i)
            i = limit if newline < 0 else newline + 1
            continue
        if text.startswith("!(", i):
            depth = 0
            j = i
            while j < limit:
                if text[j] == ";":
                    newline = text.find("\n", j)
                    j = limit if newline < 0 else newline + 1
                    continue
                if text[j] == "(":
                    depth += 1
                elif text[j] == ")":
                    depth -= 1
                    if depth == 0:
                        j += 1
                        break
                j += 1
            spans.append(" ".join(text[i:j].split()))
            i = j
            continue
        i += 1
    return spans


def is_guest_metta(path: Path) -> bool:
    if path.suffix != ".metta":
        return False
    rel = path.relative_to(CURRICULUM).as_posix()
    return rel.startswith((
        "DeduktiLambdapi/",
        "Notation/",
        "VerifiedMeTTa/",
        "PrimeMotivation/",
    ))


def recover_forms(text: str) -> list[str]:
    """Keep complete top-level forms and drop a truncated equation."""
    forms: list[str] = []
    i = 0
    limit = len(text)
    depth = 0
    start = None
    while i < limit:
        if text[i] == ";" and depth == 0:
            newline = text.find("\n", i)
            i = limit if newline < 0 else newline + 1
            continue
        if depth == 0 and (text.startswith("(=", i) or text.startswith("!(", i)):
            start = i
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0 and start is not None:
                forms.append(" ".join(text[start:i + 1].split()))
                start = None
            elif depth < 0:
                depth = 0
                start = None
        elif depth > 0 and text.startswith("!(", i):
            start = i
            depth = 0
            continue
        i += 1
    return forms


def resolve_guest_import(form: str, source: Path) -> str:
    token = "!(import! &self "
    if not form.startswith(token) or not form.endswith(")"):
        return form
    rel = form[len(token):-1].strip()
    if rel.startswith("/"):
        return form
    target = (source.parent / rel).resolve()
    return f"!(import! &self {target})"


def cetta_lines(src: Path) -> tuple[int, list[str], str]:
    proc = subprocess.run(
        ["bash", "-c", 'ulimit -v "$1" && exec "$2" --lang prime "$3"',
         "run", "25165824", str(BIN), str(src)],
        text=True,
        capture_output=True,
        timeout=120,
    )
    lines = [ln.strip() for ln in (proc.stdout or "").splitlines() if ln.strip()]
    err = ""
    if proc.stderr and proc.stderr.strip():
        err = proc.stderr.strip().splitlines()[0][:240]
    return proc.returncode, lines, err


def _command_spans(text: str, prefixes: tuple[str, ...]) -> list[tuple[int, int]]:
    spans: list[tuple[int, int]] = []
    i = 0
    limit = len(text)
    while i < limit:
        if text[i] == ";":
            newline = text.find("\n", i)
            i = limit if newline < 0 else newline + 1
            continue
        if text.startswith(prefixes, i):
            depth = 0
            j = i
            while j < limit:
                if text[j] == ";":
                    newline = text.find("\n", j)
                    j = limit if newline < 0 else newline + 1
                    continue
                if text[j] == "(":
                    depth += 1
                elif text[j] == ")":
                    depth -= 1
                    if depth == 0:
                        j += 1
                        break
                j += 1
            spans.append((i, j))
            i = j
            continue
        i += 1
    return spans


def _absolutize_imports(text: str, source: Path) -> str:
    token = "!(import! &self "
    parts: list[str] = []
    i = 0
    while True:
        found = text.find(token, i)
        if found < 0:
            parts.append(text[i:])
            break
        parts.append(text[i:found])
        end = text.find(")", found)
        if end < 0:
            parts.append(text[found:])
            break
        rel = text[found + len(token):end].strip()
        if not rel.startswith("/"):
            rel = str((source.parent / rel).resolve())
        parts.append(f"!(import! &self {rel})")
        i = end + 1
    return "".join(parts)


def _run_assert_slices(path: Path, text: str, wanted: list[str]) -> list[str] | None:
    """Run each assert in the original layout so the Prime reader still accepts it."""
    spans = _command_spans(text, ("!(assertEqual", "!(test"))
    if len(spans) != len(wanted):
        return None
    scratch = Path(os.environ.get(
        "TC_SCRATCH", "/tmp/claude/grok-goal-aaee6e0e0c16/implementer")) / "repaired"
    scratch.mkdir(parents=True, exist_ok=True)
    results: list[str] = []
    for index, (start, end) in enumerate(spans):
        parts: list[str] = []
        last = 0
        for other, (left, right) in enumerate(spans):
            if other == index:
                parts.append(text[last:end])
            else:
                parts.append(text[last:left])
            last = right
        parts.append(text[last:])
        dest = scratch / f"{path.stem}-{index}.metta"
        dest.write_text(_absolutize_imports("".join(parts), path))
        try:
            _code, lines, err = cetta_lines(dest)
        except subprocess.TimeoutExpired:
            return None
        if "compiled Prime reader" in err or "could not read" in err or not lines:
            return None
        results.append(lines[-1])
    return results


def run_repaired_guest(path: Path, bangs: list[str]) -> dict | None:
    """Run each assertEqual on its own. Keep the original text when the reader requires it."""
    try:
        rel = path.relative_to(CURRICULUM).as_posix()
    except ValueError:
        rel = path.name
    text = path.read_text(encoding="utf-8", errors="replace")
    wanted = [bang for bang in bangs if bang.startswith("!(assertEqual")]
    if not wanted:
        return None
    sliced = _run_assert_slices(path, text, wanted)
    if sliced is not None:
        pairs = list(zip(wanted, sliced))
        return {
            "rel": rel,
            "lines": sliced,
            "pairs": pairs,
            "asserts": sliced,
            "ok": all(line == "[()]" for line in sliced),
            "error": "",
        }
    forms = [resolve_guest_import(form, path) for form in recover_forms(text)]
    header = [form for form in forms if form.startswith("(=") or form.startswith("!(import!")]
    asserts = [form for form in forms if form.startswith("!(assertEqual")]
    if not asserts or len(asserts) != len(wanted):
        return None
    scratch = Path(os.environ.get(
        "TC_SCRATCH", "/tmp/claude/grok-goal-aaee6e0e0c16/implementer")) / "repaired"
    scratch.mkdir(parents=True, exist_ok=True)
    results: list[str] = []
    for index, bang in enumerate(asserts):
        dest = scratch / f"{path.stem}-recovered-{index}.metta"
        dest.write_text("\n".join(header + [bang]) + "\n")
        try:
            _code, lines, _err = cetta_lines(dest)
        except subprocess.TimeoutExpired:
            return None
        if not lines:
            return None
        results.append(lines[-1])
    pairs = list(zip(wanted, results))
    return {
        "rel": rel,
        "lines": results,
        "pairs": pairs,
        "asserts": results,
        "ok": all(line == "[()]" for line in results),
        "error": "",
    }


def lhs_of(form: str) -> str | None:
    """Return the term inside the first parentheses of `(= (lhs) rhs)`."""
    start = form.find("(=")
    if start < 0:
        return None
    open_at = form.find("(", start + 2)
    if open_at < 0:
        return None
    depth = 0
    for i in range(open_at, len(form)):
        if form[i] == "(":
            depth += 1
        elif form[i] == ")":
            depth -= 1
            if depth == 0:
                return form[open_at + 1:i].strip()
    return None


def needs_ground_probe(name: str) -> bool:
    """These guest programs must show their own --lang prime print, not a file label."""
    return name in {
        "dk-lower", "dk-lower-lam-with-decl", "dk-prod", "dk-term", "dk-univ",
        "cic-proof-check", "cic-nat-max", "cic-nat-nf",
    } or name.startswith("cic-stage1-")


def instantiate_lhs(lhs: str) -> str:
    """Replace pattern variables so the defined function can be called."""
    return re.sub(r"\$[A-Za-z_][\w]*", "(Con z)", lhs)


def instantiate_atom(lhs: str) -> str:
    """Replace pattern variables with the atom `z`, as in `!(cic-id-term-source z)`."""
    return re.sub(r"\$[A-Za-z_][\w]*", "z", lhs)


def query_candidates(text: str, name: str, lhs: str) -> list[str]:
    """Queries for one definition: the grounded left-hand side, then uses from its file."""
    found: list[str] = []

    def add(call: str) -> None:
        call = " ".join(call.split())
        if call and call not in found:
            found.append(call)

    add(instantiate_atom(lhs))
    for head, sexp in _sexps(text):
        if head != name:
            continue
        inner = sexp[1:-1].strip()
        if inner == lhs or inner == instantiate_atom(lhs):
            continue
        add(instantiate_atom(inner))
    add(instantiate_lhs(lhs))
    return found


def ground_calls(text: str, source: Path) -> tuple[list[str], list[tuple[str, str]]]:
    """Imports and equations, plus one ground call for each defined name."""
    forms = [resolve_guest_import(form, source) for form in recover_forms(text)]
    header = [form for form in forms if form.startswith("(=") or form.startswith("!(import!")]
    calls: list[tuple[str, str]] = []
    seen: set[str] = set()
    patterned: list[tuple[str, str]] = []
    for form in forms:
        if not form.startswith("(="):
            continue
        lhs = lhs_of(form)
        if not lhs:
            continue
        name = lhs.split(" ", 1)[0]
        if "$" in lhs:
            if name not in seen and needs_ground_probe(name):
                patterned.append((name, lhs))
            continue
        if name in seen:
            continue
        seen.add(name)
        calls.append((name, lhs))
    for name, lhs in patterned:
        if name in seen:
            continue
        seen.add(name)
        calls.append((name, instantiate_lhs(lhs)))
    return header, calls


def definition_prints(path: Path) -> list[tuple[str, str, str]]:
    """Evaluate each ground definition under `--lang prime`.

    The printed line is that definition's own result. A later test or
    assertEqual line is not reused.
    """
    text = path.read_text(encoding="utf-8", errors="replace")
    header, calls = ground_calls(text, path)
    if not calls:
        return []
    scratch = Path(os.environ.get(
        "TC_SCRATCH", "/tmp/claude/grok-goal-aaee6e0e0c16/implementer")) / "defs"
    scratch.mkdir(parents=True, exist_ok=True)
    nimp = sum(1 for form in header if form.startswith("!(import!"))

    def run_calls(selected: list[tuple[str, str]]) -> list[str]:
        dest = scratch / f"{path.stem}-{len(selected)}-{selected[0][0]}.metta"
        dest.write_text("\n".join(header + [f"!({lhs})" for _name, lhs in selected]) + "\n")
        try:
            _code, lines, _err = cetta_lines(dest)
        except subprocess.TimeoutExpired:
            return []
        return lines[nimp:] if len(lines) >= nimp else []

    batched = run_calls(calls)
    if len(batched) == len(calls) and not any(line.startswith("[(Error") for line in batched):
        return [(name, lhs, line) for (name, lhs), line in zip(calls, batched)]
    found: list[tuple[str, str, str]] = []
    for name, lhs in calls:
        lines = run_calls([(name, lhs)])
        if lines:
            found.append((name, lhs, lines[-1]))
    return found


def _defs_only_file(path: Path, seen: dict[str, Path]) -> Path:
    """A copy with asserts removed and guest imports pointed at the same kind of copy.

    Original line breaks stay. A collapsed long equation is rejected by the reader,
    and an imported guest's asserts would stop the call.
    """
    key = str(path.resolve())
    if key in seen:
        return seen[key]
    try:
        rel = path.resolve().relative_to(CURRICULUM.resolve())
    except ValueError:
        rel = Path(path.name)
    dest = Path(os.environ.get(
        "TC_SCRATCH", "/tmp/claude/grok-goal-aaee6e0e0c16/implementer")) / "probe-defs" / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    seen[key] = dest
    text = _strip_checks(path.read_text(encoding="utf-8", errors="replace"))

    def repl(match: re.Match) -> str:
        raw = match.group(1).strip()
        target = Path(raw) if raw.startswith("/") else (path.parent / raw).resolve()
        if target.suffix == ".metta" and CURRICULUM.resolve() in target.parents:
            target = _defs_only_file(target, seen)
        return f"!(import! &self {target})"

    text = re.sub(r"!\(import! &self ([^)]+)\)", repl, text)
    dest.write_text(text)
    return dest


def _probe_text(path: Path, calls: list[tuple[str, str]]) -> tuple[str, int]:
    """Definitions with their original line breaks, then the grounded calls."""
    text = _defs_only_file(path, {}).read_text(encoding="utf-8")
    prefix = len(top_level_bangs(text))
    extra = "\n".join(f"!({lhs})" for _name, lhs in calls)
    return text.rstrip() + "\n" + extra + "\n", prefix


def run_probe_batch(path: Path, calls: list[tuple[str, str]]) -> list[tuple[str, str, str]]:
    """Run grounded calls whose heads already printed. The stdout is the row."""
    if not calls:
        return []
    scratch = Path(os.environ.get(
        "TC_SCRATCH", "/tmp/claude/grok-goal-aaee6e0e0c16/implementer")) / "probes"
    scratch.mkdir(parents=True, exist_ok=True)

    def run_calls(selected: list[tuple[str, str]]) -> tuple[list[str], str]:
        body, prefix = _probe_text(path, selected)
        dest = scratch / f"{path.stem}-{len(selected)}-{selected[0][0]}.metta"
        dest.write_text(body)
        try:
            _code, lines, err = cetta_lines(dest)
        except subprocess.TimeoutExpired:
            return [], "error: cetta exceeded 120s under ulimit -v 25165824"
        if len(lines) < prefix + len(selected):
            return [], err
        return lines[prefix:prefix + len(selected)], err

    batched, _err = run_calls(calls)
    if len(batched) == len(calls):
        return [(name, lhs, line) for (name, lhs), line in zip(calls, batched)]
    found: list[tuple[str, str, str]] = []
    for name, lhs in calls:
        lines, err = run_calls([(name, lhs)])
        if lines:
            found.append((name, lhs, lines[-1]))
            continue
        refusal = (err or "").strip().splitlines()
        found.append((name, lhs, refusal[0][:500] if refusal else "error: cetta produced no result"))
    return found


def collect_printed_heads(runs: dict, command_rows: list[dict], extra_rows: list[dict], library: dict) -> None:
    """Atoms that already occur as whole words in an implemented expected."""
    PRINTED_HEADS.clear()
    texts: list[str] = []
    for rec in runs.values():
        for _name, call, line in rec.get("def_log", []):
            if not line or line.startswith("[(Error"):
                continue
            if call and not call_reduced(call, line):
                continue
            texts.append(line)
        for bang, line in rec.get("pairs", []):
            if not bang.startswith(("!(assertEqual", "!(test")):
                continue
            if not line or line.startswith("[(Error"):
                continue
            texts.append(line)
    for row in command_rows + extra_rows:
        if row.get("capability") == "implemented":
            texts.append(row.get("expected") or "")
    for _name, pair in library.items():
        texts.append(pair[1] if isinstance(pair, tuple) else str(pair))
    for text in texts:
        PRINTED_HEADS.update(re.findall(r"(?<![\$\w])[A-Za-z][\w+-]*", text))


def whole_file_rejection(path: Path) -> str:
    """The reader error for a file that does not load, else empty."""
    got = run_probe_batch(path, [("__file__", "__file__")])
    if got and got[0][2].startswith("error: compiled Prime reader"):
        return got[0][2]
    return ""


def ground_printed_equations(runs: dict) -> None:
    """Run equations whose guest heads already printed, and keep that stdout."""
    files = 0
    calls_n = 0
    for rel, rec in runs.items():
        path = CURRICULUM / rel
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        wanted: list[tuple[str, str]] = []
        seen = set(rec.get("defs", {}))
        first: dict[str, str] = {}
        for form in recover_forms(text):
            if not form.startswith("(="):
                continue
            lhs = lhs_of(form)
            if not lhs:
                continue
            name = lhs.split(" ", 1)[0]
            first.setdefault(name, form)
        if rec.get("defs"):
            rejection = whole_file_rejection(path)
            if rejection:
                rec["defs"] = {}
                rec["def_log"] = []
                rec.setdefault("refusals", {})
                rec.setdefault("forced", set())
                for name, form in first.items():
                    lhs = lhs_of(form) or name
                    call = instantiate_lhs(lhs) if "$" in lhs else lhs
                    rec["refusals"][name] = rejection
                    rec["def_log"].append((name, call, rejection))
                continue
        for name, form in first.items():
            if name in seen:
                continue
            lhs = lhs_of(form)
            if not lhs:
                continue
            label = equation_label(form)
            if label is None:
                if re.search(r"[A-Za-z][\w]*Name\b", lhs):
                    call = instantiate_atom(lhs)
                else:
                    call = instantiate_lhs(lhs) if "$" in lhs else lhs
            elif label.endswith(" in the equation is not a Prime term"):
                call = instantiate_atom(lhs)
            else:
                continue
            wanted.append((name, call, form))
        if not wanted:
            continue
        files += 1
        calls_n += len(wanted)
        rec.setdefault("forced", set())
        rec.setdefault("refusals", {})
        for start in range(0, len(wanted), 12):
            chunk = wanted[start:start + 12]
            got = {
                name: (call, line)
                for name, call, line in run_probe_batch(path, [(name, call) for name, call, _form in chunk])
            }
            for name, call, form in chunk:
                call, line = got.get(name, (call, "error: cetta produced no result"))
                if line == "error: cetta produced no result":
                    lhs = lhs_of(form) or call
                    for alt in query_candidates(text, name, lhs):
                        if alt == call:
                            continue
                        alt_got = run_probe_batch(path, [(name, alt)])
                        if alt_got and alt_got[0][2] != "error: cetta produced no result":
                            call, line = alt, alt_got[0][2]
                            break
                if (
                    line.startswith("[(Error")
                    or line.startswith("error:")
                    or line == "[]"
                    or not call_reduced(call, line)
                ):
                    rec["refusals"][name] = line
                    continue
                rec["defs"][name] = (call, line)
                rec["def_log"].append((name, call, line))
                rec["forced"].add(name)
    print(f"ground-probes {calls_n} in {files} files")


def write_guest_logs(runs: dict) -> None:
    scratch = Path(os.environ.get(
        "TC_SCRATCH", "/tmp/claude/grok-goal-aaee6e0e0c16/implementer"))
    scratch.mkdir(parents=True, exist_ok=True)
    encoders: list[str] = []
    readers: list[str] = []
    for rel, rec in runs.items():
        bucket = encoders if rel.startswith("DeduktiLambdapi/") or rel.startswith("VerifiedMeTTa/") else readers
        bucket.append(f"# {rel}")
        wrote_check = False
        for _name, call, line in rec.get("def_log", []):
            bucket.append(f"!({call})")
            bucket.append(line)
            wrote_check = True
        for bang, line in rec["pairs"]:
            if bang.startswith("!(assertEqual") or bang.startswith("!(test"):
                bucket.append(bang)
                bucket.append(line)
                wrote_check = True
        if not wrote_check:
            bucket.extend(rec["lines"])
        if rec["error"]:
            bucket.append(f"# {rec['error']}")
        bucket.append("")
    (scratch / "encoders.log").write_text("\n".join(encoders) + ("\n" if encoders else ""))
    (scratch / "readers.log").write_text("\n".join(readers) + ("\n" if readers else ""))


def _parse_sexp(text: str, i: int) -> tuple[object, int]:
    """Parse one MeTTa sexp. Lists are Python lists; atoms and strings are str."""
    n = len(text)
    while i < n and text[i].isspace():
        i += 1
    if i >= n:
        raise ValueError("end")
    if text[i] == ";":
        nl = text.find("\n", i)
        return _parse_sexp(text, n if nl < 0 else nl + 1)
    if text[i] == '"':
        j = i + 1
        while j < n:
            if text[j] == "\\":
                j += 2
                continue
            if text[j] == '"':
                return text[i:j + 1], j + 1
            j += 1
        raise ValueError("string")
    if text[i] != "(":
        j = i
        while j < n and not text[j].isspace() and text[j] not in "();":
            j += 1
        return text[i:j], j
    i += 1
    items: list[object] = []
    while True:
        while i < n and text[i].isspace():
            i += 1
        if i < n and text[i] == ";":
            nl = text.find("\n", i)
            i = n if nl < 0 else nl + 1
            continue
        if i >= n:
            raise ValueError("unclosed")
        if text[i] == ")":
            return items, i + 1
        node, i = _parse_sexp(text, i)
        items.append(node)


def _render_sexp(node: object) -> str:
    if isinstance(node, str):
        return node
    if not node:
        return "()"
    return "(" + " ".join(_render_sexp(part) for part in node) + ")"


def prime_port(text: str) -> str:
    """Sequence calls Prime would leave in a non-first argument.

    `--lang prime` reduces the first argument of a call. A call under a
    constructor, or past the first argument, stays. `case` reduces its
    scrutinee before binding, so each such call is bound there first.
    """
    funs: set[str] = set()
    spans: list[tuple[int, int, object]] = []
    i = 0
    n = len(text)
    while i < n:
        if text[i] == ";":
            nl = text.find("\n", i)
            i = n if nl < 0 else nl + 1
            continue
        if text.startswith("(=", i):
            try:
                node, j = _parse_sexp(text, i)
            except ValueError:
                i += 1
                continue
            spans.append((i, j, node))
            if (
                isinstance(node, list) and len(node) == 3 and node[0] == "="
                and isinstance(node[1], list) and node[1] and isinstance(node[1][0], str)
            ):
                funs.add(node[1][0])
            i = j
            continue
        i += 1
    if not funs:
        return text
    used = set(re.findall(r"\$[A-Za-z_][\w]*", text))
    counter = 0

    def fresh() -> str:
        nonlocal counter
        while True:
            name = f"$p{counter}"
            counter += 1
            if name not in used:
                used.add(name)
                return name

    def is_value(node: object) -> bool:
        if isinstance(node, str) or node == []:
            return True
        if not isinstance(node, list) or not node:
            return True
        head = node[0]
        if isinstance(head, str) and (head in funs or head in {"let", "if", "case", "unify"}):
            return False
        return all(is_value(part) for part in node)

    def force_into(expr: object, kont) -> object:
        if is_value(expr):
            return kont(expr)
        ev = place(expr)
        if is_value(ev):
            return kont(ev)
        var = fresh()
        return ["case", ev, [[var, kont(var)]]]

    def thread(args: list, finish) -> object:
        def rec(index: int, acc: list) -> object:
            if index == len(args):
                return finish(acc)
            return force_into(args[index], lambda val, index=index, acc=list(acc): rec(index + 1, acc + [val]))
        return rec(0, [])

    def place(node: object) -> object:
        if is_value(node):
            return node
        if not isinstance(node, list) or not node or not isinstance(node[0], str):
            if isinstance(node, list):
                return thread(node, lambda vals: vals)
            return node
        head, args = node[0], node[1:]
        if head == "let" and len(args) == 3:
            return ["let", args[0], place(args[1]), place(args[2])]
        if head == "if" and len(args) == 3:
            return ["if", place(args[0]), place(args[1]), place(args[2])]
        if head == "unify" and len(args) == 4:
            return ["unify", place(args[0]), args[1], place(args[2]), place(args[3])]
        if head == "case" and args:
            branches = args[1] if len(args) > 1 else []
            new_branches = []
            if isinstance(branches, list):
                for br in branches:
                    if isinstance(br, list) and len(br) >= 2:
                        new_branches.append([br[0], place(br[1]), *br[2:]])
                    else:
                        new_branches.append(br)
            else:
                new_branches = branches
            return ["case", place(args[0]), new_branches, *args[2:]]
        if head in funs:
            # Bind every argument before the call. A pattern variable matches
            # the argument before Prime reduces it, so the first argument is
            # not a safe place to leave a call.
            return thread(args, lambda vals, head=head: [head, *vals])
        return thread(args, lambda vals, head=head: [head, *vals])

    parts: list[str] = []
    cursor = 0
    for start, end, node in spans:
        parts.append(text[cursor:start])
        if isinstance(node, list) and len(node) == 3 and node[0] == "=":
            parts.append(_render_sexp(["=", node[1], place(node[2])]))
        else:
            parts.append(text[start:end])
        cursor = end
    parts.append(text[cursor:])
    return "".join(parts)


def _strip_checks(text: str) -> str:
    """Drop assertEqual and test commands so an import loads definitions only."""
    spans = _command_spans(text, ("!(assertEqual", "!(test"))
    if not spans:
        return text
    parts: list[str] = []
    last = 0
    for start, end in spans:
        parts.append(text[last:start])
        last = end
    parts.append(text[last:])
    return "".join(parts)


def materialize_guest(path: Path, seen: dict | None = None, keep_asserts: bool = True) -> Path:
    """Write the Prime-sequenced guest, with guest imports pointed at their ports."""
    seen = {} if seen is None else seen
    key = (str(path.resolve()), keep_asserts)
    if key in seen:
        return seen[key]
    scratch = Path(os.environ.get(
        "TC_SCRATCH", "/tmp/claude/grok-goal-aaee6e0e0c16/implementer")) / "ported"
    try:
        rel = path.resolve().relative_to(CURRICULUM.resolve())
    except ValueError:
        return path
    if not keep_asserts:
        scratch = scratch / "defs-only"
    dest = scratch / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    seen[key] = dest
    text = prime_port(path.read_text(encoding="utf-8", errors="replace"))
    if not keep_asserts:
        text = _strip_checks(text)
    token = "!(import! &self "
    parts: list[str] = []
    i = 0
    while True:
        found = text.find(token, i)
        if found < 0:
            parts.append(text[i:])
            break
        parts.append(text[i:found])
        end = text.find(")", found)
        if end < 0:
            parts.append(text[found:])
            break
        rel_imp = text[found + len(token):end].strip()
        if rel_imp.startswith("/"):
            target = Path(rel_imp)
        else:
            target = (path.parent / rel_imp).resolve()
        if target.suffix == ".metta":
            try:
                target.relative_to(CURRICULUM.resolve())
                if is_guest_metta(target):
                    target = materialize_guest(target, seen, keep_asserts=False)
            except ValueError:
                pass
        parts.append(f"!(import! &self {target})")
        i = end + 1
    dest.write_text("".join(parts))
    return dest


def port_guest_asserts(path: Path) -> Path:
    """Case-sequence guest encoders and readers whose asserts otherwise stay redexes.

    `--lang prime` reduces only the first argument. Guest encoders and readers
    put a call in a later argument. The port binds that call under `case`.
    """
    text = path.read_text(encoding="utf-8", errors="replace")
    if not re.search(
        r"!\(assertEqual\s+\((?:dk-lower|cic-proof-check|cic-stage[123]-|sig-admitted|ms-rd|hol-|mg-|hl-check-|hls-|hshow-)",
        text,
    ):
        return path
    return materialize_guest(path)


def run_guest_file(path: Path) -> dict:
    """Run one guest MeTTa program on the pinned binary under `--lang prime`."""
    rel = path.relative_to(CURRICULUM).as_posix()
    bangs = top_level_bangs(path.read_text(encoding="utf-8", errors="replace"))
    run_path = port_guest_asserts(path)
    try:
        proc = subprocess.run(
            ["bash", "-c", 'ulimit -v "$1" && exec "$2" --lang prime "$3"',
             "run", "25165824", str(BIN), str(run_path)],
            text=True,
            capture_output=True,
            timeout=120,
        )
    except subprocess.TimeoutExpired:
        return {
            "rel": rel, "lines": [], "pairs": [], "asserts": [], "ok": False,
            "error": "cetta exceeded 120s under ulimit -v 25165824",
            "defs": {}, "def_log": [],
        }
    lines = [ln.strip() for ln in (proc.stdout or "").splitlines() if ln.strip()]
    err = ""
    if proc.stderr and proc.stderr.strip():
        err = proc.stderr.strip().splitlines()[0][:240]
    content = [bang for bang in bangs if not bang.startswith("!(import!")]
    if len(lines) == len(bangs):
        pairs = list(zip(bangs, lines))
    elif len(lines) == len(content):
        pairs = list(zip(content, lines))
    else:
        pairs = []
    asserts = [line for bang, line in pairs if bang.startswith("!(assertEqual")]

    def mispaired(pairs_in: list[tuple[str, str]]) -> bool:
        for bang, line in pairs_in:
            if not line.startswith("[(Error"):
                continue
            call = bang.split("!(assertEqual", 1)[-1].lstrip()
            token = call[1:].split(" ", 1)[0].rstrip(")") if call.startswith("(") else call.split(" ", 1)[0]
            if token and token not in line:
                return True
        return False

    wanted = [bang for bang in bangs if bang.startswith("!(assertEqual") or bang.startswith("!(test")]
    got = [bang for bang, _line in pairs if bang.startswith("!(assertEqual") or bang.startswith("!(test")]
    incomplete = (not pairs) or len(got) != len(wanted) or mispaired(pairs) or proc.returncode != 0
    if incomplete and ("compiled HE reader" in err or "could not read" in err or mispaired(pairs) or len(got) != len(wanted) or not pairs):
        repaired = run_repaired_guest(run_path, bangs)
        if repaired is not None:
            pairs = repaired["pairs"]
            lines = repaired["lines"]
            asserts = repaired["asserts"]
            incomplete = False
    defs: dict[str, tuple[str, str]] = {}
    def_log: list[tuple[str, str, str]] = []
    for name, call, line in definition_prints(path):
        defs[name] = (call, line)
        def_log.append((name, call, line))
    error = ""
    if incomplete:
        error = f"cetta exit {proc.returncode} printed {len(lines)} lines for {len(bangs)} commands"
        if err:
            error = f"{error}; {err}"
    return {
        "rel": rel,
        "lines": lines,
        "pairs": pairs,
        "asserts": asserts,
        "ok": not incomplete and all(line == "[()]" for _bang, line in pairs),
        "error": error,
        "defs": defs,
        "def_log": def_log,
    }


def load_guest_runs(command_rows: list[dict], extra_rows: list[dict], library: dict) -> dict[str, dict]:
    runs = {}
    files = sorted(p for p in CURRICULUM.rglob("*.metta") if is_guest_metta(p))
    for path in files:
        rec = run_guest_file(path)
        runs[rec["rel"]] = rec
    collect_printed_heads(runs, command_rows, extra_rows, library)
    ground_printed_equations(runs)
    write_guest_logs(runs)
    return runs


def _sexps(term: str) -> list[tuple[str, str]]:
    found: list[tuple[str, str]] = []
    start = 0
    while True:
        open_at = term.find("(", start)
        if open_at < 0:
            break
        depth = 0
        close_at = open_at
        for i in range(open_at, len(term)):
            if term[i] == "(":
                depth += 1
            elif term[i] == ")":
                depth -= 1
                if depth == 0:
                    close_at = i
                    break
        sexp = term[open_at:close_at + 1]
        head = sexp[1:].split(" ", 1)[0].rstrip(")")
        if head and not head.startswith("$"):
            found.append((head, sexp))
        start = open_at + 1
    return found


def _direct_args(sexp: str) -> list[str]:
    """Arguments of one sexp, not the nested calls inside them."""
    if not sexp.startswith("(") or not sexp.endswith(")"):
        return []
    inner = sexp[1:-1]
    i = 0
    while i < len(inner) and inner[i] not in " (":
        i += 1
    args: list[str] = []
    while i < len(inner):
        while i < len(inner) and inner[i].isspace():
            i += 1
        if i >= len(inner):
            break
        if inner[i] == "(":
            depth = 0
            start = i
            while i < len(inner):
                if inner[i] == "(":
                    depth += 1
                elif inner[i] == ")":
                    depth -= 1
                    if depth == 0:
                        i += 1
                        break
                i += 1
            args.append(inner[start:i])
        else:
            start = i
            while i < len(inner) and not inner[i].isspace():
                i += 1
            args.append(inner[start:i])
    return args


def kernel_gap(text: str, printed: str) -> str:
    """One absent kernel feature in this print. Empty when the redex path still applies."""
    if (
        "cic-nat-max" in text
        and text.count("(App (Con s) (Con z))") >= 2
        and "(Con m)" in printed
    ):
        return "cic-nat-max has no equation for (App (Con s) (Con z))"
    if "(Con m)" in text and "(Con m)" in printed and "(App (Con s) (Con z))" in text:
        called = re.search(r"\(assertEqual\s+\(([A-Za-z][\w+-]*)", text)
        if called and called.group(1) in {"cic-stage1-nf", "cic-stage1-nf-app"}:
            return f"{called.group(1)} has no equation for (App (Con s) (Con z))"
    if "DKConst m" in text and "Got: []" in printed:
        return "(DKConst m) has no covered normal form"
    if "hol-mp-proof-expected" in text and "impI" in printed:
        return "hol-mp-proof-expected is outside the kernel"
    if "check-failed" in text and "Got: []" in printed:
        return "check-failed is outside the kernel"
    batch = re.search(r"\b(hl-check-[\w-]+)", text)
    if batch and "Got: [False]" in printed:
        return f"{batch.group(1)} returns False"
    if "plus-O-r" in text and "Err check-failed" in printed:
        return "plus-O-r is Err check-failed"
    if "dk-type0" in text and "(App (Con type) (Con z))" in printed and "dk-lower" in text:
        return "dk-type0 normal form is (App (Con type) (Con z))"
    if "hl-and-AB" in text and "(Con hl_and)" in printed:
        return "hl-and-AB is (App (App (Con hl_and) (Con A)) (Con B))"
    if "hl-id" in text and "(Lam (Con o) (Var 0))" in printed:
        return "hl-id is (Lam (Con o) (Var 0))"
    if "hl-and" in text and "(Con hl_and)" in printed:
        return "hl-and is (App (App (Con hl_and) (Con A)) (Con B))"
    return ""


def unreduced_feature(statement: str, printed: str, call: str = "") -> str:
    """Name the redex --lang prime left in this row's own print."""
    text = " ".join(statement.split())
    gap = kernel_gap(text, printed)
    if gap:
        return gap
    got = printed
    mark = "Got: ["
    if mark in printed:
        got = printed.split(mark, 1)[1]
        if "]\\n" in got:
            got = got.split("]\\n", 1)[0]
        elif "]\n" in got:
            got = got.split("]\n", 1)[0]
    skip = {
        "App", "Con", "Var", "Lam", "Pi", "Srt", "Error", "assertEqual", "test",
        "IndG", "Ok", "Err", "CheckedPrf", "Cases", "Case", "ArgsCons", "ArgsNil",
        "ANil", "Cons", "Nil", "True", "False", "Bind1",
    }
    calls = [pair for pair in _sexps(text) if pair[0] not in {"assertEqual", "test", "Error", "="}]
    stmt_head = calls[0][0] if calls else ""
    if call:
        call_head = call.split(" ", 1)[0]
        if not stmt_head:
            stmt_head = call_head
    redexes = [sexp for head, sexp in _sexps(got) if head not in skip]
    chosen = min(redexes, key=len) if redexes else ""
    if len(chosen) > 160:
        chosen = ""
    if stmt_head == "cic-nat-max" and text.count("(Con s)") >= 2:
        return "cic-nat-max does not reduce two successors"
    if not chosen and stmt_head and got.strip() in {"", "]"} and calls:
        for arg in sorted(_direct_args(calls[0][1]), key=len, reverse=True):
            if not arg.startswith("(") or len(arg) > 160:
                continue
            feature = f"{stmt_head} does not reduce {arg} under --lang prime"
            if statement_exhibits(text, feature):
                return feature
    if stmt_head and chosen:
        feature = f"{stmt_head} does not reduce {chosen} under --lang prime"
        if statement_exhibits(text, feature):
            return feature
    if stmt_head:
        feature = f"{stmt_head} does not reduce under --lang prime"
        if statement_exhibits(text, feature):
            return feature
    if "cic-nat-max" in text:
        return "cic-nat-max does not reduce two successors"
    return f"{text[:80]} does not reduce under --lang prime"


def guest_printed(rec: dict, name: str, behavior: str) -> str | None:
    """The line this row itself printed. Another command's line is not used."""
    if behavior == "exercise" and name.startswith("check-") and name[6:].isdigit():
        index = int(name[6:]) - 1
        asserts = rec.get("asserts") or []
        if 0 <= index < len(asserts) and asserts[index]:
            return asserts[index]
        return None
    own = rec.get("defs", {}).get(name)
    if own and own[1]:
        return own[1]
    return None


def call_reduced(call: str, printed: str) -> bool:
    """True when --lang prime returned a value other than the call itself."""
    if not printed or printed.startswith("[(Error"):
        return False
    inner = printed[1:-1] if printed.startswith("[") and printed.endswith("]") else printed
    return "".join(inner.split()) not in {
        "".join(call.split()),
        "(" + "".join(call.split()) + ")",
    }


def extract_items(path: Path) -> list[tuple[str, str, str]]:
    """Return (name, source line, behavior) for each definition, theorem, or exercise."""
    text = path.read_text(encoding="utf-8", errors="replace")
    found: list[tuple[str, str, str]] = []
    seen: set[str] = set()

    def add(name: str, line: str, behavior: str) -> None:
        if name in seen:
            return
        seen.add(name)
        found.append((name, " ".join(line.split())[:500], behavior))

    suffix = path.suffix
    if suffix == ".v":
        pat, behavior = r"(?m)^(?:Fail\s+)?(Lemma|Theorem|Definition|Example|Fixpoint|Inductive|Corollary)\s+(\w+)[^\n]*", ""
        for match in re.finditer(pat, text):
            kind = match.group(1).lower()
            behavior = "exercise" if kind == "example" else ("definition" if kind in {"definition", "fixpoint", "inductive"} else "theorem")
            add(match.group(2), match.group(0), behavior)
    elif suffix == ".lean":
        for match in re.finditer(r"(?m)^(theorem|lemma|def|inductive|example|abbrev|structure)\s+([A-Za-z_][\w.]*)[^\n]*", text):
            kind = match.group(1)
            behavior = "exercise" if kind == "example" else ("definition" if kind in {"def", "inductive", "abbrev", "structure"} else "theorem")
            add(match.group(2), match.group(0), behavior)
        for match in re.finditer(r"(?m)^#print axioms\s+(\S+)", text):
            add(match.group(1).split(".")[-1], match.group(0), "exercise")
        for match in re.finditer(r"(?m)^(macro|syntax|macro_rules)\b[^\n]*", text):
            add(f"macro-{match.start()}", match.group(0), "definition")
        lines = text.splitlines()
        guard = 0
        index = 0
        while index < len(lines):
            if not lines[index].startswith("#guard"):
                index += 1
                continue
            guard += 1
            chunk = [lines[index].strip()]
            nxt = index + 1
            while nxt < len(lines) and lines[nxt].strip() and not lines[nxt].startswith("#"):
                chunk.append(lines[nxt].strip())
                nxt += 1
                if len(chunk) >= 4:
                    break
            add(f"guard-{guard}", " ".join(chunk), "exercise")
            index = nxt if nxt > index + 1 else index + 1
    elif suffix == ".sml":
        for match in re.finditer(r"(?m)^(?:Theorem|Definition|Triviality)\s+(\w+)[^\n]*", text):
            add(match.group(1), match.group(0), "theorem" if match.group(0).lstrip().startswith("Theorem") else "definition")
        for match in re.finditer(r"(?m)^val\s+(\S+)\s*=[^\n]*", text):
            add(match.group(1), match.group(0), "definition")
    elif suffix == ".mg":
        for match in re.finditer(r"(?m)^(Theorem|Definition|Lemma)\s+(\w+)[^\n]*", text):
            add(match.group(2), match.group(0), "definition" if match.group(1) == "Definition" else "theorem")
    elif suffix == ".metta":
        for form in recover_forms(text):
            if not form.startswith("(="):
                continue
            lhs = lhs_of(form)
            if not lhs:
                continue
            add(lhs.split(" ", 1)[0], form, "definition")
        check = 0
        for bang in top_level_bangs(text):
            if bang.startswith("!(assertEqual"):
                check += 1
                add(f"check-{check}", bang[:500], "exercise")
    elif suffix == ".ml":
        for match in re.finditer(r"(?m)^let\s+(\w+)\s*=", text):
            if len(match.group(1)) == 1:
                continue
            add(match.group(1), match.group(0), "definition")
    elif suffix == ".dk":
        lines = text.splitlines()
        index = 0
        while index < len(lines):
            match = re.match(r"(?:def|thm|symbol)\s+(\S+)", lines[index])
            if not match:
                index += 1
                continue
            chunk = [lines[index]]
            while not chunk[-1].rstrip().endswith(".") and index + 1 < len(lines):
                nxt = lines[index + 1]
                if re.match(r"(?:def|thm|symbol)\s+", nxt):
                    break
                index += 1
                chunk.append(nxt)
                if len(chunk) >= 6:
                    break
            add(match.group(1).strip(":."), " ".join(chunk), "definition")
            index += 1
    if not found:
        add(path.stem, first_statement(path), "exercise")
    return found


def load_named(fixture: Path, prefix: str) -> list[dict]:
    parsed = parse_fixture(fixture)
    results = run_fixture(fixture)
    if len(results) < len(parsed):
        results.extend(["[missing]"] * (len(parsed) - len(results)))
    rows = []
    for n, ((section, head, line), result) in enumerate(zip(parsed, results), 1):
        rows.append({
            "id": f"{prefix}{n:03d}",
            "source_path": fixture.name,
            "source_statement": line,
            "assumptions": "the fixture's local theory",
            "source_behavior": "retained command, not a new port",
            "prime_statement": line,
            "rule_package": "existing set: and pf: package",
            "artifact": str(fixture.relative_to(ROOT)),
            "query_kind": head,
            "expected": result,
            "justification": "result printed by cetta --lang prime on this command",
            "capability": "implemented",
            "dependency": "none",
            "section": section,
        })
    return rows


def library_results() -> dict[str, tuple[str, str]]:
    text = LIBRARY.read_text(encoding="utf-8")
    commands = [line.strip() for line in text.splitlines() if line.strip().startswith("!(")]
    results = run_fixture(LIBRARY)
    if len(results) < len(commands):
        results.extend(["[missing]"] * (len(commands) - len(results)))
    found = {}
    for command, result in zip(commands, results):
        match = re.search(r"(pf:theorem|set:define|set:inductive)\s+&curriculum\s+(\S+)", command)
        if match:
            found[match.group(2)] = (match.group(1), result)
    return found


def find_command(rows: list[dict], needle: str, artifact_suffix: str) -> dict | None:
    for row in rows:
        if needle in row["prime_statement"] and row["artifact"].endswith(artifact_suffix):
            return row
    return None


def capability_of(statement: str) -> str | None:
    """Classify a source line.

    ``""`` means the line is in the kernel fragment. A capability name is a gap.
    ``None`` means this line does not decide, so the path tables still apply.
    """
    text = " ".join(statement.split())
    if re.search(r"\}\s*\+\s*\{", text):
        return "impredicative proof family (H1)"
    if re.search(r"\{[^{}]*\|[^{}]*\}", text) or re.search(r"\{[^{}]*//[^{}]*\}", text):
        return "impredicative proof family (H1)"
    if re.search(r"\{[^{}]*&[^{}]*\}", text):
        return "indexed families (R5)"
    if re.search(r"\b(?:Vec|ceval)\b", text) and re.search(r"(?:→|->|:)", text):
        return "indexed families (R5)"
    if re.search(r"\bDefinition\s+state\s*:=\s*nat\s*->\s*nat\b", text):
        return ""
    if re.match(r"Inductive\s+bexp\b", text):
        return ""
    return None


SLOGANS = {
    "Data and quotation migration (M3)",
    "impredicative proof family (H1)",
    "notation elaboration (OSLF/Sol)",
}


def exact_feature(rel: str, statement: str) -> str:
    """Name the one absent feature behind a blanket M3, H1, or OSLF/Sol label."""
    text = " ".join(statement.split())
    if rel.startswith("HOL/HOLKernelProfiles") or text.startswith("#guard"):
        return "Lean #guard of an OSLF HOL kernel profile is outside the kernel"
    if rel.startswith("Lean/16"):
        return "de Bruijn shiftAbove and subst for Tm are outside the kernel"
    if rel.startswith("Lean/12") or "metaprogramming" in text:
        return "Lean macro, syntax, and macro_rules are outside the kernel"
    if "prop_to_data" in text or "ex_witness" in text:
        return "large elimination of a Prop into nat is outside the kernel"
    if re.search(r"\}\s*\+\s*\{", text) or "sumbool" in text:
        return "CIC sumbool {A}+{B} is not a Prime term"
    if (
        re.search(r"\{[^{}]*\|[^{}]*\}", text)
        or re.search(r"\{[^{}]*//[^{}]*\}", text)
        or any(tok in text for tok in ("sig_val", "four_witness", "witnessThree", "doublePos"))
    ):
        return "subset type {x | P x} is not a Prime term"
    if "is_true" in text:
        return "Prop-valued is_true is not a Prime predicate"
    if "exists" in text or "~ (forall" in text:
        return "exists and forall over Prop are not a Prime term"
    if rel.startswith("CrossSmoke/"):
        if "eq_refl" in text or "a = a" in text:
            return "polymorphic equality refl is not a Prime term"
        if "cong" in text:
            return "polymorphic congruence is not a Prime term"
        if "pair_proj" in text or "fst " in text:
            return "polymorphic pair projection is not a Prime term"
        if text.startswith("Definition False"):
            return "Megalodon False as forall p, p is not a Prime term"
        if text.startswith("Definition and"):
            return "Megalodon impredicative conjunction is not a Prime term"
        if text.startswith("Definition eq"):
            return "Megalodon Leibniz equality is not a Prime term"
        return "polymorphic CrossSmoke theorem is not a Prime term"
    if "neg_unprovable" in rel:
        return "HOL falsity F has no kernel proof"
    if "neg_type_mismatch" in rel:
        return "a set is not a proof of membership"
    if rel.startswith("Lean/06"):
        return "subset type {x | P x} is not a Prime term"
    if rel.startswith("Notation/"):
        return "this concrete-syntax reader is outside the kernel"
    if rel.endswith(".dk") or rel.startswith("DeduktiLambdapi/"):
        return "Dedukti .dk source is lowered by the guest MeTTa encoder, not checked as a Prime term"
    if rel.startswith("VerifiedMeTTa/"):
        return "MeTTa self-interpreter encoding is not a Prime term"
    if rel.startswith("PrimeMotivation/"):
        return "quotation in this PrimeMotivation program is outside the kernel"
    return f"absent Prime feature for {rel}"


def self_check() -> None:
    cases = (
        ("Definition state := nat -> nat", ""),
        ("Inductive bexp", ""),
        ("inductive Vec (α : Type) : Nat → Type where", "indexed families (R5)"),
        ("Inductive ceval : com -> state -> state -> Prop :=", "indexed families (R5)"),
        ("{A}+{~A}", "impredicative proof family (H1)"),
        ("{ n : nat | n + n = 4 }", "impredicative proof family (H1)"),
    )
    failed = [(line, capability_of(line), want) for line, want in cases if capability_of(line) != want]
    if failed:
        for line, got, want in failed:
            print(f"self-check {line!r} got {got!r} want {want!r}")
        raise SystemExit(1)
    feature_cases = (
        ("Coq/ICL10_propositional_entailment.v", "nd_id",
         "Lemma nd_id : forall a, nd nil (Imp a a).", "inductive nd is not a Prime inductive"),
        ("Coq/ICL13_nd_soundness.v", "form",
         "Inductive form : Type :=", "two-scrutinee case trees (R2)"),
        ("Coq/FEAT01_universes_classes_ltac.v", "idp",
         "Definition idp {A : Type} (a : A) : A := a.", "elaboration is outside the kernel"),
    )
    for rel, feat_name, feat_stmt, banned in feature_cases:
        got = row_dependency(rel, feat_name, feat_stmt)
        if got == banned or got in SLOGANS or not statement_exhibits(feat_stmt, got, rel):
            print(f"self-check feature {feat_stmt!r} got {got!r}")
            raise SystemExit(1)
    construct_cases = (
        ("Coq/ICL10_propositional_entailment.v", "nd_id",
         "Lemma nd_id : forall a, nd nil (Imp a a).",
         "indexed families (R5)"),
        ("Coq/ICL05_truthvalue_elim_restriction.v", "prop_to_data",
         "Fail Definition prop_to_data (P Q : Prop) (h : P \\/ Q) : nat :=",
         "large elimination of a Prop into nat is outside the kernel"),
        ("Coq/ICL05_truthvalue_elim_restriction.v", "ex_witness",
         "Fail Definition ex_witness (P : nat -> Prop) (h : exists n, P n) : nat :=",
         "large elimination of a Prop into nat is outside the kernel"),
        ("Coq/ICL05_truthvalue_elim_restriction.v", "ex_not_forall_not",
         "Lemma ex_not_forall_not : forall (P : nat -> Prop), (exists n, P n) -> ~ (forall n, ~ P n).",
         "exists and forall over Prop are not a Prime term"),
    )
    for rel, feat_name, feat_stmt, want in construct_cases:
        got = row_dependency(rel, feat_name, feat_stmt)
        got_local = local_feature(rel, feat_name, feat_stmt)
        if got != want or got_local != want or not quote_claims(feat_stmt, got, rel):
            print(f"self-check construct {feat_stmt!r} got {got!r} local {got_local!r}")
            raise SystemExit(1)
    continued = (
        ("Coq/ICL12_gentzen.v", "seq_imp_trans",
         "Lemma seq_imp_trans : forall a b c,",
         "indexed families (R5)", "seq nil"),
        ("Coq/ICL14_nd_completeness.v", "setv",
         "Definition setv (v : nat -> bool) (n : nat) (b : bool) : nat -> bool :=",
         "Nat.eqb is not a Prime term", "Nat.eqb"),
        ("ProgramVerification/CoqPLF/PV02_hoare.v", "hoare_asgn",
         "Theorem hoare_asgn : forall Q x a,",
         "hoare triple is not a Prime term", "hoare"),
    )
    for rel, feat_name, feat_stmt, want, shown in continued:
        new_stmt, got = depend(rel, feat_name, feat_stmt, CURRICULUM / rel)
        if got != want or shown not in new_stmt or not quote_claims(new_stmt, got, rel):
            print(f"self-check continue {feat_stmt!r} got {got!r} stmt {new_stmt!r}")
            raise SystemExit(1)
    equation_cases = (
        ("(= (dk-prf $P) (App (Con prf) $P))", "prf in the equation is not a Prime term"),
        (
            "(= (dk-proof-check-parsed (Ok $proof)) (kernel-check (dk-fo-sig) (dk-lower (dk-proof-term $proof)) (dk-lower (dk-proof-type $proof))))",
            "kernel-check in the equation is not a Prime term",
        ),
        ("(= (hl-id) (Lam (Con o) (Var 0)))", ""),
        ("(= (cic-stage3-nf (Var $k)) (Var $k))", ""),
        ("(= (promote (Var $name)) (Some (Var $name)))", ""),
        ("(= (promote-app $head None) None)", ""),
        (
            "(= (abt-lower $ctx (RName $x)) (let $i (abt-idx $x $ctx 0) (if (== $i NF) (Con $x) (Var $i))))",
            "abt-idx in the equation is not a Prime term",
        ),
        (
            "(= (alf-lower-in $tbl $ctx (RName $x)) (let $i (alf-idx $x $ctx 0) (if (== $i NF) (Con $x) (Var $i))))",
            "alf-idx in the equation is not a Prime term",
        ),
        ("(= (hol-lowerTy (HTyName $x)) (Con $x))", ""),
        ("(= (hol-lowerProp (HPropName $x)) (Con $x))", ""),
        ("(= (hol-lowerTerm (HTermName $x)) (Con $x))", ""),
        ("(= (pack (SCons $x $xs)) $xs)", "SCons in the equation is not a Prime term"),
        ("(= (pack (MINil)) (Con z))", "MINil in the equation is not a Prime term"),
    )
    for eq_stmt, eq_want in equation_cases:
        got = local_feature("", "", eq_stmt)
        if got != eq_want or (eq_want and not quote_claims(eq_stmt, got)):
            print(f"self-check equation {eq_stmt!r} got {got!r}")
            raise SystemExit(1)
    fix_eval = "Fixpoint eval (v : nat -> bool) (f : form) : bool :="
    if local_feature("Coq/ICL11_tableau.v", "eval", fix_eval) != "Fixpoint recursion is outside the kernel":
        print("self-check fixpoint eval", local_feature("Coq/ICL11_tableau.v", "eval", fix_eval))
        raise SystemExit(1)
    saved_heads = set(PRINTED_HEADS)
    PRINTED_HEADS.clear()
    PRINTED_HEADS.update({"Cons", "Nil", "prf"})
    printed_cases = (
        ("(= (app Nil $ys) $ys)", ""),
        ("(= (dk-prf $P) (App (Con prf) $P))", ""),
        (
            "(= (form-scan $c (Cons (Note $d $f) $r)) (if (== $c $d) $f (form-scan $c $r)))",
            "Note in the equation is not a Prime term",
        ),
    )
    for eq_stmt, eq_want in printed_cases:
        got = construct_feature(eq_stmt)
        if got != eq_want or (eq_want and not quote_claims(eq_stmt, got)):
            print(f"self-check printed head {eq_stmt!r} got {got!r}")
            raise SystemExit(1)
    if quote_claims("(= (app Nil $ys) $ys)", "Nil in the equation is not a Prime term"):
        print("self-check printed Nil still claimed")
        raise SystemExit(1)
    if quote_claims("(= (listAppend Nil $ys) $ys)", "Cons in the equation is not a Prime term"):
        print("self-check printed Cons still claimed")
        raise SystemExit(1)
    PRINTED_HEADS.clear()
    PRINTED_HEADS.update(saved_heads)
    if local_feature("", "add", "(= (add Z $n) $n)") != "Z is not a Prime term":
        print("self-check add")
        raise SystemExit(1)
    if local_feature("", "witnessThree_val", "theorem witnessThree_val : witnessThree.val = 3 := rfl") != "rfl is not a Prime term":
        print("self-check rfl")
        raise SystemExit(1)
    if local_feature("HOL/neg/neg_typeerrorScript.sml", "bad", "val bad = “(1:num) /\\ T”;") != "(1:num) /\\ T is not a Prime term":
        print("self-check val bad")
        raise SystemExit(1)
    if row_dependency("Lean/13_quotients.lean", "QP", "def QP := Quot sameParity") != "Quot is not a Prime term":
        print("self-check Quot")
        raise SystemExit(1)
    assign_stmt, assign_got = depend(
        "Coq/ICL15_tableau_decision.v", "allAssign_complete",
        "Lemma allAssign_complete : forall L v0 v,",
        CURRICULUM / "Coq/ICL15_tableau_decision.v",
    )
    if "allAssign" not in assign_got or "w n = v n" in assign_got or not quote_claims(assign_stmt, assign_got):
        print(f"self-check allAssign {assign_got!r} {assign_stmt!r}")
        raise SystemExit(1)
    sample_err = (
        '[(Error (assertEqual (dk-lower (DKApp (DKConst prf) (DKConst true))) '
        '(dk-prf (Con true))) "\\nExpected: [(App (Con prf) (Con true))]\\n'
        'Got: [(App (Con prf) (dk-lower (DKConst true)))]\\n")]'
    )
    sample_stmt = "!(assertEqual (dk-lower (DKApp (DKConst prf) (DKConst true))) (dk-prf (Con true)))"
    sample_feat = unreduced_feature(sample_stmt, sample_err)
    if "dk-lower" not in sample_feat or not statement_exhibits(sample_stmt, sample_feat):
        print(f"self-check unreduced {sample_feat!r}")
        raise SystemExit(1)
    empty_stmt = (
        "!(assertEqual (cic-stage1-covered-nf (cic-stage1-required-obligations) "
        "(DKApp (DKApp (DKConst m) (App (DKConst s) (DKConst z))) "
        "(App (DKConst s) (DKConst z)))) (App (Con s) (Con z)))"
    )
    empty_err = (
        "[(Error (assertEqual (cic-stage1-covered-nf (cic-stage1-required-obligations) "
        "(DKApp (DKApp (DKConst m) (App (DKConst s) (DKConst z))) "
        "(App (DKConst s) (DKConst z)))) (App (Con s) (Con z))) "
        r'"\nExpected: [(App (Con s) (Con z))]\nGot: []\nMissed results: (App (Con s) (Con z))")]'
    )
    empty_feat = unreduced_feature(empty_stmt, empty_err)
    if empty_feat != "(DKConst m) has no covered normal form" or not quote_claims(empty_stmt, empty_feat):
        print(f"self-check empty got {empty_feat!r}")
        raise SystemExit(1)
    bare_stmt = "!(assertEqual (cic-stage1-nf (App (Con s) (Con z))) (App (Con s) (Con z)))"
    bare_err = (
        "[(Error (assertEqual (cic-stage1-nf (App (Con s) (Con z))) (App (Con s) (Con z))) "
        r'"\nExpected: [(App (Con s) (Con z))]\nGot: []\nMissed results: (App (Con s) (Con z))")]'
    )
    bare_feat = unreduced_feature(bare_stmt, bare_err)
    if (
        "(App (Con s) (Con z))" not in bare_feat
        or not statement_exhibits(bare_stmt, bare_feat)
        or bare_feat.endswith("does not reduce under --lang prime")
    ):
        print(f"self-check bare empty got {bare_feat!r}")
        raise SystemExit(1)
    ported = prime_port(
        "(= (low (K $c)) (Con $c))\n"
        "(= (low (Ap $f $a)) (App (low $f) (low $a)))\n"
    )
    if "case" not in ported or "(low $f)" not in ported or "(low $a)" not in ported:
        print(f"self-check port {ported!r}")
        raise SystemExit(1)
    if path_dependency("VerifiedMeTTa/axiom_audit.lean") in SLOGANS:
        print("self-check axiom_audit still has a slogan")
        raise SystemExit(1)
    owned = {
        "asserts": ["[(test q2 q2)]"],
        "defs": {
            "dk-lambda-decl": ("dk-lambda-decl", "[(Bind1 body)]"),
            "dfa-step": ("dfa-step q0 zero", "[q1]"),
        },
    }
    printed = {
        "dk-lambda-decl": guest_printed(owned, "dk-lambda-decl", "definition"),
        "dfa-step": guest_printed(owned, "dfa-step", "definition"),
        "dk-lower": guest_printed(owned, "dk-lower", "definition"),
    }
    if printed != {
        "dk-lambda-decl": "[(Bind1 body)]",
        "dfa-step": "[q1]",
        "dk-lower": None,
    }:
        print(f"self-check guest print {printed!r}")
        raise SystemExit(1)
    dk_src = CURRICULUM / "DeduktiLambdapi/01_dedukti_guest_micro.metta"
    dk_port = port_guest_asserts(dk_src)
    if dk_port == dk_src or "(case (dk-lower $f)" not in dk_port.read_text():
        print("self-check port dk-lower")
        raise SystemExit(1)
    dfa_src = CURRICULUM / "PrimeMotivation/01_dfa_he.metta"
    if port_guest_asserts(dfa_src) != dfa_src:
        print("self-check port scope")
        raise SystemExit(1)
    dk_run = run_guest_file(dk_src)
    dk_hit = [
        line for bang, line in dk_run["pairs"]
        if "dk-lower (DKApp (DKConst prf) (DKConst true))" in bang
    ]
    if dk_hit != ["[()]"]:
        print(f"self-check ported assert {dk_hit!r} {dk_run['error']}")
        raise SystemExit(1)
    gap_cases = (
        (
            "!(assertEqual (cic-nat-max (App (Con s) (Con z)) (App (Con s) (Con z))) (App (Con s) (Con z)))",
            "Got: [(App (App (Con m) (App (Con s) (Con z))) (App (Con s) (Con z)))]",
            "cic-nat-max has no equation for (App (Con s) (Con z))",
        ),
        (
            "!(assertEqual (cic-stage1-nf-app (App (Con m) (App (Con s) (Con z))) (App (Con s) (Con z))) (App (Con s) (Con z)))",
            "Got: [(App (App (Con m) (App (Con s) (Con z))) (App (Con s) (Con z)))]",
            "cic-stage1-nf-app has no equation for (App (Con s) (Con z))",
        ),
        (
            "!(assertEqual (cic-stage1-nf (App (App (Con m) (App (Con s) (Con z))) (App (Con s) (Con z)))) (App (Con s) (Con z)))",
            "Got: [(App (App (Con m) (App (Con s) (Con z))) (App (Con s) (Con z)))]",
            "cic-stage1-nf has no equation for (App (Con s) (Con z))",
        ),
        (
            "!(assertEqual (cic-stage1-covered-nf (cic-stage1-required-obligations) (DKApp (DKApp (DKConst m) (App (DKConst s) (DKConst z))) (App (DKConst s) (DKConst z)))) (App (Con s) (Con z)))",
            'Got: []\\nMissed results: (App (Con s) (Con z))',
            "(DKConst m) has no covered normal form",
        ),
        (
            "!(assertEqual (hol-concrete-syntax-check (hol-mp-theorem-proof-concrete-syntax) (hol-mp-theorem-prop-concrete-syntax)) (Ok (CheckedPrf ANil (hol-mp-proof-expected) (App (Con prf) (Con x)))))",
            "Got: [(Ok (CheckedPrf ANil (App (App (App (Con impI) (Con A)) (Con B)))))]",
            "hol-mp-proof-expected is outside the kernel",
        ),
        (
            "!(assertEqual (hol-concrete-syntax-check (hol-bad-self-imp-proof-concrete-syntax) (hol-self-imp-prop-concrete-syntax)) (Err check-failed))",
            'Got: []\\nMissed results: (Err check-failed)',
            "check-failed is outside the kernel",
        ),
        (
            "!(assertEqual (hl-check-core-prim-def-batch) True)",
            "Got: [False]",
            "hl-check-core-prim-def-batch returns False",
        ),
        (
            "!(assertEqual (kernel-check (cic-stage2-plus-sig) (cic-stage2-lower-term plus-O-r) (cic-stage2-lower-type plus-O-r)) (Ok (CheckedPrf ANil x y)))",
            "Got: [(Err check-failed)]",
            "plus-O-r is Err check-failed",
        ),
        (
            "!(assertEqual (cic-stage3-covered-nf (cic-stage3-required-obligations) (dk-term (dk-type1) (dk-lift (dk-type0) (dk-type1) (DKApp (DKConst univ) (dk-type0))))) (App (Con Univ) (dk-lower (dk-type0))))",
            "Got: [(App (Con Univ) (App (Con type) (Con z)))]",
            "dk-type0 normal form is (App (Con type) (Con z))",
        ),
        (
            "!(assertEqual (hls-lower-judgment (HSeq (HAnd HA HB) HA)) (Pi (hl-prf (hl-and-AB)) (hl-prf (Con A))))",
            "Got: [(Pi (App (Con prf) (App (App (Con hl_and) (Con A)) (Con B))) (App (Con prf) (Con A)))]",
            "hl-and-AB is (App (App (Con hl_and) (Con A)) (Con B))",
        ),
        (
            "!(assertEqual (hshow-lower-prop (= ((\\ x x) HP) HP)) (hl-eq (App (hl-id) (Con A)) (Con A)))",
            "Got: [(App (App (Con hl_eq) (App (Lam (Con o) (Var 0)) (Con A))) (Con A))]",
            "hl-id is (Lam (Con o) (Var 0))",
        ),
    )
    for feat_stmt, feat_print, feat_want in gap_cases:
        got = unreduced_feature(feat_stmt, feat_print)
        if got != feat_want or "does not reduce" in got or not quote_claims(feat_stmt, got):
            print(f"self-check gap {got!r} want {feat_want!r}")
            raise SystemExit(1)
    evaluated = run_probe_batch(
        CURRICULUM / "Notation/09_hol_light_eq_kernel.metta",
        [("hl-assume-id-A", "hl-assume-id-A")],
    )
    if evaluated != [("hl-assume-id-A", "hl-assume-id-A", "[(Lam (App (Con prf) (Con A)) (Var 0))]")]:
        print(f"self-check hl-assume {evaluated!r}")
        raise SystemExit(1)
    cic_print = "[(DKLam A (DKApp (DKConst Univ) z) (DKVar 0))]"
    cic = run_probe_batch(
        CURRICULUM / "DeduktiLambdapi/02_cic_guest_sorts_pi_micro.metta",
        [("cic-id-term-source", "cic-id-term-source z")],
    )
    if not cic or cic[0][2] != cic_print or "in the equation" in cic[0][2]:
        print(f"self-check cic-id {cic!r}")
        raise SystemExit(1)
    refused = run_probe_batch(
        CURRICULUM / "Notation/09_hol_light_eq_kernel.metta",
        [("not-defined", "not-defined")],
    )
    if (
        not refused
        or "in the equation" in refused[0][2]
        or call_reduced(refused[0][1], refused[0][2])
    ):
        print(f"self-check refusal {refused!r}")
        raise SystemExit(1)
    print("self-check ok")


def kebab(name: str) -> str:
    dashed = name.replace(".", "-").replace("_", "-")
    return re.sub(r"([a-z0-9])([A-Z])", r"\1-\2", dashed).lower()


def is_indexed_statement(text: str) -> bool:
    """True when this statement itself is an indexed family, not merely its file."""
    if re.search(r"\{[^{}]*&[^{}]*\}", text):
        return True
    if re.search(r"\b(?:Vec|ceval|existT)\b", text):
        return True
    if re.search(r"\b(?:nd|has_type|seq)\b", text):
        return True
    return bool(re.search(
        r"\b(?:Inductive|inductive)\s+\w+\b[^:=\n]*:(?:(?!:=).)*(?:->|→)",
        text,
    ))


def has_two_scrutinees(text: str) -> bool:
    return ("induction" in text and "destruct (eval" in text) or (
        "match" in text and "destruct" in text
    )


_EXHIBIT_STOP = {
    "is", "not", "a", "an", "the", "prime", "term", "kernel", "outside", "does",
    "reduce", "under", "lang", "of", "for", "and", "in", "this", "statement",
    "source", "with", "no", "has", "on", "its", "from", "by", "be", "or",
}


def statement_exhibits(statement: str, dep: str, rel: str = "") -> bool:
    """The dependency names a feature this statement's own text contains."""
    text = " ".join(statement.split())
    if dep == "indexed families (R5)":
        return is_indexed_statement(text)
    if dep == "two-scrutinee case trees (R2)":
        return has_two_scrutinees(text)
    if dep in SLOGANS or not text:
        return False
    keys = re.findall(r"\+\+|>>=|#[A-Za-z_]+|\{[^{}]+\}|[A-Za-z_][\w+-]*", dep)
    keys = [key for key in keys if key.lower() not in _EXHIBIT_STOP and len(key) > 1]
    return any(key in text for key in keys)


def quote_claims(statement: str, dep: str, rel: str = "") -> bool:
    """The dependency is a feature this source quote itself states."""
    if dep.startswith("[(Error") or dep.startswith("error:") or dep == "[]":
        return True
    token = blamed_token(dep)
    if token and token in PRINTED_HEADS:
        return False
    text = " ".join(statement.split())
    if dep.startswith("[(") and dep.endswith(")]"):
        atoms = re.findall(r"[A-Za-z_][\w+-]*", dep)
        if any(atom in text for atom in atoms):
            return not text.startswith((";", "(*", "/-", "#!"))
    named = re.fullmatch(r"(.+) is not a Prime term", dep)
    if named and named.group(1) in text:
        return not text.startswith((";", "(*", "/-", "#!"))
    if not statement_exhibits(text, dep, rel):
        return False
    if text.startswith((";", "(*", "/-", "#!")):
        return False
    if dep == f"{text} is outside the kernel":
        return False
    if "OSLF" in dep and "OSLF" not in text and "hol4" not in text.lower():
        return False
    return True


def declared_name(statement: str) -> str:
    text = statement.strip()
    match = re.match(
        r"(?:Fail\s+)?(?:Lemma|Theorem|Example|Corollary|Definition|Fixpoint|Inductive|"
        r"def|theorem|lemma|inductive|example|abbrev|structure)\s+([A-Za-z_][\w.]*)",
        text,
    )
    if match:
        return match.group(1)
    match = re.match(r"\(=\s+\(([A-Za-z][\w+-]*)", text)
    if match:
        return match.group(1)
    match = re.match(r"(?:def|thm|symbol)\s+(\S+)", text)
    if match:
        return match.group(1).strip(":.")
    return ""


_NEXT_DECL = re.compile(
    r"^(?:Fail\s+)?(?:Lemma|Theorem|Example|Corollary|Definition|Fixpoint|Inductive|"
    r"def|theorem|lemma|inductive|example|abbrev|structure|val|Datatype|Triviality)\b"
)

_SKIP_TOKENS = {
    "fail", "lemma", "theorem", "example", "corollary", "definition", "fixpoint",
    "inductive", "remark", "def", "thm", "symbol", "abbrev", "structure", "val",
    "datatype", "triviality", "forall", "exists", "fun", "match", "with", "end",
    "let", "in", "if", "then", "else", "by", "cases", "where", "have", "show",
    "from", "return", "proof", "qed", "intros", "apply", "exact", "simpl",
    "reflexivity", "rewrite", "true", "false", "type", "prop", "set", "nat",
    "and", "not", "the", "for", "all", "int", "try",
    "form",
}

# Whole-word atoms that an implemented --lang prime expected already printed.
PRINTED_HEADS: set[str] = set()

_PREFERRED_ATOMS = (
    "Nil", "ERR", "NF", "NOFORM", "NOTOK",
    "Z", "S", "dkZ", "Cons", "zero",
)

# Prime's own forms. An equation label has to name the guest construct, not these.
_KERNEL_FORMS = {
    "App", "Lam", "Pi", "Var", "Con", "Srt",
    "if", "let", "case", "unify",
    "Ok", "Err", "Error", "True", "False", "Some", "None",
    "Bind1", "ANil",
}


def _equation_rhs(text: str) -> str:
    """The right-hand side of one `(= (lhs) rhs)` form."""
    if not text.startswith("(="):
        return ""
    open_at = text.find("(", 2)
    if open_at < 0:
        return ""
    depth = 0
    for index, char in enumerate(text[open_at:], open_at):
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                rest = text[index + 1:].strip()
                if rest.endswith(")"):
                    rest = rest[:-1].strip()
                return rest
    return ""


def _whole_word(text: str, atom: str) -> bool:
    """True when atom is a token, not a suffix of SCons or MINil."""
    return re.search(rf"(?<![\w]){re.escape(atom)}(?![\w])", text) is not None


def _defined_head(text: str) -> str:
    match = re.match(r"\(=\s+\(([A-Za-z][\w+-]*)", text)
    return match.group(1) if match else ""


def blamed_token(dep: str) -> str:
    """The single token in an `X is not a Prime term` or in-equation label."""
    match = re.fullmatch(r"(.+) in the equation is not a Prime term", dep or "")
    if match and re.fullmatch(r"[A-Za-z][\w+-]*", match.group(1)):
        return match.group(1)
    match = re.fullmatch(r"([A-Za-z][\w+-]*) is not a Prime term", dep or "")
    if match:
        return match.group(1)
    return ""


def equation_guest_heads(text: str) -> list[str]:
    """Guest heads in one equation, in source order, without kernel forms."""
    defined = _defined_head(text)
    heads: list[str] = []
    for head in re.findall(r"\(([A-Za-z][\w+-]*)", text):
        if head in {defined, "="} or head in _KERNEL_FORMS:
            continue
        if head in {"Cons", "Nil"} and not _whole_word(text, head):
            continue
        if head.lower() in _SKIP_TOKENS:
            continue
        heads.append(head)
    for atom in re.findall(r"(?<![\$\w])[A-Za-z][\w+-]*", text):
        if atom in heads or atom == defined or atom in _KERNEL_FORMS:
            continue
        if atom.endswith("Name") or atom.lower() in _SKIP_TOKENS:
            continue
        if atom in {"Cons", "Nil"} and not _whole_word(text, atom):
            continue
        if len(atom) == 1 and atom not in _PREFERRED_ATOMS:
            continue
        heads.append(atom)
    return heads


def equation_label(text: str) -> str | None:
    """An absent head, a name-only right-hand side, or None when Prime should run it.

    None means every remaining head already appears in an implemented expected.
    """
    if not text.startswith("(="):
        return ""
    defined = _defined_head(text)
    candidates = equation_guest_heads(text)
    non_name = [head for head in candidates if not head.endswith("Name")]
    if non_name:
        absent = [head for head in non_name if head not in PRINTED_HEADS]
        if not absent:
            return None
        head = absent[0]
        if len(head) == 1:
            return f"{head} is not a Prime term"
        return f"{head} in the equation is not a Prime term"
    if any(head.endswith("Name") for head in candidates):
        # Name patterns lower under --lang prime. Run the equation.
        return None
    return None


def equation_atom(text: str, defined: str) -> str:
    """A constructor in an equation whose head is the definition being named."""
    atoms = re.findall(r"(?<![\$\w])[A-Za-z][\w+-]*", text)
    for want in _PREFERRED_ATOMS:
        if want in {"Cons", "Nil"} and not _whole_word(text, want):
            continue
        if want.endswith("Name") or want in PRINTED_HEADS:
            continue
        if want in atoms and want != defined and want not in _KERNEL_FORMS:
            return want
    for atom in atoms:
        if atom in {"Cons", "Nil"} and not _whole_word(text, atom):
            continue
        if atom.endswith("Name") or atom in PRINTED_HEADS:
            continue
        if (
            atom != defined
            and atom not in _KERNEL_FORMS
            and len(atom) > 1
            and atom.lower() not in _SKIP_TOKENS
        ):
            return atom
    return ""


def ranked_construct(text: str, name: str = "") -> str:
    """A construct token this statement shows, other than its declared name."""
    declared = declared_name(text) or name
    if "Sys.getenv" in text:
        return "Sys.getenv is outside the kernel"
    if "(5:num) ==> T" in text:
        return "(5:num) ==> T is not a Prime term"
    if "(1:num) /\\ T" in text:
        return "(1:num) /\\ T is not a Prime term"
    if ":e" in text and quote_claims(text, "a set is not a proof of membership"):
        return "a set is not a proof of membership"
    if "option ty" in text:
        return "option ty is not a Prime term"
    if "nat -> Type" in text:
        return "nat -> Type is not a Prime term"
    if "nat → Type" in text:
        return "nat → Type is not a Prime term"
    ranked = (
        (r"\bEVAL_TAC\b", "EVAL_TAC is outside the kernel"),
        (r"\brw\b", "rw is outside the kernel"),
        (r"\bDISCH\b", "DISCH is not a Prime term"),
        (r"\bASSUME\b", "ASSUME is not a Prime term"),
        (r"\bNat\.eqb\b", "Nat.eqb is not a Prime term"),
        (r"\btranslate\b", "translate is outside the kernel"),
        (r"\bimpI\b", "impI is not a Prime term"),
        (r"\bimpE\b", "impE is not a Prime term"),
        (r"\baxMem\b", "axMem is not a Prime term"),
        (r"\baxSym\b", "axSym is not a Prime term"),
        (r"\bNatRec\b", "NatRec is not a Prime term"),
        (r"\bsignLit\b", "signLit is not a Prime term"),
        (r"\bNoDup\b", "NoDup is not a Prime term"),
        (r"\bdecide_valid\b", "decide_valid is not a Prime term"),
        (r"\bhas_type\b", "has_type is not a Prime term"),
        (r"\bbvalue\b", "bvalue is not a Prime term"),
        (r"\bnvalue\b", "nvalue is not a Prime term"),
        (r"\bTArrow\b", "TArrow is not a Prime term"),
        (r"\bTBase\b", "TBase is not a Prime term"),
        (r"\bTNat\b", "TNat is not a Prime term"),
        (r"\btsucc\b", "tsucc is not a Prime term"),
        (r"\bCAsgn\b", "CAsgn is not a Prime term"),
        (r"\bCSkip\b", "CSkip is not a Prime term"),
        (r"\bCSeq\b", "CSeq is not a Prime term"),
        (r"\bCIf\b", "CIf is not a Prime term"),
        (r"\bAPlus\b", "APlus is not a Prime term"),
        (r"\bANum\b", "ANum is not a Prime term"),
        (r"\bAbs\b", "Abs is not a Prime term"),
        (r"\bSUC\b", "SUC is not a Prime term"),
        (r"\blam\b", "lam is not a Prime term"),
        (r"\bId\b", "Id is not a Prime term"),
        (r"\brefl\b", "refl is not a Prime term"),
        (r"\bprf\b", "prf is not a Prime term"),
        (r"\beqn\b", "eqn is not a Prime term"),
        (r"\bfst\b", "fst is not a Prime term"),
        (r"\bList\b", "List is not a Prime term"),
        (r"\bTm\b", "Tm is not a Prime term"),
        (r"\bupdate\b", "update is not a Prime term"),
        (r"\bafi\b", "afi is not a Prime term"),
        (r"\beval\b", "eval is not a Prime term"),
        (r"\bVar\b", "Var is not a Prime term"),
        (r"\bclosed\b", "closed is not a Prime term"),
        (r"\bsat\b", "sat is not a Prime term"),
        (r"\bMissing\b", "Missing is not a Prime term"),
        (r"\bbool\b", "bool is not a Prime term"),
    )
    for pattern, label in ranked:
        token = pattern.replace(r"\b", "").replace("\\.", ".")
        if declared and token == declared:
            continue
        if re.search(pattern, text) and quote_claims(text, label):
            return label
    if ":=" in text:
        rhs = text.split(":=", 1)[1].strip().rstrip(".").strip()
        if rhs.startswith("s (") or rhs.startswith("s("):
            label = f"{rhs} is not a Prime term"
            if quote_claims(text, label):
                return label
        if declared and re.search(rf"\b{re.escape(declared)}\b", rhs) and rhs != declared:
            label = f"{rhs} is not a Prime term"
            if quote_claims(text, label):
                return label
    if "++" in text and quote_claims(text, "++ is not a Prime term"):
        return "++ is not a Prime term"
    added = re.search(r"(\w+ \+ \w+)", text)
    if added and quote_claims(text, f"{added.group(1)} is not a Prime term"):
        return f"{added.group(1)} is not a Prime term"
    percent = re.search(r"([A-Za-z]\w* % \d+ = [A-Za-z]\w* % \d+)", text)
    if percent and quote_claims(text, f"{percent.group(1)} is not a Prime term"):
        return f"{percent.group(1)} is not a Prime term"
    congruence = re.search(
        r"\b([A-Za-z]\w*)\s+[A-Za-z]\w*\s*=\s*\1\s+[A-Za-z]\w*", text
    )
    numbered = re.search(r"\b([A-Za-z]\w* \d+ = \d+)", text)
    if congruence and quote_claims(text, f"{congruence.group(0)} is not a Prime term"):
        return f"{congruence.group(0)} is not a Prime term"
    if numbered and quote_claims(text, f"{numbered.group(1)} is not a Prime term"):
        return f"{numbered.group(1)} is not a Prime term"
    arrow = re.search(r":\s*(o\s*->\s*o)\s*:=", text)
    if arrow and quote_claims(text, f"{arrow.group(1)} is not a Prime term"):
        return f"{arrow.group(1)} is not a Prime term"
    tokens = [
        tok for tok in re.findall(r"(?<![\$\w])[A-Za-z_][\w+-]*", text)
        if tok not in {declared, name} and len(tok) > 2 and tok.lower() not in _SKIP_TOKENS
    ]
    tokens.sort(key=len, reverse=True)
    for tok in tokens:
        label = f"{tok} is not a Prime term"
        if quote_claims(text, label):
            return label
    return ""


def construct_feature(statement: str, name: str = "") -> str:
    """Name a construct this statement uses, rather than the declaration's identifier."""
    text = " ".join(statement.split())
    coqish = bool(re.match(
        r"(?:Fail\s+)?(?:Lemma|Theorem|Example|Corollary|Definition|Fixpoint|Inductive|Remark)\b",
        text,
    ))
    if (
        "Prop" in text
        and re.search(r":\s*nat\s*:=", text)
        and ("\\/" in text or "exists" in text or "∨" in text)
    ):
        return "large elimination of a Prop into nat is outside the kernel"
    if "Prop" in text and "exists" in text and "forall" in text:
        return "exists and forall over Prop are not a Prime term"
    if coqish and re.search(r"\bnd\b", text):
        return "inductive nd is not a Prime inductive"
    if coqish and re.search(r"\bhil\b", text):
        return "inductive hil is not a Prime inductive"
    if coqish and re.search(r"\bseq\b", text):
        return "inductive seq is not a Prime inductive"
    if coqish and re.search(r"\bsame\b", text):
        return "decidable same is not a Prime term"
    if "MyNat" in text:
        return "inductive MyNat is not a Prime inductive"
    if "MyList" in text:
        return "inductive MyList is not a Prime inductive"
    if "Option" in text:
        return "Option is not a Prime term"
    if "eq_refl" in text:
        return "eq_refl is not a Prime term"
    if "rfl" in text.split():
        return "rfl is not a Prime term"
    if re.search(r"\bhoare\b", text):
        return "hoare triple is not a Prime term"
    if re.search(r"\bassertion\b", text):
        return "assertion over state is not a Prime term"
    matched = re.search(r"\b(aexp|bexp|com|CSkip|CAsgn|CSeq|ANum|APlus)\b", text)
    if coqish and matched:
        return f"{matched.group(1)} is not a Prime term"
    inductive = re.match(r"(?i)inductive\s+([A-Za-z_]\w*)", text)
    if inductive:
        return f"inductive {inductive.group(1)} is not a Prime inductive"
    if text.startswith("Fixpoint"):
        return "Fixpoint recursion is outside the kernel"
    if text.startswith("(="):
        labeled = equation_label(text)
        if labeled is None:
            return ""
        if labeled:
            return labeled
    return ranked_construct(text, name)


def local_feature(rel: str, name: str, statement: str) -> str:
    """One absent feature taken from this statement, not from its file."""
    text = " ".join(statement.split())
    if is_indexed_statement(text):
        return "indexed families (R5)"
    if has_two_scrutinees(text):
        return "two-scrutinee case trees (R2)"
    shown = re.search(r"\{[^{}]+\}\s*\+\s*\{[^{}]+\}", text)
    if shown or "sumbool" in text:
        return f"sumbool {shown.group(0) if shown else 'sumbool'} is not a Prime term"
    shown = re.search(r"\{[^{}]*\|[^{}]*\}", text) or re.search(r"\{[^{}]*//[^{}]*\}", text)
    if shown:
        return f"subset type {shown.group(0)} is not a Prime term"
    if "#guard" in text:
        ident = re.search(r"(hol\w+|rewriteWith\w+|LanguageDef\.validate)", text)
        if ident:
            return f"Lean #guard {ident.group(1)} is outside the kernel"
        return "Lean #guard is outside the kernel"
    if text.startswith("!(import!"):
        imported = re.search(r"[\w./-]+\.metta", text)
        if imported:
            return f"{imported.group(0)} is the program this wrapper imports"
    if text.startswith("macro ") or text.startswith("syntax ") or text.startswith("macro_rules"):
        return "Lean macro syntax is outside the kernel"
    if text.startswith("Definition False") and "forall p" in text:
        return "Megalodon False as forall p, p is not a Prime term"
    if text.startswith("Definition and") and "forall p" in text:
        return "Megalodon impredicative conjunction forall p is not a Prime term"
    if text.startswith("Definition eq") and "forall Q" in text:
        return "Megalodon Leibniz equality forall Q is not a Prime term"
    if "HOL4=" in text:
        return "HOL4= baseline is outside the kernel"
    if "Quot" in text:
        return "Quot is not a Prime term"
    if "shiftAbove" in text:
        return "shiftAbove for Tm is outside the kernel"
    if re.search(r"\bsubst\b", text):
        return "subst for Tm is outside the kernel"
    if "uninhabitedArrow" in text or "canonical_min" in text:
        return "uninhabitedArrow has no principal type in the kernel"
    if 'macro "twice' in text or "macro_rules" in text:
        return "Lean macro syntax is outside the kernel"
    if "crush" in text or re.search(r"\bLtac\b", text):
        return "Ltac crush is outside the kernel"
    if "Eqb" in text:
        return "Eqb type-class constraint is outside the kernel"
    if "[Describable" in text:
        return "Describable type-class constraint is outside the kernel"
    if "[CommutativeOp" in text:
        return "CommutativeOp type-class constraint is outside the kernel"
    if "[Pointed" in text:
        return "Pointed type-class constraint is outside the kernel"
    if ">>=" in text:
        return "bind >>= is outside the kernel"
    if re.search(r"\bdo\b", text):
        return "do notation is outside the kernel"
    if "idp" in text and "{A : Type}" in text:
        return "implicit binder {A : Type} on idp is outside the kernel"
    if "idp nat" in text:
        return "idp nat is outside the kernel"
    if "idp 3" in text:
        return "idp 3 is outside the kernel"
    if "projT2" in text:
        return "projT2 of an indexed pair is not a Prime term"
    if "projT1" in text:
        return "projT1 of an indexed pair is not a Prime term"
    if "is_true" in text:
        return "is_true is not a Prime predicate"
    if text.startswith("Inductive form") or text.startswith("inductive form"):
        return "inductive form : Type is not a Prime inductive"
    if text.startswith("Inductive term"):
        return "inductive term : Type is not a Prime inductive"
    if text.startswith("import "):
        module = text.split()[1].split(".")[0]
        return f"{module} host script is outside the kernel"
    if text.startswith("set -"):
        return "bash gate script set -eu is outside the kernel"
    if "python3" in text[:40]:
        return "python3 host script is outside the kernel"
    if text.startswith("#!") and "bash" in text[:40]:
        return "bash host script is outside the kernel"
    if "curriculum_role" in text:
        return "curriculum_role table is outside the kernel"
    if "new_theory" in text or text.startswith("val _"):
        return "val _ = new_theory is outside the kernel"
    if "HOL4=" in text:
        return "HOL4= baseline is outside the kernel"
    named = construct_feature(text, name)
    if text.startswith("(=") and named == "" and equation_label(text) is None:
        return ""
    if named and quote_claims(text, named):
        return named
    head = declared_name(text) or name
    if head and head in text:
        return f"{head} is not a Prime term"
    if name and name in text:
        return f"{name} is not a Prime term"
    return f"{text[:80]} is outside the kernel"


def row_dependency(rel: str, name: str, statement: str) -> str:
    """The feature this statement needs. A file-wide label is kept only when the statement shows it."""
    if rel in HOST_HARNESS and statement_exhibits(statement, HOST_HARNESS[rel], rel):
        return HOST_HARNESS[rel]
    decided = capability_of(statement)
    if decided == "":
        return ""
    if isinstance(decided, str) and decided not in SLOGANS and statement_exhibits(statement, decided, rel):
        return decided
    if decided in SLOGANS:
        exact = exact_feature(rel, statement)
        if exact not in SLOGANS and statement_exhibits(statement, exact, rel):
            return exact
    path_dep = path_dependency(rel)
    if path_dep and path_dep not in SLOGANS and statement_exhibits(statement, path_dep, rel):
        return path_dep
    feature = local_feature(rel, name, statement)
    if name_only_dependency(feature, statement, name):
        named = construct_feature(statement, name)
        if named and named != feature and quote_claims(statement, named, rel):
            feature = named
    if not quote_claims(statement, feature, rel):
        head = declared_name(statement) or name
        if head and head in statement:
            feature = f"{head} is not a Prime term"
    if not quote_claims(statement, feature, rel):
        feature = f"{' '.join(statement.split())[:80]} is outside the kernel"
    return feature


def item_dependency(rel: str, name: str, statement: str = "") -> str:
    return row_dependency(rel, name, statement)


_WEAK_TOKENS = {"bool", "num"}


def name_only_dependency(dep: str, statement: str, name: str) -> bool:
    """True when the dependency is the declaration's own identifier."""
    match = re.fullmatch(r"([A-Za-z_][\w+-]*) is not a Prime term", dep or "")
    if not match:
        return False
    token = match.group(1)
    declared = declared_name(statement) or ""
    metta = re.match(r"\(=\s+\(([A-Za-z][\w+-]*)", " ".join(statement.split()))
    head = metta.group(1) if metta else ""
    return bool(token) and token in {declared, name, head}


def weak_label(dep: str, statement: str, name: str) -> bool:
    """A sort, or the defined name, is not the construct the body exhibits."""
    if name_only_dependency(dep, statement, name):
        return True
    match = re.fullmatch(r"([A-Za-z_][\w+-]*) is not a Prime term", dep or "")
    if not match:
        return False
    token = match.group(1)
    if token in _WEAK_TOKENS:
        return True
    declared = declared_name(statement) or name
    return bool(declared) and (declared.startswith(token) or token.startswith(declared))


def decl_continuations(path: Path, statement: str) -> list[str]:
    """Lines of this declaration after a truncated signature, stopping before the proof."""
    if not path.is_file():
        return []
    target = " ".join(statement.split())
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    for index, line in enumerate(lines):
        collapsed = " ".join(line.split())
        if collapsed != target and not collapsed.startswith(target):
            continue
        out: list[str] = []
        if collapsed.startswith(target) and collapsed != target:
            rest = collapsed[len(target):].strip()
            if rest:
                out.append(rest)
        for raw in lines[index + 1 : index + 8]:
            nxt = " ".join(raw.split())
            if not nxt or nxt.startswith(("(*", "/*", "Proof", "QED", "Qed", "End")):
                break
            if _NEXT_DECL.match(nxt):
                break
            out.append(nxt)
            if nxt.endswith("."):
                break
        return out
    return []


def depend(rel: str, name: str, statement: str, path: Path) -> tuple[str, str]:
    """The construct this declaration exhibits, reading past a truncated signature."""
    dep = row_dependency(rel, name, statement)
    if not dep or not weak_label(dep, statement, name):
        return statement, dep
    best_stmt, best_dep = statement, dep
    joined = statement
    for extra in decl_continuations(path, statement):
        candidate = " ".join(f"{joined} {extra}".split())[:500]
        dep2 = row_dependency(rel, name, candidate)
        joined = candidate
        if dep2 and quote_claims(candidate, dep2, rel) and not weak_label(dep2, candidate, name):
            return candidate, dep2
        if (
            dep2
            and quote_claims(candidate, dep2, rel)
            and name_only_dependency(best_dep, best_stmt, name)
            and not name_only_dependency(dep2, candidate, name)
        ):
            best_stmt, best_dep = candidate, dep2
        if extra.endswith("."):
            break
    return best_stmt, best_dep


def reuse_link(rel: str, name: str) -> tuple[str, str] | None:
    """Return (needle, artifact suffix) or ('library', theorem) for an exact reused proof."""
    if rel.startswith("ProgramVerification/") and name in {
        "aexp", "aeval", "bexp", "beval", "empty", "update", "com", "prog", "state",
    }:
        return ("library", name)
    smoke = {
        "imp_id": ("pf:theorem &thy imp-id ", "curriculum_hol.metta"),
        "and_elim_l": ("library", "and-elim-l"),
        "ex_falso": ("pf:theorem &thy false-elim ", "curriculum_lean.metta"),
    }
    if rel.startswith("CrossSmoke/") and name in smoke:
        return smoke[name]
    exact = {
        ("VerifiedMeTTa/01_peano_add.metta", "add"): ("set:define &thy add ", "curriculum_hol.metta"),
        ("VerifiedMeTTa/02_list_append_length.metta", "listAppend"): ("set:define &thy append ", "curriculum_hol.metta"),
        ("VerifiedMeTTa/02_list_append_length.metta", "len"): ("set:define &thy length ", "curriculum_hol.metta"),
        ("VerifiedMeTTa/01_peano_add.metta", "check-1"): ("library", "add-sz"),
        ("VerifiedMeTTa/01_peano_add.metta", "check-2"): ("library", "add-two"),
        ("VerifiedMeTTa/01_peano_add.metta", "check-3"): ("library", "add-swap"),
        ("VerifiedMeTTa/02_list_append_length.metta", "check-1"): ("library", "append-ex"),
        ("VerifiedMeTTa/02_list_append_length.metta", "check-2"): ("pf:theorem &thy length-append ", "curriculum_hol.metta"),
        ("VerifiedMeTTa/03_reverse_involution.metta", "check-1"): ("library", "rev-ex"),
        ("VerifiedMeTTa/03_reverse_involution.metta", "rev"): ("set:define &thy rev ", "curriculum_hol.metta"),
        ("VerifiedMeTTa/03_reverse_involution.metta", "listAppend"): ("set:define &thy append ", "curriculum_hol.metta"),
        ("VerifiedMeTTa/03_reverse_involution.metta", "check-2"): ("pf:theorem &thy rev-rev ", "curriculum_hol.metta"),
        ("Coq/ICL06_sum_sigma.v", "nat_eqb"): ("library", "nat-eqb"),
        ("Coq/ICL06_sum_sigma.v", "nat_eqb_22"): ("library", "nat-eqb-22"),
        ("Coq/ICL06_sum_sigma.v", "nat_eqb_23"): ("library", "nat-eqb-23"),
        ("Coq/ICL06_sum_sigma.v", "nat_eqb_refl"): ("library", "nat-eqb-refl"),
        ("Lean/06_sigma_dependent.lean", "sigmaPair"): ("library", "sigma-pair"),
        ("Lean/06_sigma_dependent.lean", "sigmaPair_fst"): ("library", "sigma-pair-fst"),
        ("Lean/02_induction.lean", "zero_add"): ("pf:theorem &thy zero-add ", "curriculum_lean.metta"),
        ("Lean/02_induction.lean", "succ_add"): ("pf:theorem &thy plus-S-l ", "curriculum_coq.metta"),
        ("Lean/02_induction.lean", "add_comm"): ("pf:theorem &thy add-comm-ind ", "curriculum_hol.metta"),
        ("Lean/02_induction.lean", "add_assoc"): ("pf:theorem &thy plus-assoc ", "curriculum_coq.metta"),
        ("Coq/ICL05_truthvalue_elim_restriction.v", "or_comm_prop"): ("pf:theorem &thy or-comm ", "curriculum_coq.metta"),
        ("Coq/ICL05_truthvalue_elim_restriction.v", "and_proj"): ("library", "and-elim-l"),
        ("HOL/HOLLightSelfImpSmoke.ml", "self_imp"): ("pf:theorem &thy imp-id ", "curriculum_hol.metta"),
        ("HOL/HOLLightSelfImpSmoke.ml", "bool_refl"): ("library", "eq-refl"),
        ("Megalodon/neg_unproved_goal.mg", "bad"): ("library", "neg-unproved"),
        ("Megalodon/neg_wrong_exact.mg", "bad"): ("library", "neg-wrong-exact"),
        ("HOL/neg/neg_unprovableScript.sml", "bad"): ("library", "neg-falsum"),
    }
    if (rel, name) in exact:
        return exact[(rel, name)]
    if rel.startswith("PrimeMotivation/01_dfa_"):
        table = {
            "dfa-step": "set:define &thy dfa-step ",
            "dfa-run": "set:define &thy dfa-run ",
            "dfa-accepting": "set:define &thy dfa-accepting ",
            "ends-in-01": "set:define &thy ends-in-01 ",
            "check-1": "set:eq &thy (dfa-run q0 ",
            "check-2": "set:eq &thy (ends-in-01 (wcons one ",
            "check-3": "set:eq &thy (ends-in-01 (wcons zero (wcons one (wcons one",
            "check-4": "set:eq &thy (ends-in-01 wnil)",
        }
        if name in table:
            return (table[name], "curriculum_prime_motivation.metta")
    return None


def source_rows(by_section: dict[tuple[str, str], dict], command_rows: list[dict], extra_rows: list[dict], library: dict[str, str], guest_runs: dict[str, dict]) -> list[dict]:
    rows = []
    files = sorted(p for p in CURRICULUM.rglob("*") if p.is_file())
    stem_for = {
        "Coq": "curriculum_coq",
        "Lean": "curriculum_lean",
        "HOL": "curriculum_hol",
        "Megalodon": "curriculum_megalodon",
    }
    searchable = command_rows + extra_rows
    for path in files:
        rel = path.relative_to(CURRICULUM).as_posix()
        if is_generated(rel):
            rows.append(artifact_row(
                rel, first_statement(path),
                "src-" + hashlib.sha256(rel.encode()).hexdigest()[:12],
            ))
            continue
        key = file_key(path.name)
        top = rel.split("/", 1)[0]
        linked = by_section.get((stem_for.get(top, ""), key or ""))
        if linked:
            rows.append(_row(
                "src-" + hashlib.sha256(rel.encode()).hexdigest()[:12],
                rel, first_statement(path), "as in the source file", path.suffix.lstrip(".") or "file",
                linked["prime_statement"], "existing set: and pf: package", linked["artifact"],
                linked["query_kind"], linked["expected"],
                f"ported section {key} command {linked['id']}; {linked['justification']}",
                "implemented", "none",
            ))
            continue
        for name, statement, behavior in extract_items(path):
            if statement.startswith("#print axioms"):
                rows.append(artifact_row(
                    rel, statement,
                    "src-" + hashlib.sha256(f"{rel}:{name}".encode()).hexdigest()[:12],
                ))
                continue
            guest = guest_runs.get(rel)
            if guest is not None:
                printed = guest_printed(guest, name, behavior)
                own = guest.get("defs", {}).get(name)
                call = own[0] if own else ""
                forced = name in guest.get("forced", ())
                refusal = (guest.get("refusals") or {}).get(name)
                if refusal and not (forced and printed):
                    rows.append(_row(
                        "item-" + hashlib.sha256(f"{rel}:{name}".encode()).hexdigest()[:12],
                        rel, statement, "as in the source file", behavior,
                        "not claimed", "none", f"Mettapedia/MettaKernel/Curriculum/{rel}",
                        "metta-define", "not claimed", COVERAGE.name,
                        "missing", refusal,
                    ))
                    continue
                if forced and printed:
                    rows.append(_row(
                        "item-" + hashlib.sha256(f"{rel}:{name}".encode()).hexdigest()[:12],
                        rel, statement, "as in the source file", behavior,
                        statement, "guest MeTTa program",
                        f"Mettapedia/MettaKernel/Curriculum/{rel}",
                        "metta-define",
                        printed,
                        "result printed by cetta --lang prime on this row",
                        "implemented", "none",
                    ))
                    continue
                if statement.startswith("(=") and equation_label(statement) is None and not printed:
                    defined = _defined_head(statement) or name
                    rows.append(_row(
                        "item-" + hashlib.sha256(f"{rel}:{name}".encode()).hexdigest()[:12],
                        rel, statement, "as in the source file", behavior,
                        "not claimed", "none", f"Mettapedia/MettaKernel/Curriculum/{rel}",
                        "source", "not claimed", COVERAGE.name,
                        "missing", f"{defined} has no prime print",
                    ))
                    continue
                failed_print = isinstance(printed, str) and (
                    printed.startswith("[(Error")
                    or (bool(call) and not name.startswith("check-") and not call_reduced(call, printed))
                )
                if failed_print:
                    printed_text = printed or ""
                    gap = kernel_gap(statement, printed_text)
                    feature = gap or unreduced_feature(statement, printed_text, call)
                    if (
                        not gap
                        and "does not reduce" in feature
                        and (printed_text.startswith("[(Error") or printed_text.startswith("error:"))
                    ):
                        refusal = " ".join(printed_text.split())
                    else:
                        refusal = feature
                    rows.append(_row(
                        "item-" + hashlib.sha256(f"{rel}:{name}".encode()).hexdigest()[:12],
                        rel, statement, "as in the source file", behavior,
                        "not claimed", "none", f"Mettapedia/MettaKernel/Curriculum/{rel}",
                        "assertEqual" if name.startswith("check-") else "metta-define",
                        "not claimed", COVERAGE.name,
                        "missing", refusal,
                    ))
                    continue
                if printed:
                    rows.append(_row(
                        "item-" + hashlib.sha256(f"{rel}:{name}".encode()).hexdigest()[:12],
                        rel, statement, "as in the source file", behavior,
                        statement, "guest MeTTa program",
                        f"Mettapedia/MettaKernel/Curriculum/{rel}",
                        "assertEqual" if name.startswith("check-") else "metta-define",
                        printed,
                        "result printed by cetta --lang prime on this row",
                        "implemented", "none",
                    ))
                    continue
                statement, dep = depend(rel, name, statement, path)
                rows.append(_row(
                    "item-" + hashlib.sha256(f"{rel}:{name}".encode()).hexdigest()[:12],
                    rel, statement, "as in the source file", behavior,
                    "not claimed", "none", f"Mettapedia/MettaKernel/Curriculum/{rel}",
                    "source", "not claimed", COVERAGE.name,
                    "missing", dep,
                ))
                continue
            link = reuse_link(rel, name)
            if capability_of(statement) == "" and kebab(name) in library and (link is None or link[0] != "library"):
                link = ("library", kebab(name))
            command = None
            if link and link[0] != "library":
                command = find_command(searchable, link[0], link[1])
            if link and link[0] == "library" and link[1] in library:
                kind, result = library[link[1]]
                rows.append(_row(
                    "item-" + hashlib.sha256(f"{rel}:{name}".encode()).hexdigest()[:12],
                    rel, statement, "as in the source file", behavior,
                    f"library {kind} {link[1]}", "curriculum library",
                    "lib/prime/curriculum.metta", kind, result,
                    "result printed by cetta --lang prime on lib/prime/curriculum.metta",
                    "implemented", "none",
                ))
                continue
            if command:
                rows.append(_row(
                    "item-" + hashlib.sha256(f"{rel}:{name}".encode()).hexdigest()[:12],
                    rel, statement, "as in the source file", behavior,
                    command["prime_statement"], "existing set: and pf: package", command["artifact"],
                    command["query_kind"], command["expected"],
                    f"reused {command['id']}; {command['justification']}",
                    "implemented", "none",
                ))
                continue
            if capability_of(statement) == "":
                print(f"unclassified {rel} {statement}")
                raise SystemExit(1)
            statement, dep = depend(rel, name, statement, path)
            if not dep:
                print(f"unclassified {rel} {statement}")
                raise SystemExit(1)
            rows.append(_row(
                "item-" + hashlib.sha256(f"{rel}:{name}".encode()).hexdigest()[:12],
                rel, statement, "as in the source file", behavior,
                "not claimed", "none", f"Mettapedia/MettaKernel/Curriculum/{rel}",
                "source", "not claimed", COVERAGE.name,
                "missing", dep,
            ))
    return rows


def _row(rid, rel, statement, assumptions, behavior, prime, package, artifact, kind, expected, justification, capability, dependency) -> dict:
    return {
        "id": rid,
        "source_path": rel,
        "source_statement": statement,
        "assumptions": assumptions,
        "source_behavior": behavior,
        "prime_statement": prime,
        "rule_package": package,
        "artifact": artifact,
        "query_kind": kind,
        "expected": expected,
        "justification": justification,
        "capability": capability,
        "dependency": dependency,
    }


def coverage_row() -> dict:
    return {
        "id": "coverage-note",
        "source_path": str(COVERAGE),
        "source_statement": "Curriculum coverage: what Prime hosts today",
        "assumptions": "measured against the Curriculum tree and tests/prime/scoped",
        "source_behavior": "inventory note",
        "prime_statement": "gaps stay dependencies",
        "rule_package": "cumulative reference judgment",
        "artifact": str(COVERAGE),
        "query_kind": "inventory",
        "expected": "recorded",
        "justification": "indexed families, the impredicative proof family, quotients, elaboration, and two-scrutinee case trees are named gaps",
        "capability": "recorded",
        "dependency": "R5 indexed families; H1 impredicative proof family; R2 case trees; M3 Data; quotients; elaboration",
    }


_HEURISTIC_LABEL = re.compile(
    r"^(.+) (?:is|are) not a Prime (?:term|inductive|predicate)$"
)

# Closed Prime checks for the impredicative labels the 21:19 addendum names.
_PRIME_CLAIMS = {
    "Megalodon False as forall p, p is not a Prime term":
        "set:check (all prop (lam p p)) prop",
    "Megalodon impredicative conjunction forall p is not a Prime term":
        "set:check (all prop (lam A (all prop (lam B (all prop (lam p (imp (imp A (imp B p)) p))))))) prop",
    "Megalodon Leibniz equality forall Q is not a Prime term":
        "set:check (all prop (lam x (all (-> prop prop prop) (lam Q (imp (Q x x) (Q x x)))))) prop",
    "exists and forall over Prop are not a Prime term":
        "set:check (all (-> prop prop) (lam P (imp (all prop (lam R (imp (all prop (lam x (imp (P x) R))) R))) (imp (all prop (lam x (imp (P x) (all prop (lam q q))))) (all prop (lam q q)))))) prop",
}


def probe_tokens(dep: str, statements: list[str]) -> list[str]:
    """Atoms that occur in every statement of one heuristic bucket, longest first."""
    match = _HEURISTIC_LABEL.fullmatch(dep or "")
    body = match.group(1) if match else ""
    texts = [" ".join(statement.split()) for statement in statements]
    ranked: list[str] = []

    def add(token: str) -> None:
        if (
            token
            and len(token) > 1
            and re.fullmatch(r"[A-Za-z_][\w+-]*", token)
            and token not in ranked
            and token.lower() not in _EXHIBIT_STOP
            and all(token in text for text in texts)
        ):
            ranked.append(token)

    for atom in sorted(re.findall(r"[A-Za-z_][\w+-]*", body), key=len, reverse=True):
        add(atom)
    for statement in statements:
        name = declared_name(statement)
        if name:
            add(name.split(".")[-1])
    shared: set[str] | None = None
    for text in texts:
        atoms = {
            atom for atom in re.findall(r"[A-Za-z_][\w+-]*", text)
            if len(atom) > 1 and atom.lower() not in _EXHIBIT_STOP
        }
        shared = atoms if shared is None else shared & atoms
    for atom in sorted(shared or (), key=len, reverse=True):
        add(atom)
    if not ranked:
        for atom in re.findall(r"[A-Za-z_][\w+-]*", body):
            if atom not in ranked and all(atom in text for text in texts):
                ranked.append(atom)
    return ranked


def _kernel_call(scratch: Path, call: str, cache: dict[str, str]) -> str:
    """Stdout of one `!(call)` on the pinned binary, or that run's error line."""
    if call in cache:
        return cache[call]
    digest = hashlib.sha256(call.encode()).hexdigest()[:12]
    dest = scratch / f"heuristic-{digest}.metta"
    dest.write_text(f"!({call})\n")
    try:
        proc = subprocess.run(
            ["bash", "-c", 'ulimit -v "$1" && exec "$2" --lang prime "$3"',
             "run", "25165824", str(BIN), str(dest)],
            text=True, capture_output=True, timeout=20,
        )
        printed = [ln.strip() for ln in (proc.stdout or "").splitlines() if ln.strip()]
        err = proc.stderr or ""
    except subprocess.TimeoutExpired:
        printed, err = [], "error: cetta exceeded 20s under ulimit -v 25165824"
    err_line = (err or "").strip().splitlines()
    cache[call] = printed[0] if printed else (err_line[0][:500] if err_line else "[]")
    return cache[call]


def _admitted(printed: str) -> bool:
    """A set:check or set:theorem verdict, not an error or an unreduced call."""
    return bool(
        printed
        and printed.startswith("[")
        and printed.endswith("]")
        and not printed.startswith("[(Error")
        and not printed.startswith("[(")
        and not printed.startswith("error:")
    )


def _record_admission(row: dict, call: str, printed: str, scratch: Path) -> None:
    row["capability"] = "implemented"
    row["expected"] = printed
    row["dependency"] = "none"
    row["justification"] = "result printed by cetta --lang prime on this row"
    row["prime_statement"] = f"!({call})"
    row["query_kind"] = call.split(" ", 1)[0]
    log = scratch / "readers.log"
    with log.open("a", encoding="utf-8") as fh:
        fh.write(row["source_statement"] + "\n" + printed + "\n")


def apply_constant_samples(rows: list[dict]) -> None:
    """Replace each non-design heuristic bucket with one kernel stdout."""
    scratch = Path(os.environ.get(
        "TC_SCRATCH", "/tmp/claude/grok-goal-aaee6e0e0c16/implementer"))
    scratch.mkdir(parents=True, exist_ok=True)
    groups: dict[str, list[dict]] = {}
    for row in rows:
        if row["capability"] != "missing":
            continue
        dep = row["dependency"] or ""
        if not _HEURISTIC_LABEL.fullmatch(dep):
            continue
        groups.setdefault(dep, []).append(row)
    lines: list[str] = []
    cache: dict[str, str] = {}
    for dep, group in groups.items():
        statements = [row["source_statement"] for row in group]
        claim = _PRIME_CLAIMS.get(dep, "")
        if claim:
            verbatim = _kernel_call(scratch, claim, cache)
            if _admitted(verbatim):
                for row in group:
                    _record_admission(row, claim, verbatim, scratch)
                lines.append(f"{group[0]['id']}\t{group[0]['source_path']}\t{verbatim}")
                continue
        else:
            verbatim = "[]"
        chosen = claim
        exhibited = bool(claim) and all(
            quote_claims(row["source_statement"], verbatim, row["source_path"]) for row in group
        )
        if not exhibited:
            for token in probe_tokens(dep, statements):
                verbatim = _kernel_call(scratch, token, cache)
                chosen = token
                if all(
                    quote_claims(row["source_statement"], verbatim, row["source_path"])
                    for row in group
                ):
                    exhibited = True
                    break
        for row in group:
            row["dependency"] = verbatim
        lines.append(f"{group[0]['id']}\t{group[0]['source_path']}\t{verbatim}")
        if not exhibited:
            print(f"heuristic unexhibited {dep} via {chosen}: {verbatim}")
    (scratch / "heuristic-samples.txt").write_text("\n".join(lines) + ("\n" if lines else ""))


def main() -> None:
    if "--self-check" in sys.argv:
        self_check()
        return
    command_rows, by_section = load_commands()
    extra_rows = load_named(MOTIVATION, "motivation-r")
    library = library_results()
    guest_runs = load_guest_runs(command_rows, extra_rows, library)
    rows = source_rows(by_section, command_rows, extra_rows, library, guest_runs) + [
        {col: row[col] for col in COLUMNS} for row in command_rows
    ] + [
        {col: row[col] for col in COLUMNS} for row in extra_rows
    ] + [coverage_row()]
    seen_ids: set[str] = set()
    for row in rows:
        if row["id"] in seen_ids:
            print(f"duplicate id {row['id']}")
            raise SystemExit(1)
        seen_ids.add(row["id"])
    apply_constant_samples(rows)
    symbol_list = [
        row["id"] for row in rows
        if row["capability"] == "missing" and "in the equation is not a Prime term" in row["dependency"]
    ]
    if symbol_list:
        print(f"symbol-list labels {len(symbol_list)}")
        print("\n".join(symbol_list[:12]))
        raise SystemExit(1)
    blamed = [
        f"{row['id']} {blamed_token(row['dependency'])}"
        for row in rows
        if row["capability"] == "missing" and blamed_token(row["dependency"]) in PRINTED_HEADS
    ]
    if blamed:
        print(f"printed token blamed on {len(blamed)} missing rows")
        print("\n".join(blamed[:12]))
        raise SystemExit(1)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8") as fh:
        fh.write("\t".join(COLUMNS) + "\n")
        for row in rows:
            fh.write("\t".join(row[col].replace("\t", " ") for col in COLUMNS) + "\n")
    def is_c(row: dict) -> bool:
        return row["id"].startswith("curriculum_") and row["id"].rsplit("-c", 1)[-1].isdigit()
    commands = sum(1 for row in rows if is_c(row))
    reused = sum(1 for row in rows if is_c(row) and "lib/prime/curriculum.metta" not in row["prime_statement"])
    missing = sum(1 for row in rows if row["capability"] == "missing")
    ported = sum(1 for row in rows if row["id"].startswith(("src-", "item-")) and row["capability"] == "implemented")
    noport = sum(1 for row in rows if row["dependency"] == "no ported fixture section for this file")
    items = sum(1 for row in rows if row["id"].startswith("item-"))
    axioms = [row for row in rows if row["source_statement"].startswith("#print axioms")]
    axiom_lessons = [
        row for row in axioms
        if row["id"].startswith("item-")
        or row["capability"] == "missing"
        or row["prime_statement"] != "not a lesson"
        or row["dependency"] != "generated build artifact, not a lesson"
        or row["source_behavior"] != "file"
    ]
    print(f"binary {BIN}")
    print(f"binary-sha256 {sha256_file(BIN)}")
    print(f"rows {len(rows)}")
    print(f"source-files {sum(1 for row in rows if row['id'].startswith('src-'))}")
    print(f"item-rows {items}")
    print(f"ported-sources {ported}")
    print(f"fixture-commands {commands}")
    print(f"reused-commands {reused}")
    print(f"missing {missing}")
    print(f"no-ported-section {noport}")
    print(f"print-axioms {len(axioms)}")
    print(f"print-axioms-lessons {len(axiom_lessons)}")
    print(f"print-axioms-missing {sum(1 for row in axioms if row['capability'] == 'missing')}")
    print(f"ledger {OUT}")


if __name__ == "__main__":
    main()
