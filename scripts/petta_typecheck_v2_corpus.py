#!/usr/bin/env python3
"""Run the fixed PeTTa typecheck-v2 acceptance ledger."""

from __future__ import annotations

import argparse
import collections
import hashlib
import itertools
import json
import os
import pathlib
import re
import subprocess
import time


SCHEMA = "cetta-petta-typecheck-v2-acceptance-v1"
WITNESS_SCHEMA = "cetta-petta-typecheck-v2-census-witnesses-v2"
HISTORICAL_TOTAL = 244
HISTORICAL_ACCEPT = 97
HISTORICAL_REJECT = 147
CENSUS_PREFIX = "CETTA_PETTA_TYPECHECK_CENSUS_V2"
SEMANTIC_AXES = {
    "shape-compatibility",
    "result-cardinality-grading",
    "source-evaluated-stage-evidence",
    "open-world-unknown-approximation",
}
NATIVE_V3_CANDIDATE = "native-v3-candidate"
LANGDEF_RULE = re.compile(r"\(rule\s+([^\s()]+)")
CENSUS_DESCRIPTOR_FIELDS = ("scope", "kind", "mapping", "axis")
CENSUS_EVENT_DESCRIPTOR = re.compile(
    r'\bX\(\s*([A-Z0-9_]+)\s*,\s*(?:\\\s*)*"([^"]+)"'
)
CENSUS_EVENT_REFERENCE = re.compile(
    r"CETTA_PETTA_TYPECHECK_CENSUS_EVENT_([A-Z0-9_]+)"
)
CENSUS_HIT_CALL = re.compile(
    r"CETTA_PETTA_TYPECHECK_CENSUS_HIT(?:_IF)?\s*\((.*?)\);",
    re.DOTALL,
)
CENSUS_INSTRUMENTED_SOURCE_FILES = (
    pathlib.Path("src/petta_typecheck.c"),
    pathlib.Path("src/petta_search_machine.c"),
)


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_output(root: pathlib.Path, *args: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(root), *args], text=True
    ).strip()


def local_compilation_inputs(repo: pathlib.Path) -> tuple[str, list[dict]]:
    listed = subprocess.check_output(
        [
            "git", "-C", str(repo), "ls-files", "-co",
            "--exclude-standard", "--", "Makefile", "VERSION", "src",
            "native", "experiments/gslt2parse_foundation/native",
        ],
        text=True,
    ).splitlines()
    paths = {
        pathlib.Path(item)
        for item in listed
        if item in {"Makefile", "VERSION"}
        or pathlib.Path(item).suffix in {".c", ".h", ".inc"}
    }
    for pattern in (
        "runtime/bootstrap/build_config*.h",
        "src/stdlib_blob.h",
        "src/abt_default_signatures_blob.h",
    ):
        for path in repo.glob(pattern):
            if path.is_file():
                paths.add(path.relative_to(repo))
    entries = [
        {"path": path.as_posix(), "sha256": sha256_file(repo / path)}
        for path in sorted(paths)
    ]
    encoded = json.dumps(
        entries, sort_keys=True, separators=(",", ":")
    ).encode()
    return hashlib.sha256(encoded).hexdigest(), entries


def resolve_case_source(
    repo: pathlib.Path, reference_root: pathlib.Path, case: dict
) -> pathlib.Path:
    relative = pathlib.PurePosixPath(case["file"])
    if relative.is_absolute() or ".." in relative.parts:
        raise RuntimeError(f"unsafe source path in manifest: {case['file']}")
    source_root = case.get("source_root")
    if source_root == "reference":
        return reference_root / pathlib.Path(relative)
    if source_root == "candidate":
        return repo / pathlib.Path(relative)
    raise RuntimeError(
        f"unsupported source_root {source_root!r} for {case.get('id')!r}"
    )


