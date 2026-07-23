#!/usr/bin/env python3
"""Adversarial tests for the finite Prime cell-causal reference model."""

from __future__ import annotations

from collections import Counter
from dataclasses import replace
from fractions import Fraction
import unittest

from cell_causal_reference import (
    Candidate,
    CellSpec,
    Check,
    EffectSpec,
    OutcomeSpec,
    Problem,
    ProducerId,
    RuleOccurrence,
    SourceOccurrence,
    exact_probability,
    import_problem,
    minimal_antichain,
    naive_independent_explanation_probability,
    normalized_result,
    outcome_event_id,
    run,
)


def producer(cell: int, generation: int = 0, lineage: str = "test") -> ProducerId:
    return ProducerId(lineage, cell, generation)


def source(application: int, position: int, *path: str) -> SourceOccurrence:
    return SourceOccurrence(application, position, tuple(path))


def rule(application: int, ordinal: int) -> RuleOccurrence:
    return RuleOccurrence(application, ordinal)


def values(result) -> Counter[str]:
    return Counter(publication.answer for publication in result.publications)


class CellCausalReferenceTests(unittest.TestCase):
    def test_complete_minimal_antichain_keeps_mixed_cardinalities(self):
        supports = [
            frozenset({"a"}),
            frozenset({"a", "b"}),
            frozenset({"a", "c"}),
            frozenset({"b", "c"}),
        ]
        self.assertEqual(
            set(minimal_antichain(supports)),
            {frozenset({"a"}), frozenset({"b", "c"})},
        )
        minimum_cardinality_only = {
            support for support in supports
            if len(support) == min(map(len, supports))
        }
        self.assertNotEqual(
            minimum_cardinality_only,
            set(minimal_antichain(supports)),
            "minimum-cardinality residual selection must be killed",
        )

        pa, pb, pc = producer(1), producer(2), producer(3)
        sa, sb, sc = source(1, 1), source(1, 2), source(1, 3)
        problem = Problem(
            cells=(
                CellSpec(pa, "a", (OutcomeSpec("a", "bad"),)),
                CellSpec(pb, "b", (OutcomeSpec("b", "bad"),)),
                CellSpec(pc, "c", (OutcomeSpec("c", "bad"),)),
            ),
            source_map=((sa, pa), (sb, pb), (sc, pc)),
            candidates=(
                Candidate(rule(1, 1), (
                    Check(sa, frozenset({"good"})),
                    Check(sb, frozenset({"good"})),
                ), "never-1"),
                Candidate(rule(1, 2), (
                    Check(sa, frozenset({"good"})),
                    Check(sc, frozenset({"good"})),
                ), "never-2"),
            ),
        )
        result = run(problem)
        a = outcome_event_id(pa, problem.cells[0].outcomes[0])
        b = outcome_event_id(pb, problem.cells[1].outcomes[0])
        c = outcome_event_id(pc, problem.cells[2].outcomes[0])
        self.assertEqual(
            set(result.residuals),
            {frozenset({a}), frozenset({b, c})},
        )

    def test_publication_occurrence_has_one_receipt_while_quotient_has_many(self):
        p = producer(1)
        s = source(1, 1)
        problem = Problem(
            cells=(CellSpec(p, "two-a-causes", (
                OutcomeSpec("cause-1", "a", Fraction(1, 4)),
                OutcomeSpec("cause-2", "a", Fraction(1, 4)),
                OutcomeSpec("other", "b", Fraction(1, 2)),
            )),),
            source_map=((s, p),),
            candidates=(Candidate(
                rule(1, 1), (Check(s, frozenset({"a"})),), "ok"
            ),),
        )
        result = run(problem)
        publications = [p for p in result.publications if p.answer == "ok"]
        self.assertEqual(len(publications), 2)
        self.assertTrue(all(len(publication.receipt) == 1
                            for publication in publications))
        family = result.rule_value_explanations()[(rule(1, 1), "ok")]
        self.assertEqual(len(family), 2)
        collapsed_receipt = frozenset().union(*family)
        self.assertNotIn(
            collapsed_receipt,
            family,
            "collapsing alternative explanations into one receipt must be killed",
        )

    def test_aliases_across_applications_share_one_cell(self):
        p = producer(1)
        left, right = source(10, 1, "left"), source(20, 1, "right")
        problem = Problem(
            cells=(CellSpec(p, "coin", (
                OutcomeSpec("a", "a", Fraction(1, 2)),
                OutcomeSpec("b", "b", Fraction(1, 2)),
            )),),
            source_map=((left, p), (right, p)),
            candidates=(
                Candidate(rule(30, 1), (
                    Check(left, frozenset({"a"})),
                    Check(right, frozenset({"a"})),
                ), "aa"),
                Candidate(rule(30, 2), (
                    Check(left, frozenset({"b"})),
                    Check(right, frozenset({"b"})),
                ), "bb"),
            ),
        )
        result = run(problem)
        self.assertEqual(values(result), Counter({"aa": 1, "bb": 1}))
        source_position_keyed = Counter(
            left_value + right_value
            for left_value in ("a", "b")
            for right_value in ("a", "b")
        )
        self.assertNotEqual(
            values(result),
            source_position_keyed,
            "source-position producer identity must be killed",
        )
        self.assertTrue(all(len(world.producer_starts) <= 1 for world in result.worlds))
        self.assertGreaterEqual(
            len({subscription.source for world in result.worlds
                 for subscription in world.subscriptions}),
            2,
        )

    def test_same_origin_resamples_are_independent_generations(self):
        p0, p1 = producer(1, 0), producer(1, 1)
        s0, s1 = source(1, 1), source(1, 2)
        outcomes = (
            OutcomeSpec("a", "a", Fraction(1, 2)),
            OutcomeSpec("b", "b", Fraction(1, 2)),
        )
        candidates = tuple(
            Candidate(rule(1, index), (
                Check(s0, frozenset({left})),
                Check(s1, frozenset({right})),
            ), left + right)
            for index, (left, right) in enumerate(
                (("a", "a"), ("a", "b"), ("b", "a"), ("b", "b")), 1
            )
        )
        problem = Problem(
            cells=(CellSpec(p0, "coin", outcomes), CellSpec(p1, "coin", outcomes)),
            source_map=((s0, p0), (s1, p1)),
            candidates=candidates,
        )
        self.assertEqual(values(run(problem)), Counter({
            "aa": 1, "ab": 1, "ba": 1, "bb": 1,
        }))

    def test_dynamic_lhs_and_rhs_demand_activate_only_when_reached(self):
        px, py, pz = producer(1), producer(2), producer(3)
        sx, sy, sz = source(1, 1), source(1, 2, "nested"), source(1, 3)
        problem = Problem(
            cells=(
                CellSpec(px, "x", (
                    OutcomeSpec("a", "a", Fraction(1, 2)),
                    OutcomeSpec("x", "x", Fraction(1, 2)),
                )),
                CellSpec(py, "y", (
                    OutcomeSpec("b", "b", Fraction(1, 2)),
                    OutcomeSpec("y", "y", Fraction(1, 2)),
                )),
                CellSpec(pz, "z", (OutcomeSpec("z", "z"),)),
            ),
            source_map=((sx, px), (sy, py), (sz, pz)),
            candidates=(Candidate(rule(1, 1), (
                Check(sx, frozenset({"a"})),
                Check(sy, frozenset({"b"}), after=frozenset({0})),
            ), "answer", rhs_demands=(sz,)),),
        )
        result = run(problem)
        self.assertEqual(values(result), Counter({"answer": 1}))
        x_refuted = [world for world in result.worlds
                     if world.candidates[0].phase == "refuted"
                     and dict(world.selected).get(px) == "x"]
        self.assertTrue(x_refuted)
        self.assertTrue(all(py not in dict(world.selected) and
                            pz not in dict(world.selected)
                            for world in x_refuted))
        y_refuted = [world for world in result.worlds
                     if world.candidates[0].phase == "refuted"
                     and dict(world.selected).get(py) == "y"]
        self.assertTrue(y_refuted)
        self.assertTrue(all(pz not in dict(world.selected) for world in y_refuted))
        published = next(world for world in result.worlds if world.publications)
        self.assertEqual(set(dict(published.selected)), {px, py, pz})

    def test_effects_are_causally_closed_and_ordered(self):
        p0, p1 = producer(1), producer(2)
        source1 = source(1, 1)
        e0 = EffectSpec("effect:zero-one", "state", "0", "1")
        e1 = EffectSpec("effect:one-two", "state", "1", "2")
        cell0 = CellSpec(p0, "first", (OutcomeSpec("first", "one", effects=(e0,)),))
        cell1 = CellSpec(
            p1,
            "second",
            (OutcomeSpec("second", "two", effects=(e1,)),),
            dependencies=(p0,),
        )
        problem = Problem(
            cells=(cell0, cell1),
            source_map=((source1, p1),),
            candidates=(Candidate(
                rule(1, 1), (Check(source1, frozenset({"two"})),), "done"
            ),),
            initial_store=(("state", "0"),),
        )
        result = run(problem)
        publication = next(iter(result.publications))
        self.assertIn(e0.occurrence, publication.receipt)
        self.assertIn(e1.occurrence, publication.receipt)
        self.assertTrue(any(dict(world.store)["state"] == "2"
                            for world in result.worlds if world.publications))

    def test_stable_fault_is_one_shared_outcome(self):
        p = producer(1)
        s = source(1, 1)
        problem = Problem(
            cells=(CellSpec(p, "faulting", (
                OutcomeSpec("fault", "fault:e", stable_fault=True),
            )),),
            source_map=((s, p),),
            candidates=(
                Candidate(rule(1, 1), (Check(s, frozenset({"fault:e"})),), "a"),
                Candidate(rule(1, 2), (Check(s, frozenset({"fault:e"})),), "b"),
            ),
        )
        result = run(problem)
        self.assertEqual(values(result), Counter({"a": 1, "b": 1}))
        self.assertTrue(all(len(world.producer_starts) <= 1 for world in result.worlds))
        receipts = [publication.receipt for publication in result.publications]
        self.assertEqual(len(set(receipts)), 1)

    def test_blackhole_and_budget_are_incomplete_not_residual(self):
        p = producer(1)
        s = source(1, 1)
        problem = Problem(
            cells=(CellSpec(p, "cycle", (), cyclic=True),),
            source_map=((s, p),),
            candidates=(Candidate(
                rule(1, 1), (Check(s, frozenset({"a"})),), "never"
            ),),
        )
        blackhole = run(problem)
        self.assertFalse(blackhole.residuals)
        self.assertTrue(any(world.candidates[0].incomplete_reason == "blackhole"
                            for world in blackhole.worlds))

        finite = replace(problem, cells=(CellSpec(
            p, "value", (OutcomeSpec("a", "a"),)
        ),))
        budgeted = run(finite, step_budget=0)
        self.assertFalse(budgeted.residuals)
        self.assertTrue(any(world.candidates[0].incomplete_reason == "budget"
                            for world in budgeted.worlds))

    def test_no_admitted_rule_is_unsupported_not_residual(self):
        result = run(Problem(cells=(), source_map=(), candidates=()))
        self.assertTrue(result.unsupported)
        self.assertFalse(result.residuals)

    def test_persistence_requires_injective_renaming_and_preserves_aliases(self):
        p = producer(1)
        left, right = source(1, 1), source(1, 2)
        problem = Problem(
            cells=(CellSpec(p, "coin", (
                OutcomeSpec("a", "a", Fraction(1, 2)),
                OutcomeSpec("b", "b", Fraction(1, 2)),
            )),),
            source_map=((left, p), (right, p)),
            candidates=(Candidate(rule(1, 1), (
                Check(left, frozenset({"a", "b"})),
                Check(right, frozenset({"a", "b"})),
            ), "same"),),
        )
        imported = import_problem(problem, {p: producer(99, lineage="import")})
        self.assertEqual(len(set(dict(imported.source_map).values())), 1)
        with self.assertRaises(ValueError):
            import_problem(replace(
                problem,
                cells=(problem.cells[0], replace(problem.cells[0], producer=producer(2))),
                source_map=((left, p), (right, producer(2))),
            ), {p: producer(99), producer(2): producer(99)})

    def test_one_cell_serves_typed_subscribers(self):
        p = producer(1)
        s = source(1, 1)
        problem = Problem(
            cells=(CellSpec(p, "one", (
                OutcomeSpec("one", "1", value_type="nat"),
            )),),
            source_map=((s, p),),
            candidates=(
                Candidate(rule(1, 1), (
                    Check(s, frozenset({"1"}), expected_type="nat"),
                ), "nat"),
                Candidate(rule(1, 2), (
                    Check(s, frozenset({"1"}), expected_type="int"),
                ), "int"),
            ),
            conversions=frozenset({("nat", "int")}),
        )
        result = run(problem)
        self.assertEqual(values(result), Counter({"nat": 1, "int": 1}))
        self.assertTrue(all(len(world.producer_starts) <= 1 for world in result.worlds))
        expected = {subscription.expected_type
                    for world in result.worlds
                    for subscription in world.subscriptions}
        self.assertEqual(expected, {"nat", "int"})

    def test_duplicate_rules_and_outcomes_preserve_occurrences(self):
        p = producer(1)
        s = source(1, 1)
        problem = Problem(
            cells=(CellSpec(p, "duplicate-a", (
                OutcomeSpec("a1", "a", Fraction(1, 2)),
                OutcomeSpec("a2", "a", Fraction(1, 2)),
            )),),
            source_map=((s, p),),
            candidates=(
                Candidate(rule(1, 1), (Check(s, frozenset({"a"})),), "same"),
                Candidate(rule(1, 2), (Check(s, frozenset({"a"})),), "same"),
            ),
        )
        result = run(problem)
        self.assertEqual(len(result.publications), 4)
        self.assertEqual(
            {publication.rule for publication in result.publications},
            {rule(1, 1), rule(1, 2)},
        )
        self.assertEqual(len({publication.receipt
                              for publication in result.publications}), 2)

    def test_declaration_and_action_order_are_denotationally_invariant(self):
        p0, p1 = producer(1), producer(2)
        s0, s1 = source(1, 1), source(1, 2)
        problem = Problem(
            cells=(
                CellSpec(p0, "x", (
                    OutcomeSpec("a", "a", Fraction(1, 2)),
                    OutcomeSpec("x", "x", Fraction(1, 2)),
                )),
                CellSpec(p1, "y", (
                    OutcomeSpec("b", "b", Fraction(1, 2)),
                    OutcomeSpec("y", "y", Fraction(1, 2)),
                )),
            ),
            source_map=((s0, p0), (s1, p1)),
            candidates=(
                Candidate(rule(1, 1), (Check(s0, frozenset({"a"})),), "left"),
                Candidate(rule(1, 2), (Check(s1, frozenset({"b"})),), "right"),
                Candidate(rule(1, 3), (
                    Check(s0, frozenset({"a"})),
                    Check(s1, frozenset({"b"})),
                ), "both"),
            ),
        )
        ordinary = run(problem)
        reversed_actions = run(problem, reverse_actions=True)
        self.assertEqual(normalized_result(ordinary), normalized_result(reversed_actions))

        depth_first = run(problem, frontier_policy="depth-first")
        ranked = run(
            problem,
            frontier_policy="ranked",
            priority=lambda state: (
                sum(candidate.phase == "active" for candidate in state.candidates),
                -len(state.publications),
                state.steps,
            ),
        )
        self.assertEqual(normalized_result(ordinary), normalized_result(depth_first))
        self.assertEqual(normalized_result(ordinary), normalized_result(ranked))

        with self.assertRaises(ValueError):
            run(problem, frontier_policy="unknown")
        with self.assertRaises(ValueError):
            run(problem, frontier_policy="ranked")
        with self.assertRaises(ValueError):
            run(problem, priority=lambda state: state.steps)

        reversed_declarations = replace(problem, candidates=tuple(reversed(problem.candidates)))
        self.assertEqual(
            {
                (publication.rule, publication.answer, publication.receipt)
                for publication in ordinary.publications
            },
            {
                (publication.rule, publication.answer, publication.receipt)
                for publication in run(reversed_declarations).publications
            },
        )

    def test_shared_event_probability_is_half_not_naive_three_quarters(self):
        p = producer(1)
        s = source(1, 1)
        problem = Problem(
            cells=(CellSpec(p, "coin", (
                OutcomeSpec("heads", "heads", Fraction(1, 2)),
                OutcomeSpec("tails", "tails", Fraction(1, 2)),
            )),),
            source_map=((s, p),),
            candidates=(
                Candidate(rule(1, 1), (
                    Check(s, frozenset({"heads"})),
                ), "goal"),
                Candidate(rule(1, 2), (
                    Check(s, frozenset({"heads"})),
                ), "goal"),
            ),
        )
        result = run(problem)
        goal_publications = [publication for publication in result.publications
                             if publication.answer == "goal"]
        self.assertEqual(len(goal_publications), 2)
        self.assertEqual(len({publication.receipt
                              for publication in goal_publications}), 1)
        self.assertEqual(exact_probability(result, problem, "goal"), Fraction(1, 2))
        self.assertEqual(
            naive_independent_explanation_probability(result, problem, "goal"),
            Fraction(3, 4),
        )


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(CellCausalReferenceTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    print(
        f"(PrimeCellCausalReferenceSummary {result.testsRun} "
        f"{result.testsRun - len(result.failures) - len(result.errors)} "
        f"{len(result.failures) + len(result.errors)})"
    )
    raise SystemExit(0 if result.wasSuccessful() else 1)
