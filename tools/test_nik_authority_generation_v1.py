#!/usr/bin/env python3
"""Determinism and fail-closed mutation gates for Prime's NIK catalog."""

from __future__ import annotations

import argparse
from hashlib import sha256
from pathlib import Path
import subprocess
import sys
import tempfile

import gslt2parse_schema_v1 as sx


def generate(
    generator: Path,
    catalog: Path,
    semantic: Path,
    header: Path,
    source: Path,
    symbol: str,
    header_include: str,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(generator),
            "--catalog",
            str(catalog),
            "--semantic",
            str(semantic),
            "--header",
            str(header),
            "--source",
            str(source),
            "--symbol",
            symbol,
            "--header-include",
            header_include,
        ],
        text=True,
        capture_output=True,
        check=False,
    )


def write_form(path: Path, value: sx.SExpr) -> None:
    path.write_text(sx.render(value) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--semantic", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--header-include", required=True)
    args = parser.parse_args()

    forms = sx.parse_sexprs(
        args.catalog.read_text(encoding="utf-8"), source=str(args.catalog)
    )
    if len(forms) != 1 or not isinstance(forms[0], tuple) or len(forms[0]) < 3:
        raise SystemExit("catalog fixture is not plural")
    root = forms[0]

    with tempfile.TemporaryDirectory(prefix="nik-authority-generation-") as raw:
        work = Path(raw)
        semantic = work / "runtime.metta"
        header = work / "runtime.h"
        source = work / "runtime.c"
        result = generate(
            args.generator,
            args.catalog,
            semantic,
            header,
            source,
            args.symbol,
            args.header_include,
        )
        if result.returncode != 0:
            raise SystemExit(result.stderr or result.stdout)
        comparisons = (
            (semantic, args.semantic),
            (header, args.header),
            (source, args.source),
        )
        for generated, checked_in in comparisons:
            if generated.read_bytes() != checked_in.read_bytes():
                raise SystemExit(f"generated artifact drifted: {checked_in.name}")

        mutations: list[tuple[str, sx.SExpr]] = []
        mutations.append(("singular", (root[0], root[1])))

        duplicate = list(root[2])
        duplicate[1] = root[1][1]
        mutations.append(("duplicate-alias", (root[0], root[1], tuple(duplicate))))

        wrong_digest = list(root[1])
        wrong_digest[4] = sx.StringLiteral("0" * 64)
        mutations.append(("wrong-digest", (root[0], tuple(wrong_digest), *root[2:])))

        duplicate_depth_presentation: sx.SExpr = (
            sx.Symbol("GPresentation"),
            sx.Symbol("LNil"),
            sx.Symbol("LNil"),
            (
                sx.Symbol("LCons"),
                (
                    sx.Symbol("GRule"),
                    sx.StringLiteral("duplicate-depth"),
                    (
                        sx.Symbol("LCons"),
                        (sx.Symbol("Formal"), sx.StringLiteral("x"), 0),
                        (
                            sx.Symbol("LCons"),
                            (sx.Symbol("Formal"), sx.StringLiteral("x"), 1),
                            sx.Symbol("LNil"),
                        ),
                    ),
                    sx.Symbol("LNil"),
                    (
                        sx.Symbol("PApp"),
                        sx.StringLiteral("J"),
                        (
                            sx.Symbol("LCons"),
                            (sx.Symbol("FVar"), sx.StringLiteral("x")),
                            (
                                sx.Symbol("LCons"),
                                (
                                    sx.Symbol("PLam"),
                                    sx.Symbol("BNone"),
                                    (sx.Symbol("FVar"), sx.StringLiteral("x")),
                                ),
                                sx.Symbol("LNil"),
                            ),
                        ),
                    ),
                ),
                sx.Symbol("LNil"),
            ),
        )
        duplicate_depth_authority = list(root[1])
        duplicate_depth_authority[4] = sx.StringLiteral(
            sha256(
                sx.render(duplicate_depth_presentation).encode("utf-8")
            ).hexdigest()
        )
        duplicate_depth_authority[5] = duplicate_depth_presentation
        mutations.append(
            (
                "duplicate-formal-across-depths",
                (root[0], tuple(duplicate_depth_authority), *root[2:]),
            )
        )

        for label, mutation in mutations:
            mutated_catalog = work / f"{label}.metta"
            write_form(mutated_catalog, mutation)
            mutation_result = generate(
                args.generator,
                mutated_catalog,
                work / f"{label}.runtime.metta",
                work / f"{label}.h",
                work / f"{label}.c",
                args.symbol,
                args.header_include,
            )
            if mutation_result.returncode == 0:
                raise SystemExit(f"NIK catalog mutation survived: {label}")

    print(
        "(NikAuthorityGenerationV1Summary deterministic=1 "
        "plural=1 mutations-killed=4)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
