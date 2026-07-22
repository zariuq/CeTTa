#!/usr/bin/env python3
"""Measure structural scaling of the shared ParserPack GLL/GLR forest."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
import math
from pathlib import Path
import subprocess
import tempfile
import time

from test_parser_pack_abi_v1 import CASES, export_stream


ROOT = Path(__file__).resolve().parents[1]
TOKEN_COUNTS = (204, 409, 818, 1637, 3276)
START = "(pp-def petta-document)"
WORK_LIMIT = 50_000_000
REPLAY_DEPTH = 1
RESULT_LIMIT = 1
MAX_STRUCTURAL_SLOPE = 1.15


class ProbeFailure(RuntimeError):
    pass


@dataclass(frozen=True)
class Receipt:
    elapsed_seconds: float
    outcome: str
    digest: str
    result_digest: str
    input_bytes: int
    nodes: int
    choices: int
    roots: int
    work: int
    graph: int
    stack: int | None


def parse_stream(raw: bytes, header: str) -> dict[str, str]:
    try:
        lines = raw.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise ProbeFailure(f"{header} emitted invalid UTF-8") from error
    if not lines or lines[0] != header or lines[-1] != "end":
        raise ProbeFailure(f"malformed {header} framing")
    fields: dict[str, str] = {}
    for line in lines[1:-1]:
        key, separator, value = line.partition("\t")
        if not separator or not key or key in fields:
            raise ProbeFailure(f"malformed {header} field")
        fields[key] = value
    return fields


def integer(fields: dict[str, str], key: str) -> int:
    try:
        value = int(fields[key])
    except (KeyError, ValueError) as error:
        raise ProbeFailure(f"missing or invalid {key} receipt") from error
    if value < 0:
        raise ProbeFailure(f"negative {key} receipt")
    return value


def run_parser(binary: Path, abi: Path, source: Path, backend: str) -> Receipt:
    started = time.perf_counter()
    completed = subprocess.run(
        [
            str(binary),
            str(abi),
            START,
            str(source),
            str(WORK_LIMIT),
            str(REPLAY_DEPTH),
            str(RESULT_LIMIT),
        ],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=120,
    )
    elapsed = time.perf_counter() - started
    if completed.returncode != 0:
        detail = completed.stderr.decode("utf-8", errors="replace").strip()
        raise ProbeFailure(f"{backend} scale process failed: {detail}")
    header = f"parser-pack-{backend}-stream-v1"
    fields = parse_stream(completed.stdout, header)
    digest = fields.get("forest-digest", "")
    if len(digest) != 64 or any(char not in "0123456789abcdef" for char in digest):
        raise ProbeFailure(f"{backend} omitted its canonical forest digest")
    outcome = fields.get("outcome", "")
    if outcome != "completed":
        raise ProbeFailure(f"{backend} scale outcome changed to {outcome!r}")
    result = fields.get("result")
    if fields.get("decision") != "accepted" or not result:
        raise ProbeFailure(f"{backend} scale semantics did not complete")
    if integer(fields, "source-passes") != 1:
        raise ProbeFailure(f"{backend} did not preserve one source pass")
    stack = integer(fields, "stack-nodes") if backend == "glr" else None
    if stack is not None and stack == 0:
        raise ProbeFailure("GLR omitted shared-stack accounting")
    return Receipt(
        elapsed_seconds=elapsed,
        outcome=outcome,
        digest=digest,
        result_digest=sha256(result.encode("utf-8")).hexdigest(),
        input_bytes=integer(fields, "input-bytes"),
        nodes=integer(fields, "forest-nodes"),
        choices=integer(fields, "forest-choices"),
        roots=integer(fields, "forest-roots"),
        work=integer(fields, "descriptors" if backend == "gll" else "facts"),
        graph=integer(fields, "gss-nodes" if backend == "gll" else "graph-nodes"),
        stack=stack,
    )


def log_slope(xs: list[int], ys: list[int | float]) -> float:
    if len(xs) != len(ys) or len(xs) < 2 or any(value <= 0 for value in ys):
        raise ProbeFailure("cannot fit a structural scale exponent")
    log_x = [math.log(value) for value in xs]
    log_y = [math.log(value) for value in ys]
    mean_x = sum(log_x) / len(log_x)
    mean_y = sum(log_y) / len(log_y)
    denominator = sum((value - mean_x) ** 2 for value in log_x)
    if denominator == 0:
        raise ProbeFailure("scale ladder has no input variation")
    return sum(
        (x_value - mean_x) * (y_value - mean_y)
        for x_value, y_value in zip(log_x, log_y, strict=True)
    ) / denominator


def pack_digest(stream: bytes) -> str:
    for line in stream.splitlines():
        if line.startswith(b"pack-digest\t"):
            digest = line.partition(b"\t")[2].decode("ascii")
            if len(digest) == 64:
                return digest
    raise ProbeFailure("exported ParserPack omitted its digest")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gll-binary", type=Path, required=True)
    parser.add_argument("--glr-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()

    gll_binary = arguments.gll_binary.resolve()
    glr_binary = arguments.glr_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    stream = export_stream(petta_root, CASES["petta-splitter"])
    gll_receipts: list[Receipt] = []
    glr_receipts: list[Receipt] = []

    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-wide-scale-", dir=ROOT / "runtime" / "bootstrap"
    ) as raw:
        directory = Path(raw)
        abi = directory / "splitter.abi"
        abi.write_bytes(stream)
        for token_count in TOKEN_COUNTS:
            source = directory / f"wide-{token_count}.metta"
            source.write_text(
                "(" + " ".join(["word"] * token_count) + ")\n",
                encoding="utf-8",
            )
            gll = run_parser(gll_binary, abi, source, "gll")
            glr = run_parser(glr_binary, abi, source, "glr")
            if gll.input_bytes != source.stat().st_size or glr.input_bytes != gll.input_bytes:
                raise ProbeFailure("scale receipt changed the input byte extent")
            if (
                gll.digest != glr.digest
                or gll.result_digest != glr.result_digest
                or gll.nodes != glr.nodes
                or gll.choices != glr.choices
                or gll.roots != glr.roots
            ):
                raise ProbeFailure("GLL and GLR scale forests differ")
            gll_receipts.append(gll)
            glr_receipts.append(glr)
            print(
                "case"
                f"\t{gll.input_bytes}"
                f"\t{gll.elapsed_seconds:.6f}"
                f"\t{glr.elapsed_seconds:.6f}"
                f"\t{gll.work}"
                f"\t{gll.graph}"
                f"\t{glr.work}"
                f"\t{glr.graph}"
                f"\t{glr.stack}"
                f"\t{gll.nodes}"
                f"\t{gll.choices}"
                f"\t{gll.digest}"
                f"\t{gll.result_digest}"
            )

    inputs = [receipt.input_bytes for receipt in gll_receipts]
    slopes = {
        "gll-wall": log_slope(inputs, [receipt.elapsed_seconds for receipt in gll_receipts]),
        "glr-wall": log_slope(inputs, [receipt.elapsed_seconds for receipt in glr_receipts]),
        "gll-work": log_slope(inputs, [receipt.work for receipt in gll_receipts]),
        "gll-graph": log_slope(inputs, [receipt.graph for receipt in gll_receipts]),
        "glr-work": log_slope(inputs, [receipt.work for receipt in glr_receipts]),
        "glr-graph": log_slope(inputs, [receipt.graph for receipt in glr_receipts]),
        "glr-stack": log_slope(inputs, [receipt.stack or 0 for receipt in glr_receipts]),
        "forest-nodes": log_slope(inputs, [receipt.nodes for receipt in gll_receipts]),
        "forest-choices": log_slope(inputs, [receipt.choices for receipt in gll_receipts]),
    }
    for name, slope in slopes.items():
        print(f"slope\t{name}\t{slope:.6f}")
    structural = [
        slopes[name]
        for name in (
            "gll-work",
            "gll-graph",
            "glr-work",
            "glr-graph",
            "glr-stack",
            "forest-nodes",
            "forest-choices",
        )
    ]
    if any(slope > MAX_STRUCTURAL_SLOPE for slope in structural):
        raise ProbeFailure("a parser structural scale exponent became superlinear")
    print(f"pack-digest\t{pack_digest(stream)}")
    print(
        f"(ParserPackWideScaleV1Summary {len(TOKEN_COUNTS)} "
        f"{len(gll_receipts)} {len(glr_receipts)} {len(structural)} 0)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ProbeFailure, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1) from error