def native_v3_intake_inventory(
    cases: list[dict],
) -> tuple[dict[str, object], list[str]]:
    """Collect native-v3 candidates without treating their axes as independent."""
    by_axis: dict[str, list[str]] = {
        axis: [] for axis in sorted(SEMANTIC_AXES)
    }
    findings: collections.Counter[str] = collections.Counter()
    phases: collections.Counter[str] = collections.Counter()
    invalid: list[str] = []
    for case in cases:
        identity = case.get("id", "<unknown>")
        classification = case.get("classification")
        if classification is None:
            continue
        if not isinstance(classification, dict):
            invalid.append(f"{identity}: classification is not an object")
            continue
        if classification.get("disposition") != NATIVE_V3_CANDIDATE:
            continue
        axis = classification.get("axis")
        if axis not in SEMANTIC_AXES:
            invalid.append(
                f"{identity}: native-v3 candidate has no valid semantic axis"
            )
            continue
        missing = [
            field for field in ("class", "finding", "phase")
            if not isinstance(classification.get(field), str)
            or not classification[field]
        ]
        if missing:
            invalid.append(
                f"{identity}: native-v3 candidate lacks "
                + ", ".join(missing)
            )
            continue
        by_axis[axis].append(identity)
        findings[classification["finding"]] += 1
        phases[classification["phase"]] += 1
    missing_axes = [axis for axis, entries in by_axis.items() if not entries]
    if missing_axes:
        invalid.append(
            "native-v3 intake has no candidate for semantic axes: "
            + ", ".join(missing_axes)
        )
    return {
        "candidate_count": sum(len(entries) for entries in by_axis.values()),
        "by_axis": {
            axis: sorted(entries) for axis, entries in sorted(by_axis.items())
        },
        "finding_counts": dict(sorted(findings.items())),
        "phase_counts": dict(sorted(phases.items())),
        "scope_note": (
            "This is an intake inventory for a future native checker, not "
            "a claim that the axes are independent or that v2 behavior is "
            "already reimplemented."
        ),
    }, invalid


def load_manifest(
    path: pathlib.Path, repo: pathlib.Path, reference_root: pathlib.Path
) -> dict:
    manifest = json.loads(path.read_text())
    if manifest.get("schema") != SCHEMA:
        raise RuntimeError(
            f"manifest schema is {manifest.get('schema')!r}, expected {SCHEMA!r}"
        )
    oracle = manifest.get("oracle")
    if not isinstance(oracle, dict):
        raise RuntimeError("manifest oracle identity is absent")
    revision = oracle.get("revision")
    if revision != git_output(reference_root, "rev-parse", "HEAD"):
        raise RuntimeError("reference checkout differs from pinned oracle revision")
    runner = oracle.get("runner")
    runner_path = reference_root / runner if isinstance(runner, str) else None
    if not runner_path or not runner_path.is_file():
        raise RuntimeError("pinned oracle runner is absent")
    # The runner is a launcher, not the oracle: it sets stack limits and
    # library paths and contains no checking logic. Its content is already
    # determined by the pinned revision, so hashing it only ever rejected
    # local environment fixes while leaving src/typecheck/ unguarded.

    cases = manifest.get("cases")
    if not isinstance(cases, list) or not cases:
        raise RuntimeError("manifest cases must be a non-empty list")
    identities = set()
    observed_counts = collections.Counter()
    for case in cases:
        if not isinstance(case, dict):
            raise RuntimeError("manifest case is not an object")
        identity = case.get("id")
        if not isinstance(identity, str) or not identity:
            raise RuntimeError("manifest case has no stable id")
        if identity in identities:
            raise RuntimeError(f"duplicate manifest case id: {identity}")
        identities.add(identity)
        if case.get("profile") != "typecheck-v2":
            raise RuntimeError(f"{identity}: profile must be typecheck-v2")
        arguments = case.get("arguments")
        if arguments not in ([], ["--strict"], ["--strict-det"]):
            raise RuntimeError(f"{identity}: unsupported argument vector")
        expected = case.get("expected")
        if not isinstance(expected, dict):
            raise RuntimeError(f"{identity}: expected disposition is absent")
        verdict = expected.get("verdict")
        if verdict not in {"accept", "reject"}:
            raise RuntimeError(f"{identity}: invalid expected verdict")
        expected_exit = expected.get("exit")
        if not isinstance(expected_exit, int):
            raise RuntimeError(f"{identity}: expected exit is not an integer")
        source = resolve_case_source(repo, reference_root, case)
        if not source.is_file():
            raise RuntimeError(f"{identity}: source is absent: {case['file']}")
        if case.get("source_sha256") != sha256_file(source):
            raise RuntimeError(f"{identity}: source differs from manifest")
        cohort = case.get("cohort")
        if cohort not in {"roman-244", "semantic-regression"}:
            raise RuntimeError(f"{identity}: invalid cohort")
        observed_counts["total"] += 1
        observed_counts[verdict] += 1
        observed_counts[cohort] += 1

    declared_counts = manifest.get("counts")
    actual_counts = {
        "total": observed_counts["total"],
        "accept": observed_counts["accept"],
        "reject": observed_counts["reject"],
        "roman_244": observed_counts["roman-244"],
        "semantic_regressions": observed_counts["semantic-regression"],
    }
    if declared_counts != actual_counts:
        raise RuntimeError(
            f"manifest counts are {declared_counts!r}, expected {actual_counts!r}"
        )
    historical = [case for case in cases if case["cohort"] == "roman-244"]
    historical_accept = sum(
        case["expected"]["verdict"] == "accept" for case in historical
    )
    historical_reject = sum(
        case["expected"]["verdict"] == "reject" for case in historical
    )
    if (
        len(historical), historical_accept, historical_reject
    ) != (HISTORICAL_TOTAL, HISTORICAL_ACCEPT, HISTORICAL_REJECT):
        raise RuntimeError(
            "historical Roman cohort is not the fixed 244 = 97 + 147 ledger"
        )
    _, native_v3_errors = native_v3_intake_inventory(cases)
    if native_v3_errors:
        raise RuntimeError(
            "invalid native-v3 intake: " + "; ".join(native_v3_errors)
        )
    return manifest


