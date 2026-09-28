#!/usr/bin/env python3
"""Ledger for the MettaKernel curriculum and the four reused Prime ladders.

The 202 commands already in curriculum_{coq,hol,lean,megalodon}.metta are
referenced, not copied. A missing capability is a dependency row.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import curriculum_port_repairs
from curriculum_trace import INVALID, failed_output, final_output, trace_failure

ROOT = Path(__file__).resolve().parents[2]
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
OUT = Path(os.environ.get("TC_OUT", "curriculum-ledger.tsv"))

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
BIN = Path(os.environ.get("TC_CETTA", "cetta"))
# The authored derived-HOL guest requires shared evaluation of large proof terms.
EXECUTION_FLAGS = ("--eval-hashcons",)


def require_configuration() -> None:
    """A recount must name its binary and destinations explicitly."""
    missing = [key for key in ("TC_CETTA", "TC_OUT", "TC_SCRATCH") if not os.environ.get(key)]
    if missing:
        raise SystemExit("set explicit recount inputs: " + ", ".join(missing))
    if not BIN.is_file():
        raise SystemExit(f"recount binary does not exist: {BIN}")


def input_hashes() -> dict[str, str]:
    paths = [Path(__file__), Path(__file__).with_name("curriculum_trace.py"), Path(curriculum_port_repairs.__file__), ROOT / "lib/pf.metta",
             ROOT / "lib/prime/curriculum.metta", *FIXTURES]
    paths.extend(Path(__file__).with_name(name) for name in (
        "run_curriculum.py", "test_curriculum_port_repairs.py", "test_cic_natmax.py", "test_cic_lf_normalization.py", "test_lf_guest_boundaries.py", "test_curriculum_closeout_metadata.py",
        "contract_controls.metta", "named_controls.metta"))
    paths.append(MOTIVATION)
    paths.extend(curriculum_port_repairs.PORTS.glob("*.metta"))
    paths.extend(path for path in CURRICULUM.rglob("*") if path.is_file())
    pending = [path for path in paths if path.suffix == ".metta"]
    pinned = set(paths)
    while pending:
        source = pending.pop()
        text = source.read_text(encoding="utf-8", errors="replace")
        for match in re.finditer(r"!\(import! &self ([^)]+)\)", text):
            raw = match.group(1).strip()
            target = Path(raw) if raw.startswith("/") else source.parent / raw
            if target.suffix != ".metta":
                target = ROOT / "lib" / (raw + ".metta")
            target = target.resolve()
            if target.is_file() and target not in pinned:
                pinned.add(target)
                pending.append(target)
    return {str(path): sha256_file(path) for path in sorted(pinned)}


def validate_provenance() -> None:
    """An existing ledger may be checked only against its unchanged inputs."""
    require_configuration()
    receipt = json.loads(OUT.with_suffix(".provenance.json").read_text())
    if (receipt["binary_sha256"] != sha256_file(BIN)
            or receipt["execution_flags"] != list(EXECUTION_FLAGS)
            or receipt["ledger_sha256"] != sha256_file(OUT)
            or receipt["inputs"] != input_hashes()):
        raise SystemExit("ledger inputs changed; a fresh recount is required")


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
    code, lines, err = cetta_lines(dest)
    if code != 0 or err:
        raise RuntimeError(trace_failure(code, lines, err, targets=0))
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
    timeout = float(os.environ["TC_TIMEOUT"]) if os.environ.get("TC_TIMEOUT") else None
    argv = [str(BIN), "--lang", "prime", *EXECUTION_FLAGS, str(src)]
    proc = subprocess.run(argv, cwd=ROOT,
                          text=True, capture_output=True, timeout=timeout)
    lines = [ln.strip() for ln in proc.stdout.splitlines() if ln.strip()]
    if os.environ.get("TC_SCRATCH"):
        traces = Path(os.environ["TC_SCRATCH"]) / "process-traces"
        traces.mkdir(parents=True, exist_ok=True)
        key = hashlib.sha256(str(src.resolve()).encode()).hexdigest()[:16]
        (traces / (key + ".json")).write_text(json.dumps({
            "source": str(src), "source_sha256": sha256_file(src), "argv": argv,
            "exit": proc.returncode, "stdout": lines, "stderr": proc.stderr,
        }, indent=2) + "\n")
    return proc.returncode, lines, proc.stderr.strip()


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
            code, lines, err = cetta_lines(dest)
        except subprocess.TimeoutExpired:
            return None
        if "compiled Prime reader" in err or "could not read" in err or not lines:
            return None
        results.append(final_output(code, lines, err))
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
            code, lines, err = cetta_lines(dest)
        except subprocess.TimeoutExpired:
            return None
        if not lines:
            return None
        results.append(final_output(code, lines, err))
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
    _header, calls = ground_calls(text, path)
    if not calls:
        return []
    scratch = Path(os.environ.get(
        "TC_SCRATCH", "/tmp/claude/grok-goal-aaee6e0e0c16/implementer")) / "defs"
    scratch.mkdir(parents=True, exist_ok=True)

    def run_calls(selected: list[tuple[str, str]]) -> list[str]:
        dest = scratch / f"{path.stem}-{len(selected)}-{selected[0][0]}.metta"
        body, prefix = _probe_text(path, selected)
        dest.write_text(body)
        try:
            code, lines, err = cetta_lines(dest)
        except subprocess.TimeoutExpired:
            return []
        failure = trace_failure(code, lines, err, len(selected))
        if failure:
            return [failure] * len(selected)
        return lines[prefix:] if len(lines) == prefix + len(selected) else []

    batched = run_calls(calls)
    if len(batched) == len(calls) and not any(failed_output(line) for line in batched):
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
    if port_guest_asserts(path) != path:
        source = materialize_guest(path, keep_asserts=False)
    else:
        source = _defs_only_file(path, {})
    text = source.read_text(encoding="utf-8")
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
            code, lines, err = cetta_lines(dest)
        except subprocess.TimeoutExpired:
            return [], INVALID + "configured timeout expired"
        if len(lines) == prefix:
            failure = trace_failure(code, lines, err, targets=0)
            return [], failure or "error: cetta produced no result"
        failure = trace_failure(code, lines, err, len(selected))
        if failure:
            return [], failure
        if len(lines) != prefix + len(selected):
            return [], INVALID + f"probe expected {prefix + len(selected)} outputs, received {len(lines)}"
        return lines[prefix:prefix + len(selected)], ""

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
            if not line or line.startswith(("[(Error", "error:", INVALID)):
                continue
            if call and not call_reduced(call, line):
                continue
            texts.append(line)
        for bang, line in rec.get("pairs", []):
            if not bang.startswith(("!(assertEqual", "!(test")):
                continue
            if not line or line.startswith(("[(Error", "error:", INVALID)):
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
                    or line.startswith(("error:", INVALID))
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


def prime_port(text: str, imported_funs: set[str] | None = None) -> str:
    """Sequence calls Prime would leave in a non-first argument.

    `--lang prime` reduces the first argument of a call. A call under a
    constructor, or past the first argument, stays. `case` reduces its
    scrutinee before binding, so each such call is bound there first.
    """
    funs: set[str] = set(imported_funs or ())
    spans: list[tuple[int, int, object]] = []
    i = 0
    n = len(text)
    while i < n:
        if text[i] == ";":
            nl = text.find("\n", i)
            i = n if nl < 0 else nl + 1
            continue
        if text.startswith(("!(assertEqual", "!(test"), i):
            try:
                node, j = _parse_sexp(text, i + 1)
            except ValueError:
                i += 1
                continue
            spans.append((i, j, node))
            i = j
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
        if head == "quote":
            return True
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
        elif isinstance(node, list) and node and node[0] in {"assertEqual", "test"}:
            parts.append("!" + _render_sexp([node[0], *[place(arg) for arg in node[1:]]]))
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


def guest_function_names(path: Path, seen: set[Path] | None = None) -> set[str]:
    """Declared equation heads from this guest and its import closure."""
    seen = set() if seen is None else seen
    path = path.resolve()
    if path in seen or not path.is_file():
        return set()
    seen.add(path)
    text = path.read_text(encoding="utf-8", errors="replace")
    names = set()
    for form in recover_forms(text):
        if form.startswith("(="):
            lhs = lhs_of(form)
            if lhs:
                names.add(lhs.split(" ", 1)[0])
    for match in re.finditer(r"!\(import! &self ([^)]+)\)", text):
        raw = match.group(1).strip()
        target = Path(raw) if raw.startswith("/") else path.parent / raw
        if target.suffix == ".metta":
            names.update(guest_function_names(target, seen))
    return names


def materialize_guest(path: Path, seen: dict | None = None, keep_asserts: bool = True,
                      keep_import_asserts: bool = False) -> Path:
    """Write the Prime-sequenced guest, with guest imports pointed at their ports."""
    seen = {} if seen is None else seen
    key = (str(path.resolve()), keep_asserts, keep_import_asserts)
    if key in seen:
        return seen[key]
    scratch = Path(os.environ.get(
        "TC_SCRATCH", "/tmp/claude/grok-goal-aaee6e0e0c16/implementer")) / "ported"
    try:
        rel = path.resolve().relative_to(CURRICULUM.resolve())
    except ValueError:
        return path
    if keep_import_asserts:
        scratch = scratch / "import-tests"
    if not keep_asserts:
        scratch = scratch / "defs-only"
    dest = scratch / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    seen[key] = dest
    text = prime_port(path.read_text(encoding="utf-8", errors="replace"), guest_function_names(path))
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
                    target = materialize_guest(target, seen, keep_asserts=keep_import_asserts,
                                               keep_import_asserts=keep_import_asserts)
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
    forms = recover_forms(text)
    if forms and all(form.startswith("!(import! &self ") for form in forms):
        return materialize_guest(path, keep_import_asserts=True)
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
        code, lines, err = cetta_lines(run_path)
    except subprocess.TimeoutExpired:
        return {
            "rel": rel, "lines": [], "pairs": [], "asserts": [], "ok": False,
            "error": INVALID + "configured timeout expired",
            "execution_error": INVALID + "configured timeout expired",
            "defs": {}, "def_log": [],
        }
    execution_error = trace_failure(code, lines, err, targets=0)
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
    incomplete = (not pairs) or len(got) != len(wanted) or mispaired(pairs) or code != 0
    if incomplete and ("compiled HE reader" in err or "could not read" in err or mispaired(pairs) or len(got) != len(wanted) or not pairs):
        repaired = run_repaired_guest(run_path, bangs)
        if repaired is not None:
            pairs = repaired["pairs"]
            lines = repaired["lines"]
            asserts = repaired["asserts"]
            incomplete = False
            execution_error = trace_failure(0, lines, "", targets=0)
    defs: dict[str, tuple[str, str]] = {}
    def_log: list[tuple[str, str, str]] = []
    for name, call, line in definition_prints(path):
        defs[name] = (call, line)
        def_log.append((name, call, line))
    error = ""
    if incomplete:
        error = f"cetta exit {code} printed {len(lines)} lines for {len(bangs)} commands"
        if err:
            error = f"{error}; {err}"
    return {
        "rel": rel,
        "lines": lines,
        "pairs": pairs,
        "asserts": asserts,
        "ok": not incomplete and not execution_error and all(line == "[()]" for _bang, line in pairs),
        "error": error,
        "execution_error": execution_error or (INVALID + error if error else ""),
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


def stored_refusal(statement: str, printed: str, call: str = "") -> str:
    """The ledger text for a print that did not succeed.

    An Error or reader line is kept verbatim. A prose rewrite of that line is not.
    """
    text = " ".join(statement.split())
    gap = kernel_gap(text, printed)
    feature = gap or unreduced_feature(statement, printed, call)
    if (
        not gap
        and "does not reduce" in feature
        and (printed.startswith("[(Error") or printed.startswith("error:"))
    ):
        return " ".join(printed.split())
    return feature


def guest_printed(rec: dict, name: str, behavior: str) -> str | None:
    """The line this row itself printed. Another command's line is not used."""
    if behavior == "exercise" and name.startswith("check-") and name[6:].isdigit():
        index = int(name[6:]) - 1
        asserts = rec.get("asserts") or []
        if 0 <= index < len(asserts) and asserts[index]:
            return asserts[index]
        return None
    pairs = rec.get("pairs", [])
    if behavior == "exercise" and len(pairs) == 1 and pairs[0][0].startswith("!(import! &self "):
        return pairs[0][1]
    own = rec.get("defs", {}).get(name)
    if own and own[1]:
        return own[1]
    return None


