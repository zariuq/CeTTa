#!/usr/bin/env python3
"""Unit controls for generator types, observer contracts and reproducers."""
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

from check_native_observers import Run, boundary_oracle, compare, constant_oracle, coverage, execute, existence_oracle, fold_oracle, nonempty_witness, observations, route_environment, shrink, single_atom, verdict_with_probe
from native_observer_cases import Case, Expr, FAMILIES, MARKER, generate, integer, lean_controls, read
from native_observer_probes import COLLECT, R82_SHA256, SCOPED_OWNERS_SHA256, SUCCESSOR_CANDIDATE_SHA256, SUCCESSOR_CANDIDATE_2_SHA256, completed_routes, debugger_coverage, probe_config


def output(text, *, code=0, status="exited", stderr=""):
    return Run(status, code, text, stderr, 0.0, {}, [])


def packet(value):
    return f"({MARKER}0 {value})\n"


class ObserverControls(unittest.TestCase):
    def test_open_existence_oracle_rejects_false_absence(self):
        case = Case(0, "match-existence", "value", ("2",), integer(0), boundary="open-existence")
        _, contracts = case.render()
        self.assertEqual(existence_oracle(case, output("true\n" + packet("false")), contracts), "equal")
        self.assertEqual(existence_oracle(case, output("true\n" + packet("true")), contracts), "value-mismatch")

    def test_runtime_environment_is_explicit(self):
        with patch.dict(os.environ, {"CETTA_OTHER_EXPERIMENT": "1", "CETTA_SPACE_DENSE": "off"}):
            env = route_environment("eager", False)
        self.assertNotIn("CETTA_OTHER_EXPERIMENT", env)
        self.assertEqual(env["CETTA_SPACE_DENSE"], "eager")
        self.assertEqual(env["CETTA_NATIVE_QUERY_REFERENCE"], "0")

    def check_pair(self, left, right, mode="answers"):
        return compare(output(packet(left)), output(packet(right)), {MARKER + "0": mode})

    def test_boolean_is_not_one(self):
        self.assertEqual(self.check_pair("true", "1"), "value-mismatch")

    def test_tuple_fields_are_ordered(self):
        self.assertEqual(self.check_pair("(pair 1 true)", "(pair true 1)"), "value-mismatch")

    def test_bag_duplicates_survive(self):
        self.assertEqual(self.check_pair("(a a b)", "(b a a)", "bag"), "equal")
        self.assertEqual(self.check_pair("(a a b)", "(a b)", "bag"), "value-mismatch")

    def test_ordered_collection_is_not_a_bag(self):
        self.assertEqual(self.check_pair("(a b)", "(b a)", "ordered"), "value-mismatch")

    def test_shared_variables_survive(self):
        self.assertEqual(self.check_pair("((p $x) (q $x))", "((q $z) (p $z))", "bag"), "equal")
        self.assertEqual(self.check_pair("((p $x) (q $x))", "((p $y) (q $z))", "bag"), "value-mismatch")

    def test_strings_are_not_alpha_renamed(self):
        self.assertEqual(self.check_pair('"$x"', '"$y"'), "value-mismatch")

    def test_effect_text_and_order_are_exact(self):
        contracts = {MARKER + "0": "answers"}
        self.assertEqual(compare(output("a  b\n" + packet("1")), output("a b\n" + packet("1")), contracts), "effect-trace-mismatch")
        self.assertEqual(compare(output("first\nsecond\n"), output("second\nfirst\n"), {}), "effect-trace-mismatch")
        self.assertEqual(compare(output("printed\n"), output("printed"), {}), "effect-trace-mismatch")

    def test_failure_is_not_empty_success(self):
        self.assertEqual(compare(output(""), output("", code=1), {}), "invalid-reference")
        self.assertEqual(compare(output("", code=-11), output(""), {}), "native-exit-mismatch")
        self.assertEqual(compare(output("", status="timeout"), output("", status="timeout"), {}), "inconclusive-execution-limit")

    def test_protocol_is_explicit(self):
        with self.assertRaises(ValueError):
            observations(packet("(a b)"), {MARKER + "1": "bag"})
        with self.assertRaises(ValueError):
            observations("", {MARKER + "0": "bag"})
        self.assertFalse(single_atom("(a (b)"))
        self.assertFalse(single_atom('"unfinished'))
        self.assertFalse(single_atom("(a) (b)"))
        self.assertTrue(single_atom('(pair "a b" (c))'))

    def test_generic_admission_is_not_route_evidence(self):
        case = generate(9, 1, ("scalar-fold",))[0]
        native = output("")
        native.counters = {"native-query-admission": 99}
        self.assertEqual(coverage(case, native, output(""))["level"], "unconfirmed")

    def test_empty_positive_schema_is_not_a_witness(self):
        case = generate(9, 1, ("open-join",))[0]
        self.assertFalse(nonempty_witness(case, output(""), {MARKER + "0": "answers"}))
        self.assertIn("(, (row", case.render()[0])

    def test_caught_error_is_not_a_number(self):
        self.assertEqual(self.check_pair("((Error bad context))", "(1)", "bag"), "value-mismatch")

    def test_constant_oracle_checks_effect_values_not_just_nonemptiness(self):
        expected = output(constant_oracle("effect", "7"))
        wrong = output("(observed-effect sort-atom)\n" + packet("(sort-atom)") +
                       f"({MARKER}1 (sort-atom))\n")
        self.assertEqual(compare(wrong, expected, {MARKER + "0": "bag", MARKER + "1": "bag"}), "effect-trace-mismatch")

    def test_boundary_oracle_is_independent_of_route_agreement(self):
        case = Case(0, "dead-rule", "value", ("0",), integer(0), consumer="count",
                    boundary="incomplete-coverage", negative=True)
        _, contracts = case.render()
        good = output(packet("0") + f"({MARKER}1 (3))\n")
        bad = output(packet("0") + f"({MARKER}1 (4))\n")
        self.assertEqual(boundary_oracle(case, good, contracts), "equal")
        self.assertEqual(boundary_oracle(case, bad, contracts), "value-mismatch")
        self.assertEqual(compare(bad, bad, contracts), "equal")

    def test_preheld_key_changes_every_observer_count(self):
        for observer in ("value", "tuple", "bag", "catch", "effect", "ordered"):
            case = Case(0, "guard-batch", observer, ("0", "1", "1", "2"), integer(0),
                        consumer="count", boundary="preheld-guard")
            _, contracts = case.render()
            # Add complete post-state packets; the independent primary
            # count check is separate from the differential post-state law.
            for value, expected in (("2", "equal"), ("3", "value-mismatch")):
                primary = constant_oracle(observer, value).splitlines(keepends=True)
                primary = "".join(line for line in primary if not line.startswith(f"({MARKER}1 "))
                extra = "".join(f"({marker} ())\n" for marker in contracts if marker != MARKER + "0")
                self.assertEqual(boundary_oracle(case, output(primary + extra), contracts), expected)

    def test_fold_oracle_counts_join_occurrences_and_big_integers(self):
        additive = Expr("+", "data", (read("a"), read("x")))
        for producer, values, expected in (("run", ("0", "1", "1"), "6"),
                ("moments", ("1", "1", "2"), "8"),
                ("scan", ("9223372036854775807", "1"), "9223372036854775808")):
            case = Case(0, "arithmetic-fold", "value", values, additive, producer=producer)
            _, contracts = case.render()
            self.assertEqual(fold_oracle(case, output(packet(expected)), contracts), "equal")
            self.assertEqual(fold_oracle(case, output(packet("0")), contracts), "value-mismatch")


