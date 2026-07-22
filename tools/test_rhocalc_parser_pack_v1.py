#!/usr/bin/env python3
"""Exact strict-rho/cost-rho ParserPack and native-reader differential."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_parser_pack_gll_v1 import run_native
from test_rhocalc_reader_authority_v1 import ACCEPTED_CASES
from test_rhocalc_reader_authority_v1 import REJECTED_CASES
from test_rhocalc_reader_authority_v1 import translate


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
EXPECTED_MATRIX_DIGEST = "72e25ca45691ac13170c57a33279f359f47ba0cd7f9f4045f824c51b73f2fcb9"
SCOPE_BOUNDARY_LABEL = "strict-unbound-bare-name"


class GateFailure(RuntimeError):
    pass


def oracle_rows(petta_root: Path) -> dict[tuple[str, str], dict[str, object]]:
    oracle = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "rhocalc_parser_pack_oracle_v1.pl"
    )
    completed = subprocess.run(
        ["swipl", "-q", "-f", str(oracle), "--", str(PRESENTATIONS)],
        cwd=oracle.parent,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=180,
    )
    if completed.returncode != 0:
        raise GateFailure("PeTTa rho oracle failed: " + completed.stderr.strip())
    lines = completed.stdout.splitlines()
    if (
        not lines
        or lines[0] != "rhocalc-parser-pack-oracle-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("PeTTa rho oracle returned malformed framing")
    rows: dict[tuple[str, str], dict[str, object]] = {}
    current: dict[str, object] | None = None
    key: tuple[str, str] | None = None
    for line in lines[1:-1]:
        fields = line.split("\t")
        if fields[0] == "case" and len(fields) == 5 and current is None:
            key = (fields[1], fields[2])
            if key in rows:
                raise GateFailure(f"duplicate PeTTa rho row: {key}")
            current = {
                "decision": fields[3],
                "forest-digest": fields[4],
                "results": [],
            }
        elif fields[0] == "result" and len(fields) == 2 and current is not None:
            results = current["results"]
            assert isinstance(results, list)
            results.append(fields[1])
        elif line == "end-case" and current is not None and key is not None:
            current["results"] = tuple(current["results"])
            rows[key] = current
            current = None
            key = None
        else:
            raise GateFailure(f"malformed PeTTa rho record: {line!r}")
    if current is not None:
        raise GateFailure("PeTTa rho oracle returned an unterminated row")
    return rows


def profile_key(profile: str) -> str:
    return "cost" if profile == "cost" else "strict"


def require_exact_backend(
    label: str,
    backend: str,
    observed: dict[str, object],
    oracle: dict[str, object],
    payload: bytes,
    scalar_count: int,
) -> None:
    for field in ("decision", "forest-digest", "results"):
        if observed.get(field) != oracle[field]:
            raise GateFailure(
                f"{label}: {backend} {field} differs from PeTTa: "
                f"{observed.get(field)!r} != {oracle[field]!r}"
            )
    for field, expected in (
        ("source-passes", "1"),
        ("decoded-bytes", str(len(payload))),
        ("input-bytes", str(len(payload))),
        ("input-scalars", str(scalar_count)),
    ):
        if observed.get(field) != expected:
            raise GateFailure(f"{label}: {backend} one-pass receipt changed")


def matrix_row(
    profile: str,
    label: str,
    row: dict[str, object],
) -> str:
    results = row["results"]
    assert isinstance(results, tuple)
    return (
        f"{profile}/{label}/{row['decision']}/{row['forest-digest']}:"
        + "|".join(results)
    )


def check(
    cetta_binary: Path,
    gll_binary: Path,
    glr_binary: Path,
    petta_root: Path,
) -> tuple[int, int, int, int, int, int, str]:
    expected = oracle_rows(petta_root)
    cases = ACCEPTED_CASES + REJECTED_CASES
    expected_keys = {(profile_key(case.profile), case.label) for case in cases}
    if set(expected) != expected_keys:
        raise GateFailure("PeTTa rho oracle case inventory changed")

    gll_exact = 0
    glr_exact = 0
    native_accepted = 0
    native_rejected = 0
    semantic_boundaries = 0
    rows: list[str] = []
    with tempfile.TemporaryDirectory(prefix="gslt2parse-rhocalc-") as raw:
        temp = Path(raw)
        pack_paths: dict[str, Path] = {}
        starts: dict[str, str] = {}
        for profile, abi_label in (("strict", "rho"), ("cost", "cost-rho")):
            abi_case = ABI_CASES[abi_label]
            path = temp / f"{profile}.abi"
            path.write_bytes(export_stream(petta_root, abi_case))
            pack_paths[profile] = path
            starts[profile] = f"(pp-def {abi_case['start']})"

        for index, case in enumerate(cases):
            profile = profile_key(case.profile)
            payload = case.source.encode("utf-8")
            input_path = temp / f"input-{index}.rho"
            input_path.write_bytes(payload)
            oracle = expected[(profile, case.label)]
            gll = run_native(
                gll_binary,
                pack_paths[profile],
                starts[profile],
                input_path,
            )
            glr = run_native(
                glr_binary,
                pack_paths[profile],
                starts[profile],
                input_path,
                protocol="parser-pack-glr-stream-v1",
                backend="GLR",
            )
            scalar_count = len(case.source)
            require_exact_backend(
                case.label, "GLL", gll, oracle, payload, scalar_count
            )
            require_exact_backend(
                case.label, "GLR", glr, oracle, payload, scalar_count
            )
            gll_exact += 1
            glr_exact += 1

            if case in ACCEPTED_CASES:
                normalized = translate(
                    cetta_binary,
                    case.profile,
                    "rho",
                    "mrho",
                    case.source,
                    expect_success=True,
                )
                if normalized != case.normalized_mrho:
                    raise GateFailure(f"{case.label}: native rho action changed")
                if oracle["decision"] != "accepted":
                    raise GateFailure(f"{case.label}: syntax rejected native input")
                native_accepted += 1
            else:
                translate(
                    cetta_binary,
                    case.profile,
                    "rho",
                    "mrho",
                    case.source,
                    expect_success=False,
                )
                native_rejected += 1
                if case.label == SCOPE_BOUNDARY_LABEL:
                    if oracle["decision"] != "accepted":
                        raise GateFailure("scope boundary disappeared from recognition")
                    semantic_boundaries += 1
                elif oracle["decision"] != "rejected":
                    raise GateFailure(
                        f"{case.label}: syntax accepts a native syntax rejection"
                    )
            rows.append(matrix_row(profile, case.label, oracle))

    digest = hashlib.sha256("\n".join(rows).encode("utf-8")).hexdigest()
    if EXPECTED_MATRIX_DIGEST and digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(f"rho ParserPack matrix changed: {digest}")
    return (
        len(expected),
        gll_exact,
        glr_exact,
        native_accepted,
        native_rejected,
        semantic_boundaries,
        digest,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta-binary", type=Path, required=True)
    parser.add_argument("--gll-binary", type=Path, required=True)
    parser.add_argument("--glr-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()
    paths = (
        arguments.cetta_binary,
        arguments.gll_binary,
        arguments.glr_binary,
    )
    if any(not path.resolve().is_file() for path in paths):
        raise GateFailure("a required native binary is absent")
    summary = check(
        arguments.cetta_binary.resolve(),
        arguments.gll_binary.resolve(),
        arguments.glr_binary.resolve(),
        arguments.petta_root.resolve(),
    )
    print(
        "(RhoCalcParserPackV1Summary "
        + " ".join(str(value) for value in summary)
        + " 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
