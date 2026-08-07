#!/usr/bin/env python3
"""Run the fixed PeTTa typecheck-v2 acceptance ledger."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
import subprocess
import time


SCHEMA = "cetta-petta-typecheck-v2-acceptance-v1"
HISTORICAL_TOTAL = 244
HISTORICAL_ACCEPT = 97
HISTORICAL_REJECT = 147


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
    if oracle.get("runner_sha256") != sha256_file(runner_path):
        raise RuntimeError("pinned oracle runner differs from manifest")

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
    return manifest


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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=pathlib.Path)
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--reference-root", type=pathlib.Path, required=True)
    parser.add_argument("--output-dir", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--baseline", type=pathlib.Path)
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
    if args.validate_only:
        print(
            f"PASS: immutable typecheck-v2 acceptance manifest covers "
            f"{len(cases)} cases ({HISTORICAL_TOTAL} historical)"
        )
        return 0
    if not args.cetta or not args.output_dir:
        raise SystemExit("--cetta and --output-dir are required unless validating")
    cetta = args.cetta.resolve()
    output_dir = args.output_dir.resolve()
    if output_dir.exists() and any(output_dir.iterdir()):
        raise SystemExit(f"output directory is not empty: {output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)

    results = []
    for source_case in cases:
        case = dict(source_case)
        source = resolve_case_source(repo, reference_root, case)
        command = [
            str(cetta), "--lang", "petta", "--profile", "typecheck-v2",
            *case["arguments"], str(source),
        ]
        started = time.monotonic()
        try:
            completed = subprocess.run(
                command, text=True, capture_output=True,
                cwd=reference_root,
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
        "mode_note": "CeTTa ran every manifest mode exactly under the typecheck-v2 profile.",
    }

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
    print(
        f"typecheck-v2 corpus: {summary['matched_total']}/{summary['total']} exact; "
        f"positive={summary['accepted_positive']}/{summary['expected_accept']}; "
        f"rejections={summary['clean_type_rejections']}/{summary['expected_reject']}"
    )
    return 0 if summary["matched_total"] == summary["total"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
