#!/usr/bin/env python3
"""Structural gate for rhoCalc's parser-facing abstract syntax projection."""

from __future__ import annotations

from dataclasses import dataclass
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

import gslt2parse_schema_v1 as schema
from rho_term_projection_plan_v1 import PlanError, compile_plans


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
ABSTRACT = PRESENTATIONS / "shared" / "rho_abstract_syntax_v1.metta"
RUNTIME_ABI = PRESENTATIONS / "shared" / "rho_runtime_term_abi_v1.metta"
STRICT_RHO = PRESENTATIONS / "languages" / "cetta_rho_reader_v1.metta"
STRICT_MRHO = PRESENTATIONS / "languages" / "cetta_rho_mrho_reader_v1.metta"
LEAN_EXPORTER = ROOT / "tests" / "support" / "export_rho_abstract_syntax_v1.lean"
EXPECTED_ORIGIN_COMMIT = "3513b71898622ee653feac2e3648561db0d400ab"
EXPECTED_ORIGIN_SHA256 = "4aed44b85b147e0142bedf8824cd51d4da4c43562f756fcf75a1bc92cd07386a"
EXPECTED_PACKAGE_DIGEST = "ee90a8366ce08bd38cc04a92ffb81d633031e1f95aa646db87b8fe3e8c708563"


class GateFailure(RuntimeError):
    pass


@dataclass(frozen=True, slots=True)
class RuntimeShape:
    constructor: str
    kind: str
    head: str
    child_count: int | str
    binder_index: int | str


def symbol(term: schema.SExpr, context: str) -> str:
    if not isinstance(term, schema.Symbol):
        raise GateFailure(f"{context}: expected symbol, found {schema.render(term)}")
    return term.text


def form(term: schema.SExpr, context: str) -> tuple[schema.SExpr, ...]:
    if not isinstance(term, tuple):
        raise GateFailure(f"{context}: expected form, found {schema.render(term)}")
    return term


def facts(
    presentation: schema.Presentation, relation: str, arity: int
) -> list[tuple[schema.SExpr, ...]]:
    found: list[tuple[schema.SExpr, ...]] = []
    for rule in presentation.rules:
        head = form(rule.head, f"rule {rule.name}")
        if head and isinstance(head[0], schema.Symbol) and head[0].text == relation:
            if len(head) != arity + 1:
                raise GateFailure(f"{relation}: wrong fact arity in {rule.name}")
            if rule.body:
                raise GateFailure(f"{relation}: non-fact rule {rule.name}")
            found.append(head[1:])
    return found


def decode_args(term: schema.SExpr) -> list[schema.SExpr]:
    if isinstance(term, schema.Symbol) and term.text == "rho-no-args":
        return []
    items = form(term, "constructor argument spine")
    if len(items) != 3 or symbol(items[0], "argument spine head") != "rho-args":
        raise GateFailure("constructor arguments are not a canonical rho-args spine")
    return [items[1], *decode_args(items[2])]


def term_head(term: schema.SExpr) -> str | None:
    if not isinstance(term, tuple) or not term or not isinstance(term[0], schema.Symbol):
        return None
    return term[0].text