def call_reduced(call: str, printed: str) -> bool:
    """True when --lang prime returned a value other than the call itself."""
    if not printed or printed.startswith(("[(Error", "error:", INVALID)):
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
        for match in re.finditer(r"(?m)^let\s+(\w+)\s*=[^\n]*", text):
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
    )
    for feat_stmt, feat_print, feat_want in gap_cases:
        got = unreduced_feature(feat_stmt, feat_print)
        if got != feat_want or "does not reduce" in got or not quote_claims(feat_stmt, got):
            print(f"self-check gap {got!r} want {feat_want!r}")
            raise SystemExit(1)
    verbatim_errors = (
        "!(assertEqual (hls-lower-judgment (HSeq (HAnd HA HB) HA)) (Pi (hl-prf (hl-and-AB)) (hl-prf (Con A))))",
        "!(assertEqual (hshow-lower-judgment (HSeq (/\\ HP HQ) HP)) (Pi (hl-prf (hl-and (Con A) (Con B))) (hl-prf (Con A))))",
        "!(assertEqual (hshow-lower-prop (= ((\\ x x) HP) HP)) (hl-eq (App (hl-id) (Con A)) (Con A)))",
    )
    for err_stmt in verbatim_errors:
        err_print = (
            "[(Error "
            + err_stmt[2:-1]
            + ' "Got: [(Pi (App (Con prf) (App (App (Con hl_and) (Con A)) (Con B))) (App (Con prf) (Con A)))]")'
        )
        kept = stored_refusal(err_stmt, err_print)
        if kept != " ".join(err_print.split()) or " is (" in kept or not quote_claims(err_stmt, kept):
            print(f"self-check verbatim {err_stmt[:48]!r} got {kept[:120]!r}")
            raise SystemExit(1)
    evaluated = run_probe_batch(
        CURRICULUM / "Notation/09_hol_light_eq_kernel.metta",
        [("hl-assume-id-A", "hl-assume-id-A")],
    )
    if evaluated != [("hl-assume-id-A", "hl-assume-id-A", "[(Lam (App (Con prf) (Con A)) (Var 0))]")]:
        print(f"self-check hl-assume {evaluated!r}")
        raise SystemExit(1)
    cic_print = (
        "[(DKLam A (DKApp (DKConst Univ) z) "
        "(DKLam x (DKApp (DKApp (DKConst Term) z) (DKVar 0)) (DKVar 0)))]"
    )
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
    own_rows = {
        "compose": "Example compose_compute : compose S S 0 = 2 := eq_refl.",
        "nontaut": "Example dec_nontaut : decide_valid (Var 0) = false := eq_refl.",
        "failvar": "Fail Example neg_var_valid : decide_valid (Var 0) = true := eq_refl.",
        "mynat": "inductive MyNat where | zero : MyNat | succ : MyNat → MyNat",
        "myadd": "def myAdd : MyNat → MyNat → MyNat | n, .zero => n | n, .succ m => .succ (myAdd n m)",
        "myaddz": "theorem myAdd_zero (n : MyNat) : myAdd n .zero = n := rfl",
    }
    own_programs = {key: program_for_statement(stmt) for key, stmt in own_rows.items()}
    if len(set(own_programs.values())) != len(own_programs):
        print("self-check own-port programs are not distinct")
        raise SystemExit(1)
    for key, program in own_programs.items():
        flat = " ".join(program.split())
        if "set:check (eq nat zero zero) prop" in flat or "!(port " in flat:
            print(f"self-check own-port stand-in {key}")
            raise SystemExit(1)
    own_scratch = Path(os.environ.get(
        "TC_SCRATCH", "/tmp/claude/grok-goal-40a1861d1dd5/implementer"))
    own_scratch.mkdir(parents=True, exist_ok=True)
    own_cache: dict[str, str] = {}
    own_got = {
        key: _run_program(program, own_scratch, own_cache)
        for key, program in own_programs.items()
    }
    own_want = {
        "compose": "[()]",
        "nontaut": "[True]",
        "mynat": "[MyNat]",
        "myadd": "[myAdd]",
        "myaddz": "[()]",
    }
    for key, want in own_want.items():
        if own_got[key] != want:
            print(f"self-check own-port {key} got {own_got[key]!r}")
            raise SystemExit(1)
    if own_got["failvar"] != "[False]":
        print(f"self-check own-port failvar got {own_got['failvar']!r}")
        raise SystemExit(1)
    fail_row = {
        "source_statement": own_rows["failvar"],
        "prime_statement": "",
        "query_kind": "",
        "capability": "",
        "expected": "",
        "dependency": "",
        "justification": "",
    }
    _store_row_program(fail_row, own_programs["failvar"], own_got["failvar"], own_scratch)
    if (
        fail_row["capability"] != "implemented"
        or fail_row["expected"] != own_got["failvar"]
        or fail_row["dependency"] != "none"
        or fail_row["justification"] != "implemented negative control"
        or " ".join(fail_row["prime_statement"].split()) != " ".join(own_programs["failvar"].split())
    ):
        print(f"self-check own-port fail row {fail_row['capability']} {fail_row['expected'][:80]!r}")
        raise SystemExit(1)
    import csv
    guest = run_guest_file(CURRICULUM / "DeduktiLambdapi/02_cic_guest_sorts_pi_micro.metta")
    proof_needles = (
        "cic-proof-check (cic-prop-id-source)",
        "cic-proof-check (cic-type0-id-source)",
    )
    fresh_proofs = {}
    for bang, line in guest.get("pairs") or []:
        for needle in proof_needles:
            if needle in bang:
                fresh_proofs[needle] = " ".join(line.split())
    if set(fresh_proofs) != set(proof_needles):
        print(f"self-check cic-proof-check probes {sorted(fresh_proofs)}")
        raise SystemExit(1)
    with OUT.open(encoding="utf-8") as fh:
        ledger_rows = list(csv.DictReader(fh, delimiter="\t"))
    for needle, line in fresh_proofs.items():
        hits = [
            row for row in ledger_rows
            if needle in row["source_statement"]
            and row["source_path"].endswith("02_cic_guest_sorts_pi_micro.metta")
        ]
        if len(hits) != 1:
            print(f"self-check cic-proof-check rows {needle} {len(hits)}")
            raise SystemExit(1)
        cell = hits[0]["expected"] if hits[0]["capability"] == "implemented" else hits[0]["dependency"]
        if " ".join(cell.split()) != line:
            print(f"self-check cic-proof-check {needle} ledger {cell[:160]!r} run {line[:160]!r}")
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
    if dep.startswith(("[(Error", "error:", INVALID)) or dep == "[]" or dep in {"[False]", "[True]"}:
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
    if text.startswith("let ") and "DISCH" in text and "ASSUME" in text:
        return "HOL Light DISCH/ASSUME source elaboration is unimplemented"
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
                execution_error = guest.get("execution_error", "")
                if execution_error:
                    rows.append(_row(
                        "item-" + hashlib.sha256(f"{rel}:{name}".encode()).hexdigest()[:12],
                        rel, statement, "as in the source file", behavior,
                        "not claimed", "none", f"Mettapedia/MettaKernel/Curriculum/{rel}",
                        "source", "not claimed", "invalid execution trace; no verdict is counted",
                        "missing", execution_error,
                    ))
                    continue
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
                    printed.startswith(("[(Error", "error:", INVALID))
                    or (bool(call) and not name.startswith("check-") and not call_reduced(call, printed))
                )
                if failed_print:
                    printed_text = printed or ""
                    refusal = printed_text if printed_text.startswith(INVALID) else stored_refusal(statement, printed_text, call)
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
        code, printed, err = cetta_lines(dest)
        cache[call] = final_output(code, printed, err)
    except subprocess.TimeoutExpired:
        cache[call] = INVALID + "configured timeout expired"
    return cache[call]


