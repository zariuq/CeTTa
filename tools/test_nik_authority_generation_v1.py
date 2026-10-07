#!/usr/bin/env python3
"""Determinism and fail-closed mutation gates for Prime's NIK catalog."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
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


def rewrite_first(
    value: sx.SExpr,
    predicate,
    replacement,
) -> tuple[sx.SExpr, bool]:
    if predicate(value):
        return replacement(value), True
    if not isinstance(value, tuple):
        return value, False
    rewritten: list[sx.SExpr] = []
    changed = False
    for item in value:
        if changed:
            rewritten.append(item)
            continue
        next_item, changed = rewrite_first(item, predicate, replacement)
        rewritten.append(next_item)
    return tuple(rewritten), changed


def authority_with_presentation(
    authority: tuple[sx.SExpr, ...], presentation: sx.SExpr
) -> tuple[sx.SExpr, ...]:
    updated = list(authority)
    updated[4] = sx.StringLiteral(
        sha256(sx.render(presentation).encode("utf-8")).hexdigest()
    )
    updated[5] = presentation
    return tuple(updated)


def count_tag(value: sx.SExpr, tag: str) -> int:
    if not isinstance(value, tuple):
        return 0
    here = int(
        bool(value) and isinstance(value[0], sx.Symbol) and
        value[0].text == tag
    )
    return here + sum(count_tag(item, tag) for item in value)


def check_export_comparison(catalog: Path, root: tuple, work: Path) -> int:
    checker = Path(__file__).with_name("check_nik_authority_export_v1.py")
    original = catalog.read_bytes()
    cases = [("exact", original, 0, None),
             ("serialization", (sx.render(root) + "\n").encode(), 1, "serialization_only"),
             ("missing-package", root[:-1], 1, "removed"),
             ("package-order", (root[0], root[2], root[1], *root[3:]),
              1, "authority_order_changed")]
    authority = root[1]
    renamed = list(authority)
    renamed[3] = sx.StringLiteral("comparison-test-revision")
    cases.append(("revision", (root[0], tuple(renamed), *root[2:]), 1, "revision"))
    first_rule = list(authority[5][4][1])
    first_rule[1] = sx.StringLiteral("comparison-added-rule")
    for label, rules in (
        ("added_rules", (sx.Symbol("LCons"), tuple(first_rule), authority[5][4])),
        ("removed_rules", authority[5][4][2]),
    ):
        presentation = (*authority[5][:4], rules, authority[5][5])
        cases.append((label, (root[0], authority_with_presentation(authority, presentation),
                             *root[2:]), 1, label))
    for label, index, replacement in (
        ("formals", 2, lambda rule: (
            sx.Symbol("LCons"), (sx.Symbol("Formal"), sx.StringLiteral("extra"), 0),
            rule[2])),
        ("premises", 3, lambda rule: (sx.Symbol("LCons"), rule[4], rule[3])),
        ("conclusion", 4, lambda rule: (sx.Symbol("PApp"),
                                      sx.StringLiteral("K"), sx.Symbol("LNil"))),
        ("side_conditions", 5, lambda rule: sx.Symbol("LNil")),
    ):
        presentation, changed = rewrite_first(
            authority[5],
            lambda value: isinstance(value, tuple) and len(value) == 6
            and value[0] == sx.Symbol("GRuleV1")
            and (index != 5 or value[5] != sx.Symbol("LNil")),
            lambda rule: (*rule[:index], replacement(rule), *rule[index + 1:]),
        )
        if not changed:
            raise SystemExit(f"export comparison mutation not exercised: {label}")
        cases.append((label, (root[0], authority_with_presentation(authority, presentation),
                             *root[2:]), 1, label))
    specimen = list(authority)
    specimen[6] = (sx.Symbol("positive"), authority[6][1], sx.StringLiteral(""))
    cases.append(("proof", (root[0], tuple(specimen), *root[2:]), 1, "positive_proof"))
    stale = list(authority)
    stale[4] = sx.StringLiteral("0" * 64)
    cases.append(("stale-digest", (root[0], tuple(stale), *root[2:]), 2, "invalid"))
    for label, mutation, expected_status, diagnostic in cases:
        exported = work / f"export-{label}.metta"
        if isinstance(mutation, bytes):
            exported.write_bytes(mutation)
        else:
            write_form(exported, mutation)
        report_path = work / f"export-{label}.json"
        result = subprocess.run(
            [sys.executable, str(checker), "--checked", str(catalog),
             "--exported", str(exported), "--report", str(report_path)],
            text=True, capture_output=True, check=False,
        )
        if result.returncode != expected_status:
            raise SystemExit(f"export comparison {label}: {result.stderr or result.stdout}")
        report = json.loads(result.stdout)
        if json.loads(report_path.read_text()) != report:
            raise SystemExit(f"export comparison report differs from stdout: {label}")
        if diagnostic in ("formals", "premises", "conclusion", "side_conditions"):
            diagnosed = any(diagnostic in rule["fields"]
                            for change in report["changes"]
                            for rule in change.get("changed_rules", []))
        elif diagnostic in ("revision", "positive_proof"):
            diagnosed = any(diagnostic in change.get("fields", [])
                            for change in report["changes"])
        elif diagnostic in ("added_rules", "removed_rules"):
            diagnosed = any(change.get(diagnostic) for change in report["changes"])
        elif diagnostic == "removed":
            diagnosed = any(change.get("change") == "removed"
                            for change in report["changes"])
        elif diagnostic == "invalid":
            diagnosed = report["status"] == "invalid" and report["input"] == "exported"
        elif diagnostic:
            diagnosed = report[diagnostic]
        else:
            diagnosed = report["status"] == "exact" and not report["changes"]
        if not diagnosed:
            raise SystemExit(f"export comparison lost its diagnostic: {label}")
    invalid = work / "export-stale-digest.metta"
    result = subprocess.run(
        [sys.executable, str(checker), "--checked", str(invalid),
         "--exported", str(invalid)], text=True, capture_output=True, check=False,
    )
    report = json.loads(result.stdout)
    if result.returncode != 2 or report["status"] != "invalid" or report["input"] != "checked":
        raise SystemExit("equal invalid catalogs incorrectly passed exact comparison")
    return len(cases) + 1


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

    evidence_parent = Path("runtime/bootstrap")
    evidence_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nik-authority-generation-",
                                     dir=evidence_parent) as raw:
        work = Path(raw)
        export_controls = check_export_comparison(args.catalog, root, work)
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
        expected_support_checks = count_tag(root, "Formal")
        actual_support_checks = semantic.read_text(encoding="utf-8").count(
            "(nik-pattern-supported-at "
        )
        if actual_support_checks != expected_support_checks:
            raise SystemExit(
                "generated runtime does not validate every formal at its "
                f"binder support: expected {expected_support_checks}, "
                f"found {actual_support_checks}"
            )

        mutations: list[tuple[str, sx.SExpr]] = []
        mutations.append(("singular", (root[0], root[1])))

        duplicate = list(root[2])
        duplicate[1] = root[1][1]
        mutations.append(("duplicate-alias", (root[0], root[1], tuple(duplicate))))

        wrong_digest = list(root[1])
        wrong_digest[4] = sx.StringLiteral("0" * 64)
        mutations.append(("wrong-digest", (root[0], tuple(wrong_digest), *root[2:])))

        duplicate_depth_presentation: sx.SExpr = (
            sx.Symbol("GPresentationV1"),
            1,
            sx.Symbol("LNil"),
            sx.Symbol("LNil"),
            (
                sx.Symbol("LCons"),
                (
                    sx.Symbol("GRuleV1"),
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
                    sx.Symbol("LNil"),
                ),
                sx.Symbol("LNil"),
            ),
            sx.Symbol("GNoConversion"),
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

        dtt_authority = root[1]
        if not isinstance(dtt_authority, tuple):
            raise SystemExit("DTT authority is malformed")
        dtt_presentation = dtt_authority[5]

        wrong_support, changed = rewrite_first(
            dtt_presentation,
            lambda value: isinstance(value, tuple)
            and value
            == (
                sx.Symbol("Formal"),
                sx.StringLiteral("u"),
                1,
            ),
            lambda _value: (
                sx.Symbol("Formal"),
                sx.StringLiteral("u"),
                0,
            ),
        )
        if not changed:
            raise SystemExit("DTT beta formal was not found")
        mutations.append(
            (
                "side-condition-support-depth",
                (
                    root[0],
                    authority_with_presentation(dtt_authority, wrong_support),
                    *root[2:],
                ),
            )
        )

        wrong_position, changed = rewrite_first(
            dtt_presentation,
            lambda value: isinstance(value, tuple)
            and value
            == (
                sx.Symbol("GExplicitSubstitution"),
                0,
                0,
                1,
                2,
            ),
            lambda _value: (
                sx.Symbol("GExplicitSubstitution"),
                0,
                0,
                1,
                9,
            ),
        )
        if not changed:
            raise SystemExit("DTT beta side condition was not found")
        mutations.append(
            (
                "side-condition-argument-position",
                (
                    root[0],
                    authority_with_presentation(dtt_authority, wrong_position),
                    *root[2:],
                ),
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
        f"plural=1 mutations-killed=6 export-controls={export_controls})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