class GeneratorControls(unittest.TestCase):
    def test_seed_and_replay(self):
        cases = generate(44, 6)
        self.assertEqual([x.key() for x in cases], [x.key() for x in generate(44, 6)])
        self.assertEqual(set(x.family for x in cases), set(FAMILIES))
        for case in cases:
            replay = Case.from_dict(case.to_dict())
            self.assertEqual(case.render(), replay.render())
            source, contracts = case.render()
            self.assertTrue(source.endswith("\n"))
            self.assertTrue(contracts)

    def test_kind_checked_constructors(self):
        flag = Expr("==", "test", (read("a"), integer(1)))
        with self.assertRaises(ValueError):
            Expr("+", "data", (flag, integer(1)))
        self.assertEqual(Expr("bad-add", "data", (flag, integer(1))).render(), "(+ (== $a 1) 1)")

    def test_observer_binders_do_not_capture_region_variables(self):
        case = Case(0, "listed-match", "tuple", ("1", "2"), integer(0))
        source, _ = case.render()
        self.assertIn("(let $__cetta_observer_value", source)
        self.assertIn("(edge $u $v)", source)
        self.assertNotIn("(let $v", source)

    def test_ordered_observers_use_structural_keys(self):
        case = Case(0, "listed-match", "ordered", ("1", "1.0"), integer(0))
        self.assertIn("(sort-key (repr", case.render()[0])

    def test_effect_collector_and_consumer_use_distinct_binders(self):
        case = Case(0, "listed-match", "effect", ("1", "1.0"), integer(0))
        source, _ = case.render()
        self.assertIn("(let $__cetta_observer_keyed_value", source)
        self.assertIn("(let (sort-key $__cetta_observer_key $__cetta_observer_value)", source)
        self.assertIn("(superpose $__cetta_observer_collection)", source)
        self.assertNotIn("(superpose (sort-atom", source)

    def test_arithmetic_match_fold_uses_an_additive_schema(self):
        for case in generate(7, 8, ("arithmetic-fold",)):
            self.assertEqual(case.expr.op, "+")
            self.assertEqual(case.expr.args[0], read("a"))
            self.assertNotIn("$a", case.expr.args[1].render())
            self.assertIn("(foldall observer-arithmetic-step", case.render()[0])
            self.assertTrue(all(candidate.expr.op == "+" and candidate.expr.args[0] == read("a")
                                for candidate in case.reductions()))

    def test_fold_producer_labels_change_the_source(self):
        additive = Expr("+", "data", (read("a"), read("x")))
        run = Case(0, "arithmetic-fold", "value", ("1", "1"), additive, producer="run")
        moments = Case(1, "arithmetic-fold", "value", ("1", "1"), additive, producer="moments")
        self.assertIn("(match &self (row $x) 2)", run.render()[0])
        self.assertIn("(, (row $x) (fold-choice $choice))", moments.render()[0])
        self.assertIn("(fold-choice left)\n(fold-choice right)", moments.render()[0])
        with self.assertRaises(ValueError):
            Case(0, "scalar-fold", "value", ("1",), additive, producer="run")

    def test_specialization_entry_shapes_remain_present(self):
        by_family = {case.family: case.render()[0] for case in generate(8, 1)}
        self.assertIn("(match &self (seed $initial) (insert-node $initial))", by_family["saturation"])
        self.assertIn("(add-atom &grouped-output", by_family["grouped-count"])
        self.assertIn("(let $frontier (superpose (start)) (relax $frontier))", by_family["relaxation"])

    def test_takeover_exercises_both_fact_shapes(self):
        cases = generate(8, 1, ("row-takeover",))
        self.assertEqual({case.fact_shape for case in cases}, {"deep", "flat", "flat-star"})
        deep = next(case for case in cases if case.fact_shape == "deep").render()[0]
        flat = next(case for case in cases if case.fact_shape == "flat").render()[0]
        self.assertIn("(kept $x (box $x))", deep)
        self.assertIn("(kept $x $x)", flat)
        self.assertNotIn("(box", flat)

    def test_collective_results_reach_each_observer(self):
        cases = generate(8, 1, ("grouped-count", "listed-match", "open-join"))
        for family in ("grouped-count", "listed-match", "open-join"):
            for consumer in ("count", "exists"):
                self.assertEqual({case.observer for case in cases if case.family == family and case.consumer == consumer},
                                 {"value", "tuple", "bag", "catch", "effect", "ordered"})
        for case in cases:
            if case.family == "listed-match" and case.consumer != "stream":
                self.assertGreaterEqual(len(case.values), 16)

    def test_boundary_labels_are_checked(self):
        with self.assertRaises(ValueError):
            Case(0, "scalar-fold", "value", ("0",), integer(0), boundary="empty-group")
        with self.assertRaises(ValueError):
            Case(0, "dead-rule", "value", ("0",), integer(0), boundary="incomplete-coverage")
        cases = generate(9, 1)
        self.assertEqual({case.boundary for case in cases},
                         {"ordinary", "empty-group", "preheld-guard", "ground-first", "incomplete-coverage", "open-existence"})

    def test_export_uses_existing_extractor(self):
        source = lean_controls(generate(4, 2, ("scalar-fold",)))
        self.assertIn("Bridges.ScalarSource", source)
        self.assertIn(".isSome = false", source)
        self.assertNotIn("native_decide", source)

    def test_shrinker_preserves_failing_predicate(self):
        case = Case(0, "scalar-fold", "value", ("1", "2", "3"), integer(7))
        def predicate(candidate):
            return "bug" if "2" in candidate.values else None
        reduced, receipt = shrink(case, predicate, 100)
        self.assertEqual(reduced.values, ("2",))
        self.assertEqual(receipt["status"], "local-fixed-point")
        self.assertTrue(all(predicate(x) is None for x in reduced.reductions()))

    def test_shrinker_does_not_claim_global_minimality(self):
        case = Case(0, "scalar-fold", "value", ("1", "2"), integer(7))
        _, receipt = shrink(case, lambda _: "bug", 2)
        self.assertEqual(receipt["status"], "budget-limited")

    def test_unstable_baseline_declines_shrinking(self):
        answers = iter(("bug", None))
        _, receipt = shrink(generate(1, 1, ("scalar-fold",))[0], lambda _: next(answers), 10)
        self.assertEqual(receipt["status"], "not-reproducible")


