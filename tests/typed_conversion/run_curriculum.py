#!/usr/bin/env python3
"""Check the curriculum ledger and the shipped cumulative controls."""

from __future__ import annotations

import os
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path("/home/aimama/aihub/hyperon/cetta-prime-2.0-draft-20260910")
sys.path.insert(0, str(ROOT / "tests/typed_conversion"))
import build_curriculum_ledger as build  # noqa: E402

BIN = build.BIN
SCRATCH = Path(os.environ.get("TC_SCRATCH", "/tmp/claude/grok-goal-aaee6e0e0c16/implementer"))
DEPS = SCRATCH / "curriculum-dependencies.md"

WANT = {
    "univ": "Established",
    "fam": "Established",
    "lam": "Established",
    "sig": "Established",
    "pair": "Established",
    "reflx": "Established",
    "reflf": "Established",
    "idno": "(Refuted RigidShape:below_iff)",
    "neq": "(Refuted Below:pi_codomain_raise)",
    "dom": "(Refuted Below:pi_iff)",
    "low": "(Refuted Below:universe_iff)",
    "formed": "Established",
    "badapp": "(Refuted Below:universe_iff)",
    "badend": "(Refuted Synth:not_typed)",
    "capture": "Accepted",
    "nosynth": "Unresolved",
    "illof": "(Refuted Below:universe_iff)",
    "illformed": "(Refuted Below:universe_iff)",
    "illhint": "(Refuted Below:universe_iff)",
    "formok": "Established",
    "formfuel": "Unresolved",
    "formshort": "Unresolved",
    "ideq": "(Refuted RigidShape:below_iff)",
    "idraise": "(Refuted raised_carrier_ne)",
    "idfun": "(Refuted raised_function_carrier_ne)",
    "fuel8": "Unresolved",
    "fuel256": "Accepted",
    "dom8": "Unresolved",
    "dom256": "(Refuted Below:pi_iff)",
}


QUOTE_KEYS = {
    "elaboration is outside the kernel": ("Ltac crush :=", "Class Eqb "),
    "two-scrutinee case trees (R2)": ("destruct (eval",),
    "quotients are not in the theory": ("def par :", "Quot.mk sameParity"),
    "principal-type refutation (L3)": ("def uninhabitedArrow",),
    "external proof-checker baseline is outside the kernel": ("HOL4=",),
    "Lean macro, syntax, and macro_rules are outside the kernel": ('macro "twice',),
    "Megalodon False as forall p, p is not a Prime term": ("Definition False",),
    "Megalodon Leibniz equality is not a Prime term": ("Definition eq",),
    "Megalodon impredicative conjunction is not a Prime term": ("Definition and",),
    "a set is not a proof of membership": ("x :e x",),
    "HOL falsity F has no kernel proof": ("Theorem bad",),
    "indexed families (R5)": ("existT", "{ b : bool &", "Inductive ceval"),
    "de Bruijn shiftAbove and subst for Tm are outside the kernel": ("def shiftAbove",),
    "Lean #guard of an OSLF HOL kernel profile is outside the kernel": ("hol4LogicRelationEnv.tuples", "#guard"),
    "Dedukti .dk source is lowered by the guest MeTTa encoder, not checked as a Prime term": ("def hol_self_imp",),
    "cic-nat-max does not reduce two successors": ("!(assertEqual (cic-nat-max",),
    "cic-stage1-covered-nf does not reduce a direct m-application of two successors": ("cic-stage1-covered-nf",),
    "cic-stage1-nf does not reduce a direct m-application of two successors": ("!(assertEqual (cic-stage1-nf ",),
    "cic-stage1-nf-app does not reduce a direct m-application of two successors": ("!(assertEqual (cic-stage1-nf-app",),
    "subset type {x | P x} is not a Prime term": ("{ n : nat |", "{x |", "sig_val"),
    "CIC sumbool {A}+{B} is not a Prime term": ("} + {",),
    "polymorphic equality refl is not a Prime term": ("eq_refl", "a = a"),
    "Prop-valued is_true is not a Prime predicate": ("is_true",),
    "large elimination of a Prop into nat is outside the kernel": ("prop_to_data", "ex_witness"),
    "polymorphic congruence is not a Prime term": ("theorem cong", "cong {"),
    "polymorphic pair projection is not a Prime term": ("pair_proj",),
    "exists and forall over Prop are not a Prime term": ("ex_not_forall_not", "exists"),
}