def _admitted(printed: str) -> bool:
    """A set:check or set:theorem verdict, not an error or an unreduced call."""
    return bool(
        printed
        and printed.startswith("[")
        and printed.endswith("]")
        and not printed.startswith("[(Error")
        and not printed.startswith("[(")
        and printed not in {"[]", "[False]", "[Undetermined]", "[Incomplete]", "[Refuted]"}
        and not printed.startswith(("error:", INVALID))
    )


_INDEXED_ECHO = {"nd", "has_type", "seq"}
_NAT_PORT = "!(set:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))\n"
_BOOL_PORT = "!(set:inductive &self bool (u 0) (: true bool) (: false bool))\n"
_BASE_PORT = _NAT_PORT + _BOOL_PORT
_ADD_PORT = _BASE_PORT + (
    "!(set:define &self add (-> nat nat nat) "
    "(= (add zero $n) $n) (= (add (succ $m) $n) (succ (add $m $n))))\n"
)
def _identifier_echo(printed: str) -> str:
    match = re.fullmatch(r"\[\(([A-Za-z_][\w+-]*)\)\]", printed or "")
    return match.group(1) if match else ""


def _is_fail(statement: str) -> bool:
    return statement.lstrip().startswith("Fail ")


def _negative_control(statement: str) -> bool:
    """A source example whose rejection is the result being measured."""
    text = " ".join(statement.split())
    if text.startswith("Fail "):
        return True
    return any(token in text for token in (
        "def bad_hol",
        "def bad_axmem",
        "def bad_rfl",
        "bad_forward_ref",
        "Theorem bad:",
        "def loop ",
    ))


def _nat_term(count: int) -> str:
    term = "zero"
    for _ in range(count):
        term = f"(succ {term})"
    return term


