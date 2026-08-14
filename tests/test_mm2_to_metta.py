#!/usr/bin/env python3
"""Differential gate for the fail-closed basic MM2-to-MeTTa translator."""

from __future__ import annotations

import csv
from collections import Counter
import os
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import mm2_to_metta as translator  # noqa: E402


EXPECTED_ACCEPTED = {
    "bc_impl_equiv.mm2",
    "mm2_exec_basic.mm2",
    "mm2_kiss_add_remove.mm2",
    "mm2_kiss_count_groupby.mm2",
    "mm2_kiss_fractal_priority.mm2",
    "mm2_kiss_priority.mm2",
    "test10_conjunctive_wq.mm2",
    "test3_var_binding.mm2",
    "test4_conjunctive.mm2",
    "test5_equal_pair.mm2",
    "test6_no_match.mm2",
    "test7_nested.mm2",
    "test8_multi_step.mm2",
    "test9_priority_ordering.mm2",
    "test_add_constant.mm2",
    "test_add_simple.mm2",
    "test_bulk_remove.mm2",
    "test_count_simple.mm2",
    "test_remove_simple.mm2",
    "transitive.mm2",
    "zip_add.mm2",
}


EXPECTED_GSLT_SUPPORT_TRANSFORM_V1 = {
    "ancestor.mm2",
    "bc_roman_4node.mm2",
    "bc_socrates_pln.mm2",
    "bc_impl_equiv.mm2",
    "bc_mammal_lassie.mm2",
    "bench_pure_mm2_3hop_scored.mm2",
    "counter_machine_5.mm2",
    "counterfactual_orchard_pure.mm2",
    "hexlife.mm2",
    "hexlife_structural.mm2",
    "mm2_exec_basic.mm2",
    "mm2_kiss_add_remove.mm2",
    "mm2_kiss_count_groupby.mm2",
    "mm2_kiss_fractal_priority.mm2",
    "mm2_kiss_priority.mm2",
    "odd_even_sort.mm2",
    "revise_proofs_symbolic.mm2",
    "std.mm2",
    "string_convert.mm2",
    "test10_conjunctive_wq.mm2",
    "test3_var_binding.mm2",
    "test4_conjunctive.mm2",
    "test5_equal_pair.mm2",
    "test6_no_match.mm2",
    "test7_nested.mm2",
    "test8_multi_step.mm2",
    "test9_priority_ordering.mm2",
    "test_add_constant.mm2",
    "test_add_simple.mm2",
    "test_bulk_remove.mm2",
    "test_count_simple.mm2",
    "test_head_limit.mm2",
    "test_remove_simple.mm2",
    "test_var_scope_across_exprs.mm2",
    "transitive.mm2",
    "zip_add.mm2",
}


def parse_he_result_line(line: str) -> list[translator.SExpr]:
    body = line[1:-1]
    converted: list[str] = []
    depth = 0
    quoted = False
    escaped = False
    for char in body:
        if quoted:
            converted.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
            converted.append(char)
        elif char == "(":
            depth += 1
            converted.append(char)
        elif char == ")":
            depth -= 1
            converted.append(char)
        elif char == "," and depth == 0:
            converted.append(" ")
        else:
            converted.append(char)
    parsed = translator.parse("(" + "".join(converted) + ")")
    assert len(parsed) == 1 and isinstance(parsed[0], list)
    return parsed[0]


def is_result_envelope(term: translator.SExpr) -> bool:
    return isinstance(term, list) and all(
        isinstance(item, list) and len(item) == 2 and item[0] == "mm2-result"
        for item in term)


def hosted_result_atoms(
    stdout: str, lane: str, expected_empty: bool
) -> list[translator.SExpr]:
    lines = [line.strip() for line in stdout.splitlines() if line.strip()]
    candidates: list[translator.SExpr] = []
    if lane in ("he", "prime"):
        for line in lines:
            if line.startswith("[") and line.endswith("]"):
                candidates.extend(parse_he_result_line(line))
    else:
        for line in lines:
            candidates.extend(translator.parse(line))
    for candidate in reversed(candidates):
        if is_result_envelope(candidate):
            assert isinstance(candidate, list)
            return [item[1] for item in candidate if isinstance(item, list)]
    if expected_empty:
        return []
    raise AssertionError(f"no result envelope in output: {stdout!r}")