def load_contract(
    abstract_path: Path = ABSTRACT,
    runtime_path: Path = RUNTIME_ABI,
) -> tuple[
    schema.Presentation,
    schema.Presentation,
    dict[str, tuple[str, list[schema.SExpr]]],
    dict[str, RuntimeShape],
]:
    admitted = schema.admit([abstract_path, runtime_path])
    abstract, runtime = admitted

    languages = facts(abstract, "rho-language", 1)
    if languages != [(schema.Symbol("RhoCalc"),)]:
        raise GateFailure("abstract projection does not name exactly RhoCalc")

    sort_rows = facts(abstract, "rho-sort", 2)
    sorts = {(symbol(row[0], "sort"), symbol(row[1], "carrier")) for row in sort_rows}
    if sorts != {("Proc", "ast"), ("Name", "ast")}:
        raise GateFailure(f"abstract sort inventory changed: {sorted(sorts)}")

    constructors: dict[str, tuple[str, list[schema.SExpr]]] = {}
    for raw_name, raw_result, raw_args in facts(abstract, "rho-constructor", 3):
        name = symbol(raw_name, "constructor name")
        if name in constructors:
            raise GateFailure(f"duplicate abstract constructor {name}")
        constructors[name] = (symbol(raw_result, f"{name} result sort"), decode_args(raw_args))

    expected_constructor_sorts = {
        "PZero": "Proc",
        "PDrop": "Proc",
        "NQuote": "Name",
        "PPar": "Proc",
        "POutput": "Proc",
        "PInput": "Proc",
    }
    observed_constructor_sorts = {
        name: result for name, (result, _) in constructors.items()
    }
    if observed_constructor_sorts != expected_constructor_sorts:
        raise GateFailure(
            f"abstract constructor inventory changed: {observed_constructor_sorts}"
        )

    expected_arg_counts = {
        "PZero": 0,
        "PDrop": 1,
        "NQuote": 1,
        "PPar": 1,
        "POutput": 2,
        "PInput": 2,
    }
    for name, expected in expected_arg_counts.items():
        actual = len(constructors[name][1])
        if actual != expected:
            raise GateFailure(f"{name}: expected {expected} arguments, found {actual}")

    pinput_args = constructors["PInput"][1]
    if term_head(pinput_args[0]) != "rho-simple":
        raise GateFailure("PInput channel is not a simple argument")
    if term_head(pinput_args[1]) != "rho-abstraction":
        raise GateFailure("PInput body lost its abstraction boundary")
    ppar_arg = constructors["PPar"][1][0]
    ppar_simple = form(ppar_arg, "PPar parameter")
    if len(ppar_simple) != 2 or symbol(ppar_simple[0], "PPar parameter head") != "rho-simple":
        raise GateFailure("PPar parameter is not simple collection data")
    ppar_type = form(ppar_simple[1], "PPar parameter type")
    if tuple(schema.render(item) for item in ppar_type) != (
        "rho-collection",
        "hash-bag",
        "Proc",
    ):
        raise GateFailure("PPar is not a HashBag(Proc) constructor")

    versions = facts(runtime, "rho-runtime-abi", 1)
    if versions != [(schema.Symbol("v1"),)]:
        raise GateFailure("runtime ABI version is not exactly v1")

    shapes: dict[str, RuntimeShape] = {}
    for row in facts(runtime, "rho-runtime-shape", 5):
        constructor = symbol(row[0], "runtime constructor")
        if constructor in shapes:
            raise GateFailure(f"duplicate runtime mapping for {constructor}")
        child_count: int | str
        binder_index: int | str
        child_count = row[3] if isinstance(row[3], int) else symbol(row[3], "child count")
        binder_index = row[4] if isinstance(row[4], int) else symbol(row[4], "binder index")
        shapes[constructor] = RuntimeShape(
            constructor,
            symbol(row[1], "runtime shape kind"),
            symbol(row[2], "runtime head"),
            child_count,
            binder_index,
        )

    if set(shapes) != set(constructors):
        missing = sorted(set(constructors) - set(shapes))
        foreign = sorted(set(shapes) - set(constructors))
        raise GateFailure(
            f"runtime mapping is not bijective over constructors; missing={missing}, foreign={foreign}"
        )
    if len({shape.head for shape in shapes.values()}) != len(shapes):
        raise GateFailure("two abstract constructors share one runtime head")

    expected_shapes = {
        "PZero": ("symbol", "rho:nil", 0, "none"),
        "PDrop": ("fixed", "rho:drop", 1, "none"),
        "NQuote": ("fixed", "rho:quote", 1, "none"),
        "PPar": ("variadic", "rho:par", "variadic", "none"),
        "POutput": ("fixed", "rho:send", 2, "none"),
        "PInput": ("binder", "rho:recv", 3, 1),
    }
    observed_shapes = {
        name: (shape.kind, shape.head, shape.child_count, shape.binder_index)
        for name, shape in shapes.items()
    }
    if observed_shapes != expected_shapes:
        raise GateFailure(f"runtime shape contract changed: {observed_shapes}")

    for name in ("PDrop", "NQuote", "POutput"):
        if shapes[name].child_count != len(constructors[name][1]):
            raise GateFailure(f"{name}: fixed runtime arity differs from abstract arity")
    if shapes["PZero"].child_count != 0:
        raise GateFailure("PZero is not represented by a nullary runtime symbol")
    if shapes["PInput"].child_count != len(constructors["PInput"][1]) + 1:
        raise GateFailure("PInput runtime ABI does not account for its binder identity")

    policies = facts(runtime, "rho-runtime-collection-policy", 4)
    expected_policy = (
        schema.Symbol("PPar"),
        schema.Symbol("hash-bag"),
        schema.Symbol("rho:par"),
        schema.Symbol("rho:nil"),
    )
    if policies != [expected_policy]:
        raise GateFailure("PPar collection/unit representation is not exact")

    return abstract, runtime, constructors, shapes