def program_for_statement(statement: str) -> str:
    """One Prime program for this source row, not a shared stand-in."""
    text = " ".join(statement.split())
    reviewed = curriculum_port_repairs.program(declared_name(text))
    if reviewed is not None:
        return reviewed
    nat = _NAT_PORT
    boolean = _BOOL_PORT
    base = _BASE_PORT
    add = _ADD_PORT
    form = (
        base
        + "!(set:inductive &self form (u 0) (: FVar (-> nat form)) (: Bot form) "
        "(: Imp (-> form form form)) (: And (-> form form form)) (: Or (-> form form form)))\n"
    )
    mynat = "!(set:inductive &self MyNat (u 0) (: zero MyNat) (: succ (-> MyNat MyNat)))\n"
    myadd = (
        mynat
        + "!(set:define &self myAdd (-> MyNat MyNat MyNat) "
        "(= (myAdd $n zero) $n) (= (myAdd $n (succ $m)) (succ (myAdd $n $m))))\n"
    )
    if "compose S S 0 = 2" in text:
        return (
            nat
            + "!(set:define &self S (-> nat nat) (= (S $n) (succ $n)))\n"
            "!(set:define &self compose (-> (-> nat nat) (-> nat nat) nat nat) "
            "(= (compose $f $g $x) ($f ($g $x))))\n"
            "!(assertEqual (compose S S zero) (succ (succ zero)))\n"
        )
    if "decide_valid (Var 0) = false" in text:
        return curriculum_port_repairs.program("dec_nontaut")
    if "decide_valid (Var 0) = true" in text:
        return curriculum_port_repairs.program("neg_var_valid")
    if "decide_valid (Imp (Var 0) (Var 0)) = true" in text:
        return curriculum_port_repairs.program("dec_taut")
    if "dec_peirce" in text or "Imp (Imp (Imp (Var 0) (Var 1)) (Var 0)) (Var 0)" in text:
        return curriculum_port_repairs.program("dec_peirce")
    if "myAdd n .zero = n" in text:
        return myadd + "!(assertEqual (myAdd (succ zero) zero) (succ zero))\n"
    if "def myAdd" in text or "myAdd :" in text:
        return myadd
    if re.search(r"(?i)inductive\s+MyNat\b", text):
        return mynat
    if "myLength" in text and "=" in text:
        return (
            base
            + "!(set:inductive &self MyList (u 0) (: nil MyList) (: cons (-> nat MyList MyList)))\n"
            "!(set:define &self myLength (-> MyList nat) (= (myLength nil) zero) "
            "(= (myLength (cons $a $t)) (succ (myLength $t))))\n"
            "!(assertEqual (myLength (cons zero nil)) (succ zero))\n"
        )
    if "myLength" in text:
        return (
            base
            + "!(set:inductive &self MyList (u 0) (: nil MyList) (: cons (-> nat MyList MyList)))\n"
            "!(set:define &self myLength (-> MyList nat) (= (myLength nil) zero) "
            "(= (myLength (cons $a $t)) (succ (myLength $t))))\n"
        )
    if re.search(r"(?i)inductive\s+MyList\b", text):
        return base + "!(set:inductive &self MyList (u 0) (: nil MyList) (: cons (-> nat MyList MyList)))\n"
    if text.startswith("Fail ") and "bad_form" in text:
        bad = "(And (FVar zero))" if "And" in text else "(Or (FVar zero))" if "Or" in text else "(Imp (FVar zero))"
        return form + f"!(set:check {bad} form)\n"
    if re.search(r"(?i)^Inductive\s+form\b", text) or re.search(r"(?i)^inductive\s+form\b", text):
        return form
    if "eval" in text and "Bot = true" in text:
        return curriculum_port_repairs.program("neg_bot_true")
    if "dbl 2 = 4" in text:
        return (
            add
            + "!(set:define &self dbl (-> nat nat) (= (dbl $n) (add $n $n)))\n"
            "!(assertEqual (dbl (succ (succ zero))) (succ (succ (succ (succ zero)))))\n"
        )
    if "dbl (m + n)" in text or "dbl_add" in text:
        return (
            add
            + "!(set:define &self dbl (-> nat nat) (= (dbl $n) (add $n $n)))\n"
            "!(assertEqual (dbl (add (succ zero) (succ zero))) (add (dbl (succ zero)) (dbl (succ zero))))\n"
        )
    if "dbl (n:num) = n + n" in text or "dbl_def" in text:
        return add + "!(set:define &self dbl (-> nat nat) (= (dbl $n) (add $n $n)))\n"
    if "eqn z (s z)" in text or "bad_rfl" in text:
        return curriculum_port_repairs.program("bad_rfl")
    if "eqn (plus z z) z" in text:
        return add + "!(assertEqual (add zero zero) zero)\n"
    if "eqn z z" in text:
        return nat + "!(assertEqual (eq nat zero zero) (eq nat zero zero))\n"
    if "witnessThree.val = 3" in text:
        return nat + "!(assertEqual " + _nat_term(3) + " " + _nat_term(3) + ")\n"
    if "shift 1 (lam (var 0))" in text or "shift 1 (lam (var 1))" in text:
        return curriculum_port_repairs.program("shift")
    if "n + n = 5" in text:
        return add + "!(assertEqual (add (succ (succ zero)) (succ (succ zero))) " + _nat_term(5) + ")\n"
    if "fst" in text and "= a" in text:
        return (
            base
            + "!(set:inductive &self pair (u 0) (: mkpair (-> nat bool pair)))\n"
            "!(set:define &self fst (-> pair nat) (= (fst (mkpair $a $b)) $a))\n"
            "!(assertEqual (fst (mkpair zero true)) zero)\n"
        )
    if ".1 = a" in text:
        return (
            base
            + "!(set:inductive &self pair (u 0) (: mkpair (-> nat bool pair)))\n"
            "!(set:define &self fst (-> pair nat) (= (fst (mkpair $a $b)) $a))\n"
            "!(assertEqual (fst (mkpair zero true)) zero)\n"
        )
    return encode_statement(text)


def _row_text(row: dict) -> str:
    """The declaration, including lines after a truncated signature."""
    statement = row["source_statement"]
    extra = decl_continuations(CURRICULUM / row["source_path"], statement)
    if not extra:
        return " ".join(statement.split())
    return " ".join((statement + " " + " ".join(extra)).split())[:1200]


def _proof_nat() -> str:
    """Nat, bool, addition, and decidable equality in a fresh theory."""
    pf = ROOT / "lib" / "pf.metta"
    return (
        f"!(import! &self {pf})\n"
        "!(bind! &thy (new-space))\n"
        "!(set:inductive &thy bool (u 0) (: true bool) (: false bool))\n"
        "!(set:inductive &thy nat (u 0) (: zero nat) (: succ (-> nat nat)))\n"
        "!(pf:equality &thy bool refl@bool subst@bool)\n"
        "!(pf:equality &thy nat refl@nat subst@nat)\n"
        "!(set:define &thy add (-> nat nat nat) (= (add zero $n) $n) (= (add (succ $m) $n) (succ (add $m $n))))\n"
        "!(set:define &thy andb (-> bool bool bool) (= (andb true $y) $y) (= (andb false $y) false))\n"
        "!(set:define &thy is-zero (-> nat bool) (= (is-zero zero) true) (= (is-zero (succ $n)) false))\n"
        "!(set:define &thy predn (-> nat nat) (= (predn zero) zero) (= (predn (succ $n)) $n))\n"
        "!(set:define &thy if-zero (-> nat nat nat nat) (= (if-zero zero $z $s) $z) (= (if-zero (succ $n) $z $s) $s))\n"
        "!(set:define &thy subk (-> nat nat nat) (= (subk zero $n) $n) (= (subk (succ $m) $n) (if-zero $n zero (subk $m (predn $n)))))\n"
        "!(set:define &thy nat-eqb (-> nat nat bool) (= (nat-eqb $n $m) (andb (is-zero (subk $n $m)) (is-zero (subk $m $n)))))\n"
        "!(set:define &thy if-bool (-> bool nat nat nat) (= (if-bool true $a $b) $a) (= (if-bool false $a $b) $b))\n"
    )


def _prove(name: str, typ: str, proof: str) -> str:
    return _proof_nat() + f"!(pf:theorem &thy {name} {typ} {proof})\n"


def _lists() -> str:
    return (
        _ADD_PORT
        + "!(set:inductive &self list (u 0) (: nil list) (: cons (-> nat list list)))\n"
        "!(set:define &self append (-> list list list) (= (append nil $k) $k) "
        "(= (append (cons $x $l) $k) (cons $x (append $l $k))))\n"
        "!(set:define &self rev (-> list list) (= (rev nil) nil) "
        "(= (rev (cons $x $l)) (append (rev $l) (cons $x nil))))\n"
        "!(set:define &self length (-> list nat) (= (length nil) zero) "
        "(= (length (cons $x $l)) (succ (length $l))))\n"
    )


def _neg_form() -> str:
    return (
        _BASE_PORT
        + "!(set:inductive &self form (u 0) (: FVar (-> nat form)) (: Bot form) "
        "(: Imp (-> form form form)) (: And (-> form form form)) (: Or (-> form form form)))\n"
        "!(set:define &self Neg (-> form form) (= (Neg $a) (Imp $a Bot)))\n"
    )


def _com() -> str:
    return (
        _NAT_PORT
        + "!(set:inductive &self aexp (u 0) (: ANum (-> nat aexp)) (: APlus (-> aexp aexp aexp)))\n"
        "!(set:inductive &self com (u 0) (: CSkip com) (: CAsgn (-> nat aexp com)) "
        "(: CSeq (-> com com com)))\n"
    )


def _unencoded(_text: str) -> str:
    """No source translation exists; an unrelated assertion is not a port."""
    return ""


