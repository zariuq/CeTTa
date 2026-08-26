#!/usr/bin/env python3
"""Run the ordered, fail-closed CeTTa chaining portfolio."""

from __future__ import annotations

import argparse
import csv
import glob
import hashlib
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from time import time_ns


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from petta_machine_stats import (  # noqa: E402
    aggregate_invocations,
    extract_observability,
)


ROOT = Path(__file__).resolve().parents[1]
BENCH = ROOT / "benchmarks" / "chaining"
MANIFEST = BENCH / "manifest.tsv"
FATAL = re.compile(
    r"(^|\n)(?:ERROR:|Error in|\(Error(?:\s|\))|Stack limit exceeded|Assertion .*failed|"
    r"Segmentation fault|Out of memory|Killed(?:\n|$))"
)
VARIABLE = re.compile(r"\$[A-Za-z_][A-Za-z0-9_]*")
OCCURS_SETUP = re.compile(
    r"^(?:\(translatePredicate \(set_prolog_flag occurs_check (?:true|True)\)\)|"
    r"\$_[0-9]+)$"
)
GENERATED_PARAMETERS = {
    "roman_chain_backward": ("25", "8"),
    "roman_chain_branching": ("10", "1"),
}
MECHANISM_MACHINE_COUNTERS = (
    "invocations",
    "transitions",
    "clause_snapshot_calls",
    "clause_snapshot_cache_hits",
    "clause_snapshot_records_examined",
    "clause_snapshot_candidates",
    "clause_candidates",
    "clause_candidates_shape_pruned",
    "match_decision_runs",
    "match_decision_clause_inputs",
    "match_decision_clause_survivors",
    "clause_match_attempts",
    "clause_match_allocated_bytes",
    "unification_calls",
    "unification_failures",
    "unification_binding_writes",
    "unification_allocated_bytes",
    "clause_binding_merge_calls",
    "clause_binding_merge_source_items",
    "clause_binding_merge_logical_writes",
    "clause_binding_merge_failures",
    "binding_apply_calls",
    "binding_apply_rewrites",
    "binding_apply_allocated_bytes",
    "binding_apply_environment_entries",
    "binding_apply_epoch_calls",
    "binding_apply_epoch_suffix_entries",
    "solve_expression_apply_calls",
    "solve_expression_apply_allocated_bytes",
    "solve_expression_open_template_admitted_calls",
    "solve_expression_open_template_admitted_allocated_bytes",
    "solve_expected_apply_calls",
    "solve_expected_apply_allocated_bytes",
    "solve_expected_open_template_admitted_calls",
    "solve_expected_open_template_admitted_allocated_bytes",
    "activation_materialization_calls",
    "activation_materialization_allocated_bytes",
    "activation_open_template_admitted_calls",
    "activation_open_template_admitted_allocated_bytes",
    "constructor_slot_frame_entries",
    "constructor_slot_frame_direct_unifications",
    "pure_grounded_slot_frame_entries",
    "pure_grounded_slot_frame_direct_dispatches",
    "relation_slot_frame_entries",
    "relation_slot_operands_reused",
    "choice_resumes",
    "choice_continuation_items_trailed",
    "deterministic_clause_choices_elided",
    "rollbacks",
    "answers",
    "choice_binding_items_discarded",
    "choice_trail_entries_discarded",
    "choice_heap_resets",
    "choice_heap_bytes_reclaimed",
    "atom_copy_allocated_bytes",
    "max_goal_depth",
    "max_choice_depth",
    "max_binding_entries",
    "max_binding_apply_environment_entries",
    "max_binding_apply_epoch_suffix_entries",
)
MECHANISM_RUNTIME_COUNTERS = (
    "bindings-apply",
    "bindings-lookup",
    "bindings-lookup-cache-hit",
    "bindings-lookup-cache-miss",
    "bindings-lookup-resolve",
    "bindings-lookup-add-guard",
    "bindings-lookup-apply",
    "bindings-lookup-loop-check",
    "bindings-lookup-match",
    "bindings-apply-rewrite-node-visit",
    "bindings-apply-epoch-node-visit",
    "bindings-loop-node-visit",
    "query-visible-node-visit",
    "bindings-seen-scan",
    "term-universe-lookup",
    "term-universe-hit",
    "term-universe-insert",
    "term-universe-byte-entry",
    "term-universe-fallback-entry",
    "term-universe-blob-bytes",
    "term-universe-lazy-decode",
    "term-universe-copy-call",
    "term-universe-copy-node",
    "term-universe-copy-memo-hit",
    "term-universe-copy-estimated-arena-bytes",
    "term-universe-copy-memo-heap-bytes",
    "term-universe-source-memo-lookup",
    "term-universe-source-memo-hit",
    "term-universe-source-memo-store",
    "bindings-entry-pool-bytes",
    "bindings-entry-pool-bytes-peak",
    "bindings-entry-retained-bytes",
    "bindings-entry-retained-bytes-peak",
    "bindings-entry-active-bytes-peak",
    "persistent-arena-alloc-bytes",
    "persistent-arena-live-bytes-peak",
    "persistent-arena-reserved-bytes-peak",
    "eval-arena-alloc-bytes",
    "eval-arena-live-bytes-peak",
    "eval-arena-reserved-bytes-peak",
    "scratch-arena-alloc-bytes",
    "scratch-arena-live-bytes-peak",
    "scratch-arena-reserved-bytes-peak",
    "loop-view-prepared-tail",
    "petta-equation-template-c0-admission-attempt",
    "petta-equation-template-c0-artifact-built",
    "petta-equation-template-c0-artifact-declined",
    "petta-equation-template-c0-execution-admitted",
    "petta-equation-template-c0-execution-match",
    "petta-equation-template-c0-execution-mismatch",
    "petta-equation-template-c0-execution-fallback",
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_tree_sha256() -> str:
    paths = run_text(
        [
            "git",
            "ls-files",
            "-c",
            "-o",
            "--exclude-standard",
            "--",
            "Makefile",
            "VERSION",
            "src",
            "native",
            "lib",
            "langdef",
            "experiments/gslt2parse_foundation/native",
        ],
        cwd=ROOT,
    ).splitlines()
    digest = hashlib.sha256()
    for relative in sorted(paths):
        path = ROOT / relative
        if not path.is_file():
            continue
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        digest.update(b"\0")
    return digest.hexdigest()


def run_text(command: list[str], cwd: Path | None = None) -> str:
    completed = subprocess.run(
        command,
        cwd=cwd,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return completed.stdout.strip()


def load_manifest() -> dict[str, dict[str, str]]:
    with MANIFEST.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    result: dict[str, dict[str, str]] = {}
    for row in rows:
        row_id = row["id"]
        if row_id in result:
            raise ValueError(f"duplicate manifest id: {row_id}")
        if Path(row["program"]).is_absolute():
            raise ValueError(f"absolute benchmark path in {row_id}")
        result[row_id] = row
    return result


def verify_nil_source_provenance() -> None:
    base = BENCH / "nil_current"
    provenance = base / "source_provenance.tsv"
    with provenance.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    declared = {row["artifact"]: row["artifact_sha256"] for row in rows}
    actual = {
        str(path.relative_to(base)): sha256_file(path)
        for path in sorted(base.glob("**/*"))
        if path.is_file() and path != provenance
    }
    if declared != actual:
        missing = sorted(set(declared) - set(actual))
        extra = sorted(set(actual) - set(declared))
        changed = sorted(
            key for key in set(actual) & set(declared) if actual[key] != declared[key]
        )
        raise ValueError(
            "Nil provenance mismatch: "
            f"missing={missing}, extra={extra}, changed={changed}"
        )


def nil_provenance_identity() -> dict[str, str]:
    provenance = BENCH / "nil_current" / "source_provenance.tsv"
    with provenance.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    revisions = {row["source_revision"] for row in rows}
    if len(revisions) != 1:
        raise ValueError(f"Nil provenance has multiple source revisions: {revisions}")
    return {
        "nil_source_revision": revisions.pop(),
        "nil_provenance_sha256": sha256_file(provenance),
    }


def resolve_python312_libdir(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    env_value = os.environ.get("PYTHON312_LIBDIR")
    if env_value:
        candidates.append(Path(env_value))
    executable = shutil.which("python3.12")
    if executable:
        try:
            value = run_text(
                [
                    executable,
                    "-c",
                    "import sysconfig; print(sysconfig.get_config_var('LIBDIR') or '')",
                ]
            )
            if value:
                candidates.append(Path(value))
        except subprocess.CalledProcessError:
            pass
    candidates.extend(
        Path(path)
        for path in glob.glob(
            str(Path.home() / ".local/share/uv/python/cpython-3.12*/lib")
        )
    )
    for candidate in candidates:
        if (candidate / "libpython3.12.so.1.0").is_file():
            return candidate.resolve()
    raise ValueError(
        "SWI Janus requires libpython3.12.so.1.0; set --python312-libdir"
    )


def petta_checkout_identity(root: Path) -> dict[str, str]:
    if not (root / "run.sh").is_file():
        raise ValueError(f"PeTTa run.sh is absent: {root}")
    revision = run_text(["git", "rev-parse", "HEAD"], cwd=root)
    status = run_text(["git", "status", "--porcelain"], cwd=root)
    if status:
        print(
            "WARNING: measuring a dirty PeTTa checkout; "
            "the result identity records its status digest",
            file=sys.stderr,
        )
    return {
        "revision": revision,
        "dirty": "1" if status else "0",
        "status_sha256": sha256_bytes(status.encode("utf-8")),
        "run_sha256": sha256_file(root / "run.sh"),
        "swi": run_text(["swipl", "--version"]),
    }


def compose_program(row: dict[str, str], scratch: Path) -> Path:
    program = (BENCH / row["program"]).resolve()
    if not program.is_relative_to(BENCH.resolve()) or not program.is_file():
        raise ValueError(f"invalid program for {row['id']}: {program}")
    output = scratch / f"{row['id']}.metta"
    if program.suffix == ".sh":
        parameters = GENERATED_PARAMETERS.get(row["id"])
        if parameters is None:
            raise ValueError(f"missing generator parameters for {row['id']}")
        completed = subprocess.run(
            ["bash", str(program), *parameters],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        output.write_bytes(completed.stdout)
        return output
    query_value = row["query"]
    if query_value == "-":
        return program
    query = (BENCH / query_value).resolve()
    if not query.is_relative_to(BENCH.resolve()) or not query.is_file():
        raise ValueError(f"invalid query for {row['id']}: {query}")
    program_bytes = program.read_bytes()
    with output.open("wb") as stream:
        stream.write(program_bytes)
        if program_bytes and not program_bytes.endswith(b"\n"):
            stream.write(b"\n")
        stream.write(query.read_bytes())
    return output


def alpha_normalize(line: str) -> str:
    names: dict[str, str] = {}

    def replace(match: re.Match[str]) -> str:
        token = match.group(0)
        if token not in names:
            names[token] = f"$V{len(names)}"
        return names[token]

    return VARIABLE.sub(replace, line)


def unwrap_result_bag(line: str) -> list[str]:
    if not (line.startswith("[") and line.endswith("]")):
        return [line]
    content = line[1:-1]
    if not content:
        return []
    results: list[str] = []
    start = 0
    depth = 0
    for index, character in enumerate(content):
        if character in "([{":
            depth += 1
        elif character in ")]}":
            depth -= 1
            if depth < 0:
                raise ValueError(f"unbalanced result bag: {line}")
        elif character == "," and depth == 0:
            results.append(content[start:index].strip())
            start = index + 1
    if depth != 0:
        raise ValueError(f"unbalanced result bag: {line}")
    results.append(content[start:].strip())
    return results


def normalize(raw: bytes, kind: str) -> bytes:
    text = raw.decode("utf-8", errors="strict").replace("\r\n", "\n")
    lines = [line for line in text.split("\n") if line]
    if kind in {"occurs-check", "occurs-check-alpha"}:
        if not lines or not OCCURS_SETUP.fullmatch(lines[0]):
            first = lines[0] if lines else "<empty>"
            raise ValueError(f"missing occurs-check setup result; first line was {first!r}")
        lines = lines[1:]
        if kind == "occurs-check-alpha":
            lines = [alpha_normalize(line) for line in lines]
    elif kind == "roman-setup":
        while lines and lines[0] == "true":
            lines.pop(0)
        if not lines or not OCCURS_SETUP.fullmatch(lines[0]):
            raise ValueError("missing Roman occurs-check phase boundary")
        lines = lines[1:]
    elif kind in {"result-lines", "mutation-results"}:
        lines = [result for line in lines for result in unwrap_result_bag(line)]
        if kind == "mutation-results":
            lines = [line for line in lines if line not in {"()", "true"}]
    elif kind == "identity-alpha":
        lines = [alpha_normalize(line) for line in lines]
    elif kind != "identity":
        raise ValueError(f"unknown normalizer: {kind}")
    return (("\n".join(lines) + "\n") if lines else "").encode("utf-8")


def engine_specs(row: dict[str, str]) -> list[tuple[str, str]]:
    profiles = {
        "petta": [("cetta-petta", "petta"), ("swi-petta", "swi")],
        "he-prime": [("cetta-he", "he"), ("cetta-prime", "prime")],
        "he-prime-petta": [
            ("cetta-he", "he"),
            ("cetta-prime", "prime"),
            ("swi-petta", "swi"),
        ],
        "he-prime-petta-zero": [
            ("cetta-he", "he"),
            ("cetta-prime", "prime"),
            ("swi-petta", "swi"),
            ("cetta-zero", "zero"),
        ],
    }
    try:
        return profiles[row["engines"]]
    except KeyError as error:
        raise ValueError(f"unknown engine profile: {row['engines']}") from error


def balanced_sample_order(
    specs: list[tuple[str, str]], sample: int
) -> list[tuple[str, str]]:
    return specs if sample % 2 else list(reversed(specs))


def cetta_ab_specs(row: dict[str, str]) -> list[tuple[str, str]]:
    if ("cetta-petta", "petta") not in engine_specs(row):
        raise ValueError(
            f"CeTTa A/B requires a PeTTa lane: {row['id']}"
        )
    return [
        ("reference-cetta-petta", "petta"),
        ("cetta-petta", "petta"),
    ]


def select_rows(
    manifest: dict[str, dict[str, str]],
    requested: list[str] | None,
    cetta_ab: bool,
) -> list[str]:
    selected = list(requested) if requested else list(manifest)
    unknown = sorted(set(selected) - set(manifest))
    if unknown:
        raise ValueError(f"unknown row(s): {', '.join(unknown)}")
    if not cetta_ab:
        return selected
    incompatible = [
        row_id
        for row_id in selected
        if ("cetta-petta", "petta") not in engine_specs(manifest[row_id])
    ]
    if requested and incompatible:
        raise ValueError(
            "CeTTa A/B requires a PeTTa lane; incompatible row(s): "
            + ", ".join(incompatible)
        )
    return [row_id for row_id in selected if row_id not in incompatible]


def run_engine(
    name: str,
    language: str,
    row: dict[str, str],
    program: Path,
    sample: int,
    scratch: Path,
    cetta: Path,
    petta_root: Path,
    python_libdir: Path,
    mechanism_stats: bool = False,
    measure_instructions: bool = False,
) -> dict[str, object]:
    raw = scratch / f"{name}.{sample}.stdout"
    error = scratch / f"{name}.{sample}.stderr"
    timing = scratch / f"{name}.{sample}.time"
    source = program
    if language == "zero":
        candidate = program.with_suffix(".zero.metta")
        if not candidate.is_file():
            raise ValueError(f"missing Zero companion: {candidate}")
        source = candidate
    if mechanism_stats and language == "swi":
        raise ValueError("mechanism statistics require a CeTTa engine")
    if language == "swi":
        command = ["bash", str(petta_root / "run.sh"), str(source), "--silent"]
        cwd = petta_root
    else:
        command = [str(cetta)]
        if mechanism_stats:
            command.append("--emit-runtime-stats")
        command.extend(("--lang", language))
        if row["oracle"] == "count":
            command.append("--count-only")
        command.append(str(source))
        cwd = ROOT
    timed = [
        "/usr/bin/time",
        "-f",
        "%e\t%M",
        "-o",
        str(timing),
        *command,
    ]
    instruction_file = scratch / f"{name}.{sample}.instructions"
    measured = timed
    if measure_instructions:
        measured = [
            "perf",
            "stat",
            "-x",
            "\t",
            "-e",
            "instructions:u",
            "-o",
            str(instruction_file),
            "--",
            *timed,
        ]
    timeout = int(row["timeout_s"])
    wrapped = [
        "timeout",
        "--signal=TERM",
        "--kill-after=5s",
        f"{timeout}s",
        *measured,
    ]
    environment = os.environ.copy()
    environment["LC_ALL"] = "C.UTF-8"
    environment["CETTA_PETTA_SEARCH_MACHINE"] = "1"
    environment["CETTA_PETTA_MACHINE_STATS"] = "1" if mechanism_stats else "0"
    if language == "swi":
        existing = environment.get("LD_LIBRARY_PATH")
        environment["LD_LIBRARY_PATH"] = str(python_libdir) + (
            f":{existing}" if existing else ""
        )
        environment["PETTA_STACK_LIMIT"] = "8g"
    started = time_ns()
    with raw.open("wb") as stdout, error.open("wb") as stderr:
        completed = subprocess.run(
            wrapped, cwd=cwd, env=environment, stdout=stdout, stderr=stderr
        )
    elapsed_ns = time_ns() - started
    stderr_text = error.read_text(encoding="utf-8", errors="replace")
    stdout_text = raw.read_text(encoding="utf-8", errors="replace")
    if completed.returncode != 0:
        raise RuntimeError(
            f"{name} failed on {row['id']} sample {sample} "
            f"(exit {completed.returncode}):\n{stderr_text[-4000:]}"
        )
    machine_stats: dict[str, int | float] | None = None
    runtime_counters: dict[str, int] | None = None
    ordinary_stderr = stderr_text
    if mechanism_stats:
        invocations, runtime_counters, ordinary_stderr = extract_observability(
            stderr_text
        )
        machine_stats = aggregate_invocations(invocations)
        if not runtime_counters:
            raise RuntimeError(
                f"{name} emitted no runtime counters on {row['id']}"
            )
    diagnostic = stdout_text + "\n" + ordinary_stderr
    if FATAL.search(diagnostic):
        raise RuntimeError(
            f"{name} emitted a fatal diagnostic on {row['id']}:\n"
            f"{diagnostic[-4000:]}"
        )
    raw_bytes = raw.read_bytes()
    if row["oracle"] == "count":
        raw_lines = [line for line in stdout_text.splitlines() if line]
        if language == "swi":
            count = sum("(: " in line for line in raw_lines)
        else:
            if not raw_lines or not raw_lines[-1].isdigit():
                raise RuntimeError(
                    f"{name} count-only output lacks a final integer on {row['id']}"
                )
            count = int(raw_lines[-1])
        normalized = f"{count}\n".encode("ascii")
    else:
        normalized = normalize(raw_bytes, row["normalizer"])
        count = normalized.count(b"\n")
    time_fields = timing.read_text(encoding="utf-8").strip().split("\t")
    if len(time_fields) != 2:
        raise RuntimeError(f"missing time/RSS result for {name} on {row['id']}")
    result: dict[str, object] = {
        "engine": name,
        "sample": sample,
        "wall_s": time_fields[0],
        "max_rss_kib": int(time_fields[1]),
        "outer_elapsed_ns": elapsed_ns,
        "count": count,
        "sha256": sha256_bytes(normalized),
        "normalized": normalized,
    }
    if measure_instructions:
        instruction_lines = [
            line
            for line in instruction_file.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.startswith("#")
        ]
        if len(instruction_lines) != 1:
            raise RuntimeError(
                f"missing instruction result for {name} on {row['id']}"
            )
        fields = instruction_lines[0].split("\t")
        if len(fields) < 3 or fields[2] != "instructions:u":
            raise RuntimeError(
                f"malformed instruction result for {name} on {row['id']}"
            )
        try:
            result["instructions"] = int(fields[0])
        except ValueError as error:
            raise RuntimeError(
                f"instructions were not counted for {name} on {row['id']}"
            ) from error
    if mechanism_stats:
        result["machine_stats"] = machine_stats
        result["runtime_counters"] = runtime_counters
    return result


def qualify(row: dict[str, str], results: list[dict[str, object]]) -> None:
    references: dict[str, tuple[int, str, bytes]] = {}
    for result in results:
        key = str(result["engine"])
        value = (
            int(result["count"]),
            str(result["sha256"]),
            bytes(result["normalized"]),
        )
        previous = references.setdefault(key, value)
        if previous != value:
            raise RuntimeError(f"nondeterministic output from {key} on {row['id']}")
    expected_count = row["expected_count"]
    if expected_count != "-":
        for engine, (count, _, _) in references.items():
            if count != int(expected_count):
                raise RuntimeError(
                    f"count oracle failed for {engine} on {row['id']}: "
                    f"expected {expected_count}, got {count}"
                )
    expected_sha = row["expected_ordered_sha256"]
    if expected_sha != "-":
        for engine, (_, actual, _) in references.items():
            if actual != expected_sha:
                raise RuntimeError(
                    f"ordered SHA-256 oracle failed for {engine} on {row['id']}: "
                    f"expected {expected_sha}, got {actual}"
                )
    if row["oracle"] in {"exact", "cross-engine"}:
        values = list(references.values())
        if any(value[2] != values[0][2] for value in values[1:]):
            summary = ", ".join(
                f"{engine}=count:{value[0]},sha256:{value[1]}"
                for engine, value in references.items()
            )
            raise RuntimeError(
                f"ordered cross-engine oracle failed on {row['id']}: {summary}"
            )


def mechanism_record(row_id: str, result: dict[str, object]) -> dict[str, object]:
    machine = result.get("machine_stats")
    runtime = result.get("runtime_counters")
    if not isinstance(machine, dict) or not isinstance(runtime, dict):
        raise RuntimeError(f"missing mechanism statistics for {row_id}")
    missing_machine = [name for name in MECHANISM_MACHINE_COUNTERS if name not in machine]
    missing_runtime = [name for name in MECHANISM_RUNTIME_COUNTERS if name not in runtime]
    if missing_machine or missing_runtime:
        raise RuntimeError(
            f"incomplete mechanism statistics for {row_id}: "
            f"machine={missing_machine}, runtime={missing_runtime}"
        )
    record: dict[str, object] = {
        "row": row_id,
        "sample": result["sample"],
        "wall_s": result["wall_s"],
        "max_rss_kib": result["max_rss_kib"],
        "count": result["count"],
        "sha256": result["sha256"],
    }
    for name in MECHANISM_MACHINE_COUNTERS:
        record[f"machine_{name}"] = machine[name]
    for name in MECHANISM_RUNTIME_COUNTERS:
        record[f"runtime_{name.replace('-', '_')}"] = runtime[name]
    apply_calls = int(machine["binding_apply_calls"])
    apply_bytes = int(machine["binding_apply_allocated_bytes"])
    environment_entries = int(machine["binding_apply_environment_entries"])
    rewrite_visits = int(runtime["bindings-apply-rewrite-node-visit"])
    epoch_visits = int(runtime["bindings-apply-epoch-node-visit"])
    node_visits = rewrite_visits + epoch_visits
    record["derived_binding_apply_node_visits"] = node_visits
    record["derived_binding_apply_bytes_per_call"] = (
        f"{apply_bytes / apply_calls:.6f}" if apply_calls else ""
    )
    record["derived_binding_apply_environment_entries_per_call"] = (
        f"{environment_entries / apply_calls:.6f}" if apply_calls else ""
    )
    record["derived_binding_apply_node_visits_per_call"] = (
        f"{node_visits / apply_calls:.6f}" if apply_calls else ""
    )
    return record


def write_mechanism_tsv(
    path: Path,
    rows: list[dict[str, object]],
    identities: dict[str, str],
) -> None:
    fields = (
        "row",
        "sample",
        "wall_s",
        "max_rss_kib",
        "count",
        "sha256",
        *(f"machine_{name}" for name in MECHANISM_MACHINE_COUNTERS),
        *(f"runtime_{name.replace('-', '_')}" for name in MECHANISM_RUNTIME_COUNTERS),
        "derived_binding_apply_node_visits",
        "derived_binding_apply_bytes_per_call",
        "derived_binding_apply_environment_entries_per_call",
        "derived_binding_apply_node_visits_per_call",
        *(f"identity_{key}" for key in identities),
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=fields, delimiter="\t", lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", action="append", dest="rows")
    parser.add_argument("--samples", type=int, default=1)
    parser.add_argument(
        "--cetta", type=Path, default=ROOT / "runtime/cetta-perf-57c44f9a-release"
    )
    parser.add_argument("--reference-cetta", type=Path)
    parser.add_argument("--reference-cetta-build-class")
    parser.add_argument("--stats-cetta", type=Path)
    parser.add_argument("--stats-cetta-build-class")
    parser.add_argument("--stats-runs", type=int, default=1)
    parser.add_argument("--stats-output", type=Path)
    parser.add_argument("--instructions", action="store_true")
    parser.add_argument("--cetta-build-class", required=True)
    parser.add_argument("--petta-root", required=True, type=Path)
    parser.add_argument("--python312-libdir")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error("--samples must be positive")
    if args.stats_runs < 1:
        parser.error("--stats-runs must be positive")
    if args.stats_output and not args.stats_cetta:
        parser.error("--stats-output requires --stats-cetta")
    if args.stats_cetta and not args.stats_cetta_build_class:
        parser.error("--stats-cetta requires --stats-cetta-build-class")
    if args.stats_cetta_build_class and not args.stats_cetta:
        parser.error("--stats-cetta-build-class requires --stats-cetta")
    if args.reference_cetta and not args.reference_cetta_build_class:
        parser.error("--reference-cetta requires --reference-cetta-build-class")
    if args.reference_cetta_build_class and not args.reference_cetta:
        parser.error("--reference-cetta-build-class requires --reference-cetta")
    manifest = load_manifest()
    try:
        selected = select_rows(
            manifest, args.rows, args.reference_cetta is not None
        )
    except ValueError as error:
        parser.error(str(error))
    cetta = args.cetta.resolve()
    if not cetta.is_file() or not os.access(cetta, os.X_OK):
        parser.error(f"CeTTa binary is not executable: {cetta}")
    reference_cetta: Path | None = None
    if args.reference_cetta:
        reference_cetta = args.reference_cetta.resolve()
        if not reference_cetta.is_file() or not os.access(reference_cetta, os.X_OK):
            parser.error(
                f"reference CeTTa binary is not executable: {reference_cetta}"
            )
    stats_cetta: Path | None = None
    if args.stats_cetta:
        stats_cetta = args.stats_cetta.resolve()
        if not stats_cetta.is_file() or not os.access(stats_cetta, os.X_OK):
            parser.error(f"stats CeTTa binary is not executable: {stats_cetta}")
    petta_root = args.petta_root.resolve()
    verify_nil_source_provenance()
    petta_identity = petta_checkout_identity(petta_root)
    python_libdir = resolve_python312_libdir(args.python312_libdir)
    identities = {
        "cetta_sha256": sha256_file(cetta),
        "cetta_version": run_text([str(cetta), "-v"]),
        "cetta_git_revision": run_text(["git", "rev-parse", "HEAD"], cwd=ROOT),
        "cetta_source_tree_sha256": source_tree_sha256(),
        "cetta_build_class": args.cetta_build_class,
        "compiler": run_text(["gcc", "--version"]).splitlines()[0],
        "cetta_petta_route": "search-machine",
        "environment": (
            "LC_ALL=C.UTF-8;CETTA_PETTA_SEARCH_MACHINE=1;"
            "CETTA_PETTA_MACHINE_STATS=0;PETTA_STACK_LIMIT=8g"
        ),
        "petta_revision": petta_identity["revision"],
        "petta_dirty": petta_identity["dirty"],
        "petta_status_sha256": petta_identity["status_sha256"],
        "petta_run_sha256": petta_identity["run_sha256"],
        "swi": petta_identity["swi"],
        "python312_libdir_sha256": sha256_file(
            python_libdir / "libpython3.12.so.1.0"
        ),
        **nil_provenance_identity(),
    }
    if stats_cetta:
        identities["stats_cetta_sha256"] = sha256_file(stats_cetta)
        identities["stats_cetta_build_class"] = args.stats_cetta_build_class
        identities["stats_environment"] = (
            "LC_ALL=C.UTF-8;CETTA_PETTA_SEARCH_MACHINE=1;"
            "CETTA_PETTA_MACHINE_STATS=1"
        )
    if reference_cetta:
        identities["reference_cetta_sha256"] = sha256_file(reference_cetta)
        identities["reference_cetta_version"] = run_text(
            [str(reference_cetta), "-v"]
        )
        identities["reference_cetta_build_class"] = (
            args.reference_cetta_build_class
        )
    if args.instructions:
        identities["perf"] = run_text(["perf", "--version"])
    print("IDENTITY\t" + "\t".join(f"{key}={value}" for key, value in identities.items()))
    output_rows: list[dict[str, object]] = []
    mechanism_rows: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory(prefix="bench-chaining.", dir=ROOT / "runtime") as raw:
        scratch = Path(raw)
        for row_id in selected:
            row = manifest[row_id]
            program = compose_program(row, scratch)
            specs = (
                cetta_ab_specs(row) if reference_cetta else engine_specs(row)
            )
            row_results: list[dict[str, object]] = []
            for sample in range(1, args.samples + 1):
                order = balanced_sample_order(specs, sample)
                for name, language in order:
                    run_cetta = (
                        reference_cetta
                        if name == "reference-cetta-petta"
                        else cetta
                    )
                    result = run_engine(
                        name,
                        language,
                        row,
                        program,
                        sample,
                        scratch,
                        run_cetta,
                        petta_root,
                        python_libdir,
                        measure_instructions=args.instructions,
                    )
                    result["run_sequence"] = len(row_results) + 1
                    row_results.append(result)
            qualify(row, row_results)
            for result in row_results:
                public = {
                    key: value
                    for key, value in result.items()
                    if key not in {"normalized", "machine_stats", "runtime_counters"}
                }
                public["row"] = row_id
                public.update(
                    {f"identity_{key}": value for key, value in identities.items()}
                )
                output_rows.append(public)
                print(
                    "RESULT\t"
                    + "\t".join(
                        f"{key}={public[key]}"
                        for key in (
                            "row",
                            "engine",
                            "sample",
                            "wall_s",
                            "max_rss_kib",
                            "count",
                            "sha256",
                        )
                        + (("instructions",) if args.instructions else ())
                    )
                )
            print(f"ORACLE PASS\t{row_id}\t{row['oracle']}")
            if stats_cetta and ("cetta-petta", "petta") in specs:
                row_mechanisms: list[dict[str, object]] = []
                for sample in range(1, args.stats_runs + 1):
                    result = run_engine(
                        "cetta-petta-stats",
                        "petta",
                        row,
                        program,
                        sample,
                        scratch,
                        stats_cetta,
                        petta_root,
                        python_libdir,
                        mechanism_stats=True,
                    )
                    row_mechanisms.append(result)
                qualify(row, row_mechanisms)
                for result in row_mechanisms:
                    record = mechanism_record(row_id, result)
                    record.update(
                        {f"identity_{key}": value for key, value in identities.items()}
                    )
                    mechanism_rows.append(record)
                    print(
                        "MECHANISM\t"
                        + "\t".join(
                            f"{key}={record[key]}"
                            for key in (
                                "row",
                                "sample",
                                "machine_unification_binding_writes",
                                "machine_binding_apply_calls",
                                "machine_binding_apply_allocated_bytes",
                                "machine_binding_apply_environment_entries",
                                "derived_binding_apply_node_visits",
                            )
                        )
                    )
                print(f"MECHANISM PASS\t{row_id}")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", newline="", encoding="utf-8") as stream:
            fieldnames = [
                "row",
                "engine",
                "sample",
                "run_sequence",
                "wall_s",
                "max_rss_kib",
                "outer_elapsed_ns",
                "count",
                "sha256",
            ]
            if args.instructions:
                fieldnames.append("instructions")
            fieldnames.extend(f"identity_{key}" for key in identities)
            writer = csv.DictWriter(
                stream,
                fieldnames=fieldnames,
                delimiter="\t",
                lineterminator="\n",
            )
            writer.writeheader()
            writer.writerows(output_rows)
    if stats_cetta and not mechanism_rows:
        raise RuntimeError("selected rows contain no CeTTa PeTTa mechanism lane")
    if args.stats_output:
        write_mechanism_tsv(args.stats_output, mechanism_rows, identities)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1) from error