def collect_node_labels(term: schema.SExpr, labels: set[str]) -> None:
    if not isinstance(term, tuple):
        return
    if (
        len(term) >= 2
        and isinstance(term[0], schema.Symbol)
        and term[0].text == "node"
        and isinstance(term[1], schema.Symbol)
    ):
        labels.add(term[1].text)
    for child in term:
        collect_node_labels(child, labels)


def check_concrete_actions(
    presentation_path: Path,
    surface: str,
    shapes: dict[str, RuntimeShape],
) -> None:
    strict = schema.parse_presentation(presentation_path)
    labels: set[str] = set()
    for rule in strict.rules:
        collect_node_labels(rule.head, labels)
        for premise in rule.body:
            collect_node_labels(premise, labels)
    abstract_constructors = set(shapes)
    used_abstract_constructors = labels & abstract_constructors
    if used_abstract_constructors != abstract_constructors:
        raise GateFailure(
            f"strict {surface} actions do not cover the abstract signature exactly: "
            f"{sorted(used_abstract_constructors)} != {sorted(abstract_constructors)}"
        )
    runtime_heads = {shape.head for shape in shapes.values()}
    leaked_runtime_heads = labels & runtime_heads
    if leaked_runtime_heads:
        raise GateFailure(
            f"strict {surface} presentation bypasses the abstract/runtime ABI: "
            f"{sorted(leaked_runtime_heads)}"
        )


def git_output(cwd: Path, *args: str, binary: bool = False) -> bytes | str:
    completed = subprocess.run(
        ["git", *args],
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=not binary,
        check=False,
    )
    if completed.returncode != 0:
        stderr = completed.stderr.decode() if binary else completed.stderr
        raise GateFailure(f"git {' '.join(args)} failed: {stderr.strip()}")
    return completed.stdout


def check_origin(mettapedia_root: Path) -> None:
    head = str(git_output(mettapedia_root, "rev-parse", "HEAD")).strip()
    if head != EXPECTED_ORIGIN_COMMIT:
        raise GateFailure(f"Mettapedia origin commit changed: {head}")
    repository = Path(str(git_output(mettapedia_root, "rev-parse", "--show-toplevel")).strip())
    prefix = str(git_output(mettapedia_root, "rev-parse", "--show-prefix")).strip()
    source_path = prefix + "Mettapedia/OSLF/MeTTaIL/Syntax.lean"
    source = git_output(repository, "show", f"HEAD:{source_path}", binary=True)
    assert isinstance(source, bytes)
    digest = hashlib.sha256(source).hexdigest()
    if digest != EXPECTED_ORIGIN_SHA256:
        raise GateFailure(f"Mettapedia rhoCalc origin digest changed: {digest}")