def quote_requires(dep: str, quote: str) -> bool:
    """The quoted source line has to be the feature this bucket names."""
    q = " ".join(quote.split())
    if not q or q.startswith(("(*", "/-", ";", "*", "--", "`", "resolution,")):
        return False
    if dep == "indexed families (R5)":
        return ("existT" in q and "&" in q) or ("ceval" in q and (":" in q or "->" in q or "→" in q))
    if dep == "elaboration is outside the kernel":
        return "Ltac crush :=" in q or q.startswith("Class Eqb")
    if dep == "Lean #guard of an OSLF HOL kernel profile is outside the kernel":
        return "#guard" in q and "tuples" in q
    if dep == "two-scrutinee case trees (R2)":
        return "induction D" in q and "destruct (eval" in q and "(*" not in q
    if dep == "Dedukti .dk source is lowered by the guest MeTTa encoder, not checked as a Prime term":
        return q.startswith("def ") and ":=" in q and "prf" in q
    if dep == "de Bruijn shiftAbove and subst for Tm are outside the kernel":
        return "shiftAbove" in q and "Tm" in q
    if dep == "subset type {x | P x} is not a Prime term":
        return "{" in q and "|" in q
    if dep == "CIC sumbool {A}+{B} is not a Prime term":
        return "} + {" in q or "}+{" in q
    if dep == "principal-type refutation (L3)":
        return "uninhabitedArrow" in q and "canonical_min" in q
    if dep == "polymorphic equality refl is not a Prime term":
        return "eq_refl" in q or "a = a" in q
    if dep == "quotients are not in the theory":
        return ("def par" in q and "Quot.lift" in q) or "Quot.mk sameParity" in q
    if dep == "Prop-valued is_true is not a Prime predicate":
        return "is_true" in q and "Prop" in q
    if dep == "large elimination of a Prop into nat is outside the kernel":
        return "prop_to_data" in q or "ex_witness" in q
    if dep == "polymorphic congruence is not a Prime term":
        return "cong" in q and "=" in q
    if dep == "polymorphic pair projection is not a Prime term":
        return "pair_proj" in q or ".1" in q
    if dep == "HOL falsity F has no kernel proof":
        return "Theorem bad" in q and q.rstrip(".").endswith(" F")
    if dep == "Lean macro, syntax, and macro_rules are outside the kernel":
        return 'macro "twice' in q
    if dep == "Megalodon False as forall p, p is not a Prime term":
        return q.startswith("Definition False") and "forall p:prop, p" in q
    if dep == "Megalodon Leibniz equality is not a Prime term":
        return q.startswith("Definition eq") and "forall Q" in q
    if dep == "Megalodon impredicative conjunction is not a Prime term":
        return q.startswith("Definition and") and "A -> B -> p" in q
    if dep == "a set is not a proof of membership":
        return "Theorem" in q and "x :e x" in q
    if dep == "cic-stage1-covered-nf does not reduce a direct m-application of two successors":
        return "cic-stage1-covered-nf" in q and "DKConst m" in q
    if dep == "exists and forall over Prop are not a Prime term":
        return "exists" in q and "forall" in q
    if dep == "external proof-checker baseline is outside the kernel":
        return "HOL4=" in q
    if dep == "cic-nat-max does not reduce two successors":
        return "assertEqual" in q and "cic-nat-max" in q and "(Con s)" in q
    if dep == "cic-stage1-nf does not reduce a direct m-application of two successors":
        return "assertEqual" in q and "(cic-stage1-nf " in q and "Con m" in q
    if dep == "cic-stage1-nf-app does not reduce a direct m-application of two successors":
        return "assertEqual" in q and "cic-stage1-nf-app" in q and "Con m" in q
    return False


def _code_lines(file_lines: list[str]):
    in_block = False
    in_coq = False
    for index, raw in enumerate(file_lines):
        stripped = raw.strip()
        if in_block:
            if "-/" in stripped:
                in_block = False
            continue
        if in_coq:
            if "*)" in stripped:
                in_coq = False
            continue
        if not stripped or stripped.startswith((";", "*", "--")):
            continue
        if stripped.startswith("/-"):
            if "-/" not in stripped:
                in_block = True
            continue
        if stripped.startswith("(*"):
            if "*)" not in stripped:
                in_coq = True
            continue
        yield index, raw