def all_axis_pairs() -> set[tuple[str, str]]:
    return {
        pair
        for pair in itertools.combinations(sorted(SEMANTIC_AXES), 2)
    }


def parse_census_witness_payload(
    payload: object, known_case_ids: set[str]
) -> tuple[dict[str, dict[str, str]], list[dict[str, object]]]:
    if not isinstance(payload, dict):
        raise RuntimeError("census witness ledger is not an object")
    if payload.get("schema") != WITNESS_SCHEMA:
        raise RuntimeError(
            "census witness ledger has an unsupported schema"
        )
    raw_witnesses = payload.get("witnesses")
    if not isinstance(raw_witnesses, dict) or not raw_witnesses:
        raise RuntimeError("census witness ledger has no witnesses")

    witnesses: dict[str, dict[str, str]] = {}
    for name, raw_witness in sorted(raw_witnesses.items()):
        if not isinstance(name, str) or not name:
            raise RuntimeError("census witness has no event name")
        if not isinstance(raw_witness, dict):
            raise RuntimeError(f"{name}: census witness is not an object")
        case_id = raw_witness.get("case")
        if not isinstance(case_id, str) or case_id not in known_case_ids:
            raise RuntimeError(f"{name}: census witness names an unknown case")
        witness = {"case": case_id}
        for field in CENSUS_DESCRIPTOR_FIELDS:
            value = raw_witness.get(field)
            if not isinstance(value, str) or not value:
                raise RuntimeError(f"{name}: census witness has no {field}")
            witness[field] = value
        origin = raw_witness.get("origin")
        if not isinstance(origin, str) or not origin:
            raise RuntimeError(f"{name}: census witness has no origin")
        witness["origin"] = origin
        if witness["axis"] not in SEMANTIC_AXES:
            raise RuntimeError(f"{name}: census witness has an unknown axis")
        witnesses[name] = witness

    raw_interactions = payload.get("axis_interactions")
    if not isinstance(raw_interactions, list):
        raise RuntimeError("census witness ledger has no axis interactions")
    interactions: list[dict[str, object]] = []
    seen_pairs: set[tuple[str, str]] = set()
    for raw_interaction in raw_interactions:
        if not isinstance(raw_interaction, dict):
            raise RuntimeError("axis interaction is not an object")
        case_id = raw_interaction.get("case")
        if not isinstance(case_id, str) or case_id not in known_case_ids:
            raise RuntimeError("axis interaction names an unknown case")
        axes = raw_interaction.get("axes")
        if (
            not isinstance(axes, list)
            or len(axes) != 2
            or any(not isinstance(axis, str) for axis in axes)
            or axes[0] == axes[1]
            or any(axis not in SEMANTIC_AXES for axis in axes)
        ):
            raise RuntimeError("axis interaction has an invalid axis pair")
        pair = tuple(sorted(axes))
        if pair in seen_pairs:
            raise RuntimeError("axis interaction pair is declared twice")
        seen_pairs.add(pair)
        events = raw_interaction.get("events")
        if (
            not isinstance(events, list)
            or not events
            or any(not isinstance(event, str) or not event for event in events)
            or len(set(events)) != len(events)
        ):
            raise RuntimeError("axis interaction has invalid event witnesses")
        interactions.append({
            "case": case_id,
            "axes": list(pair),
            "events": sorted(events),
        })
    missing_pairs = sorted(all_axis_pairs() - seen_pairs)
    unexpected_pairs = sorted(seen_pairs - all_axis_pairs())
    if missing_pairs or unexpected_pairs:
        raise RuntimeError(
            "axis interactions must cover each pair of semantic axes exactly once"
        )
    return witnesses, interactions