class ExecutionControls(unittest.TestCase):
    def run_script(self, source, **kwargs):
        return execute([sys.executable, "-c", source], cwd=Path.cwd(), environment=dict(os.environ),
                       timeout=kwargs.pop("timeout", 2), output_limit=kwargs.pop("output_limit", 65536), **kwargs)

    def test_timeout_is_retained(self):
        self.assertEqual(self.run_script("import time; time.sleep(3)", timeout=0.05).status, "timeout")

    def test_output_limit_is_retained(self):
        result = self.run_script("print('x' * 100000)", output_limit=1024)
        self.assertEqual(result.status, "output-limit")

    def test_invalid_encoding_is_not_replaced_and_compared(self):
        result = self.run_script("import sys; sys.stdout.buffer.write(bytes([255]))")
        self.assertEqual(result.status, "invalid-utf8")

    def test_debugger_file_output_limit_is_inherited(self):
        result = self.run_script("import resource; print(resource.getrlimit(resource.RLIMIT_FSIZE))", file_limit=4096)
        self.assertEqual(result.stdout, "(4096, 4096)\n")

    def test_only_requested_diagnostics_are_separated(self):
        script = "import sys; print('runtime-counter native-query-admission 4', file=sys.stderr)"
        self.assertEqual(self.run_script(script).counters, {})
        result = self.run_script(script, stats=True)
        self.assertEqual(result.counters, {"native-query-admission": 4})
        self.assertEqual(result.stderr, "")