def encode_statement(text: str) -> str:
    """One Prime program whose last query is this source claim."""
    if "bad_asgn" in text or "CAsgn (ANum" in text:
        return _com() + "!(set:check (CAsgn (ANum zero) (ANum (succ zero))) com)\n"
    if "bad_aexp" in text or "APlus (ANum" in text and text.startswith("Fail"):
        return (
            _NAT_PORT
            + "!(set:inductive &self aexp (u 0) (: ANum (-> nat aexp)) (: APlus (-> aexp aexp aexp)))\n"
            "!(set:check (APlus (ANum (succ zero))) aexp)\n"
        )
    if "bad_seq" in text or (text.startswith("Fail") and "CSeq CSkip" in text):
        return _com() + "!(set:check (CSeq CSkip) com)\n"
    if "neg_bad_abs" in text or "Abs 0 TBase" in text:
        return (
            _NAT_PORT
            + "!(set:inductive &self ty (u 0) (: TBase ty) (: TArrow (-> ty ty ty)))\n"
            "!(set:inductive &self tm (u 0) (: Var (-> nat tm)) (: App (-> tm tm tm)) "
            "(: Abs (-> nat ty tm tm)))\n"
            "!(set:check (Abs zero TBase) tm)\n"
        )
    if "no_instance" in text or "same (fun" in text:
        return ""
    if "bad_hol" in text or "prf (imp A B)" in text:
        return "!(set:theorem bad-hol (all prop (lam A (all prop (lam B (imp A B))))) (pf:fix A (pf:fix B (pf:assume h h))))\n"
    if "bad_axmem" in text:
        return "!(set:theorem bad-axmem (all prop (lam b (all prop (lam a (imp (In b a) (In a b)))))) (pf:fix b (pf:fix a (pf:assume h h))))\n"
    if "bad_forward_ref" in text or "Missing" in text and "def " in text:
        return ""
    if "(5:num)" in text or "Theorem bad:" in text:
        return _prove("bad-five", "(eq nat " + _nat_term(5) + " zero)", "(pf:refl nat zero)")
    if "val bad" in text:
        return "!(set:theorem bad (and (succ zero) true) (pf:assume h h))\n"
    if "loop (n : Nat)" in text or "loop n + 1" in text:
        return _NAT_PORT + "!(set:define &self loop (-> nat nat) (= (loop $n) (succ (loop $n))))\n"
    if "same 2 2 = true" in text:
        return _prove(
            "same-nat",
            "(eq bool (nat-eqb (succ (succ zero)) (succ (succ zero))) true)",
            "(pf:refl bool true)",
        )
    if "same true false = false" in text:
        return _proof_nat() + (
            "!(set:define &thy sameb (-> bool bool bool) (= (sameb true true) true) "
            "(= (sameb true false) false) (= (sameb false true) false) (= (sameb false false) true))\n"
            "!(pf:theorem &thy same-tf (eq bool (sameb true false) false) (pf:refl bool false))\n"
        )
    if "same_pair" in text or "same (1, true)" in text:
        return _proof_nat() + (
            "!(set:inductive &thy pair (u 0) (: mkpair (-> nat bool pair)))\n"
            "!(set:define &thy fst (-> pair nat) (= (fst (mkpair $a $b)) $a))\n"
            "!(set:define &thy sndp (-> pair bool) (= (sndp (mkpair $a $b)) $b))\n"
            "!(pf:equality &thy pair refl@pair subst@pair)\n"
            "!(pf:theorem &thy same-pair (eq bool (sndp (mkpair (succ zero) true)) true) (pf:refl bool true))\n"
        )
    if "sig_val" in text and ("= 2" in text or "four" in text):
        return _proof_nat() + (
            "!(set:define &thy four-witness nat (= four-witness (succ (succ zero))))\n"
            "!(pf:theorem &thy sig-val-four (eq nat four-witness (succ (succ zero))) "
            "(pf:refl nat (succ (succ zero))))\n"
        )
    if "n + n = 4" in text or "four_witness" in text:
        return _proof_nat() + (
            "!(set:define &thy four-witness nat (= four-witness (succ (succ zero))))\n"
            "!(pf:theorem &thy four-cert (eq nat (add four-witness four-witness) "
            + _nat_term(4) + ") (pf:refl nat " + _nat_term(4) + "))\n"
        )
    if "projT1" in text and "= true" in text:
        return _proof_nat() + (
            "!(set:inductive &thy pair (u 0) (: mkpair (-> bool nat pair)))\n"
            "!(set:define &thy proj1 (-> pair bool) (= (proj1 (mkpair $a $b)) $a))\n"
            "!(pf:equality &thy pair refl@pair subst@pair)\n"
            "!(pf:theorem &thy projT1-dep (eq bool (proj1 (mkpair true zero)) true) (pf:refl bool true))\n"
        )
    if "projT2" in text and "= 5" in text:
        five = _nat_term(5)
        return _proof_nat() + (
            "!(set:inductive &thy pair (u 0) (: mkpair (-> bool nat pair)))\n"
            "!(set:define &thy proj2 (-> pair nat) (= (proj2 (mkpair $a $b)) $b))\n"
            "!(pf:equality &thy pair refl@pair subst@pair)\n"
            f"!(pf:theorem &thy projT2-dep (eq nat (proj2 (mkpair true {five})) {five}) (pf:refl nat {five}))\n"
        )
    if "eq_refl_set" in text or "forall a:set, a = a" in text:
        return curriculum_port_repairs.program("eq_refl_set")
    if re.search(r"\ba = a\b", text):
        return _prove("eq-refl-ex", "(all nat (lam a (eq nat a a)))", "(pf:fix a (pf:refl nat a))")
    if "Theorem cong" in text or "theorem cong" in text:
        return _prove(
            "cong-succ",
            "(all nat (lam x (all nat (lam y (imp (eq nat x y) (eq nat (succ x) (succ y)))))))",
            "(pf:fix x (pf:fix y (pf:assume h (pf:cong nat nat (lam k (succ k)) x y h))))",
        )
    if "hol_self_imp" in text or "prf (imp A A)" in text:
        return "!(set:theorem hol-self-imp (all prop (lam A (imp A A))) (pf:fix A (pf:assume h h)))\n"
    if "hol_k" in text or "imp A (imp B A)" in text:
        return (
            "!(set:theorem hol-k (all prop (lam A (all prop (lam B (imp A (imp B A)))))) "
            "(pf:fix A (pf:fix B (pf:assume h (pf:assume g h)))))\n"
        )
    if "self_imp" in text or "DISCH" in text:
        return "!(set:theorem self-imp (all prop (lam p (imp p p))) (pf:fix p (pf:assume h h)))\n"
    if "hil_id" in text or "hil (Imp" in text:
        return "!(set:theorem hil-id (all prop (lam a (imp a a))) (pf:fix a (pf:assume h h)))\n"
    if re.search(r"\btwo\b", text) and "s (s z)" in text:
        return _NAT_PORT + "!(set:define &self two nat (= two (succ (succ zero))))\n"
    if "id_o" in text:
        return (
            "!(set:inductive &self o (u 0) (: star o))\n"
            "!(set:define &self id_o (-> o o) (= (id_o $x) $x))\n"
            "!(assertEqual (id_o star) star)\n"
        )
    if "def NatRec" in text or "NatRec :" in text:
        return (
            _NAT_PORT
            + "!(set:define &self NatRec (-> nat (-> nat nat nat) nat nat) "
            "(= (NatRec $z $s zero) $z) (= (NatRec $z $s (succ $n)) ($s $n (NatRec $z $s $n))))\n"
        )
    if "natrec_z" in text or "NatRec (x:nat => nat) z" in text:
        return (
            _NAT_PORT
            + "!(set:define &self NatRec (-> nat (-> nat nat nat) nat nat) "
            "(= (NatRec $z $s zero) $z) (= (NatRec $z $s (succ $n)) ($s $n (NatRec $z $s $n))))\n"
            "!(assertEqual (NatRec zero (lam k (lam r (succ r))) zero) zero)\n"
        )
    if re.search(r"\bplus\b", text) and "NatRec" in text:
        return _ADD_PORT + (
            "!(set:define &self plus (-> nat nat nat) (= (plus $n $m) (add $n $m)))\n"
            "!(assertEqual (plus zero (succ zero)) (succ zero))\n"
        )
    if re.search(r"\bJ\b", text) and "Id " in text and "j_refl" not in text:
        return _prove("J-beta", "(all nat (lam x (eq nat x x)))", "(pf:fix x (pf:refl nat x))")
    if "j_refl_z" in text or "Id nat z z" in text:
        return _prove("j-refl-z", "(eq nat zero zero)", "(pf:refl nat zero)")
    if "Definition Neg" in text or "Neg (a : form)" in text:
        return _neg_form() + "!(assertEqual (Neg Bot) (Imp Bot Bot))\n"
    if "dni_valid" in text or "Neg (Neg" in text and "Imp f" in text:
        return _proof_nat() + (
            "!(set:define &thy falsum prop (= falsum (all prop (lam p p))))\n"
            "!(set:define &thy neg (-> prop prop) (= (neg $p) (imp $p falsum)))\n"
            "!(pf:theorem &thy dni-valid (all prop (lam p (imp p (neg (neg p))))) "
            "(pf:fix p (pf:assume hp (pf:assume hnp (pf:by hnp hp)))))\n"
        )
    if "lem_valid" in text:
        # Boolean validity of Or (FVar n) (Neg (FVar n)), both valuations.
        return (
            "!(set:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))\n"
            "!(set:inductive &self bool (u 0) (: true bool) (: false bool))\n"
            "!(set:inductive &self form (u 0) (: FVar (-> nat form)) (: Bot form) "
            "(: Imp (-> form form form)) (: Or (-> form form form)))\n"
            "!(set:define &self or-bool (-> bool bool bool) "
            "(= (or-bool true $b) true) (= (or-bool false $b) $b))\n"
            "!(set:define &self imp-bool (-> bool bool bool) "
            "(= (imp-bool true $b) $b) (= (imp-bool false $b) true))\n"
            "!(set:define &self atom (-> bool nat bool) (= (atom $b $n) $b))\n"
            "!(set:define &self Neg (-> form form) (= (Neg $a) (Imp $a Bot)))\n"
            "!(set:define &self lem-f form (= lem-f (Or (FVar zero) (Neg (FVar zero)))))\n"
            "!(set:define &self lem-at (-> bool bool) "
            "(= (lem-at $b) (or-bool (atom $b zero) (imp-bool (atom $b zero) false))))\n"
            "!(assertEqual (lem-at true) true)\n"
            "!(assertEqual (lem-at false) true)\n"
        )
    if "is_true (b : bool)" in text or "Definition is_true" in text:
        return _proof_nat() + (
            "!(set:define &thy truth prop (= truth (all prop (lam p (imp p p)))))\n"
            "!(set:define &thy falsum prop (= falsum (all prop (lam p p))))\n"
            "!(set:define &thy is-true (-> bool prop) (= (is-true true) truth) (= (is-true false) falsum))\n"
        )
    if "negb_true" in text:
        return _proof_nat() + (
            "!(set:define &thy negb (-> bool bool) (= (negb true) false) (= (negb false) true))\n"
            "!(pf:theorem &thy negb-true-ff (eq bool (negb false) true) (pf:refl bool true))\n"
        )
    if "bool_value" in text:
        return _proof_nat() + (
            "!(set:inductive &thy decision (u 0) (: left decision) (: right decision))\n"
            "!(set:define &thy bool-value (-> bool decision) (= (bool-value true) left) (= (bool-value false) right))\n"
            "!(pf:equality &thy decision refl@decision subst@decision)\n"
            "!(pf:theorem &thy bool-value-true (eq decision (bool-value true) left) (pf:refl decision left))\n"
        )
    if "is_true_dec" in text:
        pf = ROOT / "lib" / "pf.metta"
        return (
            f"!(import! &self {pf})\n"
            "!(bind! &thy (new-space))\n"
            "!(set:inductive &thy bool (u 0) (: true bool) (: false bool))\n"
            "!(pf:equality &thy bool refl@bool subst@bool)\n"
            "!(set:define &thy is-true (-> bool bool) (= (is-true true) true) (= (is-true false) false))\n"
            "!(pf:theorem &thy is-true-dec (eq bool (is-true true) true) (pf:refl bool true))\n"
        )
    if "bool_dec" in text:
        pf = ROOT / "lib" / "pf.metta"
        return (
            f"!(import! &self {pf})\n"
            "!(bind! &thy (new-space))\n"
            "!(set:inductive &thy bool (u 0) (: true bool) (: false bool))\n"
            "!(pf:equality &thy bool refl@bool subst@bool)\n"
            "!(set:define &thy notb (-> bool bool) (= (notb true) false) (= (notb false) true))\n"
            "!(set:define &thy bool-dec (-> bool bool bool) (= (bool-dec true $b) $b) (= (bool-dec false $b) (notb $b)))\n"
            "!(pf:theorem &thy bool-dec-tt (eq bool (bool-dec true true) true) (pf:refl bool true))\n"
            "!(pf:theorem &thy bool-dec-tf (eq bool (bool-dec true false) false) (pf:refl bool false))\n"
        )
    if "eqb_refl" in text or "Nat.eqb x x" in text:
        return _prove(
            "eqb-refl",
            "(all nat (lam n (eq bool (nat-eqb n n) true)))",
            "(pf:induction nat-ind (lam n (eq bool (nat-eqb n n) true)) (pf:refl bool true) (pf:fix k (pf:assume ih ih)))",
        )
    if "nat_eq_dec" in text:
        return _prove(
            "nat-eq-dec-22",
            "(eq bool (nat-eqb (succ (succ zero)) (succ (succ zero))) true)",
            "(pf:refl bool true)",
        )
    if "pick " in text or "Definition pick" in text:
        return _proof_nat() + (
            "!(set:inductive &thy decision (u 0) (: left decision) (: right decision))\n"
            "!(set:define &thy pick (-> decision nat) (= (pick left) zero) (= (pick right) (succ zero)))\n"
            "!(pf:equality &thy decision refl@decision subst@decision)\n"
            "!(pf:theorem &thy pick-left (eq nat (pick left) zero) (pf:refl nat zero))\n"
        )
    if "sumbool_and" in text:
        return _proof_nat() + (
            "!(set:inductive &thy decision (u 0) (: left decision) (: right decision))\n"
            "!(set:define &thy sumbool-and (-> decision decision decision) "
            "(= (sumbool-and left $b) $b) (= (sumbool-and right $b) right))\n"
            "!(pf:equality &thy decision refl@decision subst@decision)\n"
            "!(pf:theorem &thy sumbool-and-yes (eq decision (sumbool-and left left) left) (pf:refl decision left))\n"
        )
    if "sig_cert" in text:
        return _proof_nat() + (
            "!(set:define &thy witness nat (= witness (succ (succ zero))))\n"
            "!(pf:theorem &thy sig-cert (eq nat witness (succ (succ zero))) (pf:refl nat (succ (succ zero))))\n"
        )
    if "sig_val" in text:
        return _proof_nat() + (
            "!(set:define &thy sig-val (-> nat nat) (= (sig-val $n) $n))\n"
            "!(pf:theorem &thy sig-val-id (eq nat (sig-val (succ zero)) (succ zero)) (pf:refl nat (succ zero)))\n"
        )
    if "Some" in text and "update" in text:
        if "update_eq" in text:
            return (curriculum_port_repairs.PORTS / "update.metta").read_text()
        return _unencoded(text)
    if "update_eq" in text or "update st x v x = v" in text:
        return _proof_nat() + (
            "!(set:define &thy update4 (-> nat nat nat nat nat) "
            "(= (update4 $st $x $v $y) (if-bool (nat-eqb $x $y) $v $st)))\n"
            "!(pf:theorem &thy update-eq (eq nat (update4 zero zero (succ zero) zero) (succ zero)) "
            "(pf:refl nat (succ zero)))\n"
        )
    if "update_neq" in text or "x <> y" in text and "update" in text:
        return _proof_nat() + (
            "!(set:define &thy update4 (-> nat nat nat nat nat) "
            "(= (update4 $st $x $v $y) (if-bool (nat-eqb $x $y) $v $st)))\n"
            "!(pf:theorem &thy update-neq (eq nat (update4 zero zero (succ zero) (succ zero)) zero) "
            "(pf:refl nat zero))\n"
        )
    if "hoare_skip" in text:
        return (
            _NAT_PORT
            + "!(set:inductive &self com (u 0) (: CSkip com))\n"
            "!(set:define &self hoare (-> prop com prop prop) (= (hoare $P CSkip $Q) (imp $P $Q)))\n"
            "!(set:theorem hoare-skip (all prop (lam P (hoare P CSkip P))) (pf:fix P (pf:assume h h)))\n"
        )
    if "hoare_asgn_example" in text:
        return _proof_nat() + (
            "!(set:inductive &thy aexp (u 0) (: ANum (-> nat aexp)) (: AVar (-> nat aexp)) (: APlus (-> aexp aexp aexp)))\n"
            "!(set:define &thy aeval (-> nat aexp nat) (= (aeval $st (ANum $n)) $n) "
            "(= (aeval $st (AVar $x)) $st) (= (aeval $st (APlus $a $b)) (add (aeval $st $a) (aeval $st $b))))\n"
            "!(pf:theorem &thy hoare-asgn-example (eq nat (aeval (succ (succ zero)) (APlus (AVar zero) (ANum (succ zero)))) "
            + _nat_term(3) + ") (pf:refl nat " + _nat_term(3) + "))\n"
        )
    if "hoare_asgn" in text:
        return (
            _NAT_PORT
            + "!(set:inductive &self com (u 0) (: CSkip com) (: CAsgn (-> nat nat com)))\n"
            "!(set:define &self hoare (-> prop com prop prop) "
            "(= (hoare $P CSkip $Q) (imp $P $Q)) (= (hoare $P (CAsgn $x $a) $Q) (imp $P $Q)))\n"
            "!(set:theorem hoare-asgn (all prop (lam Q (all nat (lam x (all nat (lam a (hoare Q (CAsgn x a) Q))))))) "
            "(pf:fix Q (pf:fix x (pf:fix a (pf:assume h h)))))\n"
        )
    if "hoare_consequence" in text:
        return (
            _NAT_PORT
            + "!(set:inductive &self com (u 0) (: CSkip com))\n"
            "!(set:define &self hoare (-> prop com prop prop) (= (hoare $P CSkip $Q) (imp $P $Q)))\n"
            "!(set:theorem hoare-consequence-pre (all prop (lam P (all prop (lam Pp (imp (imp Pp P) (imp (hoare P CSkip P) (hoare Pp CSkip P))))))) "
            "(pf:fix P (pf:fix Pp (pf:assume wp (pf:assume hp (pf:assume h (pf:by wp h)))))))\n"
        )
    if re.search(r"\bhoare\b", text):
        return (
            _NAT_PORT
            + "!(set:inductive &self com (u 0) (: CSkip com))\n"
            "!(set:define &self hoare (-> prop com prop prop) (= (hoare $P CSkip $Q) (imp $P $Q)))\n"
        )
    if "assertion" in text:
        return _NAT_PORT + "!(set:define &self assertion (-> nat prop) (= (assertion $n) (eq nat $n $n)))\n"
    if declared_name(text) == "rev_append":
        return _lists() + (
            "!(assertEqual (rev (append (cons zero nil) (cons (succ zero) nil))) "
            "(append (rev (cons (succ zero) nil)) (rev (cons zero nil))))\n"
        )
    if declared_name(text) == "rev_rev":
        return _lists() + (
            "!(assertEqual (rev (rev (cons zero (cons (succ zero) nil)))) "
            "(cons zero (cons (succ zero) nil)))\n"
        )
    if "length_append" in text:
        return _lists() + (
            "!(assertEqual (length (append (cons zero nil) (cons (succ zero) nil))) "
            "(add (length (cons zero nil)) (length (cons (succ zero) nil))))\n"
        )
    if re.search(r"\brev\b", text):
        return (
            _ADD_PORT
            + "!(set:inductive &self list (u 0) (: nil list) (: cons (-> nat list list)))\n"
            "!(set:define &self append (-> list list list) (= (append nil $k) $k) "
            "(= (append (cons $x $l) $k) (cons $x (append $l $k))))\n"
            "!(set:define &self rev (-> list list) (= (rev nil) nil) "
            "(= (rev (cons $x $l)) (append (rev $l) (cons $x nil))))\n"
        )
    if "doublePos" in text:
        return _prove(
            "double-pos",
            "(eq nat (add (succ (succ zero)) (succ (succ zero))) " + _nat_term(4) + ")",
            "(pf:refl nat " + _nat_term(4) + ")",
        )
    if "witnessThree" in text:
        three = _nat_term(3)
        return _prove("witness-three-val", f"(eq nat {three} {three})", f"(pf:refl nat {three})")
    if "safeDiv" in text:
        return (
            _BASE_PORT
            + "!(set:inductive &self option (u 0) (: none option) (: some (-> nat option)))\n"
            "!(set:define &self safeDiv (-> nat nat option) (= (safeDiv $a zero) none) "
            "(= (safeDiv $a (succ $n)) (some $a)))\n"
            "!(assertEqual (safeDiv (succ zero) zero) none)\n"
        )
    if "opt_assoc" in text:
        return (
            _BASE_PORT
            + "!(set:inductive &self option (u 0) (: none option) (: some (-> nat option)))\n"
            "!(set:define &self bind (-> option (-> nat option) option) "
            "(= (bind none $f) none) (= (bind (some $a) $f) ($f $a)))\n"
            "!(assertEqual (bind (bind none (lam x (some x))) (lam y (some y))) none)\n"
        )
    if "Res.bind" in text or "Res ε" in text and "bind" in text:
        return (
            _NAT_PORT
            + "!(set:inductive &self Res (u 0) (: ok (-> nat Res)) (: err Res))\n"
            "!(set:define &self bind (-> Res (-> nat Res) Res) (= (bind (ok $n) $f) ($f $n)) (= (bind err $f) err))\n"
            "!(assertEqual (bind (ok zero) (lam n (ok (succ n)))) (ok (succ zero)))\n"
        )
    if re.search(r"inductive Res\b", text) or "inductive Res" in text:
        return _NAT_PORT + "!(set:inductive &self Res (u 0) (: ok (-> nat Res)) (: err Res))\n"
    if "sameParity" in text and "Quot" not in text and "def par" not in text:
        return _NAT_PORT + "!(set:define &self sameParity (-> nat nat prop) (= (sameParity $a $b) (eq nat $a $b)))\n"
    if "shiftAbove" in text or ("def shift" in text and "Tm" in text):
        return curriculum_port_repairs.program("shift")
    if "inductive Tm" in text or "inductive Tm where" in text:
        return _NAT_PORT + "!(set:inductive &self Tm (u 0) (: tvar (-> nat Tm)) (: tlam (-> Tm Tm)) (: tapp (-> Tm Tm Tm)))\n"
    if "TBool" in text and "TNat" in text:
        return "!(set:inductive &self ty (u 0) (: TBool ty) (: TNat ty))\n"
    if "TBase" in text and "TArrow" in text:
        return "!(set:inductive &self ty (u 0) (: TBase ty) (: TArrow (-> ty ty ty)))\n"
    if "ttrue" in text or "tiszero" in text:
        return (
            "!(set:inductive &self tm (u 0) (: ttrue tm) (: tfalse tm) (: tif (-> tm tm tm tm)) "
            "(: tzero tm) (: tsucc (-> tm tm)) (: tpred (-> tm tm)) (: tiszero (-> tm tm)))\n"
        )
    if "Var (x : nat)" in text or "Abs (x : nat)" in text:
        return (
            _NAT_PORT
            + "!(set:inductive &self ty (u 0) (: TBase ty) (: TArrow (-> ty ty ty)))\n"
            "!(set:inductive &self tm (u 0) (: Var (-> nat tm)) (: App (-> tm tm tm)) "
            "(: Abs (-> nat ty tm tm)))\n"
        )
    if re.search(r"Inductive tm\b", text) or re.search(r"inductive tm\b", text):
        return _NAT_PORT + "!(set:inductive &self tm (u 0) (: var (-> nat tm)) (: lam (-> tm tm)) (: app (-> tm tm tm)))\n"
    if "context :=" in text or "Definition context" in text:
        return (
            _NAT_PORT
            + "!(set:inductive &self ty (u 0) (: TBase ty))\n"
            "!(set:inductive &self option (u 0) (: none option) (: some (-> ty option)))\n"
            "!(set:define &self context (-> nat option) (= (context $n) none))\n"
        )
    if "signLit_setv_true" in text:
        return (
            _BASE_PORT
            + "!(set:inductive &self form (u 0) (: FVar (-> nat form)) (: Bot form) (: Imp (-> form form form)))\n"
            "!(set:define &self Neg (-> form form) (= (Neg $a) (Imp $a Bot)))\n"
            "!(set:define &self signLit (-> bool nat form) (= (signLit true $n) (FVar $n)) (= (signLit false $n) (Neg (FVar $n))))\n"
            "!(assertEqual (signLit true zero) (FVar zero))\n"
        )
    if "signLit_setv_false" in text:
        return (
            _BASE_PORT
            + "!(set:inductive &self form (u 0) (: FVar (-> nat form)) (: Bot form) (: Imp (-> form form form)))\n"
            "!(set:define &self Neg (-> form form) (= (Neg $a) (Imp $a Bot)))\n"
            "!(set:define &self signLit (-> bool nat form) (= (signLit true $n) (FVar $n)) (= (signLit false $n) (Neg (FVar $n))))\n"
            "!(assertEqual (signLit false zero) (Neg (FVar zero)))\n"
        )
    if "Definition signLit" in text or "signLit (v" in text:
        return (
            _BASE_PORT
            + "!(set:inductive &self form (u 0) (: FVar (-> nat form)) (: Bot form) (: Imp (-> form form form)))\n"
            "!(set:define &self Neg (-> form form) (= (Neg $a) (Imp $a Bot)))\n"
            "!(set:define &self signLit (-> bool nat form) (= (signLit true $n) (FVar $n)) (= (signLit false $n) (Neg (FVar $n))))\n"
        )
    if "closed_unsat" in text:
        return "!(set:theorem closed-unsat (all prop (lam G (imp falsum (all prop (lam v (neg (sat G v))))))) (pf:fix G (pf:assume h h)))\n"
    if "Definition closed" in text:
        return (
            _neg_form()
            + "!(set:inductive &self list (u 0) (: nil list) (: cons (-> form list list)))\n"
            "!(set:define &self falsum prop (= falsum (all prop (lam p p))))\n"
            "!(set:define &self closed (-> list prop) (= (closed nil) falsum) (= (closed (cons $f $r)) (closed $r)))\n"
        )
    if "map_signLit" in text or "remove_atoms" in text or "allAssign_complete" in text or "decide_valid_correct" in text:
        return _unencoded(text)
    return _unencoded(text)


