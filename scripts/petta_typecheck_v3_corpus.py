#!/usr/bin/env python3
"""Run and classify the complete typecheck-v3 differential ledger."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import pathlib
import subprocess
import time


MANIFEST_SCHEMA = "cetta-petta-typecheck-v2-acceptance-v1"
H5_SCHEMA = "cetta-petta-typecheck-v3-h5-matrix-v1"
RESULT_SCHEMA = "cetta-petta-typecheck-v3-corpus-results-v2"
SUMMARY_SCHEMA = "cetta-petta-typecheck-v3-corpus-summary-v2"
RUNNER_PREFIX = "PettaTypecheckV3FileV1"


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_head(root: pathlib.Path) -> str:
    return subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()


def source_path(
    repo: pathlib.Path, reference_root: pathlib.Path, case: dict
) -> pathlib.Path:
    relative = pathlib.PurePosixPath(case["file"])
    if relative.is_absolute() or ".." in relative.parts:
        raise RuntimeError(f"unsafe case path: {case['file']}")
    if case.get("source_root") == "candidate":
        return repo / pathlib.Path(relative)
    if case.get("source_root") == "reference":
        return reference_root / pathlib.Path(relative)
    raise RuntimeError(f"{case.get('id')}: unsupported source root")


def policy_name(case: dict) -> str:
    arguments = case.get("arguments")
    if arguments == []:
        return "default"
    if arguments == ["--strict"]:
        return "strict"
    if arguments == ["--strict-det"]:
        return "strict-det"
    raise RuntimeError(f"{case.get('id')}: unsupported policy arguments")


def policy_verdict(outcome: str, policy: str) -> str:
    if outcome == "refuted":
        return "reject"
    if outcome == "established":
        return "accept"
    if outcome == "undetermined":
        return "accept" if policy == "default" else "reject"
    return outcome


def decode_field(text: str) -> str:
    result = []
    index = 0
    while index < len(text):
        if text[index] != "\\" or index + 1 >= len(text):
            result.append(text[index])
            index += 1
            continue
        escaped = text[index + 1]
        result.append({"t": "\t", "n": "\n", "r": "\r", "\\": "\\"}.get(
            escaped, escaped
        ))
        index += 2
    return "".join(result)


def parse_runner_output(stdout: str) -> dict:
    lines = [line for line in stdout.splitlines() if line]
    if len(lines) != 1:
        raise RuntimeError("v3 file runner did not emit exactly one record")
    fields = lines[0].split("\t")
    if len(fields) != 11 or fields[0] != RUNNER_PREFIX:
        raise RuntimeError("v3 file runner emitted a malformed record")
    return {
        "outcome": fields[1],
        "boundary": fields[2],
        "relation": decode_field(fields[3]),
        "subject": decode_field(fields[4]),
        "declarations_seen": int(fields[5]),
        "equations_checked": int(fields[6]),
        "established_equations": int(fields[7]),
        "undetermined_equations": int(fields[8]),
        "incomplete_equations": int(fields[9]),
        "diagnostic": decode_field(fields[10]),
    }


def classify(
    case: dict, observed: dict, h5: dict | None
) -> tuple[str, object, str]:
    policy = policy_name(case)
    v3_verdict = policy_verdict(observed["outcome"], policy)
    v2_verdict = case["expected"]["verdict"]
    if v3_verdict in {"incomplete", "fault"}:
        return "unclassified", None, v2_verdict
    if v3_verdict == v2_verdict:
        return "native-agreement", None, v3_verdict
    if h5 and h5.get("diverges_from_v2") and (
        h5.get("v3_policy_verdict") == v3_verdict
    ):
        migration = h5.get("migration")
        if h5.get("finding") == "implementation-defect" or migration:
            return "named-refinement", migration, v2_verdict
    return "legacy-v2", None, v2_verdict


def load_inputs(
    manifest_path: pathlib.Path,
    h5_path: pathlib.Path,
    repo: pathlib.Path,
    reference_root: pathlib.Path,
) -> tuple[dict, dict[str, dict]]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != MANIFEST_SCHEMA:
        raise RuntimeError("unsupported acceptance manifest schema")
    oracle = manifest.get("oracle")
    if not isinstance(oracle, dict) or oracle.get("revision") != git_head(
        reference_root
    ):
        raise RuntimeError("compatibility reference is not at the manifest revision")
    cases = manifest.get("cases")
    if not isinstance(cases, list) or len(cases) != 389:
        raise RuntimeError("acceptance manifest is not the 389-case ledger")
    for case in cases:
        source = source_path(repo, reference_root, case)
        if not source.is_file() or sha256_file(source) != case.get("source_sha256"):
            raise RuntimeError(f"{case.get('id')}: source digest does not match")
        policy_name(case)

    h5_payload = json.loads(h5_path.read_text(encoding="utf-8"))
    if h5_payload.get("schema") != H5_SCHEMA:
        raise RuntimeError("unsupported H5 matrix schema")
    h5_rows = h5_payload.get("candidates")
    if not isinstance(h5_rows, list) or len(h5_rows) != 51:
        raise RuntimeError("H5 matrix is not the 51-candidate ledger")
    h5 = {row["id"]: row for row in h5_rows}
    return manifest, h5


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=pathlib.Path, required=True)
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--h5-matrix", type=pathlib.Path, required=True)
    parser.add_argument("--reference-root", type=pathlib.Path, required=True)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--require-resolved", action="store_true")
    args = parser.parse_args()

    repo = pathlib.Path.cwd().resolve()
    runner = args.runner.resolve()
    manifest_path = args.manifest.resolve()
    h5_path = args.h5_matrix.resolve()
    reference_root = args.reference_root.resolve()
    output_dir = args.output_dir.resolve()
    if not runner.is_file():
        raise SystemExit(f"v3 file runner is absent: {runner}")
    if output_dir.exists() and any(output_dir.iterdir()):
        raise SystemExit(f"output directory is not empty: {output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        manifest, h5 = load_inputs(
            manifest_path, h5_path, repo, reference_root
        )
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"invalid v3 differential inputs: {error}") from error

    results = []
    category_counts: collections.Counter[str] = collections.Counter()
    outcome_counts: collections.Counter[str] = collections.Counter()
    policy_verdict_counts: collections.Counter[str] = collections.Counter()
    product_verdict_counts: collections.Counter[str] = collections.Counter()
    started_all = time.monotonic()
    for source_case in manifest["cases"]:
        case = dict(source_case)
        source = source_path(repo, reference_root, case)
        policy = policy_name(case)
        started = time.monotonic()
        try:
            completed = subprocess.run(
                [str(runner), policy, str(source)],
                cwd=reference_root,
                text=True,
                capture_output=True,
                timeout=args.timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as expired:
            observed = {
                "outcome": "incomplete",
                "boundary": "none",
                "relation": "process-timeout",
                "subject": "",
                "declarations_seen": 0,
                "equations_checked": 0,
                "established_equations": 0,
                "undetermined_equations": 0,
                "incomplete_equations": 0,
                "diagnostic": str(expired),
            }
            stderr = ""
        else:
            stderr = completed.stderr
            if completed.returncode == 0:
                try:
                    observed = parse_runner_output(completed.stdout)
                except (RuntimeError, ValueError) as error:
                    observed = {
                        "outcome": "fault",
                        "boundary": "none",
                        "relation": "runner-protocol",
                        "subject": "",
                        "declarations_seen": 0,
                        "equations_checked": 0,
                        "established_equations": 0,
                        "undetermined_equations": 0,
                        "incomplete_equations": 0,
                        "diagnostic": str(error),
                    }
            else:
                observed = {
                    "outcome": "fault",
                    "boundary": "none",
                    "relation": "runner-fault",
                    "subject": "",
                    "declarations_seen": 0,
                    "equations_checked": 0,
                    "established_equations": 0,
                    "undetermined_equations": 0,
                    "incomplete_equations": 0,
                    "diagnostic": stderr.strip(),
                }
        category, migration, product_verdict = classify(
            case, observed, h5.get(case["id"])
        )
        v3_verdict = policy_verdict(observed["outcome"], policy)
        case["v3"] = {
            **observed,
            "policy": policy,
            "policy_verdict": v3_verdict,
            "classification": category,
            "migration": migration,
            "active_route": (
                "native-agreement"
                if category == "native-agreement"
                else "legacy-v2"
            ),
            "refinement_verdict": (
                v3_verdict if category == "named-refinement" else None
            ),
            "product_verdict": product_verdict,
            "seconds": round(time.monotonic() - started, 4),
            "stderr": stderr,
        }
        results.append(case)
        category_counts[category] += 1
        outcome_counts[observed["outcome"]] += 1
        policy_verdict_counts[v3_verdict] += 1
        product_verdict_counts[product_verdict] += 1

    resolved = (
        category_counts["unclassified"] == 0
        and outcome_counts["incomplete"] == 0
        and outcome_counts["fault"] == 0
    )
    summary = {
        "schema": SUMMARY_SCHEMA,
        "total": len(results),
        "category_counts": dict(sorted(category_counts.items())),
        "outcome_counts": dict(sorted(outcome_counts.items())),
        "policy_verdict_counts": dict(sorted(policy_verdict_counts.items())),
        "product_verdict_counts": dict(sorted(product_verdict_counts.items())),
        "resolved": resolved,
        "seconds": round(time.monotonic() - started_all, 4),
        "runner_sha256": sha256_file(runner),
        "acceptance_manifest_sha256": sha256_file(manifest_path),
        "h5_matrix_sha256": sha256_file(h5_path),
        "compatibility_reference": manifest["oracle"],
        "authority_note": (
            "The fixed v2 checkout supplies source and expected compatibility "
            "verdicts only. Every native judgment is computed by the generated "
            "v3 calculus over live five-provider facts. The product keeps "
            "native agreement and otherwise delegates the complete block to "
            "v2. Named refinements are recorded for explicit promotion; none "
            "is activated merely because it appears in this corpus."
        ),
    }
    payload = {"schema": RESULT_SCHEMA, "summary": summary, "cases": results}
    (output_dir / "results.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (output_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(
        "typecheck-v3 corpus: "
        f"{len(results)}/389 classified; "
        f"native-agreement={category_counts['native-agreement']}; "
        f"named-refinement={category_counts['named-refinement']}; "
        f"legacy-v2={category_counts['legacy-v2']}; "
        f"unclassified={category_counts['unclassified']}; "
        f"incomplete={outcome_counts['incomplete']}; "
        f"fault={outcome_counts['fault']}"
    )
    return 0 if resolved or not args.require_resolved else 1


if __name__ == "__main__":
    raise SystemExit(main())