class DebuggerProbeControls(unittest.TestCase):
    @staticmethod
    def receipt(events):
        return {"architecture": "i386:x86-64", "exit": {"code": 0}, "errors": [], "pending": 0, "events": events}

    def test_unknown_build_has_no_profile(self):
        with self.assertRaises(ValueError):
            probe_config("scalar-fold", "wrong", Path("unused"))
        self.assertEqual(probe_config("scalar-fold", R82_SHA256, Path("unused"))["symbols"], ["petta_eval_scalar_fold"])
        self.assertEqual(probe_config("scalar-fold", SUCCESSOR_CANDIDATE_SHA256, Path("unused"))["symbols"],
                         ["petta_eval_scalar_fold"])
        self.assertEqual(probe_config("scalar-fold", SUCCESSOR_CANDIDATE_2_SHA256, Path("unused"))["symbols"],
                         ["petta_eval_scalar_fold"])
        self.assertEqual(probe_config("scalar-fold", SCOPED_OWNERS_SHA256, Path("unused"))["symbols"],
                         ["petta_eval_scalar_fold"])

    def test_existence_absence_and_decline_are_distinct(self):
        event = {"symbol": "space_match_exists_ground_exact", "call": 1, "return_u32": 0, "out_bool": 1}
        self.assertEqual(completed_routes("match-existence", self.receipt([event])), [1])
        event["out_bool"] = 0
        self.assertFalse(completed_routes("match-existence", self.receipt([event])))

    def test_entry_and_interruption_are_not_completion(self):
        for status in (None, 0, 2):
            event = {"symbol": "petta_eval_scalar_fold", "call": 1, "return_u32": status}
            self.assertFalse(completed_routes("scalar-fold", self.receipt([event])))
        event["return_u32"] = 1
        self.assertEqual(completed_routes("scalar-fold", self.receipt([event])), [1])

    def test_bigint_evidence_belongs_to_a_completed_fold(self):
        parent = {"symbol": "cetta_native_foldall", "call": 1, "return_u32": 2}
        big = {"symbol": "cetta_fold_big_start", "call": 2, "fold_owner": 7}
        result = debugger_coverage("arithmetic-fold", self.receipt([parent, big]), self.receipt([]), "equal", "equal")
        self.assertEqual(result["subroutes"], ["ordered-scan"])
        big["fold_owner"] = 1
        result = debugger_coverage("arithmetic-fold", self.receipt([parent, big]), self.receipt([]), "equal", "equal")
        self.assertEqual(result["subroutes"], ["ordered-scan", "bigint"])

    def test_reference_must_decline_query_worker(self):
        native = self.receipt([{"symbol": "petta_eval_scalar_fold", "call": 1, "return_u32": 1}])
        self.assertEqual(debugger_coverage("scalar-fold", native, native, "equal", "equal")["level"], "unconfirmed")
        self.assertEqual(debugger_coverage("scalar-fold", native, self.receipt([]), "equal", "equal")["level"], "debugger-return-confirmed")

    def test_generic_query_success_is_not_guard_evidence(self):
        event = {"symbol": "cetta_native_query_run", "call": 1, "return_u32": 1}
        self.assertFalse(completed_routes("guard-batch", self.receipt([event])))
        event.update(consumer=1, effect_count=1, has_guard=True)
        self.assertEqual(completed_routes("guard-batch", self.receipt([event])), [1])

    def test_inlined_path_needs_its_own_success_and_parent_completion(self):
        parent = {"symbol": COLLECT, "call": 1, "return_u32": 1}
        checkpoint = {"kind": "checkpoint", "name": "group-result", "owner": 1, "value": 0}
        self.assertFalse(completed_routes("grouped-count", self.receipt([parent, checkpoint])))
        checkpoint["value"] = 1
        self.assertEqual(completed_routes("grouped-count", self.receipt([parent, checkpoint])), [1])
        parent["return_u32"] = 2
        self.assertFalse(completed_routes("grouped-count", self.receipt([parent, checkpoint])))

    def test_dead_rule_and_saturation_must_belong_to_same_invocation(self):
        events = [{"symbol": COLLECT, "call": 1, "return_u32": 1},
                  {"symbol": COLLECT, "call": 2, "return_u32": 1},
                  {"kind": "checkpoint", "name": "dead-premise-count", "owner": 1, "value": 0},
                  {"kind": "checkpoint", "name": "saturation-finished", "owner": 2}]
        self.assertFalse(completed_routes("dead-rule", self.receipt(events)))
        events[-1]["owner"] = 1
        self.assertEqual(completed_routes("dead-rule", self.receipt(events)), [1])

    def test_probe_failures_and_observation_changes_do_not_qualify(self):
        native = self.receipt([{"symbol": "petta_eval_scalar_fold", "call": 1, "return_u32": 1}])
        reference = self.receipt([])
        self.assertFalse(debugger_coverage("scalar-fold", native, reference, "value-mismatch", "equal")["probe_valid"])
        native["errors"] = ["event-limit"]
        self.assertFalse(completed_routes("scalar-fold", native))
        native["errors"] = []
        native["pending"] = 1
        self.assertFalse(completed_routes("scalar-fold", native))

    def test_failed_probe_cannot_hide_a_runtime_mismatch(self):
        for verdict in ("value-mismatch", "native-exit-mismatch", "effect-trace-mismatch", "invalid-reference"):
            self.assertEqual(verdict_with_probe(verdict, False), verdict)
        self.assertEqual(verdict_with_probe("equal", False), "inconclusive-route-probe")
        self.assertEqual(verdict_with_probe("equal", True), "equal")


if __name__ == "__main__":
    unittest.main()