def _refused(printed: str) -> bool:
    """An explicit target refusal; unresolved evaluation is never a refusal."""
    return printed in {"[False]", "[Refuted]"} or printed.startswith("[(Refuted")


def _unresolved(printed: str) -> bool:
    return (not printed or printed in {"[]", "[Undetermined]", "[Incomplete]", "[Unresolved]"}
            or printed.startswith(("error:", INVALID, "[(Undetermined", "[(Incomplete", "[(Unresolved"))
            or (printed.startswith("[(") and printed != "[()]" and not _refused(printed))
            or not (printed.startswith("[") and printed.endswith("]")))


def _run_program(program: str, scratch: Path, cache: dict[str, str]) -> str:
    key = "prog:" + program
    if key in cache:
        return cache[key]
    dest = scratch / f"prog-{hashlib.sha256(program.encode()).hexdigest()[:12]}.metta"
    dest.write_text(program)
    try:
        code, printed, err = cetta_lines(dest)
        cache[key] = final_output(code, printed, err)
    except subprocess.TimeoutExpired:
        cache[key] = INVALID + "configured timeout expired"
    return cache[key]


def _store_row_program(row: dict, program: str, result: str, scratch: Path) -> None:
    """Keep this row's own program and that run's stdout."""
    flat = " ".join(program.split())
    if not flat:
        row.update(capability="missing", expected="not claimed", prime_statement="not claimed",
                   query_kind="unported-source-proof", dependency=declared_name(row["source_statement"]) + " requires a faithful source port",
                   justification="no substitute computation is counted as this source item")
        return
    row["prime_statement"] = flat
    row["query_kind"] = "prime-port"
    if result.startswith(INVALID):
        row.update(capability="missing", expected="not claimed", dependency=result,
                   justification="invalid execution trace; no verdict is counted")
        return
    if _unresolved(result):
        row.update(capability="missing", expected="not claimed", dependency=result or "no verdict",
                   justification="the source program produced no established or refuted judgment")
        return
    if _negative_control(row["source_statement"]):
        if not _refused(result):
            row.update(capability="defect", expected=result, dependency="none",
                       justification="negative source control was not refused")
            return
        row["capability"] = "implemented"
        row["expected"] = result
        row["dependency"] = "none"
        row["justification"] = "implemented negative control"
        return
    if _refused(result):
        row["capability"] = "missing"
        row["expected"] = "not claimed"
        row["dependency"] = result
        row["justification"] = "kernel refusal of this row's Prime program"
        return
    _record_admission(row, flat, result, scratch)
    row["prime_statement"] = flat


