#!/usr/bin/env python3
"""Census and destructive canaries for CeTTa's direct typing authorities."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import tempfile

import frame_nik_authority_v1 as framer
import gslt2parse_schema_v1 as schema


ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True, slots=True)
class ExpectedAuthority:
    path: Path
    name: str
    fiber: str
    outcomes: tuple[str, ...]
    default: str
    rule_count: int


AUTHORITIES = (
    ExpectedAuthority(
        ROOT / "langdef/he/generated/typing_consistency_core_v1.metta",
        "he-typing-consistency-core",
        "he",
        ("HCheckEstablished", "HCheckRefuted", "HCheckUndetermined", "HCheckIncomplete"),
        "HCheckUndetermined",
        22,
    ),
    ExpectedAuthority(
        ROOT / "langdef/he/generated/profiled_type_inference_core_v1.metta",
        "he-profiled-type-inference-core",
        "he",
        ("HCheckEstablished", "HCheckRefuted", "HCheckUndetermined", "HCheckIncomplete"),
        "HCheckUndetermined",
        21,
    ),
    ExpectedAuthority(
        ROOT / "langdef/he/generated/typing_closed_ground_core_v1.metta",
        "he-typing-closed-ground-core-v1",
        "he",
        ("HCheckEstablished", "HCheckRefuted", "HCheckUndetermined", "HCheckIncomplete"),
        "HCheckUndetermined",
        32,
    ),
    ExpectedAuthority(
        ROOT / "langdef/petta/generated/typecheck_v2_guard_v1.metta",
        "petta-typecheck-v2-guard",
        "petta",
        ("PTEstablished", "PTRefuted", "PTUndetermined", "PTIncomplete"),
        "PTUndetermined",
        116,
    ),
    ExpectedAuthority(
        ROOT / "langdef/petta/generated/typecheck_v2_boundary_core_v1.metta",
        "petta-typecheck-v2-boundary-core-v1",
        "petta",
        ("PTEstablished", "PTRefuted", "PTUndetermined", "PTIncomplete"),
        "PTUndetermined",
        60,
    ),
    ExpectedAuthority(
        ROOT / "langdef/prime/generated/closed_formation_v1.metta",
        "prime-closed-formation-v1",
        "prime",
        ("PEstablished", "PRefuted", "PUndetermined", "PIncomplete"),
        "PUndetermined",
        15,
    ),
    ExpectedAuthority(
        ROOT / "langdef/prime/generated/native_ground_judgments_v1.metta",
        "prime-native-ground-judgments-v1",
        "prime",
        ("PEstablished", "PRefuted", "PUndetermined", "PIncomplete"),
        "PUndetermined",
        44,
    ),
    ExpectedAuthority(
        ROOT / "langdef/prime/generated/elaborated_dependent_formation_core_v1.metta",
        "prime-elaborated-dependent-formation-core-v1",
        "prime",
        ("PEstablished", "PRefuted", "PUndetermined", "PIncomplete"),
        "PUndetermined",
        23,
    ),
    ExpectedAuthority(
        ROOT / "langdef/prime/generated/typed_publication_core_v1.metta",
        "prime-typed-publication-core-v1",
        "prime",
        ("PEstablished", "PRefuted", "PUndetermined"),
        "PUndetermined",
        18,
    ),
)

COMMON_COMMITMENTS = {
    "direct-computation",
    "certificate-free",
    "exclusive-outcomes",
    "explicit-default",
}
CALIBRATION_RULES = {
    "prime-native-ground-normalize-one-plus-one",
    "prime-native-ground-normalize-two",
}


class GateFailure(RuntimeError):
    pass


def audit_authority(
    presentation: schema.Presentation, expected: ExpectedAuthority
) -> None:
    if presentation.name != expected.name:
        raise GateFailure(
            f"{expected.path}: expected presentation {expected.name}, "
            f"got {presentation.name}"
        )
    frame = presentation.nik_frame
    if frame is None:
        raise GateFailure(f"{expected.path}: missing nik-authority-frame-v1")
    if frame.mode != "direct-decision" or frame.certificate_policy != "none":
        raise GateFailure(f"{expected.path}: typing authority is not certificate-free")
    if frame.fiber != expected.fiber:
        raise GateFailure(
            f"{expected.path}: expected fiber {expected.fiber}, got {frame.fiber}"
        )
    if frame.outcomes != expected.outcomes or frame.default_outcome != expected.default:
        raise GateFailure(f"{expected.path}: outcome algebra drifted")
    if frame.native_projection != "pending":
        raise GateFailure(f"{expected.path}: unexpected native-projection grade")
    if frame.status != "AUTHORED_FRAGMENT":
        raise GateFailure(f"{expected.path}: fragment status was overstated")
    if not COMMON_COMMITMENTS.issubset(frame.commitments):
        raise GateFailure(f"{expected.path}: common direct-typing commitments are absent")
    if len(presentation.rules) != expected.rule_count:
        raise GateFailure(
            f"{expected.path}: expected {expected.rule_count} rules, "
            f"got {len(presentation.rules)}"
        )
    for rule in presentation.rules:
        if rule.nik_frame is None or not rule.nik_frame.native_projection:
            raise GateFailure(f"{expected.path}: rule {rule.name} lacks projection metadata")


def audit(paths: tuple[Path, ...]) -> tuple[schema.Presentation, ...]:
    presentations = schema.admit(paths)
    if len(presentations) != len(AUTHORITIES):
        raise GateFailure("typing-authority inventory changed")
    by_name = {presentation.name: presentation for presentation in presentations}
    if len(by_name) != len(presentations):
        raise GateFailure("typing-authority names are not unique")
    for expected in AUTHORITIES:
        presentation = by_name.get(expected.name)
        if presentation is None:
            raise GateFailure(f"missing typing authority {expected.name}")
        audit_authority(presentation, expected)

    prime_ground = by_name["prime-native-ground-judgments-v1"]
    operators = {(operator.name, operator.arity) for operator in prime_ground.operators}
    if ("PSynth", 2) not in operators or any(
        name == "PSynth" and arity != 2 for name, arity in operators
    ):
        raise GateFailure("Prime synthesis must return its type through PSynth/2")
    if ("PrimeConsistentDir", 3) not in operators or any(
        name == "PrimeConsistent" for name, _arity in operators
    ):
        raise GateFailure("Prime consistency must be explicitly directed")
    calibration = {
        rule.name
        for rule in prime_ground.rules
        if rule.nik_frame is not None and rule.nik_frame.role == "calibration"
    }
    if calibration != CALIBRATION_RULES:
        raise GateFailure("Prime calibration-rule boundary drifted")
    for presentation in presentations:
        if presentation.name == prime_ground.name:
            continue
        if any(
            rule.nik_frame is not None and rule.nik_frame.role == "calibration"
            for rule in presentation.rules
        ):
            raise GateFailure(f"{presentation.source}: unexpected calibration rule")
    return presentations


def expect_audit_failure(paths: tuple[Path, ...], needle: str) -> None:
    try:
        audit(paths)
    except (GateFailure, schema.SchemaError) as error:
        if needle not in str(error):
            raise GateFailure(f"wrong rejection: expected {needle!r}, got {error}")
        return
    raise GateFailure(f"typing-authority mutation survived; expected {needle!r}")


def audit_framing_tool(temp: Path) -> int:
    source = temp / "unframed.metta"
    output = temp / "framed.metta"
    source.write_text(
        """(gslt-presentation-v1 framing-canary-v1
  (signature
    (operator Source 1)
    (operator Target 1))
  (equations)
  (rewrites
    (rule framing-canary
      (head (Target ?value))
      (body (Source ?value)))))
