#!/usr/bin/env python3
"""Exact C GLL/GLR and PeTTa differential gate for canonical ParserPacks."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
MATRIX_DIGEST = "34791ce2f3fcf354d171ba43c09da37a6220cf203dc195b587cf8df9bac90141"
STREAMING_FOREST_SCALARS = 768
STREAMING_FOREST_DIGEST = (
    "f2da536c07c0af06092799fe6899934a6d24694a113e8706c8f7d73d15e828d0"
)

STARTS = {
    "megalodon": "(pp-rel mg-name-list)",
    "metamath": "(pp-def database)",
    "tptp": "(pp-def tptp-file)",
}

CASES = (
    ("megalodon", "empty", ()),
    ("megalodon", "single_name", (97,)),
    ("megalodon", "two_names", (97, 32, 98)),
    ("megalodon", "leading_digit", (49, 97)),
    ("megalodon", "unicode_initial", (955,)),
    ("metamath", "empty", ()),
    ("metamath", "whitespace", (32,)),
    ("metamath", "constant", (36, 99, 32, 97, 32, 36, 46)),
    ("metamath", "truncated_constant", (36, 99, 32, 97, 32, 36)),
    ("metamath", "unicode_junk", (955,)),
    ("tptp", "empty", ()),
    (
        "tptp",
        "basic_fof",
        (102, 111, 102, 40, 97, 44, 97, 120, 105, 111, 109, 44, 112, 41, 46, 10),
    ),
    (
        "tptp",
        "missing_dot",
        (102, 111, 102, 40, 97, 44, 97, 120, 105, 111, 109, 44, 112, 41, 10),
    ),
    ("tptp", "unicode_junk", (955,)),
)


class GateFailure(RuntimeError):
    pass


def oracle_rows(
    petta_root: Path,
    environment: dict[str, str] | None = None,
) -> dict[tuple[str, str], dict[str, object]]:
    oracle = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "parser_pack_gll_oracle_v1.pl"
    )
    completed = subprocess.run(
        ["swipl", "-q", "-f", str(oracle), "--", str(PRESENTATIONS)],
        cwd=oracle.parent,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=120,
        env=environment,
    )
    if completed.returncode != 0:
        raise GateFailure("PeTTa GLL oracle failed: " + completed.stderr.strip())
    lines = completed.stdout.splitlines()
    if not lines or lines[0] != "parser-pack-gll-oracle-v1" or lines[-1] != "end":
        raise GateFailure("PeTTa GLL oracle returned malformed framing")
    rows: dict[tuple[str, str], dict[str, object]] = {}
    current: dict[str, object] | None = None
    current_key: tuple[str, str] | None = None
    for line in lines[1:-1]:
        fields = line.split("\t")
        if fields[0] == "case" and len(fields) == 5 and current is None:
            current_key = (fields[1], fields[2])
            if current_key in rows:
                raise GateFailure(f"duplicate oracle row: {current_key}")
            current = {
                "decision": fields[3],
                "forest-digest": fields[4],
                "results": [],
            }
        elif fields[0] == "result" and len(fields) == 2 and current is not None:
            results = current["results"]
            assert isinstance(results, list)
            results.append(fields[1])
        elif line == "end-case" and current is not None and current_key is not None:
            current["results"] = tuple(current["results"])
            rows[current_key] = current
            current = None
            current_key = None
        else:
            raise GateFailure(f"malformed oracle record: {line!r}")
    if current is not None or len(rows) != len(CASES):
        raise GateFailure("PeTTa GLL oracle returned an incomplete matrix")
    return rows


def input_bytes(codepoints: tuple[int, ...]) -> bytes:
    return "".join(chr(codepoint) for codepoint in codepoints).encode("utf-8")


def run_native(
    binary: Path,
    abi_path: Path,
    start: str,
    input_path: Path,
    protocol: str = "parser-pack-gll-stream-v1",
    backend: str = "GLL",
    expect_success: bool = True,
) -> dict[str, object]:
    completed = subprocess.run(
        [str(binary), str(abi_path), start, str(input_path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=120,
    )
    if not expect_success:
        if completed.returncode == 0 or not completed.stderr.strip():
            raise GateFailure(f"native {backend} did not fail closed on invalid UTF-8")
        return {}
    if completed.returncode != 0:
        raise GateFailure(f"native {backend} failed: " + completed.stderr.strip())
    lines = completed.stdout.splitlines()
    if not lines or lines[0] != protocol or lines[-1] != "end":
        raise GateFailure(f"native {backend} returned malformed framing")
    parsed: dict[str, object] = {"results": []}
    for line in lines[1:-1]:
        fields = line.split("\t")
        if len(fields) != 2:
            raise GateFailure(f"malformed native {backend} record: {line!r}")
        name, value = fields
        if name == "result":
            results = parsed["results"]
            assert isinstance(results, list)
            results.append(value)
        elif name in parsed:
            raise GateFailure(f"duplicate native {backend} field: {name}")
        else:
            parsed[name] = value
    parsed["results"] = tuple(parsed["results"])
    return parsed


def matrix_row_text(
    language: str,
    label: str,
    row: dict[str, object],
) -> str:
    results = row["results"]
    assert isinstance(results, tuple)
    return (
        f"{language}/{label}/{row['decision']}/{row['forest-digest']}:"
        + "|".join(results)
    )


def require_exact_row(
    language: str,
    label: str,
    backend: str,
    observed: dict[str, object],
    oracle: dict[str, object],
    payload: bytes,
    codepoints: tuple[int, ...],
) -> None:
    for field in ("decision", "forest-digest", "results"):
        if observed.get(field) != oracle[field]:
            raise GateFailure(
                f"{language}/{label}: {field} differs: "
                f"{backend}={observed.get(field)!r}, PeTTa={oracle[field]!r}"
            )
    if (
        observed.get("source-passes") != "1"
        or observed.get("decoded-bytes") != str(len(payload))
        or observed.get("input-bytes") != str(len(payload))
        or observed.get("input-scalars") != str(len(codepoints))
    ):
        raise GateFailure(
            f"{language}/{label}: native {backend} one-pass accounting failed"
        )


def check_matrix(
    binary: Path,
    petta_root: Path,
    glr_binary: Path | None = None,
) -> tuple[int, int, int, int]:
    expected = oracle_rows(petta_root)
    gll_exact = 0
    glr_exact = 0
    with tempfile.TemporaryDirectory(prefix="gslt2parse-parser-pack-native-") as raw:
        temp = Path(raw)
        abi_paths: dict[str, Path] = {}
        for language in STARTS:
            stream = export_stream(petta_root, ABI_CASES[language])
            abi_path = temp / f"{language}.abi"
            abi_path.write_bytes(stream)
            abi_paths[language] = abi_path

        gll_rows: list[str] = []
        glr_rows: list[str] = []
        for index, (language, label, codepoints) in enumerate(CASES):
            payload = input_bytes(codepoints)
            input_path = temp / f"input-{index}.utf8"
            input_path.write_bytes(payload)
            gll = run_native(
                binary, abi_paths[language], STARTS[language], input_path
            )
            oracle = expected[(language, label)]
            require_exact_row(
                language, label, "GLL", gll, oracle, payload, codepoints
            )
            gll_rows.append(matrix_row_text(language, label, gll))
            gll_exact += 1

            if glr_binary is not None:
                glr = run_native(
                    glr_binary,
                    abi_paths[language],
                    STARTS[language],
                    input_path,
                    protocol="parser-pack-glr-stream-v1",
                    backend="GLR",
                )
                require_exact_row(
                    language, label, "GLR", glr, oracle, payload, codepoints
                )
                for field in ("decision", "forest-digest", "results"):
                    if glr.get(field) != gll.get(field):
                        raise GateFailure(
                            f"{language}/{label}: native GLL/GLR {field} differs"
                        )
                glr_rows.append(matrix_row_text(language, label, glr))
                glr_exact += 1

        gll_digest = hashlib.sha256(
            "\n".join(gll_rows).encode("utf-8")
        ).hexdigest()
        if gll_digest != MATRIX_DIGEST:
            raise GateFailure(f"canonical GLL matrix digest changed: {gll_digest}")
        if glr_binary is not None:
            glr_digest = hashlib.sha256(
                "\n".join(glr_rows).encode("utf-8")
            ).hexdigest()
            if glr_digest != MATRIX_DIGEST:
                raise GateFailure(
                    f"canonical GLR matrix digest changed: {glr_digest}"
                )

        streaming_path = temp / "metamath-streaming-forest.utf8"
        streaming_payload = b" " * STREAMING_FOREST_SCALARS
        streaming_path.write_bytes(streaming_payload)
        streaming_gll = run_native(
            binary,
            abi_paths["metamath"],
            STARTS["metamath"],
            streaming_path,
        )
        for field, expected_value in (
            ("outcome", "completed"),
            ("decision", "accepted"),
            ("forest-digest", STREAMING_FOREST_DIGEST),
            ("forest-materialized", "0"),
            ("source-passes", "1"),
            ("decoded-bytes", str(STREAMING_FOREST_SCALARS)),
        ):
            if streaming_gll.get(field) != expected_value:
                raise GateFailure(
                    "streaming canonical GLL forest boundary changed: "
                    f"{field}={streaming_gll.get(field)!r}"
                )
        if glr_binary is not None:
            streaming_glr = run_native(
                glr_binary,
                abi_paths["metamath"],
                STARTS["metamath"],
                streaming_path,
                protocol="parser-pack-glr-stream-v1",
                backend="GLR",
            )
            for field in (
                "outcome",
                "decision",
                "forest-digest",
                "forest-materialized",
                "forest-nodes",
                "forest-choices",
                "forest-roots",
                "source-passes",
                "decoded-bytes",
            ):
                if streaming_glr.get(field) != streaming_gll.get(field):
                    raise GateFailure(
                        "streaming canonical GLL/GLR forest differs: "
                        f"{field}"
                    )

        prime_stream = export_stream(petta_root, ABI_CASES["prime"])
        prime_path = temp / "prime.abi"
        prime_input = temp / "prime-empty.utf8"
        prime_path.write_bytes(prime_stream)
        prime_input.write_bytes(b"")
        prime_gll = run_native(
            binary, prime_path, "(pp-def cetta-prime-file)", prime_input
        )
        if prime_gll.get("outcome") != "unsupported-open-pack":
            raise GateFailure("native GLL Prime open-pack boundary changed")
        if glr_binary is not None:
            prime_glr = run_native(
                glr_binary,
                prime_path,
                "(pp-def cetta-prime-file)",
                prime_input,
                protocol="parser-pack-glr-stream-v1",
                backend="GLR",
            )
            if prime_glr.get("outcome") != "unsupported-open-pack":
                raise GateFailure("native GLR Prime open-pack boundary changed")

        invalid_path = temp / "invalid.utf8"
        invalid_path.write_bytes(b"\xc0\x80")
        run_native(
            binary,
            abi_paths["metamath"],
            STARTS["metamath"],
            invalid_path,
            expect_success=False,
        )
        invalid = 1
        if glr_binary is not None:
            run_native(
                glr_binary,
                abi_paths["metamath"],
                STARTS["metamath"],
                invalid_path,
                protocol="parser-pack-glr-stream-v1",
                backend="GLR",
                expect_success=False,
            )
            invalid += 1
    return gll_exact, glr_exact, 1, invalid


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--glr-binary", type=Path)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()

    gll_exact, glr_exact, partial, invalid = check_matrix(
        arguments.binary.resolve(),
        arguments.petta_root.resolve(),
        arguments.glr_binary.resolve() if arguments.glr_binary else None,
    )
    if arguments.glr_binary:
        print(
            f"(ParserPackDualNativeV1MatrixSummary {len(CASES)} "
            f"{gll_exact} {glr_exact} {partial} {invalid} {MATRIX_DIGEST} 0)"
        )
    else:
        print(
            f"(ParserPackGLLV1MatrixSummary {len(CASES)} {gll_exact} "
            f"{partial} {invalid} {MATRIX_DIGEST} 0)"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