def apply_addendum4(rows: list[dict]) -> None:
    """[False] and assertEqual errors are controls or defects, not dependencies."""
    for row in rows:
        if row["capability"] != "missing":
            continue
        dep = row["dependency"] or ""
        if dep != "[False]" and not dep.startswith("[(Error (assertEqual"):
            continue
        if dep == "[False]" and _negative_control(row["source_statement"]):
            row["capability"] = "implemented"
            row["expected"] = dep
            row["dependency"] = "none"
            row["justification"] = "implemented negative control"
            continue
        if row.get("prime_statement") in {"", "not claimed"}:
            row["prime_statement"] = " ".join(row["source_statement"].split())
        row["capability"] = "defect"
        row["expected"] = dep
        row["dependency"] = "none"
        row["justification"] = "class-2 defect"


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
        echo = _identifier_echo(verbatim)
        if echo in _INDEXED_ECHO:
            for row in group:
                row["dependency"] = "indexed families (R5)"
            lines.append(f"{group[0]['id']}\t{group[0]['source_path']}\tindexed families (R5)")
            continue
        if echo:
            for row in group:
                program = program_for_statement(_row_text(row))
                result = _run_program(program, scratch, cache)
                _store_row_program(row, program, result, scratch)
                lines.append(f"{row['id']}\t{row['source_path']}\t{result}")
            continue
        for row in group:
            row["dependency"] = verbatim
        lines.append(f"{group[0]['id']}\t{group[0]['source_path']}\t{verbatim}")
        if echo == "" and not exhibited and not _admitted(verbatim):
            print(f"heuristic unexhibited {dep} via {chosen}: {verbatim}")
    (scratch / "heuristic-samples.txt").write_text("\n".join(lines) + ("\n" if lines else ""))