def check_lean_projection(mettapedia_root: Path) -> None:
    expected_toolchain = LEAN_EXPORTER.with_suffix(".toolchain").read_text(
        encoding="utf-8"
    ).strip()
    actual_toolchain = (mettapedia_root / "lean-toolchain").read_text(
        encoding="utf-8"
    ).strip()
    if actual_toolchain != expected_toolchain:
        raise GateFailure(
            f"Lean toolchain changed: {actual_toolchain!r} != {expected_toolchain!r}"
        )
    build = subprocess.run(
        ["lake", "build", "Mettapedia.OSLF.MeTTaIL.Syntax"],
        cwd=mettapedia_root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if build.returncode != 0:
        raise GateFailure("could not build rhoCalc authority: " + build.stderr.strip())
    emitted = subprocess.run(
        ["lake", "env", "lean", "--run", str(LEAN_EXPORTER)],
        cwd=mettapedia_root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
    )
    if emitted.returncode != 0:
        raise GateFailure("rho abstract syntax exporter failed: " + emitted.stderr.strip())
    noise_prefixes = (
        "Failed to create stream fd:",
        "info: [root]: lakefile.lean and lakefile.toml are both present;",
    )
    generated = "\n".join(
        line for line in emitted.stdout.splitlines()
        if not line.startswith(noise_prefixes)
    ) + "\n"
    checked_in = ABSTRACT.read_text(encoding="utf-8")
    if generated != checked_in:
        raise GateFailure("checked-in abstract rho syntax differs from rhoCalc export")


def expect_contract_rejected(
    abstract_text: str,
    runtime_text: str,
    needle: str,
) -> None:
    with tempfile.TemporaryDirectory(prefix="rho-abstract-canary-", dir=ROOT) as raw:
        directory = Path(raw)
        abstract_path = directory / "abstract.metta"
        runtime_path = directory / "runtime.metta"
        abstract_path.write_text(abstract_text, encoding="utf-8")
        runtime_path.write_text(runtime_text, encoding="utf-8")
        try:
            load_contract(abstract_path, runtime_path)
        except (GateFailure, schema.SchemaError) as error:
            if needle not in str(error):
                raise GateFailure(
                    f"wrong destructive-canary rejection: expected {needle!r}, got {error}"
                ) from error
            return
    raise GateFailure(f"destructive canary unexpectedly admitted; expected {needle!r}")


def expect_plan_rejected(
    abstract_text: str,
    runtime_text: str,
    needle: str,
) -> None:
    with tempfile.TemporaryDirectory(prefix="rho-plan-canary-", dir=ROOT) as raw:
        directory = Path(raw)
        abstract_path = directory / "abstract.metta"
        runtime_path = directory / "runtime.metta"
        abstract_path.write_text(abstract_text, encoding="utf-8")
        runtime_path.write_text(runtime_text, encoding="utf-8")
        try:
            compile_plans(runtime_path, abstract_path)
        except (PlanError, schema.SchemaError) as error:
            if needle not in str(error):
                raise GateFailure(
                    f"wrong plan-canary rejection: expected {needle!r}, got {error}"
                ) from error
            return
    raise GateFailure(f"plan canary unexpectedly admitted; expected {needle!r}")


def run_mutations() -> int:
    abstract = ABSTRACT.read_text(encoding="utf-8")
    runtime = RUNTIME_ABI.read_text(encoding="utf-8")
    pinput_block = """    (rule rho-constructor-PInput
      (head (rho-constructor PInput Proc (rho-args (rho-simple Name) (rho-args (rho-abstraction (rho-arrow Name Proc)) rho-no-args))))
      (body))
"""
    if pinput_block not in abstract:
        raise GateFailure("PInput mutation anchor changed")
    expect_contract_rejected(
        abstract.replace(pinput_block, "", 1),
        runtime,
        "abstract constructor inventory changed",
    )
    expect_contract_rejected(
        abstract.replace(
            "(rho-abstraction (rho-arrow Name Proc))",
            "(rho-simple (rho-arrow Name Proc))",
            1,
        ),
        runtime,
        "PInput body lost its abstraction boundary",
    )

    pdrop_block = """    (rule rho-runtime-PDrop
      (head (rho-runtime-shape PDrop fixed rho:drop 1 none))
      (body))
"""
    if pdrop_block not in runtime:
        raise GateFailure("PDrop runtime mutation anchor changed")
    expect_contract_rejected(
        abstract,
        runtime.replace(pdrop_block, "", 1),
        "runtime mapping is not bijective",
    )
    expect_contract_rejected(
        abstract,
        runtime.replace(
            "(rho-runtime-shape PInput binder rho:recv 3 1)",
            "(rho-runtime-shape PInput binder rho:recv 3 2)",
            1,
        ),
        "runtime shape contract changed",
    )
    expect_contract_rejected(
        abstract,
        runtime.replace(
            "(rho-runtime-collection-policy PPar hash-bag rho:par rho:nil)",
            "(rho-runtime-collection-policy PPar hash-bag rho:par rho:drop)",
            1,
        ),
        "PPar collection/unit representation is not exact",
    )
    expect_contract_rejected(
        abstract,
        runtime.replace(
            "  ))\n",
            "    (rule rho-runtime-Foreign\n"
            "      (head (rho-runtime-shape Foreign fixed rho:foreign 0 none))\n"
            "      (body))\n"
            "  ))\n",
            1,
        ),
        "runtime mapping is not bijective",
    )

    pzero_projection = """    (rule rho-runtime-projection-rho-PZero
      (head (rho-runtime-projection-rule rho PZero rho-projection-symbol))
      (body))
"""
    if pzero_projection not in runtime:
        raise GateFailure("PZero projection mutation anchor changed")
    expect_plan_rejected(
        abstract,
        runtime.replace(pzero_projection, "", 1),
        "projection/runtime constructor sets differ",
    )
    expect_plan_rejected(
        abstract,
        runtime.replace(
            "(rho-projection-binder 1 0 2)",
            "(rho-projection-binder 1 1 2)",
            1,
        ),
        "binder indexes are not bijective",
    )
    expect_plan_rejected(
        abstract,
        runtime.replace(
            "(rho-projection-cons bare-name\n"
            "                (rho-projection-cons binder rho-projection-nil))",
            "(rho-projection-cons bare-name rho-projection-nil)",
            1,
        ),
        "binder label is not a text wrapper",
    )
    expect_plan_rejected(
        abstract,
        runtime.replace(
            "(rho-projection-cons variable\n"
            "            (rho-projection-cons bare-name rho-projection-nil))",
            "(rho-projection-cons foreign-name rho-projection-nil)",
            1,
        ),
        "variable label is not a text wrapper",
    )
    return 10


def check(mettapedia_root: Path) -> tuple[int, int, int, int, int, str]:
    check_origin(mettapedia_root)
    check_lean_projection(mettapedia_root)
    abstract, runtime, constructors, shapes = load_contract()
    check_concrete_actions(STRICT_RHO, ".rho", shapes)
    check_concrete_actions(STRICT_MRHO, ".mrho", shapes)
    plans = compile_plans()
    if set(plans) != {"rho", "mrho"}:
        raise GateFailure("typed projection plan inventory changed")
    mutation_count = run_mutations()
    digest = schema.package_digest((abstract, runtime))
    if EXPECTED_PACKAGE_DIGEST and digest != EXPECTED_PACKAGE_DIGEST:
        raise GateFailure(f"rho abstract syntax package changed: {digest}")
    return (2, len(constructors), len(shapes), len(plans), mutation_count, digest)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mettapedia-root", type=Path, required=True)
    arguments = parser.parse_args()
    mettapedia_root = arguments.mettapedia_root.resolve()
    if not mettapedia_root.is_dir():
        raise GateFailure("Mettapedia root is absent")
    summary = check(mettapedia_root)
    print(
        "(RhoAbstractSyntaxV1Summary "
        + " ".join(str(value) for value in summary)
        + " 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
