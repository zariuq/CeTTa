#!/usr/bin/env python3
"""Pin the native strict-rho and cost-rho reader/translation boundary."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
import json
import os
from pathlib import Path
import subprocess


EXPECTED_MATRIX_DIGEST = "65497cb595bd22a7c967eee5bd6dd253216a64e2e403d3227cf01a19388253a6"


class GateFailure(RuntimeError):
    pass


@dataclass(frozen=True)
class AcceptedCase:
    label: str
    profile: str
    source: str
    normalized_mrho: str


@dataclass(frozen=True)
class RejectedCase:
    label: str
    profile: str
    source: str


ACCEPTED_CASES = (
    AcceptedCase("strict-nil", "strict-core", "0", "rho:nil"),
    AcceptedCase(
        "strict-send",
        "strict-core",
        "@{0}!(0)",
        "(rho:send (rho:quote rho:nil) rho:nil)",
    ),
    AcceptedCase(
        "strict-receive-parallel",
        "strict-core",
        "for ($x <- @{0}) {*$x} | @{0}!(0)",
        "(rho:par (rho:recv (rho:quote rho:nil) $x (rho:drop $x)) "
        "(rho:send (rho:quote rho:nil) rho:nil))",
    ),
    AcceptedCase(
        "strict-bare-bound-name",
        "strict-core",
        "for (x <- @{0}) {*x}",
        "(rho:recv (rho:quote rho:nil) $x (rho:drop $x))",
    ),
    AcceptedCase(
        "strict-comments-and-layout",
        "strict-core",
        "/* block */ @{0} ! ( // line\n 0 )",
        "(rho:send (rho:quote rho:nil) rho:nil)",
    ),
    AcceptedCase(
        "cost-signed",
        "cost",
        "{0}alice",
        "(rho:cost:signed rho:nil alice)",
    ),
    AcceptedCase(
        "cost-signature-product",
        "cost",
        "{0}alice * bob",
        "(rho:cost:signed rho:nil (rho:cost:sig-mul alice bob))",
    ),
    AcceptedCase(
        "cost-purse-stack",
        "cost",
        "purse pay {alice : bob : ()}",
        "(rho:cost:purse pay (rho:cost:stack-cons alice "
        "(rho:cost:stack-cons bob rho:cost:stack-empty)))",
    ),
    AcceptedCase(
        "cost-composite",
        "cost",
        "{for ($m <- pay) {{0}cont}}alice | {pay!({0}payload)}bob | "
        "purse pay {alice : ()} | purse pay {bob : ()}",
        "(rho:cost:par "
        "(rho:cost:signed (rho:recv pay $m "
        "(rho:cost:signed rho:nil cont)) alice) "
        "(rho:cost:signed (rho:send pay "
        "(rho:cost:signed rho:nil payload)) bob) "
        "(rho:cost:purse pay "
        "(rho:cost:stack-cons alice rho:cost:stack-empty)) "
        "(rho:cost:purse pay "
        "(rho:cost:stack-cons bob rho:cost:stack-empty)))",
    ),
)


REJECTED_CASES = (
    RejectedCase("strict-empty", "strict-core", ""),
    RejectedCase("strict-name-is-not-process", "strict-core", "@{0}"),
    RejectedCase(
        "strict-unbound-bare-name",
        "strict-core",
        "for (x <- @{0}) {*y}",
    ),
    RejectedCase("strict-reserved-for-prefix", "strict-core", "for?!(0)"),
    RejectedCase("strict-nonascii-identifier", "strict-core", "é!(0)"),
    RejectedCase("strict-unterminated-comment", "strict-core", "0 /*"),
    RejectedCase("strict-trailing-input", "strict-core", "0 trailing"),
    RejectedCase("cost-bare-process", "cost", "0"),
    RejectedCase("cost-missing-signature", "cost", "{0}"),
    RejectedCase("cost-malformed-stack", "cost", "purse pay {alice}"),
)


def translate(
    binary: Path,
    profile: str,
    input_syntax: str,
    output_syntax: str,
    source: str,
    *,
    expect_success: bool,
) -> str:
    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    completed = subprocess.run(
        [
            str(binary),
            "--translate",
            "--lang",
            "rhocalc",
            "--profile",
            profile,
            "--syntax",
            input_syntax,
            "--lang",
            "rhocalc",
            "--profile",
            "strict-core",
            "--syntax",
            output_syntax,
            "-e",
            source,
        ],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        env=environment,
        timeout=30,
    )
    if expect_success:
        if completed.returncode != 0:
            raise GateFailure(
                "rhocalc translation unexpectedly failed: "
                + completed.stderr.strip()
            )
        return completed.stdout.rstrip("\n")
    if completed.returncode == 0 or not completed.stderr.strip():
        raise GateFailure("invalid rhocalc input did not fail closed")
    return ""


def matrix_digest(records: list[dict[str, str]]) -> str:
    canonical = json.dumps(
        records,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return sha256(canonical).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()
    if not binary.is_file():
        raise GateFailure(f"CeTTa binary is absent: {binary}")

    records: list[dict[str, str]] = []
    roundtrips = 0
    for case in ACCEPTED_CASES:
        normalized = translate(
            binary,
            case.profile,
            "rho",
            "mrho",
            case.source,
            expect_success=True,
        )
        if normalized != case.normalized_mrho:
            raise GateFailure(
                f"native rho authority changed for {case.label}: "
                f"{normalized!r} != {case.normalized_mrho!r}"
            )

        mrho_replay = translate(
            binary,
            case.profile,
            "mrho",
            "mrho",
            normalized,
            expect_success=True,
        )
        if mrho_replay != normalized:
            raise GateFailure(f"mrho replay changed {case.label}")

        canonical_rho = translate(
            binary,
            case.profile,
            "mrho",
            "rho",
            normalized,
            expect_success=True,
        )
        rho_replay = translate(
            binary,
            case.profile,
            "rho",
            "mrho",
            canonical_rho,
            expect_success=True,
        )
        if rho_replay != normalized:
            raise GateFailure(f"rho/mrho round trip changed {case.label}")
        roundtrips += 1
        records.append(
            {
                "decision": "accepted",
                "label": case.label,
                "normalized_mrho": normalized,
                "profile": case.profile,
                "rho": canonical_rho,
                "source": case.source,
            }
        )

    for case in REJECTED_CASES:
        translate(
            binary,
            case.profile,
            "rho",
            "mrho",
            case.source,
            expect_success=False,
        )
        records.append(
            {
                "decision": "rejected",
                "label": case.label,
                "profile": case.profile,
                "source": case.source,
            }
        )

    digest = matrix_digest(records)
    if EXPECTED_MATRIX_DIGEST and digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(f"rhocalc reader authority matrix changed: {digest}")
    print(
        f"(RhoCalcReaderAuthorityV1Summary {len(ACCEPTED_CASES)} "
        f"{len(REJECTED_CASES)} {roundtrips} {digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