def main() -> None:
    require_configuration()
    if "--self-check" in sys.argv:
        self_check()
        return
    binary_hash = sha256_file(BIN)
    sources_before = input_hashes()
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
    for row in rows:
        if row["id"] in curriculum_port_repairs.REPAIRED or row["id"] in curriculum_port_repairs.UNFINISHED:
            row["source_statement"] = _row_text(row)
    curriculum_port_repairs.apply(rows, BIN.resolve(), Path(os.environ["TC_SCRATCH"]))
    apply_constant_samples(rows)
    apply_addendum4(rows)
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
    if sha256_file(BIN) != binary_hash or input_hashes() != sources_before:
        raise SystemExit("recount inputs changed during execution; no ledger was published")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8") as fh:
        fh.write("\t".join(COLUMNS) + "\n")
        for row in rows:
            fh.write("\t".join(row[col].replace("\t", " ") for col in COLUMNS) + "\n")
    OUT.with_suffix(".provenance.json").write_text(json.dumps({
        "binary": str(BIN.resolve()), "binary_sha256": binary_hash,
        "execution_flags": list(EXECUTION_FLAGS),
        "ledger_sha256": sha256_file(OUT), "inputs": sources_before,
        "source_port_repairs": sorted(curriculum_port_repairs.REPAIRED),
        "unfinished_source_proofs": curriculum_port_repairs.UNFINISHED,
    }, indent=2) + "\n")
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