""",
        encoding="utf-8",
    )
    unframed = schema.parse_presentation(source)
    framed_text = framer.frame_text(
        source.read_text(encoding="utf-8"),
        unframed,
        mode="direct-decision",
        certificate_policy="none",
        fiber="canary",
        outcomes=("Accepted", "Rejected"),
        default_outcome="Rejected",
        native_projection="pending",
        status="AUTHORED_FRAGMENT",
        commitments=("direct-computation", "certificate-free"),
        head_projections={"Target": "native-target"},
        rule_projections={},
        calibration_rules=set(),
    )
    output.write_text(framed_text, encoding="utf-8")
    framed = schema.parse_presentation(output)
    if framed.nik_frame is None or framed.nik_frame.fiber != "canary":
        raise GateFailure("framing tool did not preserve its authority fiber")
    if len(framed.rules) != 1 or framed.rules[0].body != unframed.rules[0].body:
        raise GateFailure("framing tool changed an authored rule body")
    if (
        framed.rules[0].nik_frame is None
        or framed.rules[0].nik_frame.native_projection != "native-target"
    ):
        raise GateFailure("framing tool did not attach the native projection")

    try:
        framer.frame_text(
            framed_text,
            framed,
            mode="direct-decision",
            certificate_policy="none",
            fiber="canary",
            outcomes=("Accepted", "Rejected"),
            default_outcome="Rejected",
            native_projection="pending",
            status="AUTHORED_FRAGMENT",
            commitments=(),
            head_projections={"Target": "native-target"},
            rule_projections={},
            calibration_rules=set(),
        )
    except ValueError as error:
        if "already carries" not in str(error):
            raise GateFailure(f"wrong already-framed rejection: {error}") from error
    else:
        raise GateFailure("framing tool accepted an already-framed presentation")

    try:
        framer.frame_text(
            source.read_text(encoding="utf-8"),
            unframed,
            mode="direct-decision",
            certificate_policy="none",
            fiber="canary",
            outcomes=("Accepted", "Rejected"),
            default_outcome="Rejected",
            native_projection="pending",
            status="AUTHORED_FRAGMENT",
            commitments=(),
            head_projections={},
            rule_projections={},
            calibration_rules=set(),
        )
    except ValueError as error:
        if "has no native projection" not in str(error):
            raise GateFailure(f"wrong missing-projection rejection: {error}") from error
    else:
        raise GateFailure("framing tool accepted a rule without a native projection")
    return 3


def main() -> int:
    source_paths = tuple(expected.path for expected in AUTHORITIES)
    audit(source_paths)
    passed = len(AUTHORITIES) + 3

    prime_index = next(
        index
        for index, expected in enumerate(AUTHORITIES)
        if expected.name == "prime-native-ground-judgments-v1"
    )
    prime_path = source_paths[prime_index]
    prime_text = prime_path.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="nik-type-authority-frame-v1-") as raw_temp:
        temp = Path(raw_temp)

        passed += audit_framing_tool(temp)

        wrong_fiber = temp / "wrong-fiber.metta"
        wrong_fiber.write_text(
            prime_text.replace("(fiber prime)", "(fiber he)", 1), encoding="utf-8"
        )
        paths = source_paths[:prime_index] + (wrong_fiber,) + source_paths[prime_index + 1 :]
        expect_audit_failure(paths, "expected fiber prime")
        passed += 1

        missing_commitment = temp / "missing-commitment.metta"
        missing_commitment.write_text(
            prime_text.replace(" certificate-free", "", 1), encoding="utf-8"
        )
        paths = (
            source_paths[:prime_index]
            + (missing_commitment,)
            + source_paths[prime_index + 1 :]
        )
        expect_audit_failure(paths, "common direct-typing commitments are absent")
        passed += 1

        untagged_calibration = temp / "untagged-calibration.metta"
        untagged_calibration.write_text(
            prime_text.replace("(role calibration)", "(role calculus)", 1),
            encoding="utf-8",
        )
        paths = (
            source_paths[:prime_index]
            + (untagged_calibration,)
            + source_paths[prime_index + 1 :]
        )
        expect_audit_failure(paths, "calibration-rule boundary drifted")
        passed += 1

    total = len(AUTHORITIES) + 9
    if passed != total:
        raise GateFailure(f"gate accounting mismatch: {passed}/{total}")
    print(f"(NikTypeAuthorityFrameV1Summary checks={total} failures=0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
