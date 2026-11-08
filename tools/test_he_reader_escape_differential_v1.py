#!/usr/bin/env python3
"""Run the ratified HE escape contract against Rust/CeTTa/LeaTTa."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from typing import Any


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(SCRIPTS))

import build_he_reader_gslt_v1 as builder  # noqa: E402
from run_he_parser_oracle_v1 import build_oracle  # noqa: E402
from test_he_parser_authority_v1 import run_case  # noqa: E402
from test_he_unicode_residual_dfa_v1 import residual_accepts  # noqa: E402


FIXTURE = (
    ROOT
    / "experiments"
    / "gslt2parse_foundation"
    / "he_reader_escape_differential_v1.json"
)
PROJECTION_PATTERN = re.compile(
    r"\(sexpr-wrapper-hex\s+he-v1\s+unicode-escape\s+"
    r"(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\)"
)


class GateFailure(RuntimeError):
    pass


def digest(path: Path) -> str:
    if not path.is_file():
        raise GateFailure(f"pinned source is absent: {path}")
    return sha256(path.read_bytes()).hexdigest()


def run_process(
    command: list[str], *, cwd: Path, timeout: int = 300
) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
        timeout=timeout,
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            + completed.stderr
        )
    return completed


def checked_revision(root: Path, expected: str, label: str) -> None:
    observed = run_process(["git", "rev-parse", "HEAD"], cwd=root).stdout.strip()
    if observed != expected:
        raise GateFailure(f"{label} revision changed: {observed}")


def load_fixture() -> dict[str, Any]:
    try:
        fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise GateFailure("escape differential fixture is unreadable") from error
    if fixture.get("schema") != "he-reader-escape-differential-v1":
        raise GateFailure("escape differential schema changed")
    expected_semantics = {
        "simple_escape": "apostrophe-quote-backslash-n-r-t",
        "byte_escape": "exactly-two-hex-digits-through-7f",
        "unicode_escape": "one-brace-one-through-six-hex-unicode-scalar",
        "malformed_escape": "rejected",
    }
    if (
        fixture.get("policy") != "ratified-he-syntax-v1"
        or fixture.get("canonical_semantics") != expected_semantics
    ):
        raise GateFailure("escape fixture lacks the ratified HE semantics")
    cases = fixture.get("cases")
    if not isinstance(cases, list) or len(cases) != 18:
        raise GateFailure("escape differential case inventory changed")
    identifiers = [case.get("id") for case in cases if isinstance(case, dict)]
    if len(identifiers) != 18 or len(set(identifiers)) != 18:
        raise GateFailure("escape differential case identifiers are malformed")
    return fixture


def check_sources(
    fixture: dict[str, Any],
    he_root: Path,
    cetta_root: Path,
    mettapedia_root: Path,
) -> int:
    authorities = fixture.get("authorities")
    if not isinstance(authorities, dict) or len(authorities) != 8:
        raise GateFailure("escape authority inventory changed")
    expected_paths = {
        "rust_parser": he_root / "lib" / "src" / "metta" / "text.rs",
        "cetta_parser": cetta_root / "src" / "parser.c",
        "leatta_parser": (
            mettapedia_root
            / "lean"
            / "algorithms"
            / "Algorithms"
            / "MeTTa"
            / "Simple"
            / "Parser.lean"
        ),
        "leatta_consumed_spec": (
            mettapedia_root
            / "lean"
            / "batteries"
            / "mettail-core"
            / "MeTTailCore"
            / "MeTTaSyntax"
            / "Spec.lean"
        ),
        "gslt_recognizer": (
            ROOT
            / "experiments"
            / "gslt2parse_foundation"
            / "presentations"
            / "languages"
            / "he_reader_v1.metta"
        ),
        "gslt_projection": (
            ROOT
            / "experiments"
            / "gslt2parse_foundation"
            / "presentations"
            / "shared"
            / "sexpr_atom_projection_v1.metta"
        ),
        "cetta_oracle": ROOT / "tools" / "cetta_parser_oracle_v1.c",
        "leatta_oracle": ROOT / "tools" / "leatta_parser_oracle_v1.lean",
    }
    if set(authorities) != set(expected_paths):
        raise GateFailure("escape authority labels changed")
    # The recorded digests and revisions say where the observations were
    # made; a source that has moved since is a note, and its observations,
    # made again, decide.
    for label, path in expected_paths.items():
        row = authorities[label]
        if not isinstance(row, dict):
            raise GateFailure(f"escape authority record is malformed: {label}")
        if not path.is_file():
            raise GateFailure(f"escape authority source is absent: {label}")
        if digest(path) != row.get("sha256"):
            print(f"note: {label} has changed since its observations were recorded")
    if (
        authorities["leatta_parser"]["revision"]
        != authorities["leatta_consumed_spec"]["revision"]
    ):
        raise GateFailure("LeaTTa parser and consumed-spec revisions differ")
    return len(authorities)


def build_cetta_oracle(cetta_root: Path) -> Path:
    directory = ROOT / "runtime" / "bootstrap" / "he_reader_escape_differential_v1"
    directory.mkdir(parents=True, exist_ok=True)
    binary = directory / "cetta-parser-oracle-v1"
    # The reader's standalone closure, as the Makefile builds it (parser.c,
    # SYMBOL_STANDALONE_SRC, atom.c with frame identities, name_key.c), and
    # -pthread, which these sources need at compile time as well as link time.
    run_process(
        [
            "gcc",
            "-std=c11",
            "-pthread",
            "-O2",
            "-ffunction-sections",
            "-fdata-sections",
            f"-I{cetta_root / 'src'}",
            "-Wl,--gc-sections",
            "-o",
            str(binary),
            str(ROOT / "tools" / "cetta_parser_oracle_v1.c"),
            str(cetta_root / "src" / "symbol.c"),
            str(cetta_root / "src" / "scalar_trie.c"),
            str(cetta_root / "src" / "atom.c"),
            str(cetta_root / "src" / "binding" / "frame_identity.c"),
            str(cetta_root / "src" / "name_key.c"),
            str(cetta_root / "src" / "parser.c"),
            "-lm",
        ],
        cwd=ROOT,
    )
    if not binary.is_file():
        raise GateFailure("CeTTa source oracle was not produced")
    return binary


def rust_observation(binary: Path, path: Path) -> dict[str, str]:
    observations = run_case(binary, "atoms", path)
    if len(observations) != 1:
        raise GateFailure("Rust oracle did not return exactly one observation")
    observation = observations[0]
    if observation[0] == "sym":
        return {
            "status": "accepted",
            "value_hex": observation[1].encode("utf-8").hex(),
        }
    if observation[0] == "error":
        return {"status": "rejected", "error": observation[1]}
    raise GateFailure(f"unexpected Rust observation: {observation!r}")


def one_line_observation(
    binary: Path, path: Path, *, cwd: Path
) -> dict[str, str]:
    lines = run_process([str(binary), str(path)], cwd=cwd).stdout.splitlines()
    if len(lines) != 1:
        raise GateFailure("CeTTa oracle framing changed")
    status, separator, value = lines[0].partition("\t")
    if not separator or status not in {"accepted", "rejected"}:
        raise GateFailure("CeTTa oracle record is malformed")
    result = {"status": status}
    if status == "accepted":
        result["value_hex"] = value
    return result


def leatta_observations(
    oracle: Path, paths: list[Path], mettapedia_root: Path
) -> list[dict[str, str]]:
    algorithms = mettapedia_root / "lean" / "algorithms"
    # the algorithms project is built with Mettapedia's toolchain (its own
    # toolchain file is local and may lag)
    toolchain_file = mettapedia_root / "lean" / "mettapedia" / "lean-toolchain"
    prefix = (["elan", "run", toolchain_file.read_text(encoding="utf-8").strip()]
              if toolchain_file.is_file() else [])
    lines = run_process(
        [*prefix, "lake", "env", "lean", "--run", str(oracle), *(str(path) for path in paths)],
        cwd=algorithms,
        timeout=600,
    ).stdout.splitlines()
    if not lines or lines[0] != "leatta-parser-oracle-v1" or lines[-1] != "end":
        raise GateFailure("LeaTTa oracle framing changed")
    records: list[dict[str, str]] = []
    for line in lines[1:-1]:
        status, separator, value = line.partition("\t")
        if not separator or status not in {
            "accepted",
            "accepted-other",
            "rejected",
        }:
            raise GateFailure("LeaTTa oracle record is malformed")
        records.append({"status": status, "value_hex": value})
    if len(records) != len(paths):
        raise GateFailure("LeaTTa oracle result count changed")
    return records


def compare_observation(
    case_id: str,
    engine: str,
    expected: dict[str, str],
    observed: dict[str, str],
) -> None:
    if observed.get("status") != expected.get("status"):
        raise GateFailure(f"{case_id}: {engine} acceptance changed: {observed}")
    if expected["status"] == "accepted":
        if observed.get("value_hex") != expected.get("value_hex"):
            raise GateFailure(f"{case_id}: {engine} decoded value changed")
    elif "error_contains" in expected:
        if expected["error_contains"] not in observed.get("error", ""):
            raise GateFailure(f"{case_id}: {engine} rejection reason changed")


def parse_unicode_envelope(source: bytes) -> tuple[int, bool, str]:
    try:
        text = source.decode("ascii")
    except UnicodeDecodeError as error:
        raise GateFailure("Unicode escape fixture is not ASCII") from error
    if not text.startswith('"\\u') or not text.endswith('"'):
        raise GateFailure("Unicode escape fixture lacks its quoted envelope")
    payload = text[3:-1]
    opening_braces = len(payload) - len(payload.lstrip("{"))
    remainder = payload[opening_braces:]
    has_closing_brace = remainder.endswith("}")
    body = remainder[:-1] if has_closing_brace else remainder
    return opening_braces, has_closing_brace, body


def projection_contract(fixture: dict[str, Any]) -> tuple[int, int, int, int]:
    path = (
        ROOT
        / "experiments"
        / "gslt2parse_foundation"
        / "presentations"
        / "shared"
        / "sexpr_atom_projection_v1.metta"
    )
    matches = PROJECTION_PATTERN.findall(path.read_text(encoding="utf-8"))
    if len(matches) != 1:
        raise GateFailure("Unicode projection action is absent or ambiguous")
    observed = tuple(int(value) for value in matches[0])
    row = fixture.get("projection_contract")
    if not isinstance(row, dict):
        raise GateFailure("Unicode projection contract is absent")
    expected = (
        row.get("min_digits"),
        row.get("max_digits"),
        row.get("max_codepoint"),
        row.get("radix"),
    )
    if observed != expected or row.get("profile") != "he-v1" or row.get("label") != "unicode-escape":
        raise GateFailure("Unicode projection contract changed")
    return observed


def check_unicode_seam(fixture: dict[str, Any]) -> tuple[int, int, int, int]:
    states = builder.unicode_residual_dfa(builder.unicode_valid_class_sequences())
    minimum, maximum, _max_codepoint, _radix = projection_contract(fixture)
    unicode_cases = 0
    recognized = 0
    projected = 0
    seam: set[str] = set()
    for case in fixture["cases"]:
        expected = case.get("unicode")
        if expected is None:
            continue
        if not isinstance(expected, dict):
            raise GateFailure(f"{case['id']}: malformed Unicode expectation")
        unicode_cases += 1
        source = bytes.fromhex(case["source_hex"])
        opening, closing, body = parse_unicode_envelope(source)
        if (
            opening != expected.get("opening_braces")
            or closing != expected.get("has_closing_brace")
            or body != expected.get("body")
        ):
            raise GateFailure(f"{case['id']}: Unicode envelope changed")
        accepts = opening == 1 and closing and residual_accepts(states, body)
        covers = accepts and minimum <= len(body) <= maximum
        if (
            accepts != expected.get("recognizer_accepts")
            or covers != expected.get("projection_covers")
        ):
            raise GateFailure(f"{case['id']}: GSLT seam classification changed")
        recognized += int(accepts)
        projected += int(covers)
        if accepts and not covers:
            seam.add(case["id"])
    expected_seam = fixture.get("expected_strict_seam")
    if not isinstance(expected_seam, list) or seam != set(expected_seam):
        raise GateFailure("GSLT recognition/action strict seam changed")
    if (unicode_cases, recognized, projected, len(seam)) != (11, 1, 1, 0):
        raise GateFailure("GSLT Unicode seam accounting changed")
    return unicode_cases, recognized, projected, len(seam)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--he-root", type=Path)
    parser.add_argument("--cetta-root", type=Path, required=True)
    parser.add_argument("--mettapedia-root", type=Path)
    # Recorded: CeTTa observed live against the fixture's recorded
    # observations of the Rust and LeaTTa readers (their revisions and source
    # digests are the record's provenance), with no HE or Mettapedia checkout.
    # Live: all three observed again, each authority's source pinned.
    parser.add_argument("--recorded", action="store_true")
    arguments = parser.parse_args()
    recorded = arguments.recorded or not arguments.he_root or not arguments.mettapedia_root
    cetta_root = arguments.cetta_root.resolve()
    he_root = arguments.he_root.resolve() if arguments.he_root else None
    mettapedia_root = arguments.mettapedia_root.resolve() if arguments.mettapedia_root else None
    checkouts = [("CeTTa", cetta_root)]
    if not recorded:
        checkouts += [("Rust HE", he_root), ("MeTTapedia", mettapedia_root)]
    for label, path in checkouts:
        if not path.is_dir():
            raise GateFailure(f"{label} checkout is absent")

    fixture = load_fixture()
    source_count = 0 if recorded else check_sources(fixture, he_root, cetta_root, mettapedia_root)
    rust_binary = None if recorded else build_oracle(he_root)
    cetta_binary = build_cetta_oracle(cetta_root)
    cases = fixture["cases"]
    accepted = {"rust": 0, "cetta": 0, "leatta": 0}

    bootstrap = ROOT / "runtime" / "bootstrap"
    bootstrap.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="he-reader-escape-differential-", dir=bootstrap
    ) as raw:
        directory = Path(raw)
        paths: list[Path] = []
        for index, case in enumerate(cases):
            path = directory / f"{index:02d}-{case['id']}.metta"
            path.write_bytes(bytes.fromhex(case["source_hex"]))
            paths.append(path)

        leatta = (
            [case.get("leatta") for case in cases]
            if recorded
            else leatta_observations(
                ROOT / "tools" / "leatta_parser_oracle_v1.lean",
                paths,
                mettapedia_root,
            )
        )
        for case, path, leatta_observed in zip(cases, paths, leatta, strict=True):
            observations = {
                "rust": case.get("rust") if recorded else rust_observation(rust_binary, path),
                "cetta": one_line_observation(cetta_binary, path, cwd=ROOT),
                "leatta": leatta_observed,
            }
            if recorded and not all(isinstance(observations[e], dict) for e in ("rust", "leatta")):
                raise GateFailure(f"{case['id']}: missing recorded observation")
            for engine, observed in observations.items():
                expected = case.get(engine)
                if not isinstance(expected, dict):
                    raise GateFailure(f"{case['id']}: missing {engine} expectation")
                # a recorded engine's observation is its record
                if not (recorded and engine != "cetta"):
                    compare_observation(case["id"], engine, expected, observed)
                accepted[engine] += int(observed["status"] == "accepted")

    if accepted != {"rust": 9, "cetta": 16, "leatta": 18}:
        raise GateFailure(f"parser acceptance totals changed: {accepted}")
    unicode_counts = check_unicode_seam(fixture)
    print(
        "(HEReaderEscapeDifferentialV1Summary "
        f"{len(cases)} {accepted['rust']} {accepted['cetta']} "
        f"{accepted['leatta']} {' '.join(str(value) for value in unicode_counts)} "
        f"{source_count} 0{' recorded' if recorded else ''})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