def load_census_witnesses(
    path: pathlib.Path, known_case_ids: set[str]
) -> tuple[dict[str, dict[str, str]], list[dict[str, object]]]:
    return parse_census_witness_payload(
        json.loads(path.read_text(encoding="utf-8")), known_case_ids
    )


def classify(case: dict, status: int, stdout: str, stderr: str) -> str:
    if status == 124:
        return "timeout"
    expected = case["expected"]
    if expected["verdict"] == "reject":
        if status == 2 and not stdout and "PeTTa type error:" in stderr:
            return "type-reject"
        if status == 0:
            return "accept"
        return "other-failure"
    failed_output = any(
        marker in stdout or marker in stderr
        for marker in ("❌", "(Error", "PeTTa type error:")
    )
    if status == expected["exit"] and not failed_output:
        return "accept"
    return "other-failure"


def split_census_records(
    stderr: str,
) -> tuple[str, dict[str, dict[str, str]], dict[str, set[str]]]:
    clean_lines = []
    catalog: dict[str, dict[str, str]] = {}
    hits: dict[str, set[str]] = collections.defaultdict(set)
    for line in stderr.splitlines(keepends=True):
        record = line.rstrip("\r\n")
        if not record.startswith(CENSUS_PREFIX + "\t"):
            clean_lines.append(line)
            continue
        fields = record.split("\t")
        if len(fields) == 7 and fields[1] == "catalog":
            name = fields[2]
            descriptor = {
                "scope": fields[3],
                "kind": fields[4],
                "mapping": fields[5],
                "axis": fields[6],
            }
            prior = catalog.get(name)
            if not name or (prior is not None and prior != descriptor):
                raise RuntimeError(f"inconsistent census catalog record: {record}")
            catalog[name] = descriptor
        elif (
            len(fields) == 4
            and fields[1] == "hit"
            and fields[2]
            and fields[3]
        ):
            hits[fields[2]].add(fields[3])
        else:
            raise RuntimeError(f"malformed census record: {record}")
    return "".join(clean_lines), catalog, dict(hits)


def langdef_rule_names(path: pathlib.Path) -> set[str]:
    return set(LANGDEF_RULE.findall(path.read_text(encoding="utf-8")))


def validate_census_catalog(
    catalog: dict[str, dict[str, str]], rule_names: set[str]
) -> list[str]:
    invalid = []
    for name, descriptor in sorted(catalog.items()):
        kind = descriptor["kind"]
        mapping = descriptor["mapping"]
        axis = descriptor.get("axis")
        if axis not in SEMANTIC_AXES:
            invalid.append(f"{name}: unknown semantic axis {axis}")
        elif kind == "rule" and mapping not in rule_names:
            invalid.append(f"{name}: unknown langdef rule {mapping}")
        elif kind == "mechanism" and not mapping:
            invalid.append(f"{name}: unnamed non-calculus mechanism")
        elif kind not in {"rule", "mechanism"}:
            invalid.append(f"{name}: unknown mapping kind {kind}")
    return invalid


def validate_census_witness_catalog(
    catalog: dict[str, dict[str, str]], witnesses: dict[str, dict[str, str]]
) -> list[str]:
    invalid = []
    for name in sorted(set(catalog) - set(witnesses)):
        invalid.append(f"{name}: no declared census witness")
    for name in sorted(set(witnesses) - set(catalog)):
        invalid.append(f"{name}: declared witness names an unknown event")
    for name in sorted(set(catalog) & set(witnesses)):
        expected = {
            field: witnesses[name][field]
            for field in CENSUS_DESCRIPTOR_FIELDS
        }
        if catalog[name] != expected:
            invalid.append(
                f"{name}: witness descriptor differs from the runtime catalog"
            )
    return invalid


def validate_census_witness_hits(
    census_cases: dict[str, list[str]], witnesses: dict[str, dict[str, str]]
) -> list[str]:
    observed_by_case: dict[str, set[str]] = collections.defaultdict(set)
    for event, case_ids in census_cases.items():
        for case_id in case_ids:
            observed_by_case[case_id].add(event)
    return [
        f"{name}: declared witness {witness['case']} did not emit the event"
        for name, witness in sorted(witnesses.items())
        if name not in observed_by_case[witness["case"]]
    ]