class TranslatorGate(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cetta = Path(os.environ.get("CETTA_BIN", ROOT / "cetta"))
        version = subprocess.run(
            [str(cls.cetta), "--version"], text=True,
            capture_output=True, check=True).stdout
        if "(mork)" not in version and "(main)" not in version:
            raise RuntimeError(
                "translator differential gate requires a MORK-capable build")

    def corpus_rows(self):
        aihub = ROOT.parent.parent
        manifest = ROOT / "benchmarks" / "mm2_corpus" / "manifest.tsv"
        with manifest.open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle, delimiter="\t"):
                path = Path(row["path"].replace("$AIHUB", str(aihub)))
                yield row["name"], path

    def test_mm2_syntax_alpha_normalization(self) -> None:
        authored = translator.parse(
            "(u $r $s $r $s) (u $r (v $s $r) $s)")
        printed = translator.parse(
            "(u $ $ _1 _2) (u $ (v $ _1) _2)")
        self.assertEqual(
            translator.canonical_mm2_support(authored),
            translator.canonical_mm2_support(printed))

    def test_mm2_syntax_alpha_normalization_preserves_coreference(self) -> None:
        repeated = translator.parse("(u $r $s $r $s)")
        crossed = translator.parse("(u $r $s $s $r)")
        self.assertNotEqual(
            translator.canonical_mm2_support(repeated),
            translator.canonical_mm2_support(crossed))

    def test_mm2_syntax_reference_without_binder_is_a_symbol(self) -> None:
        literal = translator.parse("(u _1)")[0]
        self.assertEqual(
            translator.alpha_normalize_mork_syntax(literal), literal)

    def test_mm2_support_observation_is_order_and_duplicate_insensitive(self) -> None:
        left = translator.parse("(p $x $x) (q a) (q a)")
        right = translator.parse("(q a) (p $ _1)")
        self.assertEqual(
            translator.canonical_mm2_support(left),
            translator.canonical_mm2_support(right))

    def test_gslt_support_transform_v1_matches_native_corpus(self) -> None:
        admitted: dict[str, Path] = {}
        for name, path in self.corpus_rows():
            forms = translator.parse(path.read_text(encoding="utf-8"))
            if translator.gslt_support_transform_v1_accepts_program(forms):
                admitted[name] = path
        self.assertEqual(set(admitted), EXPECTED_GSLT_SUPPORT_TRANSFORM_V1)

        for name, path in admitted.items():
            with self.subTest(program=name):
                native = subprocess.run(
                    [str(self.cetta), "--lang", "mm2", str(path)],
                    text=True, capture_output=True, timeout=90, check=True)
                profiled = subprocess.run(
                    [str(self.cetta), "--lang", "mm2", "--profile", "gslt",
                     str(path)],
                    text=True, capture_output=True, timeout=90, check=True)
                self.assertEqual(
                    translator.canonical_mm2_support(
                        translator.parse(profiled.stdout)),
                    translator.canonical_mm2_support(
                        translator.parse(native.stdout)))

    def test_supported_corpus_matches_native_in_all_lanes(self) -> None:
        accepted: dict[str, tuple[Path, translator.CompiledProgram]] = {}
        for name, path in self.corpus_rows():
            try:
                accepted[name] = (
                    path, translator.compile_program(
                        path.read_text(encoding="utf-8")))
            except translator.TranslationError:
                pass
        self.assertEqual(set(accepted), EXPECTED_ACCEPTED)

        for name, (path, program) in accepted.items():
            with self.subTest(program=name, lane="mm2"):
                native = subprocess.run(
                    [str(self.cetta), "--lang", "mm2", str(path)],
                    text=True, capture_output=True, timeout=30, check=True)
                native_atoms = translator.parse(native.stdout)
                native_keys = Counter(
                    translator.mork_encoded_key(atom) for atom in native_atoms
                )

            generated = translator.render_program(program, include_engine=True)
            for backend in ("native", "pathmap"):
                for lane in ("he", "prime", "petta"):
                    with self.subTest(program=name, backend=backend, lane=lane):
                        hosted = subprocess.run(
                            [str(self.cetta), "--space-engine", backend,
                             "--lang", lane, "--quiet", "/dev/stdin"],
                            input=generated, text=True, capture_output=True,
                            timeout=30, check=True)
                        hosted_atoms = hosted_result_atoms(
                            hosted.stdout, lane, expected_empty=not native_keys)
                        hosted_keys = Counter(
                            translator.mork_encoded_key(atom)
                            for atom in hosted_atoms
                        )
                        self.assertEqual(hosted_keys, native_keys)

    def test_semantic_witnesses_match_native_in_all_lanes(self) -> None:
        witnesses = {
            "priority competition": """
                (ready)
                (exec (1 second)
                      (, (ready))
                      (O (+ (ran second)) (- (ready))))
                (exec (0 first)
                      (, (ready))
                      (O (+ (ran first)) (- (ready))))
            """,
            "bulk query observes the pre-write space": """
                (seed a)
                (seed b)
                (exec (0 bulk)
                      (, (seed $x))
                      (O (+ (seed c)) (+ (seen $x))))
            """,
            "interacting add and remove sinks": """
                (seed a)
                (seed b)
                (exec (0 mixed)
                      (, (seed $value))
                      (O (- (x a)) (+ (x $value))))
            """,
            "grouped distinct count": """
                (item red a)
                (item red a)
                (item red b)
                (item blue c)
                (exec (0 count)
                      (, (item $color $value))
                      (O (count (card $color $n) $n $value)))
            """,
            "later priority observes earlier output": """
                (edge a b)
                (edge b c)
                (exec (1 second)
                      (, (path $x $z))
                      (O (+ (done $x $z))))
                (exec (0 first)
                      (, (edge $x $y) (edge $y $z))
                      (O (+ (path $x $z))))
            """,
        }

        for label, source in witnesses.items():
            with self.subTest(case=label, lane="mm2"):
                program = translator.compile_program(source)
                native = subprocess.run(
                    [str(self.cetta), "--lang", "mm2", "/dev/stdin"],
                    input=source, text=True, capture_output=True,
                    timeout=30, check=True)
                native_keys = Counter(
                    translator.mork_encoded_key(atom)
                    for atom in translator.parse(native.stdout)
                )

            generated = translator.render_program(program, include_engine=True)
            for backend in ("native", "pathmap"):
                for lane in ("he", "prime", "petta"):
                    with self.subTest(
                            case=label, backend=backend, lane=lane):
                        hosted = subprocess.run(
                            [str(self.cetta), "--space-engine", backend,
                             "--lang", lane, "--quiet", "/dev/stdin"],
                            input=generated, text=True, capture_output=True,
                            timeout=30, check=True)
                        hosted_atoms = hosted_result_atoms(
                            hosted.stdout, lane, expected_empty=not native_keys)
                        hosted_keys = Counter(
                            translator.mork_encoded_key(atom)
                            for atom in hosted_atoms
                        )
                        self.assertEqual(hosted_keys, native_keys)

    def test_unsupported_semantics_fail_closed(self) -> None:
        rejected = {
            "variable priority":
                "(exec $p (, (x)) (O (+ (y))))",
            "external source":
                "(exec (0) (I (BTM (x))) (O (+ (y))))",
            "head sink":
                "(x) (exec (0) (, (x)) (O (head 1 (y))))",
            "float reduction":
                "(x 1.0) (exec (0) (, (x $n)) (O (fsum (y $k) $k $n)))",
            "dynamic exec":
                "(x) (exec (0) (, (x)) (O (+ (exec (1) (, (y)) (, (z))))))",
            "unbound output":
                "(x) (exec (0) (, (x)) (O (+ (y $v))))",
            "non-ground data":
                "(x $v)",
            "malformed input":
                "(x",
        }
        for label, source in rejected.items():
            with self.subTest(case=label):
                with self.assertRaises(translator.TranslationError):
                    translator.compile_program(source)


if __name__ == "__main__":
    unittest.main()