def _strip_coq_comment(text: str) -> str:
    while "(*" in text and "*)" in text:
        start = text.find("(*")
        end = text.find("*)", start)
        if end < 0:
            break
        text = text[:start] + text[end + 2:]
    return " ".join(text.split())


def _excerpt_at(file_lines: list[str], index: int) -> str:
    raw = file_lines[index]
    stripped = raw.strip()
    if "cic-stage1-covered-nf" in stripped or "DKConst m" in stripped or stripped.startswith("!(assertEqual"):
        start = index
        if not stripped.startswith("!"):
            for prev in range(index, max(-1, index - 8), -1):
                if file_lines[prev].strip().startswith("!(assertEqual"):
                    start = prev
                    break
        parts = []
        depth = 0
        for j in range(start, min(len(file_lines), start + 8)):
            line = file_lines[j].strip()
            if not line or line.startswith(";"):
                if parts:
                    break
                continue
            parts.append(line)
            depth += line.count("(") - line.count(")")
            if depth <= 0 and parts[0].startswith("!(assertEqual"):
                break
        return " ".join(parts)
    if "destruct (eval" in raw:
        start = None
        for prev in range(index, max(-1, index - 12), -1):
            if "induction D" in file_lines[prev]:
                start = prev
                break
        parts = []
        if start is not None:
            for j in range(start, index):
                piece = " ".join(file_lines[j].split())
                if not piece or piece.startswith("(*"):
                    continue
                parts.append(piece)
                if piece.endswith("."):
                    break
        tail = _strip_coq_comment(stripped).lstrip("-").strip()
        return " ".join(parts + [tail])
    if stripped == "def uninhabitedArrow :" or "uninhabitedArrow" in stripped:
        parts = [stripped]
        for nxt in file_lines[index + 1:index + 4]:
            piece = nxt.strip()
            if not piece:
                continue
            parts.append(piece)
            if "canonical_min" in piece:
                break
        return " ".join(parts)
    if stripped.startswith("Theorem bad"):
        body = stripped.split(":", 1)[1].strip() if ":" in stripped else ""
        if body and body != "F":
            return stripped
        parts = [stripped]
        for nxt in file_lines[index + 1:index + 4]:
            piece = nxt.strip()
            if not piece or piece.startswith("(*"):
                continue
            parts.append(piece)
            break
        return " ".join(parts)
    if stripped.startswith("#guard") or "tuples" in raw:
        start = index
        if not stripped.startswith("#guard"):
            for prev in range(index, max(-1, index - 4), -1):
                if file_lines[prev].strip().startswith("#guard"):
                    start = prev
                    break
        parts = []
        for j in range(start, min(len(file_lines), start + 4)):
            piece = file_lines[j].strip()
            if not piece:
                if parts:
                    break
                continue
            if piece.startswith("#guard") and parts:
                break
            parts.append(piece)
            if "tuples" in piece or "==" in piece:
                break
        return " ".join(parts)
    return stripped


def exhibiting_line(dep: str, items: list[dict]) -> tuple[str, str]:
    """One source line from this bucket that states the named absent feature."""
    for item in items:
        if quote_requires(dep, item["source_statement"]):
            return item["source_path"], item["source_statement"]
    keys = QUOTE_KEYS.get(dep, ())
    seen: set[str] = set()
    for item in items:
        rel = item["source_path"]
        if rel in seen:
            continue
        seen.add(rel)
        path = build.CURRICULUM / rel
        if not keys or not path.is_file():
            continue
        file_lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for index, raw in _code_lines(file_lines):
            if not any(key in raw for key in keys):
                continue
            quote = _excerpt_at(file_lines, index)
            if quote_requires(dep, quote):
                return rel, quote
    item = items[0]
    return item["source_path"], item["source_statement"]


def verdict(line: str) -> tuple[str, str] | None:
    line = line.strip()
    if not line.startswith("[(K "):
        return None
    body = line[1:-1] if line.endswith("]") else line
    # (K name result) or (K name (Refuted ...))
    if not body.startswith("(K "):
        return None
    rest = body[3:]
    name, _, tail = rest.partition(" ")
    if tail.endswith(")"):
        tail = tail[:-1]
    return name, tail