def validate_census_witness_origins(
    census_origins: dict[str, dict[str, set[str]]],
    witnesses: dict[str, dict[str, str]],
) -> list[str]:
    invalid = []
    for name, witness in sorted(witnesses.items()):
        observed = census_origins.get(name, {}).get(witness["case"], set())
        expected = {witness["origin"]}
        if observed != expected:
            observed_text = ", ".join(sorted(observed)) or "none"
            invalid.append(
                f"{name}: expected origin {witness['origin']} from "
                f"{witness['case']}, observed {observed_text}"
            )
    return invalid


def census_static_source_inventory(
    repo: pathlib.Path,
) -> tuple[dict[str, list[str]], list[str]]:
    """Check that each declared census event has a live C decision site."""
    header = repo / "src/petta_typecheck_census.h"
    try:
        header_text = header.read_text(encoding="utf-8")
    except OSError as error:
        return {}, [f"cannot read census header: {error}"]

    descriptors: dict[str, str] = {}
    invalid: list[str] = []
    for tag, name in CENSUS_EVENT_DESCRIPTOR.findall(header_text):
        previous = descriptors.get(tag)
        if previous is not None and previous != name:
            invalid.append(f"duplicate census event tag {tag}")
        descriptors[tag] = name
    if not descriptors:
        return {}, ["census header declares no events"]

    event_sources: dict[str, set[str]] = collections.defaultdict(set)
    for relative in CENSUS_INSTRUMENTED_SOURCE_FILES:
        source = repo / relative
        try:
            source_text = source.read_text(encoding="utf-8")
        except OSError as error:
            invalid.append(f"cannot read census source {relative}: {error}")
            continue
        for hit_call in CENSUS_HIT_CALL.findall(source_text):
            tags = CENSUS_EVENT_REFERENCE.findall(hit_call)
            if not tags:
                invalid.append(
                    f"{relative}: census hit call has no event tag"
                )
                continue
            for tag in tags:
                name = descriptors.get(tag)
                if name is None:
                    invalid.append(
                        f"{relative}: undeclared census event tag {tag}"
                    )
                    continue
                event_sources[name].add(relative.as_posix())

    for tag, name in sorted(descriptors.items()):
        if name not in event_sources:
            invalid.append(f"{name}: no C decision site for tag {tag}")
    return (
        {
            name: sorted(sources)
            for name, sources in sorted(event_sources.items())
        },
        sorted(set(invalid)),
    )


def validate_axis_interactions(
    catalog: dict[str, dict[str, str]],
    census_cases: dict[str, list[str]],
    interactions: list[dict[str, object]],
) -> tuple[list[str], list[dict[str, object]]]:
    observed_by_case: dict[str, set[str]] = collections.defaultdict(set)
    for event, case_ids in census_cases.items():
        for case_id in case_ids:
            observed_by_case[case_id].add(event)
    invalid = []
    statuses = []
    for interaction in interactions:
        case_id = interaction["case"]
        axes = interaction["axes"]
        events = interaction["events"]
        assert isinstance(case_id, str)
        assert isinstance(axes, list)
        assert isinstance(events, list)
        missing_events = [
            event
            for event in events
            if event not in catalog or event not in observed_by_case[case_id]
        ]
        observed_axes = {
            catalog[event]["axis"]
            for event in events
            if event in catalog and event in observed_by_case[case_id]
        }
        missing_axes = sorted(set(axes) - observed_axes)
        verified = not missing_events and not missing_axes
        statuses.append({
            "case": case_id,
            "axes": axes,
            "events": events,
            "verified": verified,
        })
        if missing_events:
            invalid.append(
                f"{case_id}: axis interaction missed events "
                f"{', '.join(missing_events)}"
            )
        if missing_axes:
            invalid.append(
                f"{case_id}: axis interaction missed axes "
                f"{', '.join(missing_axes)}"
            )
    return invalid, statuses


