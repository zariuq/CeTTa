#!/usr/bin/env python3
"""Seal .rho/.mrho recognition and scoped abstract-syntax convergence."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
from pathlib import Path
import subprocess
import tempfile

from gslt2parse_schema_v1 import SExpr, StringLiteral, Symbol, parse_sexprs, render
from rho_term_projection_plan_v1 import compile_plans
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_parser_pack_gll_v1 import run_native
from test_rhocalc_reader_authority_v1 import translate


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
EXPECTED_MATRIX_DIGEST = "cdcb3e9b6d531f68429b8330f870f933ddf5083ad7ad7e0c5d1f908948110297"


class GateFailure(RuntimeError):
    pass


@dataclass(frozen=True)
class SurfaceCase:
    label: str
    rho: str
    mrho: str
    decision: str


CASES = (
    SurfaceCase("strict-nil", "0", "rho:nil", "accepted"),
    SurfaceCase(
        "strict-send",
        "@{0}!(0)",
        "(rho:send (rho:quote rho:nil) rho:nil)",
        "accepted",
    ),
    SurfaceCase(
        "strict-receive-parallel",
        "for ($x <- @{0}) {*$x} | @{0}!(0)",
        "(rho:par (rho:recv (rho:quote rho:nil) $x (rho:drop $x)) "
        "(rho:send (rho:quote rho:nil) rho:nil))",
        "accepted",
    ),
    SurfaceCase(
        "strict-bare-bound-name",
        "for (x <- @{0}) {*x}",
        "(rho:recv (rho:quote rho:nil) $x (rho:drop $x))",
        "accepted",
    ),
    SurfaceCase(
        "strict-comments-and-layout",
        "/* block */ @{0} ! ( // line\n 0 )",
        "; layout\n(rho:send (rho:quote rho:nil) rho:nil)",
        "accepted",
    ),
    SurfaceCase(
        "strict-shadowing",
        "for ($x <- @{0}) {for ($x <- $x) {*$x} | $x!(0)} | "
        "@{0}!(@{0}!(0))",
        "(rho:par (rho:recv (rho:quote rho:nil) $x "
        "(rho:par (rho:recv $x $x_rho1 (rho:drop $x_rho1)) "
        "(rho:send $x rho:nil))) "
        "(rho:send (rho:quote rho:nil) "
        "(rho:send (rho:quote rho:nil) rho:nil)))",
        "accepted",
    ),
    SurfaceCase(
        "strict-quote-seals-outer-binder",
        "for ($x <- @{0}) {@{*$x}!(0)} | @{0}!(@{0}!(0))",
        "(rho:par (rho:recv (rho:quote rho:nil) $x_rho1 "
        "(rho:send (rho:quote (rho:drop $x)) rho:nil)) "
        "(rho:send (rho:quote rho:nil) "
        "(rho:send (rho:quote rho:nil) rho:nil)))",
        "accepted",
    ),
    SurfaceCase(
        "strict-quote-inner-binder-restores-scope",
        "for ($x <- @{0}) {@{for ($y <- @{0}) {*$y}}!(0) | $x!(0)} | "
        "@{0}!(@{0}!(0))",
        "(rho:par (rho:recv (rho:quote rho:nil) $x "
        "(rho:par (rho:send (rho:quote "
        "(rho:recv (rho:quote rho:nil) $y (rho:drop $y))) rho:nil) "
        "(rho:send $x rho:nil))) "
        "(rho:send (rho:quote rho:nil) "
        "(rho:send (rho:quote rho:nil) rho:nil)))",
        "accepted",
    ),
    SurfaceCase(
        "strict-open-name-variable",
        "$x!(0)",
        "(rho:send $x rho:nil)",
        "accepted",
    ),
    SurfaceCase(
        "reject-name-not-process",
        "@{0}",
        "(rho:quote rho:nil)",
        "rejected",
    ),
    SurfaceCase(
        "reject-trailing-or-wrong-arity",
        "0 trailing",
        "(rho:send (rho:quote rho:nil) rho:nil rho:nil)",
        "rejected",
    ),
    SurfaceCase(
        "reject-malformed-binder",
        "for ($ <- @{0}) {0}",
        "(rho:recv (rho:quote rho:nil) binder rho:nil)",
        "rejected",
    ),
    SurfaceCase(
        "reject-unknown-form",
        "for?!(0)",
        "(rho:fresh $x rho:nil)",
        "rejected",
    ),
)


def is_symbol(term: SExpr, spelling: str) -> bool:
    return isinstance(term, Symbol) and term.text == spelling


def tagged(term: SExpr, tag: str, length: int) -> bool:
    return (
        isinstance(term, tuple)
        and len(term) == length
        and is_symbol(term[0], tag)
    )


def node_parts(term: SExpr, context: str) -> tuple[str, SExpr]:
    if not tagged(term, "node", 3) or not isinstance(term[1], Symbol):
        raise GateFailure(f"{context}: expected a node, got {render(term)}")
    return term[1].text, term[2]


def unpack_fixed(term: SExpr, count: int, context: str) -> list[SExpr]:
    if count == 1:
        return [term]
    if not isinstance(term, tuple) or len(term) != 3:
        raise GateFailure(f"{context}: malformed {count}-field sequence")
    if not (is_symbol(term[0], "pair") or is_symbol(term[0], "cons")):
        raise GateFailure(f"{context}: malformed {count}-field sequence")
    return [term[1], *unpack_fixed(term[2], count - 1, context)]


def unpack_list(term: SExpr, context: str) -> list[SExpr]:
    values: list[SExpr] = []
    cursor = term
    while not is_symbol(cursor, "nil"):
        if not isinstance(cursor, tuple) or len(cursor) != 3:
            raise GateFailure(f"{context}: malformed proper sequence")
        if not (is_symbol(cursor[0], "pair") or is_symbol(cursor[0], "cons")):
            raise GateFailure(f"{context}: malformed proper sequence")
        values.append(cursor[1])
        cursor = cursor[2]
    return values


def identifier_text(term: SExpr, context: str) -> str:
    if is_symbol(term, "nil"):
        return ""
    if isinstance(term, tuple) and len(term) == 2 and is_symbol(term[0], "cp"):
        if not isinstance(term[1], int):
            raise GateFailure(f"{context}: noninteger codepoint")
        try:
            return chr(term[1])
        except ValueError as error:
            raise GateFailure(f"{context}: invalid codepoint") from error
    if isinstance(term, tuple) and len(term) == 3:
        if is_symbol(term[0], "node") and isinstance(term[1], Symbol):
            if term[1].text not in {"identifier", "variable", "bare-name", "binder"}:
                raise GateFailure(f"{context}: unexpected identifier wrapper")
            return identifier_text(term[2], context)
        if is_symbol(term[0], "pair") or is_symbol(term[0], "cons"):
            return identifier_text(term[1], context) + identifier_text(term[2], context)
    raise GateFailure(f"{context}: malformed identifier {render(term)}")


PZERO: SExpr = (Symbol("PZero"),)


def make_parallel(items: list[SExpr]) -> SExpr:
    flattened: list[SExpr] = []
    for item in items:
        if item == PZERO:
            continue
        if (
            isinstance(item, tuple)
            and len(item) == 2
            and is_symbol(item[0], "PPar")
            and isinstance(item[1], tuple)
            and item[1]
            and is_symbol(item[1][0], "rho-bag")
        ):
            flattened.extend(item[1][1:])
        else:
            flattened.append(item)
    flattened.sort(key=render)
    if not flattened:
        return PZERO
    if len(flattened) == 1:
        return flattened[0]
    return (Symbol("PPar"), (Symbol("rho-bag"), *flattened))


def normalize_name(term: SExpr, env: tuple[str, ...], context: str) -> SExpr:
    label, payload = node_parts(term, context)
    if label == "NQuote":
        return (Symbol("NQuote"), normalize_process(payload, (), context + "/quote"))
    if label not in {"variable", "bare-name"}:
        raise GateFailure(f"{context}: unexpected name node {label}")
    name = identifier_text(term, context)
    if name in env:
        return (Symbol("BVar"), env.index(name))
    return (Symbol("FVar"), StringLiteral(name))


def normalize_process(term: SExpr, env: tuple[str, ...], context: str) -> SExpr:
    label, payload = node_parts(term, context)
    if label == "PZero":
        return PZERO
    if label == "PDrop":
        return (Symbol("PDrop"), normalize_name(payload, env, context + "/drop"))
    if label == "POutput":
        name, continuation = unpack_fixed(payload, 2, context + "/output")
        return (
            Symbol("POutput"),
            normalize_name(name, env, context + "/channel"),
            normalize_process(continuation, env, context + "/continuation"),
        )
    if label == "PInput":
        first, second, body = unpack_fixed(payload, 3, context + "/input")
        if context.startswith("rho/"):
            binder, channel = first, second
        elif context.startswith("mrho/") or context.startswith("native-mrho/"):
            channel, binder = first, second
        else:
            raise GateFailure(f"{context}: unknown presentation adapter")
        binder_name = identifier_text(binder, context + "/binder")
        return (
            Symbol("PInput"),
            normalize_name(channel, env, context + "/channel"),
            (
                Symbol("rho-lambda"),
                normalize_process(body, (binder_name, *env), context + "/body"),
            ),
        )
    if label == "PPar":
        return make_parallel(
            [
                normalize_process(child, env, context + "/par")
                for child in unpack_list(payload, context + "/parallel")
            ]
        )
    raise GateFailure(f"{context}: unexpected process node {label}")


def normalize_result(result: str, surface: str, label: str) -> str:
    forms = parse_sexprs(result, source=f"{surface}/{label}")
    if len(forms) != 1 or not tagged(forms[0], "result", 3):
        raise GateFailure(f"{surface}/{label}: malformed semantic result")
    form = forms[0]
    assert isinstance(form, tuple)
    if not is_symbol(form[2], "nil"):
        raise GateFailure(f"{surface}/{label}: parser left trailing input")
    document_label, process = node_parts(form[1], f"{surface}/{label}/document")
    if document_label != "document":
        raise GateFailure(f"{surface}/{label}: missing document node")
    return render(normalize_process(process, (), f"{surface}/{label}"))


def oracle_rows(petta_root: Path) -> dict[tuple[str, str], dict[str, object]]:
    oracle = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "rhocalc_surface_convergence_oracle_v1.pl"
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
        raise GateFailure("PeTTa surface oracle failed: " + completed.stderr.strip())
    lines = completed.stdout.splitlines()
    if (
        not lines
        or lines[0] != "rhocalc-surface-convergence-oracle-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("PeTTa surface oracle returned malformed framing")
    rows: dict[tuple[str, str], dict[str, object]] = {}
    current: dict[str, object] | None = None
    key: tuple[str, str] | None = None
    for line in lines[1:-1]:
        fields = line.split("\t")
        if fields[0] == "case" and len(fields) == 5 and current is None:
            key = (fields[1], fields[2])
            if key in rows:
                raise GateFailure(f"duplicate PeTTa surface row: {key}")
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
            raise GateFailure(f"malformed PeTTa surface record: {line!r}")
    if current is not None:
        raise GateFailure("PeTTa surface oracle returned an unterminated row")
    return rows


def require_exact_backend(
    surface: str,
    label: str,
    backend: str,
    observed: dict[str, object],
    oracle: dict[str, object],
    source: str,
) -> None:
    for field in ("decision", "forest-digest", "results"):
        if observed.get(field) != oracle[field]:
            raise GateFailure(
                f"{surface}/{label}: {backend} {field} differs from PeTTa"
            )
    payload = source.encode("utf-8")
    for field, expected in (
        ("source-passes", "1"),
        ("decoded-bytes", str(len(payload))),
        ("input-bytes", str(len(payload))),
        ("input-scalars", str(len(source))),
    ):
        if observed.get(field) != expected:
            raise GateFailure(
                f"{surface}/{label}: {backend} one-pass receipt changed"
            )


def require_native_replay(
    row: dict[str, object], source: str, backend: str, label: str
) -> None:
    if row.get("decision") != "accepted":
        raise GateFailure(f"{label}: translated .mrho rejected by {backend}")
    payload = source.encode("utf-8")
    for field, expected in (
        ("source-passes", "1"),
        ("decoded-bytes", str(len(payload))),
        ("input-bytes", str(len(payload))),
        ("input-scalars", str(len(source))),
    ):
        if row.get(field) != expected:
            raise GateFailure(f"{label}: translated {backend} receipt changed")


def row_text(surface: str, label: str, row: dict[str, object]) -> str:
    results = row["results"]
    assert isinstance(results, tuple)
    return (
        f"{surface}/{label}/{row['decision']}/{row['forest-digest']}:"
        + "|".join(results)
    )


def run_projection(
    binary: Path,
    plan_path: Path,
    result_path: Path,
    label: str,
) -> dict[str, str]:
    completed = subprocess.run(
        [str(binary), str(plan_path), str(result_path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=60,
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"{label}: native term projection failed: {completed.stderr.strip()}"
        )
    lines = completed.stdout.splitlines()
    if (
        not lines
        or lines[0] != "parser-term-projection-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure(f"{label}: malformed term-projection framing")
    fields: dict[str, str] = {}
    for line in lines[1:-1]:
        parts = line.split("\t", 1)
        if len(parts) != 2 or parts[0] in fields:
            raise GateFailure(f"{label}: malformed term-projection field")
        fields[parts[0]] = parts[1]
    if set(fields) != {"projected", "alpha-digest", "spelling-digest"}:
        raise GateFailure(f"{label}: incomplete term-projection receipt")
    for digest_field in ("alpha-digest", "spelling-digest"):
        value = fields[digest_field]
        if len(value) != 64 or any(ch not in "0123456789abcdef" for ch in value):
            raise GateFailure(f"{label}: malformed {digest_field}")
    return fields


def check(
    cetta_binary: Path,
    gll_binary: Path,
    glr_binary: Path,
    projection_binary: Path,
    petta_root: Path,
) -> tuple[int, int, int, int, int, int, int, int, str]:
    expected = oracle_rows(petta_root)
    expected_keys = {
        (surface, case.label) for case in CASES for surface in ("rho", "mrho")
    }
    if set(expected) != expected_keys:
        raise GateFailure("PeTTa surface oracle case inventory changed")

    gll_exact = 0
    glr_exact = 0
    converged = 0
    native_converged = 0
    projected_converged = 0
    native_projected = 0
    rejected_pairs = 0
    records: list[str] = []
    with tempfile.TemporaryDirectory(prefix="gslt2parse-rho-surfaces-") as raw:
        temp = Path(raw)
        pack_paths: dict[str, Path] = {}
        projection_paths: dict[str, Path] = {}
        starts: dict[str, str] = {}
        for surface, plan in compile_plans().items():
            plan_path = temp / f"{surface}.projection"
            plan_path.write_text(render(plan), encoding="utf-8")
            projection_paths[surface] = plan_path
        for surface, abi_label in (("rho", "rho"), ("mrho", "rho-mrho")):
            abi_case = ABI_CASES[abi_label]
            pack_path = temp / f"{surface}.abi"
            pack_path.write_bytes(export_stream(petta_root, abi_case))
            pack_paths[surface] = pack_path
            starts[surface] = f"(pp-def {abi_case['start']})"

        for index, case in enumerate(CASES):
            normalized: dict[str, str] = {}
            projections: dict[str, dict[str, str]] = {}
            for surface in ("rho", "mrho"):
                source = getattr(case, surface)
                input_path = temp / f"input-{index}-{surface}.utf8"
                input_path.write_bytes(source.encode("utf-8"))
                gll = run_native(
                    gll_binary,
                    pack_paths[surface],
                    starts[surface],
                    input_path,
                )
                glr = run_native(
                    glr_binary,
                    pack_paths[surface],
                    starts[surface],
                    input_path,
                    protocol="parser-pack-glr-stream-v1",
                    backend="GLR",
                )
                oracle = expected[(surface, case.label)]
                if oracle["decision"] != case.decision:
                    raise GateFailure(f"{surface}/{case.label}: oracle decision changed")
                require_exact_backend(
                    surface, case.label, "GLL", gll, oracle, source
                )
                require_exact_backend(
                    surface, case.label, "GLR", glr, oracle, source
                )
                gll_exact += 1
                glr_exact += 1
                records.append(row_text(surface, case.label, oracle))
                if case.decision == "accepted":
                    results = oracle["results"]
                    assert isinstance(results, tuple)
                    if len(results) != 1:
                        raise GateFailure(
                            f"{surface}/{case.label}: expected one semantic result"
                        )
                    normalized[surface] = normalize_result(
                        results[0], surface, case.label
                    )
                    result_path = temp / f"result-{index}-{surface}.term"
                    result_path.write_text(results[0], encoding="utf-8")
                    projections[surface] = run_projection(
                        projection_binary,
                        projection_paths[surface],
                        result_path,
                        f"{surface}/{case.label}",
                    )
                elif oracle["results"]:
                    raise GateFailure(
                        f"{surface}/{case.label}: rejected input has semantic results"
                    )

            if case.decision == "rejected":
                translate(
                    cetta_binary,
                    "strict-core",
                    "rho",
                    "mrho",
                    case.rho,
                    expect_success=False,
                )
                rejected_pairs += 1
                continue

            if normalized["rho"] != normalized["mrho"]:
                raise GateFailure(
                    f"{case.label}: .rho/.mrho scoped abstract terms differ: "
                    f"{normalized['rho']} != {normalized['mrho']}"
                )
            if (
                projections["rho"]["alpha-digest"]
                != projections["mrho"]["alpha-digest"]
            ):
                raise GateFailure(
                    f"{case.label}: .rho/.mrho projected Atom graphs differ"
                )
            converged += 1
            projected_converged += 1
            records.append(f"semantic/{case.label}/{normalized['rho']}")
            records.append(
                f"projection/{case.label}/"
                f"{projections['rho']['alpha-digest']}"
            )

            translated = translate(
                cetta_binary,
                "strict-core",
                "rho",
                "mrho",
                case.rho,
                expect_success=True,
            )
            translated_path = temp / f"input-{index}-native.mrho"
            translated_path.write_bytes(translated.encode("utf-8"))
            translated_gll = run_native(
                gll_binary,
                pack_paths["mrho"],
                starts["mrho"],
                translated_path,
            )
            translated_glr = run_native(
                glr_binary,
                pack_paths["mrho"],
                starts["mrho"],
                translated_path,
                protocol="parser-pack-glr-stream-v1",
                backend="GLR",
            )
            require_native_replay(
                translated_gll, translated, "GLL", case.label
            )
            require_native_replay(
                translated_glr, translated, "GLR", case.label
            )
            for field in ("decision", "forest-digest", "results"):
                if translated_gll.get(field) != translated_glr.get(field):
                    raise GateFailure(
                        f"{case.label}: translated GLL/GLR {field} differs"
                    )
            translated_results = translated_gll["results"]
            assert isinstance(translated_results, tuple)
            if len(translated_results) != 1:
                raise GateFailure(
                    f"{case.label}: translated .mrho has non-unique semantics"
                )
            native_normalized = normalize_result(
                translated_results[0], "native-mrho", case.label
            )
            if native_normalized != normalized["rho"]:
                raise GateFailure(
                    f"{case.label}: native translation changed the abstract term"
                )
            native_result_path = temp / f"result-{index}-native.term"
            native_result_path.write_text(
                translated_results[0], encoding="utf-8"
            )
            native_projection = run_projection(
                projection_binary,
                projection_paths["mrho"],
                native_result_path,
                f"native/{case.label}",
            )
            if (
                native_projection["alpha-digest"]
                != projections["rho"]["alpha-digest"]
            ):
                raise GateFailure(
                    f"{case.label}: native translation projects differently"
                )
            native_converged += 1
            native_projected += 1
            records.append(f"native/{case.label}/{native_normalized}")
            records.append(
                f"native-projection/{case.label}/"
                f"{native_projection['alpha-digest']}"
            )

    digest = hashlib.sha256("\n".join(records).encode("utf-8")).hexdigest()
    if EXPECTED_MATRIX_DIGEST and digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(f"rho surface convergence matrix changed: {digest}")
    return (
        len(CASES),
        gll_exact,
        glr_exact,
        converged,
        native_converged,
        projected_converged,
        native_projected,
        rejected_pairs,
        digest,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta-binary", type=Path, required=True)
    parser.add_argument("--gll-binary", type=Path, required=True)
    parser.add_argument("--glr-binary", type=Path, required=True)
    parser.add_argument("--projection-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()
    binaries = (
        arguments.cetta_binary,
        arguments.gll_binary,
        arguments.glr_binary,
        arguments.projection_binary,
    )
    if any(not binary.resolve().is_file() for binary in binaries):
        raise GateFailure("a required native binary is absent")
    summary = check(
        arguments.cetta_binary.resolve(),
        arguments.gll_binary.resolve(),
        arguments.glr_binary.resolve(),
        arguments.projection_binary.resolve(),
        arguments.petta_root.resolve(),
    )
    print(
        "(RhoSurfaceConvergenceV1Summary "
        + " ".join(str(value) for value in summary)
        + " 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