def main() -> int:
    build.main()
    text = build.OUT.read_text(encoding="utf-8")
    rows = text.splitlines()[1:]
    files = {p.relative_to(build.CURRICULUM).as_posix() for p in build.CURRICULUM.rglob("*") if p.is_file()}
    recorded = set()
    item_paths = set()
    commands = 0
    loads = 0
    command_expected = {}
    dep_counts: Counter[str] = Counter()
    generated_bad = []
    fresh_port = []
    named = {
        "Coq/ICL01_types_functions.v",
        "Lean/01_basics.lean",
        "HOL/HOL01_logicScript.sml",
        "Megalodon/01_basics.mg",
    }
    named_rows = {}
    for row in rows:
        cols = row.split("\t")
        item = dict(zip(build.COLUMNS, cols))
        if item["id"].startswith("src-") or item["id"].startswith("item-"):
            recorded.add(item["source_path"])
            if item["source_path"] in named:
                named_rows[item["source_path"]] = item
        if item["id"].startswith("item-"):
            item_paths.add(item["source_path"])
        if item["id"].startswith("curriculum_") and item["id"].rsplit("-c", 1)[-1].isdigit():
            if "lib/prime/curriculum.metta" in item["prime_statement"]:
                loads += 1
            else:
                commands += 1
            command_expected[item["id"]] = item["expected"]
        if item["dependency"] == "no ported fixture section for this file":
            fresh_port.append(item["source_path"])
        if item["source_path"] in {".run_all_aa_thm_chain.last.log", "ITP_EXPANSION_SOURCES.md"}:
            if item["dependency"] != "generated build artifact, not a lesson":
                fresh_port.append(item["source_path"] + " not generated")
        if item["capability"] == "missing" and item["id"].startswith(("src-", "item-")):
            dep_counts[item["dependency"]] += 1
        if (
            item["id"].startswith("src-")
            and item["capability"] == "implemented"
            and ("/.hol/" in f"/{item['source_path']}" or item["source_path"].endswith(".pre"))
        ):
            generated_bad.append(f"{item['source_path']}: generated artifact marked implemented")
    missing_files = sorted(files - recorded)
    extra = sorted(recorded - files)
    audit_text = (build.CURRICULUM / "VerifiedMeTTa/axiom_audit.lean").read_text(encoding="utf-8")
    audit_lines = sum(1 for line in audit_text.splitlines() if line.startswith("#print axioms"))
    print_axioms = []
    audit_recorded = False
    for row in rows:
        cols = row.split("\t")
        if len(cols) != len(build.COLUMNS):
            continue
        item = dict(zip(build.COLUMNS, cols))
        if item["source_statement"].startswith("#print axioms"):
            print_axioms.append(item)
        if (
            item["source_path"] == "VerifiedMeTTa/axiom_audit.lean"
            and item["capability"] == "recorded"
            and item["dependency"] == "generated build artifact, not a lesson"
            and item["prime_statement"] == "not a lesson"
        ):
            audit_recorded = True
    print_bad = [
        item["id"] for item in print_axioms
        if item["capability"] == "missing"
        or item["id"].startswith("item-")
        or item["prime_statement"] != "not a lesson"
        or item["dependency"] != "generated build artifact, not a lesson"
        or item["source_behavior"] != "file"
    ]
    for path in sorted(p for p in files if p.startswith("Notation/") and p.endswith(".metta")):
        if path not in item_paths:
            fresh_port.append(f"{path}: notation lesson has no item row")
    fresh_bad = list(generated_bad) + fresh_port
    if len(print_axioms) != audit_lines or print_bad or not audit_recorded:
        fresh_bad.append(
            f"print-axioms {len(print_axioms)} file {audit_lines} "
            f"lessons-or-missing {len(print_bad)} recorded {audit_recorded}"
        )
    log_lines: list[str] = []
    for log_name in ("encoders.log", "readers.log"):
        log_path = SCRATCH / log_name
        if log_path.is_file():
            log_lines.extend(log_path.read_text(encoding="utf-8", errors="replace").splitlines())
    log_pairs = list(zip(log_lines, log_lines[1:]))
    skeptic_rows = {
        "Lemma nd_id : forall a, nd nil (Imp a a).": "indexed families (R5)",
        "Inductive form : Type :=": "[form]",
        "Definition idp {A : Type} (a : A) : A := a.": "implicit binder {A : Type} on idp is outside the kernel",
        "(= (dk-lambda-decl) (Bind1 body))": "[(Bind1 body)]",
        "(= (dfa-step q0 zero) q1)": "[q1]",
    }
    seen_skeptic: set[str] = set()
    for row in rows:
        cols = row.split("\t")
        if len(cols) != len(build.COLUMNS):
            continue
        item = dict(zip(build.COLUMNS, cols))
        stmt = item["source_statement"]
        if stmt in skeptic_rows:
            seen_skeptic.add(stmt)
            want = skeptic_rows[stmt]
            got = item["expected"] if item["capability"] == "implemented" else item["dependency"]
            if got != want and not str(got).startswith("error: compiled Prime reader"):
                fresh_bad.append(f"{stmt[:48]}: got {got!r}")
            elif item["capability"] == "implemented" and stmt.startswith("(="):
                head = stmt.split("(", 2)[-1].split()[0].rstrip(")")
                if not any(line == want and f"!({head}" in prev for prev, line in log_pairs):
                    fresh_bad.append(f"{head}: expected {want} is not its own log line")
        if (
            item["capability"] == "implemented"
            and item["justification"] == "result printed by cetta --lang prime on this row"
        ):
            expected = item["expected"]
            if not any(line == expected and (stmt in prev or stmt[:80] in prev) for prev, line in log_pairs):
                # Ground definition rows log !(call), not the equation text.
                call_head = ""
                if stmt.startswith("(="):
                    call_head = stmt.split("(", 2)[-1].split()[0].rstrip(")")
                if not call_head or not any(
                    line == expected and f"!({call_head}" in prev for prev, line in log_pairs
                ):
                    fresh_bad.append(f"{item['id']}: expected {expected!r} is not this row's log line")
                    if len(fresh_bad) > 12:
                        break
    missing_skeptic = [stmt for stmt in skeptic_rows if stmt not in seen_skeptic]
    if missing_skeptic:
        fresh_bad.append(f"skeptic rows absent {len(missing_skeptic)}")
    for fixture in build.FIXTURES:
        parsed = build.parse_fixture(fixture)
        results = build.run_fixture(fixture)
        for n, result in enumerate(results, 1):
            cid = f"{fixture.stem}-c{n:03d}"
            if command_expected.get(cid) != result:
                fresh_bad.append(f"{cid}: ledger {command_expected.get(cid)!r} run {result!r}")
                if len(fresh_bad) > 8:
                    break
        if len(fresh_bad) > 8:
            break
    ported_lines = []
    for path in sorted(named):
        item = named_rows.get(path)
        if item is None:
            fresh_bad.append(f"{path}: no ledger row")
            continue
        if item["capability"] != "implemented" or "not re-ported" in item["prime_statement"]:
            fresh_bad.append(f"{path}: not linked to a fixture command")
        elif not str(item["expected"]).startswith("["):
            fresh_bad.append(f"{path}: expected was not a run result")
        else:
            ported_lines.append(
                f"ported {path} {item['query_kind']} {item['artifact']} {item['expected']}"
            )
    proc = subprocess.run(
        ["bash", "-c", 'ulimit -v 25165824 && exec "$1" --lang prime "$2"',
         "run", str(BIN), str(ROOT / "tests/typed_conversion/contract_controls.metta")],
        text=True, capture_output=True,
    )
    proc2 = subprocess.run(
        ["bash", "-c", 'ulimit -v 25165824 && exec "$1" --lang prime "$2"',
         "run", str(BIN), str(ROOT / "tests/typed_conversion/named_controls.metta")],
        text=True, capture_output=True,
    )
    found = {}
    for blob in (proc.stdout, proc2.stdout):
        for line in blob.splitlines():
            parsed = verdict(line)
            if parsed:
                found[parsed[0]] = parsed[1]
    bad = [f"{name}: got {found.get(name)} want {want}" for name, want in WANT.items() if found.get(name) != want]
    lines = [
        f"binary {BIN}",
        f"binary-sha256 {build.sha256_file(BIN)}",
        f"source-files {len(files)}",
        f"recorded-files {len(recorded)}",
        f"missing-files {len(missing_files)}",
        f"extra-files {len(extra)}",
        f"reused-commands {commands}",
        f"library-loads {loads}",
        f"control-mismatches {len(bad)}",
        f"fixture-rerun-mismatches {len(fresh_bad)}",
        f"print-axioms {len(print_axioms)}",
        f"print-axioms-missing {sum(1 for item in print_axioms if item['capability'] == 'missing')}",
        f"print-axioms-item-lessons {sum(1 for item in print_axioms if item['id'].startswith('item-'))}",
    ]
    lines.extend(ported_lines)
    lines.extend(bad)
    lines.extend(fresh_bad)
    lines.append("curriculum " + (
        "green" if not missing_files and not extra and commands == 202 and loads == 4 and not bad and not fresh_bad
        else "red"
    ))
    print("\n".join(lines))
    DEPS.write_text(
        "\n".join([
            "# Curriculum dependencies",
            "",
            "Implemented by reference, not copied: the 202 commands in",
            "`curriculum_coq.metta`, `curriculum_hol.metta`, `curriculum_lean.metta`,",
            "and `curriculum_megalodon.metta`.",
            "",
            "Guest encoders and concrete-syntax readers that cetta ran are implemented.",
            "Their expected field is the line that run printed.",
            "`#print axioms` lines are generated artifacts.",
            "",
            "Missing rows by dependency:",
            *[f"- {count} {name}" for name, count in sorted(dep_counts.items(), key=lambda kv: (-kv[1], kv[0]))],
            "",
        ])
    )
    blankets = {
        "Data and quotation migration (M3)",
        "impredicative proof family (H1)",
        "notation elaboration (OSLF/Sol)",
    }
    samples: dict[str, dict] = {}
    blanket_hits = []
    parsed_rows = []
    for row in rows:
        cols = row.split("\t")
        if len(cols) != len(build.COLUMNS):
            continue
        item = dict(zip(build.COLUMNS, cols))
        parsed_rows.append(item)
        if item["capability"] != "missing":
            continue
        samples.setdefault(item["dependency"], item)
        guestish = item["source_statement"].startswith("#print axioms") or (
            item["source_path"].endswith(".metta")
            and item["source_path"].startswith((
                "DeduktiLambdapi/", "Notation/", "VerifiedMeTTa/", "PrimeMotivation/",
            ))
        )
        if guestish and item["dependency"] in blankets:
            blanket_hits.append(item["id"] + " " + item["source_path"])
    by_dep: dict[str, list[dict]] = {}
    unexhibited = []
    for item in parsed_rows:
        if item["capability"] != "missing":
            continue
        by_dep.setdefault(item["dependency"], []).append(item)
        if not build.quote_claims(item["source_statement"], item["dependency"], item["source_path"]):
            unexhibited.append(item)
    if unexhibited:
        for item in unexhibited[:12]:
            print(f"unexhibited {item['id']} {item['dependency']} :: {item['source_statement'][:160]}")
        raise SystemExit(f"{len(unexhibited)} missing rows do not exhibit their dependency")
    sample_lines = [
        "# Dependency sample",
        "",
        "Each remaining missing bucket, with one source line and the absent feature.",
        "Every missing row was checked, not only this sample.",
        "",
    ]
    for dep, items in sorted(by_dep.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        item = items[0]
        path, quote = item["source_path"], item["source_statement"]
        if not build.quote_claims(quote, dep, path):
            raise SystemExit(f"sample quote does not show {dep}: {quote}")
        sample_lines.extend([
            f"## {dep}",
            f"count: {len(items)}",
            f"source: {path}",
            f"> {quote}",
            f"feature: {dep}",
            "",
        ])
    sample_lines.append(f"blanket-on-print-or-guest: {len(blanket_hits)}")
    sample_lines.extend(blanket_hits[:20])
    sample_lines.append("")
    (SCRATCH / "dependency-sample.md").write_text("\n".join(sample_lines))
    return 0 if lines[-1].endswith("green") else 1


if __name__ == "__main__":
    sys.exit(main())