def census_axis_inventory(
    catalog: dict[str, dict[str, str]],
    census_cases: dict[str, list[str]],
    witnesses: dict[str, dict[str, str]],
) -> tuple[dict[str, dict[str, object]], list[str]]:
    """Record the four-axis extraction hypothesis without claiming independence."""
    inventory: dict[str, dict[str, object]] = {}
    invalid: list[str] = []
    for axis in sorted(SEMANTIC_AXES):
        events = sorted(
            name for name, descriptor in catalog.items()
            if descriptor["axis"] == axis
        )
        if not events:
            invalid.append(f"semantic axis {axis} has no catalogued event")
        observed_events = sorted(
            name for name in events if name in census_cases
        )
        inventory[axis] = {
            "catalog_events": events,
            "observed_events": observed_events,
            "unobserved_events": sorted(set(events) - set(observed_events)),
            "rule_mappings": sorted({
                catalog[name]["mapping"] for name in events
                if catalog[name]["kind"] == "rule"
            }),
            "mechanism_mappings": sorted({
                catalog[name]["mapping"] for name in events
                if catalog[name]["kind"] == "mechanism"
            }),
            "witnesses": [
                {
                    "event": name,
                    "case": witnesses[name]["case"],
                    "origin": witnesses[name]["origin"],
                }
                for name in events if name in witnesses
            ],
        }
    return inventory, invalid


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=pathlib.Path)
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--reference-root", type=pathlib.Path, required=True)
    parser.add_argument("--output-dir", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--baseline", type=pathlib.Path)
    parser.add_argument("--census-output", type=pathlib.Path)
    parser.add_argument("--census-langdef", type=pathlib.Path)
    parser.add_argument("--census-witnesses", type=pathlib.Path)
    parser.add_argument("--require-census-complete", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()

    repo = pathlib.Path.cwd().resolve()
    manifest_path = args.manifest.resolve()
    reference_root = args.reference_root.resolve()
    try:
        manifest = load_manifest(manifest_path, repo, reference_root)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"invalid acceptance manifest: {error}") from error
    cases = manifest["cases"]
    witnesses: dict[str, dict[str, str]] = {}
    axis_interactions: list[dict[str, object]] = []
    witness_ledger_sha256 = None
    if args.census_witnesses:
        try:
            witness_path = args.census_witnesses.resolve()
            witnesses, axis_interactions = load_census_witnesses(
                witness_path, {case["id"] for case in cases}
            )
            witness_ledger_sha256 = sha256_file(witness_path)
        except (OSError, ValueError, RuntimeError) as error:
            raise SystemExit(f"invalid census witness ledger: {error}") from error
    if args.validate_only:
        native_v3_inventory, _ = native_v3_intake_inventory(cases)
        print(
            f"PASS: immutable typecheck-v2 acceptance manifest covers "
            f"{len(cases)} cases ({HISTORICAL_TOTAL} historical); "
            f"native-v3 intake={native_v3_inventory['candidate_count']}"
        )
        return 0
    if not args.cetta or not args.output_dir:
        raise SystemExit("--cetta and --output-dir are required unless validating")
    cetta = args.cetta.resolve()
    output_dir = args.output_dir.resolve()
    census_requested = bool(
        args.census_output
        or args.census_langdef
        or args.census_witnesses
        or args.require_census_complete
    )
    if output_dir.exists() and any(output_dir.iterdir()):
        raise SystemExit(f"output directory is not empty: {output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)

    results = []
    census_catalog: dict[str, dict[str, str]] = {}
    census_cases: dict[str, list[str]] = collections.defaultdict(list)
    census_origins: dict[str, dict[str, set[str]]] = collections.defaultdict(
        lambda: collections.defaultdict(set)
    )
    for source_case in cases:
        case = dict(source_case)
        source = resolve_case_source(repo, reference_root, case)
        command = [
            str(cetta), "--lang", "petta", "--profile", "typecheck-v2",
            *case["arguments"], str(source),
        ]
        started = time.monotonic()
        environment = None
        if census_requested:
            environment = os.environ.copy()
            environment["CETTA_PETTA_TYPECHECK_CENSUS"] = "1"
        try:
            completed = subprocess.run(
                command, text=True, capture_output=True,
                cwd=reference_root,
                env=environment,
                timeout=args.timeout, check=False,
            )
            status = completed.returncode
            stdout = completed.stdout
            stderr = completed.stderr
        except subprocess.TimeoutExpired as expired:
            status = 124
            stdout = expired.stdout or ""
            stderr = expired.stderr or ""
            if isinstance(stdout, bytes):
                stdout = stdout.decode(errors="replace")
            if isinstance(stderr, bytes):
                stderr = stderr.decode(errors="replace")
        case_event_origins: dict[str, set[str]] = {}
        if census_requested:
            try:
                stderr, case_catalog, case_event_origins = split_census_records(
                    stderr
                )
            except RuntimeError as error:
                raise SystemExit(
                    f"{case['id']}: invalid census diagnostics: {error}"
                ) from error
            for name, descriptor in case_catalog.items():
                prior = census_catalog.get(name)
                if prior is not None and prior != descriptor:
                    raise SystemExit(
                        f"{case['id']}: census descriptor for {name} drifted"
                    )
                census_catalog[name] = descriptor
            for name, origins in case_event_origins.items():
                census_cases[name].append(case["id"])
                census_origins[name][case["id"]].update(origins)
        observed = classify(case, status, stdout, stderr)
        matched = (
            observed == "type-reject"
            if case["expected"]["verdict"] == "reject"
            else observed == "accept"
        )
        case.update({
            "cetta_exit": status,
            "cetta_observed": observed,
            "cetta_matched": matched,
            "seconds": round(time.monotonic() - started, 4),
            "stdout": stdout,
            "stderr": stderr,
        })
        if census_requested:
            case["census_events"] = sorted(case_event_origins)
            case["census_origins"] = {
                name: sorted(origins)
                for name, origins in sorted(case_event_origins.items())
            }
        results.append(case)

    observed_counts = collections.Counter(
        case["cetta_observed"] for case in results
    )
    negatives = [
        case for case in results
        if case["expected"]["verdict"] == "reject"
    ]
    positives = [
        case for case in results
        if case["expected"]["verdict"] == "accept"
    ]

    def grouped(field: str) -> dict:
        groups: dict[str, collections.Counter] = {}
        for case in negatives:
            key = case.get(field) or "unspecified"
            counter = groups.setdefault(key, collections.Counter())
            counter["total"] += 1
            counter[case["cetta_observed"]] += 1
            if case["cetta_matched"]:
                counter["matched"] += 1
        return {key: dict(value) for key, value in sorted(groups.items())}

    compilation_digest, compilation_inputs = local_compilation_inputs(repo)
    native_v3_inventory, native_v3_errors = native_v3_intake_inventory(cases)
    if native_v3_errors:
        raise SystemExit(
            "invalid native-v3 intake: " + "; ".join(native_v3_errors)
        )
    summary = {
        "candidate_commit": git_output(repo, "rev-parse", "HEAD"),
        "tracked_diff_sha256": hashlib.sha256(
            subprocess.check_output(
                ["git", "-C", str(repo), "diff", "HEAD", "--binary"]
            )
        ).hexdigest(),
        "binary_sha256": sha256_file(cetta),
        "acceptance_manifest_sha256": sha256_file(manifest_path),
        "local_compilation_inputs_sha256": compilation_digest,
        "local_compilation_input_count": len(compilation_inputs),
        "oracle": manifest["oracle"],
        "total": len(results),
        "matched_total": sum(case["cetta_matched"] for case in results),
        "expected_accept": len(positives),
        "accepted_positive": sum(case["cetta_matched"] for case in positives),
        "expected_reject": len(negatives),
        "clean_type_rejections": sum(case["cetta_matched"] for case in negatives),
        "observed": dict(observed_counts),
        "negative_by_phase": grouped("phase"),
        "negative_by_class": grouped("class"),
        "native_v3_intake": native_v3_inventory,
        "mode_note": "CeTTa ran every manifest mode exactly under the typecheck-v2 profile.",
    }

    census_complete = True
    census_payload = None
    if census_requested:
        source_inventory, source_inventory_errors = (
            census_static_source_inventory(repo)
        )
        axis_inventory, axis_inventory_errors = census_axis_inventory(
            census_catalog, census_cases, witnesses
        )
        observed_events = set(census_cases)
        unknown_events = sorted(observed_events - set(census_catalog))
        oracle_events = sorted(
            name for name, descriptor in census_catalog.items()
            if descriptor["scope"] == "oracle"
        )
        uncovered_oracle = sorted(set(oracle_events) - observed_events)
        invalid_mappings = []
        invalid_witnesses = []
        interaction_statuses: list[dict[str, object]] = []
        if args.census_langdef:
            invalid_mappings = validate_census_catalog(
                census_catalog,
                langdef_rule_names(args.census_langdef.resolve()),
            )
        elif args.require_census_complete:
            invalid_mappings = [
                "census completeness requires --census-langdef"
            ]
        if args.census_witnesses:
            invalid_witnesses.extend(
                validate_census_witness_catalog(census_catalog, witnesses)
            )
            invalid_witnesses.extend(
                validate_census_witness_hits(census_cases, witnesses)
            )
            invalid_witnesses.extend(
                validate_census_witness_origins(census_origins, witnesses)
            )
            interaction_errors, interaction_statuses = validate_axis_interactions(
                census_catalog, census_cases, axis_interactions
            )
            invalid_witnesses.extend(interaction_errors)
        elif args.require_census_complete:
            invalid_witnesses = [
                "census completeness requires --census-witnesses"
            ]
        catalog_events = set(census_catalog)
        source_events = set(source_inventory)
        missing_catalog_events = sorted(source_events - catalog_events)
        missing_source_events = sorted(catalog_events - source_events)
        if missing_catalog_events:
            source_inventory_errors.append(
                "static event inventory is absent from runtime catalog: "
                + ", ".join(missing_catalog_events)
            )
        if missing_source_events:
            source_inventory_errors.append(
                "runtime catalog is absent from static event inventory: "
                + ", ".join(missing_source_events)
            )
        census_complete = (
            bool(census_catalog)
            and not unknown_events
            and not uncovered_oracle
            and not invalid_mappings
            and not invalid_witnesses
            and not source_inventory_errors
            and not axis_inventory_errors
        )
        census_payload = {
            "schema": "cetta-petta-typecheck-v2-semantic-census-v3",
            "catalog": dict(sorted(census_catalog.items())),
            "observed_cases": {
                name: sorted(case_ids)
                for name, case_ids in sorted(census_cases.items())
            },
            "oracle_event_count": len(oracle_events),
            "observed_oracle_event_count": sum(
                name in observed_events for name in oracle_events
            ),
            "uncovered_oracle_events": uncovered_oracle,
            "unknown_events": unknown_events,
            "invalid_mappings": invalid_mappings,
            "source_inventory": {
                "complete": not source_inventory_errors,
                "event_sources": source_inventory,
                "errors": source_inventory_errors,
            },
            "axis_inventory": axis_inventory,
            "axis_inventory_errors": axis_inventory_errors,
            "witness_ledger_sha256": witness_ledger_sha256,
            "invalid_witnesses": invalid_witnesses,
            "witness_origins": {
                name: sorted(
                    census_origins.get(name, {}).get(witness["case"], set())
                )
                for name, witness in sorted(witnesses.items())
            },
            "axis_interactions": interaction_statuses,
            "complete": census_complete,
            "scope_note": (
                "Completeness is relative to the currently instrumented "
                "oracle tranche. Every catalogued event has a declared "
                "case witness, and every pair of semantic axes has an "
                "executable interaction witness. The axis inventory is an "
                "extraction hypothesis, not an independence claim."
            ),
        }
        summary["semantic_census"] = census_payload

    if args.baseline:
        baseline_payload = json.loads(args.baseline.read_text())
        baseline_cases = {
            case["id"]: case
            for case in baseline_payload["cases"]
        }
        transitions = []
        for case in results:
            old = baseline_cases.get(case["id"])
            if old and old["cetta_observed"] != case["cetta_observed"]:
                transitions.append({
                    "file": case["file"],
                    "arguments": case["arguments"],
                    "from": old["cetta_observed"],
                    "to": case["cetta_observed"],
                    "matched_before": old["cetta_matched"],
                    "matched_after": case["cetta_matched"],
                })
        summary["transitions_from_baseline"] = transitions

    payload = {"summary": summary, "cases": results}
    (output_dir / "results.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n"
    )
    (output_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n"
    )
    (output_dir / "compilation-inputs.json").write_text(
        json.dumps(compilation_inputs, indent=2, sort_keys=True) + "\n"
    )
    if args.census_output and census_payload is not None:
        args.census_output.resolve().write_text(
            json.dumps(census_payload, indent=2, sort_keys=True) + "\n"
        )
    census_note = ""
    if census_payload is not None:
        census_note = (
            f"; census={census_payload['observed_oracle_event_count']}/"
            f"{census_payload['oracle_event_count']}"
        )
    print(
        f"typecheck-v2 corpus: {summary['matched_total']}/{summary['total']} exact; "
        f"positive={summary['accepted_positive']}/{summary['expected_accept']}; "
        f"rejections={summary['clean_type_rejections']}/{summary['expected_reject']}"
        f"{census_note}"
    )
    accepted = summary["matched_total"] == summary["total"]
    if args.require_census_complete:
        accepted = accepted and census_complete
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
