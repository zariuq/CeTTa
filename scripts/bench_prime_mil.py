#!/usr/bin/env python3
"""Validate and sample Prime MIL candidate-accounting workloads."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import time
from typing import Any


WORKLOADS = {
    "native-grandparent": Path(
        "examples/prime/prime_native_mil_grandparent.metta"
    ),
    "native-list-map-rel": Path(
        "examples/prime/prime_native_mil_list_map_rel.metta"
    ),
    "native-path-canary": Path("tests/prime/native_hyp_path.metta"),
    "hopper-encryption-native": Path(
        "examples/prime/hopper_encryption_native_learning.metta"
    ),
    "grandparent": Path("examples/prime/metagol_typed_grandparent.metta"),
    "higher-order-map": Path("examples/prime/metagol_higher_order_map.metta"),
}

SUMMARY_PREFIX = "[(MIL:BenchV1 "
RSS_PREFIX = "__PRIME_MIL_MAX_RSS_KIB__="
RUNTIME_COUNTER_PREFIX = "runtime-counter "
CHECKING_ROUTE_COUNTERS = (
    "prime-checking-route-scoped-regular",
    "prime-checking-route-authored-regular",
    "prime-checking-route-declared-regular",
    "prime-checking-route-closed-regular",
    "prime-checking-route-ambient-formation",
    "prime-legacy-he-checking",
)
PRIME_HE_APPLICABILITY_COUNTER = "prime-legacy-he-typed-applicability"
PRIME_DECLARATION_COUNTERS = (
    "prime-level-normalization-step",
    "prime-declaration-polymorphic-lookup",
    "prime-declaration-level-parameter-fresh",
    "prime-declaration-level-instance",
    "prime-declaration-level-constraint",
)
PRIME_INTERIOR_CHECK_COUNTERS = (
    "prime-regular-kernel-conversion-interior-check",
    "prime-regular-kernel-synthesis-interior-check",
    "prime-regular-kernel-checking-interior-check",
)
PRIME_NATIVE_CALCULUS_COUNTERS = (
    "prime-native-calculus-candidate",
    "prime-native-map-realized",
    "prime-native-hyp-realized",
    "prime-native-calculus-declined",
    "prime-native-calculus-fault",
    "prime-native-hyp-admission-cache-hit",
    "prime-native-hyp-admission-cache-miss",
    "prime-native-hyp-denotation-admitted",
    "prime-native-hyp-denotation-fallback",
    "prime-native-hyp-candidate-bag-realized",
    "prime-native-hyp-finite-provider-admitted",
    "prime-native-hyp-finite-provider-fallback",
    "prime-native-hyp-finite-search-realized",
    "prime-native-map-rel-realized",
)
NATIVE_REALIZATION_REQUIREMENTS = {
    "native-grandparent": (
        "prime-native-hyp-realized",
        "prime-native-hyp-candidate-bag-realized",
        "prime-native-hyp-finite-provider-admitted",
        "prime-native-hyp-finite-search-realized",
    ),
    "native-list-map-rel": (
        "prime-native-hyp-realized",
        "prime-native-hyp-candidate-bag-realized",
        "prime-native-hyp-finite-provider-admitted",
        "prime-native-hyp-finite-search-realized",
        "prime-native-map-rel-realized",
    ),
    "native-path-canary": (
        "prime-native-hyp-realized",
        "prime-native-hyp-candidate-bag-realized",
        "prime-native-hyp-finite-provider-admitted",
        "prime-native-hyp-finite-search-realized",
    ),
    "hopper-encryption-native": (
        "prime-native-hyp-realized",
        "prime-native-hyp-candidate-bag-realized",
        "prime-native-hyp-finite-provider-admitted",
        "prime-native-hyp-finite-search-realized",
        "prime-native-map-rel-realized",
    ),
}
COUNT_FIELDS = (
    "CandidatesGenerated",
    "CandidatesChecked",
    "Established",
    "Refuted",
    "Undetermined",
    "Incomplete",
    "SafeFrontier",
    "ExampleChecked",
    "ExampleConsistent",
)
LAW_FIELDS = (
    "CountConserved",
    "TraceConserved",
    "TypedProducerEqualsSafeFrontier",
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_summary(stdout: str) -> dict[str, Any]:
    lines = [line for line in stdout.splitlines() if line.startswith(SUMMARY_PREFIX)]
    if len(lines) != 1:
        raise RuntimeError(
            f"expected one MIL.BenchV1 record, observed {len(lines)}"
        )
    line = lines[0]
    name_match = re.search(r"\(Name ([^()\s]+)\)", line)
    if not name_match:
        raise RuntimeError("MIL.BenchV1 record has no atomic Name field")
    counts: dict[str, int] = {}
    for field in COUNT_FIELDS:
        match = re.search(rf"\({re.escape(field)} ([0-9]+)\)", line)
        if not match:
            raise RuntimeError(f"MIL.BenchV1 record has no {field} count")
        counts[field] = int(match.group(1))
    laws: dict[str, bool] = {}
    for field in LAW_FIELDS:
        match = re.search(rf"\({re.escape(field)} (True|False)\)", line)
        if not match:
            raise RuntimeError(f"MIL.BenchV1 record has no {field} law")
        laws[field] = match.group(1) == "True"

    generated = counts["CandidatesGenerated"]
    classified = (
        counts["Established"]
        + counts["Refuted"]
        + counts["Undetermined"]
        + counts["Incomplete"]
    )
    safe = (
        counts["Established"]
        + counts["Undetermined"]
        + counts["Incomplete"]
    )
    if counts["CandidatesChecked"] != generated:
        raise RuntimeError("not every generated candidate was checked")
    if classified != generated:
        raise RuntimeError("candidate verdict counts do not partition the bag")
    if counts["SafeFrontier"] != safe:
        raise RuntimeError("safe frontier removed more than checked refutations")
    if counts["ExampleChecked"] != safe:
        raise RuntimeError("examples did not receive the complete safe frontier")
    failed_laws = [name for name, holds in laws.items() if not holds]
    if failed_laws:
        raise RuntimeError(
            "candidate accounting law failed: " + ", ".join(failed_laws)
        )
    return {
        "name": name_match.group(1),
        "counts": counts,
        "laws": laws,
        "record": line,
    }


def parse_checking_routes(stderr_lines: list[str]) -> dict[str, Any]:
    counters: dict[str, int] = {}
    for line in stderr_lines:
        if not line.startswith(RUNTIME_COUNTER_PREFIX):
            continue
        fields = line.split()
        if len(fields) != 3:
            raise RuntimeError(f"malformed runtime counter: {line!r}")
        name = fields[1]
        try:
            value = int(fields[2])
        except ValueError as exc:
            raise RuntimeError(f"non-integer runtime counter: {line!r}") from exc
        counters[name] = value
    observed_names = (
        CHECKING_ROUTE_COUNTERS
        + (PRIME_HE_APPLICABILITY_COUNTER,)
        + PRIME_DECLARATION_COUNTERS
        + PRIME_INTERIOR_CHECK_COUNTERS
        + PRIME_NATIVE_CALCULUS_COUNTERS
    )
    missing = [name for name in observed_names if name not in counters]
    if missing:
        raise RuntimeError(
            "runtime-stats binary did not expose checking routes: "
            + ", ".join(missing)
        )
    routes = {name: counters[name] for name in CHECKING_ROUTE_COUNTERS}
    native = sum(
        value for name, value in routes.items()
        if name not in (
            "prime-checking-route-ambient-formation",
            "prime-legacy-he-checking",
        )
    )
    ambient = routes["prime-checking-route-ambient-formation"]
    legacy = routes["prime-legacy-he-checking"]
    legacy_applicability = counters[PRIME_HE_APPLICABILITY_COUNTER]
    declaration_work = {
        name: counters[name] for name in PRIME_DECLARATION_COUNTERS
    }
    interior_checks = {
        name: counters[name] for name in PRIME_INTERIOR_CHECK_COUNTERS
    }
    native_calculus = {
        name: counters[name] for name in PRIME_NATIVE_CALCULUS_COUNTERS
    }
    if native == 0 and ambient == 0 and legacy == 0 and legacy_applicability == 0:
        raise RuntimeError("workload produced no observable type-check route")
    if native > 0 and ambient == 0 and legacy == 0 and legacy_applicability == 0:
        qualification = "prime-native-regular"
    elif (native == 0 and ambient > 0 and legacy == 0 and
          legacy_applicability == 0):
        qualification = "ambient-formation-only"
    elif (native == 0 and ambient == 0 and legacy > 0 and
          legacy_applicability == 0):
        qualification = "legacy-he-only"
    else:
        qualification = "mixed-prime-and-he"
    return {
        "qualification": qualification,
        "native_checks": native,
        "ambient_formation_checks": ambient,
        "legacy_he_checks": legacy,
        "legacy_he_applicability_checks": legacy_applicability,
        "declaration_work": declaration_work,
        "interior_checks": interior_checks,
        "native_calculus": native_calculus,
        "counters": routes,
    }


def native_calculus_qualification_error(
    workload: str,
    counters: dict[str, int],
    interior_checks: dict[str, int] | None = None,
) -> str | None:
    faults = counters["prime-native-calculus-fault"]
    if faults != 0:
        return f"native-calculus host reported {faults} faults"
    required = NATIVE_REALIZATION_REQUIREMENTS.get(workload, ())
    missing = [name for name in required if counters[name] == 0]
    if missing:
        return "expected live native realizations: " + ", ".join(missing)
    if "prime-native-hyp-realized" in required:
        denoted = counters["prime-native-hyp-denotation-admitted"]
        fallback = counters["prime-native-hyp-denotation-fallback"]
        if denoted == 0 or fallback != 0:
            return (
                "native hyp qualification requires exact typed denotation, "
                f"observed {denoted} admitted and {fallback} fallback"
            )
        if interior_checks is None:
            return "native hyp qualification has no interior-check receipt"
        interior_total = sum(interior_checks.values())
        if interior_total != 0:
            return (
                "admitted native hyp execution performed "
                f"{interior_total} interior checks"
            )
    return None


def run_sample(
    repo: Path,
    cetta: Path,
    source: Path,
    lane: str,
    timeout_seconds: float,
) -> dict[str, Any]:
    expected = source.with_suffix(".expected")
    environment = os.environ.copy()
    environment["CETTA_NIK_TYPED_APPLICABILITY"] = "1"
    environment["CETTA_PRIME_HE_TYPED_APPLICABILITY"] = (
        "0" if lane == "prime-no-he-applicability" else "1"
    )
    command = [
        "/usr/bin/time",
        "-f",
        RSS_PREFIX + "%M",
        str(cetta),
        "--emit-runtime-stats",
        "--lang",
        "prime",
        str(source),
    ]
    started_ns = time.monotonic_ns()
    completed = subprocess.run(
        command,
        cwd=repo,
        env=environment,
        text=True,
        capture_output=True,
        timeout=timeout_seconds,
        check=False,
    )
    wall_ns = time.monotonic_ns() - started_ns
    stderr_lines = completed.stderr.splitlines()
    rss_lines = [line for line in stderr_lines if line.startswith(RSS_PREFIX)]
    runtime_lines = [
        line for line in stderr_lines
        if line.startswith(RUNTIME_COUNTER_PREFIX)
    ]
    ordinary_stderr = [
        line for line in stderr_lines
        if not line.startswith(RSS_PREFIX)
        and not line.startswith(RUNTIME_COUNTER_PREFIX)
    ]
    if completed.returncode != 0:
        raise RuntimeError(
            f"{source}: exit {completed.returncode}: "
            + "\n".join(ordinary_stderr[-10:])
        )
    if ordinary_stderr:
        raise RuntimeError(f"{source}: unexpected stderr: {ordinary_stderr!r}")
    if len(rss_lines) != 1:
        raise RuntimeError(f"{source}: expected one peak-RSS observation")
    observed = completed.stdout
    oracle = expected.read_text(encoding="utf-8")
    if observed != oracle:
        raise RuntimeError(f"{source}: output differs from {expected}")
    summary = parse_summary(observed)
    checking_routes = parse_checking_routes(runtime_lines)
    return {
        "wall_ns": wall_ns,
        "peak_rss_kib": int(rss_lines[0][len(RSS_PREFIX):]),
        "accounting": summary,
        "checking_routes": checking_routes,
    }


def median(values: list[int]) -> float:
    return float(statistics.median(values))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument(
        "--workload", action="append", choices=tuple(WORKLOADS)
    )
    parser.add_argument("--compare-he-assisted", action="store_true")
    parser.add_argument(
        "--require-prime-native",
        action="store_true",
        help=(
            "fail unless every sampled workload uses only Prime's native "
            "regular checking routes"
        ),
    )
    parser.add_argument(
        "--require-zero-legacy-he-applicability",
        action="store_true",
        help="fail if Prime typed applicability consults the HE classifier",
    )
    parser.add_argument("--out", type=Path)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    repo = args.repo.resolve()
    cetta = args.cetta.resolve()
    if not cetta.is_file():
        parser.error(f"CeTTa executable does not exist: {cetta}")
    selected = args.workload or list(WORKLOADS)
    lanes = ["prime-no-he-applicability"]
    if args.compare_he_assisted:
        lanes.append("he-assisted-applicability")

    samples: dict[str, dict[str, list[dict[str, Any]]]] = {
        lane: {name: [] for name in selected} for lane in lanes
    }
    try:
        for sample_index in range(args.runs):
            lane_order = lanes if sample_index % 2 == 0 else list(reversed(lanes))
            for name in selected:
                source = (repo / WORKLOADS[name]).resolve()
                for lane in lane_order:
                    samples[lane][name].append(
                        run_sample(repo, cetta, source, lane, args.timeout)
                    )
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"FAIL: Prime MIL benchmark: {exc}", file=sys.stderr)
        return 1

    result: dict[str, Any] = {
        "schema": "PrimeMILBenchmarkV2",
        "cetta": str(cetta),
        "runs": args.runs,
        "lanes": {},
    }
    for lane in lanes:
        lane_result: dict[str, Any] = {
            "typed_applicability": (
                "disabled"
                if lane == "prime-no-he-applicability"
                else "he-assisted"
            ),
            "workloads": {},
        }
        for name in selected:
            source = (repo / WORKLOADS[name]).resolve()
            expected = source.with_suffix(".expected")
            workload_samples = samples[lane][name]
            first_accounting = workload_samples[0]["accounting"]
            first_routes = workload_samples[0]["checking_routes"]
            for sample in workload_samples[1:]:
                if sample["accounting"] != first_accounting:
                    print(
                        f"FAIL: {lane}/{name}: nondeterministic accounting",
                        file=sys.stderr,
                    )
                    return 1
                if sample["checking_routes"] != first_routes:
                    print(
                        f"FAIL: {lane}/{name}: nondeterministic checking route",
                        file=sys.stderr,
                    )
                    return 1
            if (args.require_prime_native and
                    first_routes["qualification"] != "prime-native-regular"):
                print(
                    f"FAIL: {lane}/{name}: requested Prime-native checking, "
                    f"observed {first_routes['qualification']} "
                    f"({first_routes['ambient_formation_checks']} ambient "
                    f"formation decisions, "
                    f"{first_routes['legacy_he_checks']} legacy HE checks, "
                    f"{first_routes['legacy_he_applicability_checks']} "
                    f"legacy HE applicability checks)",
                    file=sys.stderr,
                )
                return 1
            if (args.require_zero_legacy_he_applicability and
                    first_routes["legacy_he_applicability_checks"] != 0):
                print(
                    f"FAIL: {lane}/{name}: requested zero legacy HE "
                    f"applicability, observed "
                    f"{first_routes['legacy_he_applicability_checks']}",
                    file=sys.stderr,
                )
                return 1
            native_calculus = first_routes["native_calculus"]
            native_error = native_calculus_qualification_error(
                name,
                native_calculus,
                first_routes["interior_checks"],
            )
            if native_error is not None:
                print(f"FAIL: {lane}/{name}: {native_error}", file=sys.stderr)
                return 1
            lane_result["workloads"][name] = {
                "source": str(source.relative_to(repo)),
                "source_sha256": sha256_file(source),
                "expected_sha256": sha256_file(expected),
                "accounting": first_accounting,
                "checking_routes": first_routes,
                "samples": [
                    {
                        "wall_ns": sample["wall_ns"],
                        "peak_rss_kib": sample["peak_rss_kib"],
                    }
                    for sample in workload_samples
                ],
                "median_wall_ns": median(
                    [sample["wall_ns"] for sample in workload_samples]
                ),
                "median_peak_rss_kib": median(
                    [sample["peak_rss_kib"] for sample in workload_samples]
                ),
            }
        result["lanes"][lane] = lane_result

    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(rendered, encoding="utf-8")
    if args.quiet:
        workloads = ", ".join(selected)
        print(
            "PASS: Prime MIL candidate bags conserve every occurrence and "
            f"report checker provenance across {workloads}"
        )
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
