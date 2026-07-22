#!/usr/bin/env python3
"""Shared model for CeTTa runtime-readiness evidence and qualification."""

from __future__ import annotations

import hashlib
import json
import math
import os
import signal
import subprocess
import time
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


PROPERTY_SCHEMA = "cetta.main-readiness.properties.v2"
EVIDENCE_SCHEMA = "cetta.main-readiness.evidence.v3"


class ReadinessModelError(ValueError):
    """A readiness manifest or evidence record is malformed."""


def canonical_json(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def terminate_process_session(process: subprocess.Popen[Any]) -> None:
    """Terminate a runner and every process in sessions nested below it."""
    # A process created with start_new_session=True is its session leader, so
    # its PID remains the session identifier even if the leader exits before a
    # nested benchmark.  Do not derive the identifier from the live leader:
    # that was precisely how orphaned benchmark grandchildren escaped cleanup.
    session_id = process.pid
    owned_session_ids = {session_id}

    def process_table() -> list[tuple[int, int, int]]:
        result = subprocess.run(
            ["ps", "-eo", "pid=,ppid=,sid="],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        rows: list[tuple[int, int, int]] = []
        for line in result.stdout.splitlines():
            fields = line.split()
            if len(fields) == 3:
                rows.append(tuple(map(int, fields)))
        return rows

    # A nested readiness runner used to create another private session.  A
    # signal to the outer session then orphaned the expensive benchmark.  Find
    # descendant sessions while the outer leader is still present and retain
    # their identifiers throughout cleanup.
    descendants = {process.pid}
    table = process_table()
    changed = True
    while changed:
        changed = False
        for pid, parent, sid in table:
            if parent in descendants and pid not in descendants:
                descendants.add(pid)
                owned_session_ids.add(sid)
                changed = True

    def session_pids() -> list[int]:
        return [
            pid
            for pid, _parent, sid in process_table()
            if sid in owned_session_ids
        ]

    for pid in session_pids():
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass

    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        # Reap the leader when possible, then wait for the entire private
        # session—not only its leader—to drain.
        process.poll()
        if not session_pids():
            return
        time.sleep(0.05)

    for pid in session_pids():
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass

    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        process.poll()
        if not session_pids():
            return
        time.sleep(0.05)
    remaining = session_pids()
    if remaining:
        raise RuntimeError(
            "failed to terminate readiness process session: "
            + ", ".join(str(pid) for pid in remaining)
        )


def load_property_manifest(path: Path) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ReadinessModelError(f"cannot read property manifest: {exc}") from exc
    if manifest.get("schema") != PROPERTY_SCHEMA:
        raise ReadinessModelError(
            f"property schema must be {PROPERTY_SCHEMA!r}"
        )
    if manifest.get("qualification_runs_required") != 5:
        raise ReadinessModelError("qualification requires exactly five runs")
    sizes = manifest.get("routine_ladder_sizes")
    if (
        not isinstance(sizes, list)
        or len(sizes) < 4
        or any(not isinstance(size, int) or size <= 0 for size in sizes)
        or sizes != sorted(set(sizes))
    ):
        raise ReadinessModelError(
            "routine_ladder_sizes must be a strictly increasing positive ladder"
        )
    memory_limits = manifest.get("modeled_memory_bytes_per_entry_limits")
    if (
        not isinstance(memory_limits, dict)
        or not memory_limits
        or any(
            not isinstance(name, str)
            or not name
            or not isinstance(limit, (int, float))
            or limit <= 0
            for name, limit in memory_limits.items()
        )
    ):
        raise ReadinessModelError(
            "modeled_memory_bytes_per_entry_limits must map series names "
            "to positive bounds"
        )
    qualification_mutants = manifest.get("qualification_mutants")
    if (
        not isinstance(qualification_mutants, list)
        or not qualification_mutants
        or qualification_mutants != sorted(set(qualification_mutants))
        or any(
            not isinstance(mutant, str) or not mutant.startswith("mutant:")
            for mutant in qualification_mutants
        )
    ):
        raise ReadinessModelError(
            "qualification_mutants must be a sorted, unique, nonempty mutant list"
        )
    negative_registry = manifest.get("negative_calibration_witnesses")
    if (
        not isinstance(negative_registry, dict)
        or not negative_registry
        or any(
            not isinstance(name, str)
            or not name.startswith("negative:")
            or not isinstance(gate, str)
            or not gate
            for name, gate in negative_registry.items()
        )
    ):
        raise ReadinessModelError(
            "negative_calibration_witnesses must map negative ids to gates"
        )
    properties = manifest.get("properties")
    if not isinstance(properties, list) or not properties:
        raise ReadinessModelError("property manifest must contain properties")
    seen_ids: set[str] = set()
    seen_paths: set[str] = set()
    largest_classes: dict[str, str] = {}
    required_lists = (
        "source_anchors",
        "routine_witnesses",
        "exhaustive_witnesses",
        "negative_calibrations",
        "evidence",
    )
    for prop in properties:
        if not isinstance(prop, dict):
            raise ReadinessModelError("each property must be an object")
        prop_id = prop.get("id")
        impl = prop.get("implementation_path")
        largest = prop.get("largest_scale_class")
        if not isinstance(prop_id, str) or not prop_id:
            raise ReadinessModelError("each property requires a nonempty id")
        if prop_id in seen_ids:
            raise ReadinessModelError(f"duplicate property id: {prop_id}")
        seen_ids.add(prop_id)
        if not isinstance(impl, str) or not impl:
            raise ReadinessModelError(f"{prop_id}: missing implementation_path")
        if impl in seen_paths:
            raise ReadinessModelError(
                f"{prop_id}: implementation_path must identify one property: {impl}"
            )
        seen_paths.add(impl)
        if not isinstance(largest, str) or not largest:
            raise ReadinessModelError(f"{prop_id}: missing largest_scale_class")
        if largest in largest_classes:
            raise ReadinessModelError(
                f"{prop_id}: largest-scale class {largest!r} is already owned by "
                f"{largest_classes[largest]!r}"
            )
        largest_classes[largest] = prop_id
        largest_witness = prop.get("largest_scale_witness")
        exhaustive_witnesses = prop.get("exhaustive_witnesses", [])
        if (
            not isinstance(largest_witness, str)
            or largest_witness not in exhaustive_witnesses
        ):
            raise ReadinessModelError(
                f"{prop_id}: largest_scale_witness must name an exhaustive witness"
            )
        routine_scale_representative = prop.get("routine_scale_representative")
        if routine_scale_representative is not None and (
            not isinstance(routine_scale_representative, str)
            or routine_scale_representative not in prop.get("routine_witnesses", [])
        ):
            raise ReadinessModelError(
                f"{prop_id}: routine_scale_representative must name a routine witness"
            )
        positive_calibration = prop.get("positive_calibration")
        if (
            not isinstance(positive_calibration, str)
            or positive_calibration not in prop.get("routine_witnesses", [])
        ):
            raise ReadinessModelError(
                f"{prop_id}: positive calibration must name a routine witness"
            )
        for field in required_lists:
            values = prop.get(field)
            if not isinstance(values, list) or not values:
                raise ReadinessModelError(f"{prop_id}: {field} must be nonempty")
        for mutant in prop["negative_calibrations"]:
            if not isinstance(mutant, str) or not mutant.startswith("negative:"):
                raise ReadinessModelError(
                    f"{prop_id}: invalid negative calibration {mutant!r}"
                )
            if mutant not in negative_registry:
                raise ReadinessModelError(
                    f"{prop_id}: unknown negative calibration {mutant!r}"
                )
        for anchor in prop["source_anchors"]:
            if (
                not isinstance(anchor, dict)
                or not isinstance(anchor.get("path"), str)
                or not isinstance(anchor.get("symbol"), str)
                or not anchor["path"]
                or not anchor["symbol"]
            ):
                raise ReadinessModelError(f"{prop_id}: invalid source anchor")
            expected = anchor.get("expected_occurrences")
            if not isinstance(expected, int) or expected <= 0:
                raise ReadinessModelError(
                    f"{prop_id}: source anchor requires positive "
                    "expected_occurrences"
                )
    referenced_negatives = {
        negative
        for prop in properties
        for negative in prop["negative_calibrations"]
    }
    unused_negatives = set(negative_registry) - referenced_negatives
    if unused_negatives:
        raise ReadinessModelError(
            "unused negative calibration witnesses: "
            + ", ".join(sorted(unused_negatives))
        )
    return manifest


def resolve_source_anchors(root: Path, manifest: Mapping[str, Any]) -> list[dict[str, Any]]:
    resolved: list[dict[str, Any]] = []
    for prop in manifest["properties"]:
        for anchor in prop["source_anchors"]:
            relative = Path(anchor["path"])
            path = root / relative
            if not path.is_file():
                raise ReadinessModelError(
                    f"{prop['id']}: source anchor file is missing: {relative}"
                )
            symbol = anchor["symbol"]
            lines = path.read_text(encoding="utf-8").splitlines()
            matches = [index for index, line in enumerate(lines, 1) if symbol in line]
            if not matches:
                raise ReadinessModelError(
                    f"{prop['id']}: source symbol {symbol!r} is absent from {relative}"
                )
            expected = anchor["expected_occurrences"]
            if len(matches) != expected:
                raise ReadinessModelError(
                    f"{prop['id']}: source symbol {symbol!r} occurs "
                    f"{len(matches)} times in {relative}; expected {expected}"
                )
            resolved.append(
                {
                    "property": prop["id"],
                    "implementation_path": prop["implementation_path"],
                    "path": relative.as_posix(),
                    "symbol": symbol,
                    "line": matches[0],
                    "lines": matches,
                    "occurrences": len(matches),
                    "sha256": sha256_file(path),
                }
            )
    return resolved


def git_value(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def working_tree_content_identity(
    root: Path, *, excluded_paths: Sequence[str] = ("README.md",)
) -> dict[str, Any]:
    """Hash the effective candidate bytes independently of Git commit topology."""
    result = subprocess.run(
        [
            "git",
            "ls-files",
            "-z",
            "--cached",
            "--others",
            "--exclude-standard",
        ],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    excluded = set(excluded_paths)
    names = sorted(
        os.fsdecode(name)
        for name in result.stdout.split(b"\0")
        if name and os.fsdecode(name) not in excluded
    )
    entries: list[dict[str, Any]] = []
    for name in names:
        path = root / name
        if path.is_symlink():
            mode = path.lstat().st_mode & 0o7777
            entry = {
                "path": name,
                "kind": "symlink",
                "mode": mode,
                "sha256": sha256_bytes(os.fsencode(os.readlink(path))),
            }
        elif path.is_file():
            mode = path.stat().st_mode & 0o7777
            entry = {
                "path": name,
                "kind": "file",
                "mode": mode,
                "sha256": sha256_file(path),
            }
        elif path.is_dir():
            raise ReadinessModelError(
                f"candidate source contains an undeclared gitlink: {name}"
            )
        else:
            entry = {
                "path": name,
                "kind": "missing",
                "mode": None,
                "sha256": None,
            }
        entries.append(entry)
    return {
        "schema": "cetta.working-tree-content.v1",
        "file_count": len(entries),
        "paths_sha256": sha256_bytes(canonical_json(names)),
        "content_sha256": sha256_bytes(canonical_json(entries)),
        "excluded_paths": sorted(excluded),
    }


def repository_identity(root: Path) -> dict[str, Any]:
    diff = subprocess.run(
        ["git", "diff", "--binary", "HEAD", "--", ":(exclude)README.md"],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    ).stdout
    staged = subprocess.run(
        [
            "git",
            "diff",
            "--binary",
            "--cached",
            "HEAD",
            "--",
            ":(exclude)README.md",
        ],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    ).stdout
    untracked_names = git_value(
        root, "ls-files", "--others", "--exclude-standard"
    ).splitlines()
    untracked = {
        name: sha256_file(root / name)
        for name in sorted(untracked_names)
        if name != "README.md" and (root / name).is_file()
    }
    return {
        "commit": git_value(root, "rev-parse", "HEAD"),
        "tree": git_value(root, "rev-parse", "HEAD^{tree}"),
        "branch": git_value(root, "branch", "--show-current"),
        "tracked_diff_sha256": sha256_bytes(diff),
        "staged_diff_sha256": sha256_bytes(staged),
        "untracked_sha256": untracked,
        "working_tree": working_tree_content_identity(root),
    }


def dependency_identity(paths: Mapping[str, Path]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for name, path in sorted(paths.items()):
        result[name] = repository_identity(path) if path.is_dir() else {
            "commit": "missing",
            "tree": "missing",
            "branch": "missing",
            "tracked_diff_sha256": "missing",
            "staged_diff_sha256": "missing",
            "untracked_sha256": {},
        }
    return result


def evidence_key(payload: Mapping[str, Any]) -> str:
    return sha256_bytes(canonical_json(payload))


def calibration_subject(artifact: Mapping[str, Any]) -> dict[str, Any]:
    """Return the semantics-bearing identity shared by both readiness tiers."""
    compiler_and_build = artifact.get("compiler_and_build")
    if not isinstance(compiler_and_build, Mapping):
        raise ReadinessModelError("artifact has no compiler/build identity")
    stable_toolchain = {
        key: value
        for key, value in compiler_and_build.items()
        if key != "generated"
    }
    required = (
        "schema",
        "candidate",
        "inputs",
        "dependencies",
        "property_schema",
        "property_manifest_sha256",
        "source_anchors",
    )
    missing = [key for key in required if key not in artifact]
    if missing:
        raise ReadinessModelError(
            "artifact is missing calibration identity fields: "
            + ", ".join(missing)
        )
    return {
        "schema": artifact["schema"],
        "candidate": artifact["candidate"],
        "inputs": artifact["inputs"],
        "toolchain": stable_toolchain,
        "dependencies": artifact["dependencies"],
        "property_schema": artifact["property_schema"],
        "property_manifest_sha256": artifact["property_manifest_sha256"],
        "source_anchors": artifact["source_anchors"],
    }


def qualification_verdict(
    records: Sequence[Mapping[str, Any]],
    *,
    required_runs: int,
    subject_key: str,
) -> dict[str, Any]:
    """Conservatively decide whether routine readiness may be authoritative."""
    if required_runs <= 0:
        raise ReadinessModelError("required_runs must be positive")
    failures: list[str] = []
    pair_ids: set[str] = set()
    passed_pairs = 0
    for index, record in enumerate(records, 1):
        record_failures: list[str] = []
        pair_id = record.get("pair_id")
        if not isinstance(pair_id, str) or not pair_id:
            record_failures.append(f"record {index} has no pair id")
        elif pair_id in pair_ids:
            record_failures.append(f"duplicate pair id {pair_id}")
        else:
            pair_ids.add(pair_id)
        if record.get("subject_key") != subject_key:
            record_failures.append(
                f"record {index} belongs to a different subject"
            )
        routine_status = record.get("routine_status")
        exhaustive_status = record.get("exhaustive_status")
        if routine_status != exhaustive_status:
            record_failures.append(
                f"record {index} routine/exhaustive disagreement"
            )
        if routine_status != "passed" or exhaustive_status != "passed":
            record_failures.append(
                f"record {index} did not pass both tiers"
            )
        if record.get("routine_mutants_killed") is not True:
            record_failures.append(
                f"record {index} did not qualify every mutant"
            )
        if record.get("baseline_match") is not True:
            record_failures.append(
                f"record {index} used different performance baselines"
            )
        if record.get("common_prefix_match") is not True:
            record_failures.append(
                f"record {index} did not authenticate the shared full-gate prefix"
            )
        failures.extend(record_failures)
        if not record_failures:
            passed_pairs += 1
    qualified = passed_pairs >= required_runs and not failures
    status = "qualified" if qualified else ("blocked" if failures else "qualifying")
    return {
        "status": status,
        "qualified": qualified,
        "required_runs": required_runs,
        "recorded_pairs": len(records),
        "passed_pairs": passed_pairs,
        "remaining_pairs": max(0, required_runs - passed_pairs),
        "failures": failures,
    }


def log_slope(samples: Sequence[tuple[float, float]]) -> float:
    if len(samples) < 2:
        raise ReadinessModelError("at least two samples are required")
    if any(x <= 0 or y <= 0 for x, y in samples):
        raise ReadinessModelError("slope samples must be positive")
    xs = [math.log(x) for x, _ in samples]
    ys = [math.log(y) for _, y in samples]
    mean_x = sum(xs) / len(xs)
    mean_y = sum(ys) / len(ys)
    denominator = sum((x - mean_x) ** 2 for x in xs)
    if denominator == 0:
        raise ReadinessModelError("slope sample sizes must differ")
    return sum(
        (x - mean_x) * (y - mean_y) for x, y in zip(xs, ys)
    ) / denominator


def linear_fit(samples: Sequence[tuple[float, float]]) -> tuple[float, float]:
    if len(samples) < 2:
        raise ReadinessModelError("at least two samples are required")
    xs = [x for x, _ in samples]
    ys = [y for _, y in samples]
    mean_x = sum(xs) / len(xs)
    mean_y = sum(ys) / len(ys)
    denominator = sum((x - mean_x) ** 2 for x in xs)
    if denominator == 0:
        raise ReadinessModelError("linear-fit sample sizes must differ")
    rate = sum(
        (x - mean_x) * (y - mean_y) for x, y in zip(xs, ys)
    ) / denominator
    intercept = mean_y - rate * mean_x
    return rate, intercept


def memory_rate_verdict(
    samples: Sequence[tuple[float, float]],
    *,
    max_bytes_per_entry: float,
) -> dict[str, Any]:
    if max_bytes_per_entry <= 0:
        raise ReadinessModelError("max_bytes_per_entry must be positive")
    bytes_per_entry, intercept = linear_fit(samples)
    return {
        "bytes_per_entry": bytes_per_entry,
        "intercept_bytes": intercept,
        "max_bytes_per_entry": max_bytes_per_entry,
        "passed": 0 <= bytes_per_entry <= max_bytes_per_entry,
    }


def growth_verdict(
    time_samples: Sequence[tuple[float, float]],
    rss_samples: Sequence[tuple[float, float]],
    *,
    max_time_slope: float = 1.35,
    max_rss_slope: float = 1.25,
) -> dict[str, Any]:
    time_slope = log_slope(time_samples)
    rss_slope = log_slope(rss_samples)
    return {
        "time_slope": time_slope,
        "rss_slope": rss_slope,
        "max_time_slope": max_time_slope,
        "max_rss_slope": max_rss_slope,
        "passed": time_slope <= max_time_slope and rss_slope <= max_rss_slope,
    }


def paired_growth_verdict(
    candidate_time: Sequence[tuple[float, float]],
    baseline_time: Sequence[tuple[float, float]],
    candidate_rss: Sequence[tuple[float, float]],
    baseline_rss: Sequence[tuple[float, float]],
    *,
    max_time_ratio: float,
    time_absolute_slack: float,
    max_time_slope_delta: float,
    max_rss_ratio: float,
    rss_absolute_slack: float,
    max_rss_slope_delta: float,
) -> dict[str, Any]:
    """Compare scaling shape to an alternating, byte-pinned baseline."""

    bounds = (
        max_time_ratio,
        max_time_slope_delta,
        max_rss_ratio,
        max_rss_slope_delta,
    )
    if any(bound <= 0 for bound in bounds):
        raise ReadinessModelError("paired growth bounds must be positive")
    if time_absolute_slack < 0 or rss_absolute_slack < 0:
        raise ReadinessModelError("paired growth slack must be nonnegative")

    def keyed(
        label: str, samples: Sequence[tuple[float, float]]
    ) -> dict[float, float]:
        result: dict[float, float] = {}
        for size, value in samples:
            if size <= 0 or value <= 0:
                raise ReadinessModelError(
                    f"{label} samples must have positive sizes and values"
                )
            if size in result:
                raise ReadinessModelError(f"{label} repeats size {size}")
            result[size] = value
        if len(result) < 2:
            raise ReadinessModelError(f"{label} requires at least two sizes")
        return result

    candidate_time_by_size = keyed("candidate time", candidate_time)
    baseline_time_by_size = keyed("baseline time", baseline_time)
    candidate_rss_by_size = keyed("candidate RSS", candidate_rss)
    baseline_rss_by_size = keyed("baseline RSS", baseline_rss)
    size_sets = {
        tuple(sorted(candidate_time_by_size)),
        tuple(sorted(baseline_time_by_size)),
        tuple(sorted(candidate_rss_by_size)),
        tuple(sorted(baseline_rss_by_size)),
    }
    if len(size_sets) != 1:
        raise ReadinessModelError("paired growth samples use different sizes")
    sizes = next(iter(size_sets))

    def cells(
        candidate: Mapping[float, float],
        baseline: Mapping[float, float],
        ratio: float,
        slack: float,
    ) -> list[dict[str, Any]]:
        return [
            {
                "size": size,
                "candidate": candidate[size],
                "baseline": baseline[size],
                "limit": max(baseline[size] * ratio, baseline[size] + slack),
                "passed": candidate[size]
                <= max(baseline[size] * ratio, baseline[size] + slack),
            }
            for size in sizes
        ]

    time_cells = cells(
        candidate_time_by_size,
        baseline_time_by_size,
        max_time_ratio,
        time_absolute_slack,
    )
    rss_cells = cells(
        candidate_rss_by_size,
        baseline_rss_by_size,
        max_rss_ratio,
        rss_absolute_slack,
    )
    material_time_sizes = [
        size
        for size in sizes
        if baseline_time_by_size[size] >= time_absolute_slack
    ]
    if len(material_time_sizes) >= 3:
        candidate_time_slope = log_slope(
            [(size, candidate_time_by_size[size]) for size in material_time_sizes]
        )
        baseline_time_slope = log_slope(
            [(size, baseline_time_by_size[size]) for size in material_time_sizes]
        )
        time_slope_delta: float | None = (
            candidate_time_slope - baseline_time_slope
        )
        time_slope_passed = time_slope_delta <= max_time_slope_delta
    else:
        candidate_time_slope = None
        baseline_time_slope = None
        time_slope_delta = None
        time_slope_passed = True
    candidate_rss_slope = log_slope(
        [(size, candidate_rss_by_size[size]) for size in sizes]
    )
    baseline_rss_slope = log_slope(
        [(size, baseline_rss_by_size[size]) for size in sizes]
    )
    rss_slope_delta = candidate_rss_slope - baseline_rss_slope
    rss_slope_passed = rss_slope_delta <= max_rss_slope_delta
    passed = (
        all(cell["passed"] for cell in time_cells)
        and all(cell["passed"] for cell in rss_cells)
        and time_slope_passed
        and rss_slope_passed
    )
    return {
        "passed": passed,
        "time_cells": time_cells,
        "rss_cells": rss_cells,
        "material_time_sizes": material_time_sizes,
        "candidate_time_slope": candidate_time_slope,
        "baseline_time_slope": baseline_time_slope,
        "time_slope_delta": time_slope_delta,
        "max_time_slope_delta": max_time_slope_delta,
        "time_slope_passed": time_slope_passed,
        "candidate_rss_slope": candidate_rss_slope,
        "baseline_rss_slope": baseline_rss_slope,
        "rss_slope_delta": rss_slope_delta,
        "max_rss_slope_delta": max_rss_slope_delta,
        "rss_slope_passed": rss_slope_passed,
    }


def counter_contract(
    counters: Mapping[str, int],
    *,
    zero: Iterable[str] = (),
    positive: Iterable[str] = (),
    equal: Iterable[tuple[str, str]] = (),
    conservation: Iterable[tuple[str, str, str]] = (),
) -> tuple[bool, list[str]]:
    failures: list[str] = []
    for name in zero:
        if counters.get(name, 0) != 0:
            failures.append(f"{name} must be zero")
    for name in positive:
        if counters.get(name, 0) <= 0:
            failures.append(f"{name} must be positive")
    for left, right in equal:
        if counters.get(left) != counters.get(right):
            failures.append(f"{left} must equal {right}")
    for acquired, committed, released in conservation:
        if counters.get(acquired, 0) != (
            counters.get(committed, 0) + counters.get(released, 0)
        ):
            failures.append(
                f"{acquired} must equal {committed} plus {released}"
            )
    return not failures, failures


def route_contract(expected: str, observed: str) -> tuple[bool, str | None]:
    if expected == observed:
        return True, None
    return False, f"expected implementation route {expected!r}, got {observed!r}"
