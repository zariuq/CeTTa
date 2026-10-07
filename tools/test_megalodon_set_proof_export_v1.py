#!/usr/bin/env python3
"""Megalodon checks again the set proofs C has checked.

The development with the eleven laws (Part 7) is checked alone first.  Then
C's `power-in-universe-term` is exported and must be accepted; the same tree
with a wrong citation, and the same tree under the converse statement, must be
rejected inside the exported theorem.  A second rule-form theorem
(`power-self`, from the law of the power set), a theorem citing a theorem, a
theorem over seven definitions of the space, and the eleven seeds as C states
them go through the same exporter.  A citation of an axiom outside
Megalodon's foundation is not exported.

Readable proofs are: C compiles a local lemma (pf:have) as a redex and a
pf:by step as the eliminations its checker found, and its package carries
the proof so compiled.  The readable proof of power-in-universe, and Cantor's
core proved both in the rules of the logic and sentence by sentence, are
written with Megalodon's claim for each local lemma and must be accepted; the
readable core with one of its lemmas stated of the other set must be
rejected.

Every theorem of the Naproche-ZF curriculum, in its space &thy, is exported
with what it cites and must be accepted; its pairs rest on the law of the
choice operator, which the article states as C states it and proves by
Megalodon's Eps_i_ax.  C's package of a theorem cites the earlier theorems
by name, as the article does; for every theorem written, the package
declares exactly what its proof cites.  The same tree with the choice law cited wrongly, and
the proof that pairs agree in their first members stated of the second
members, must be rejected.

Every theorem of the same curriculum with the signature's equation as the
identity of sets, whose theory adopts the rule of substitution, must be
accepted too: each substitution step becomes Megalodon's own equality applied
to a relation.  A substitution that rests on no law cites none; the same tree
stated with the membership moved the other way must be rejected.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import tempfile
import unittest

import gslt2parse_schema_v1 as sx
import megalodon_set_proof_export_v1 as export

DRAFT = Path(__file__).resolve().parents[1]
POWER_IN_UNIVERSE = DRAFT / "tests/prime/profiles/megalodon_hotg/trinity/power-in-universe.metta"
NATIVE_PACKAGE = DRAFT / "tests/prime/profiles/megalodon_hotg/scoped/native_proof_package.metta"
UNIVERSE_LAWS = DRAFT / "tests/prime/profiles/megalodon_hotg/scoped/hotg_universe_native_use.metta"
NAPROCHE = DRAFT / "tests/prime/profiles/megalodon_hotg/scoped/curriculum_naproche.metta"
NAPROCHE_EQ = DRAFT / "tests/prime/profiles/megalodon_hotg/scoped/curriculum_naproche_eq.metta"
CANTOR_READABLE = DRAFT / "tests/prime/profiles/megalodon_hotg/scoped/cantor_readable.metta"


def naproche_theorems(source: Path = NAPROCHE) -> list[str]:
    """The theorems a curriculum publishes, in its order."""
    return re.findall(r"^!\(pf:theorem &thy (\S+)", source.read_text(encoding="utf-8"), re.M)


class SetProofExportTests(unittest.TestCase):
    cetta: Path
    megalodon: Path
    preamble: Path
    development: Path
    known_cache: dict = {}

    def known(self, source: Path, names: list[str], space: str | None = None) -> export.Known:
        key = (source, tuple(names), space)
        if key not in self.known_cache:
            self.known_cache[key] = export.gather(self.cetta, source, names, space)
        return self.known_cache[key]

    def article(self, source: Path, theorem: str | None, **options) -> export.Article:
        return export.write_article(
            self.known(source, [theorem] if theorem else []), theorem,
            self.development.read_text(encoding="utf-8"), export.source_label(source),
            **options)

    def naproche_article(self, theorem: str, source: Path = NAPROCHE,
                         **options) -> export.Article:
        """One article of a curriculum; C is asked once about all of it."""
        return export.write_article(
            self.known(source, naproche_theorems(source), "&thy"), theorem,
            self.development.read_text(encoding="utf-8"), export.source_label(source),
            **options)

    def check(self, article: export.Article):
        with tempfile.TemporaryDirectory(prefix="megalodon-set-export-") as directory:
            path = Path(directory) / "article.mg"
            path.write_text(article.text, encoding="utf-8")
            return export.check_article(self.megalodon, self.preamble, path)

    def accepted(self, article: export.Article) -> None:
        run = self.check(article)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(run.stdout + run.stderr, "")

    def rejected_inside_theorem(self, article: export.Article) -> None:
        run = self.check(article)
        self.assertNotEqual(run.returncode, 0, "Megalodon accepted an altered article")
        line = export.failure_line(run)
        first, last = article.theorem_lines
        self.assertIsNotNone(line, run.stdout + run.stderr)
        self.assertTrue(first <= line <= last,
                        f"rejected at line {line}, outside the theorem ({first}-{last}): "
                        + run.stdout + run.stderr)

    def theorem_text(self, article: export.Article) -> str:
        first, last = article.theorem_lines
        return "\n".join(article.text.splitlines()[first - 1:last])

    def test_0_development_checks_alone(self):
        run = export.check_article(self.megalodon, self.preamble, self.development)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(run.stdout + run.stderr, "")

    def test_power_in_universe_term(self):
        article = self.article(POWER_IN_UNIVERSE, "power-in-universe-term")
        known = self.known(POWER_IN_UNIVERSE, ["power-in-universe-term"])
        self.assertEqual(set(known.theorems["power-in-universe-term"].assumptions),
                         {"universeClosed", "universeIn"})
        self.assertEqual(article.cited_seeds, ("universeClosed", "universeIn"))
        body = self.theorem_text(article)
        self.assertIn("Theorem prime_power_in_universe_term : forall N:set, Power N :e UnivOf N.",
                      body)
        self.assertIn("law_universe_closed", body)
        self.assertIn("law_universe_in", body)
        self.assertTrue(article.text.startswith(self.development.read_text(encoding="utf-8")
                                                .rstrip("\n")))
        self.accepted(article)

    def test_wrong_citation_is_rejected(self):
        cite = dict(export.SEED_LAWS, universeClosed="law_universe_transitive")
        article = self.article(POWER_IN_UNIVERSE, "power-in-universe-term", cite=cite)
        body = self.theorem_text(article)
        self.assertIn("law_universe_transitive", body)
        self.assertNotIn("law_universe_closed", body)
        self.rejected_inside_theorem(article)

    def test_converse_statement_is_rejected(self):
        converse, = sx.parse_sexprs("(all set (lam N (In (UnivOf N) (Power N))))")
        article = self.article(POWER_IN_UNIVERSE, "power-in-universe-term", statement=converse)
        self.assertIn("forall N:set, UnivOf N :e Power N.", self.theorem_text(article))
        self.rejected_inside_theorem(article)
        # C refuses the same tree for the converse.
        tree = self.known(POWER_IN_UNIVERSE,
                          ["power-in-universe-term"]).theorems["power-in-universe-term"].proof
        answer, = export.run_queries(self.cetta, POWER_IN_UNIVERSE,
                                     [f"!(set:proves {sx.render(tree)} {sx.render(converse)})"])
        self.assertNotEqual(answer, sx.Symbol("True"))

    def test_second_rule_form_theorem_power_self(self):
        article = self.article(NATIVE_PACKAGE, "power-self")
        self.assertEqual(article.cited_seeds, ("powerLaw",))
        self.assertIn("Theorem prime_power_self : forall x:set, x :e Power x.",
                      self.theorem_text(article))
        self.accepted(article)

    def test_cited_theorem_is_exported_first(self):
        article = self.article(NATIVE_PACKAGE, "native-power-empty")
        self.assertEqual(article.theorems, ("power-self", "native-power-empty"))
        self.assertIn("exact prime_power_self Empty.", self.theorem_text(article))
        # C's package cites the theorem by name, and assumes the law only
        # through it.
        theorems = self.known(NATIVE_PACKAGE, ["native-power-empty"]).theorems
        self.assertEqual(theorems["native-power-empty"].assumptions, ("power-self",))
        self.assertEqual(theorems["power-self"].assumptions, ("powerLaw",))
        self.accepted(article)

    def test_definitions_of_the_space(self):
        article = self.article(UNIVERSE_LAWS, "UnivOf_Min")
        self.assertEqual(set(article.definitions),
                         {"and", "Subq", "TransSet", "Union_closed", "Power_closed",
                          "Repl_closed", "ZF_closed"})
        self.assertIn("Definition prime_ZF_closed : set->prop :=", article.text)
        self.accepted(article)

    def test_the_eleven_seeds_are_the_part_7_laws(self):
        article = self.article(POWER_IN_UNIVERSE, None, seeds=list(export.SEED_LAWS))
        self.assertEqual(article.text.count("Theorem prime_seed_"), 11)
        self.accepted(article)

    def test_readable_proof_is_exported(self):
        article = self.article(POWER_IN_UNIVERSE, "power-in-universe")
        body = self.theorem_text(article)
        # Each local lemma is a claim; no readable step is left in the proof.
        self.assertEqual(body.count("claim "), 2, body)
        self.assertNotIn("pf:", body)
        self.assertEqual(article.cited_seeds, ("universeClosed", "universeIn"))
        self.accepted(article)

    def readable_article(self, theorem: str, **options) -> export.Article:
        return export.write_article(
            self.known(CANTOR_READABLE, ["cantor-core", "cantor-core-readable"], "&thy"),
            theorem, self.development.read_text(encoding="utf-8"),
            export.source_label(CANTOR_READABLE), **options)

    def test_cantor_core_both_ways(self):
        rules = self.readable_article("cantor-core")
        readable = self.readable_article("cantor-core-readable")
        known = self.known(CANTOR_READABLE, ["cantor-core", "cantor-core-readable"], "&thy")
        # C's package of the readable proof carries its pf:by steps as
        # eliminations, and keeps its local lemmas.
        proof = sx.render(known.theorems["cantor-core-readable"].proof)
        self.assertNotIn("pf:by", proof)
        self.assertEqual(proof.count("(pf:have "), 7)
        body = self.theorem_text(readable)
        self.assertEqual(body.count("claim "), 7, body)
        self.assertIn("Definition prime_diagonal : set->set->set :=", readable.text)
        self.assertEqual(set(rules.theorems[:-1]), set(readable.theorems[:-1]))
        self.accepted(rules)
        self.accepted(readable)

    def test_cantor_core_readable_wrong_lemma_is_rejected(self):
        # The first lemma, that B is in the power set of A, stated of the
        # power set of B instead.
        known = self.known(CANTOR_READABLE, ["cantor-core", "cantor-core-readable"], "&thy")
        proof = known.theorems["cantor-core-readable"].proof
        rendered = sx.render(proof)
        wrong = rendered.replace("(pf:have B-in-power (In (diagonal A f) (Power A))",
                                 "(pf:have B-in-power (In (diagonal A f) (Power (diagonal A f)))", 1)
        self.assertNotEqual(wrong, rendered)
        altered, = sx.parse_sexprs(wrong)
        theorems = dict(known.theorems)
        theorems["cantor-core-readable"] = export.Theorem(
            "cantor-core-readable", known.theorems["cantor-core-readable"].proposition, altered,
            known.theorems["cantor-core-readable"].assumptions)
        altered_known = export.Known(theorems, known.assumed, known.propositions,
                                     known.verdicts, known.definitions, known.unwritable)
        article = export.write_article(
            altered_known, "cantor-core-readable", self.development.read_text(encoding="utf-8"),
            export.source_label(CANTOR_READABLE), statement=known.theorems[
                "cantor-core-readable"].proposition)
        self.rejected_inside_theorem(article)
        # C refuses the same proof.
        answer, = export.run_queries(
            self.cetta, CANTOR_READABLE,
            [f"!(set:proves &thy {sx.render(altered)} "
             f"{sx.render(known.theorems['cantor-core-readable'].proposition)})"])
        self.assertNotEqual(answer, sx.Symbol("True"))

    def test_cited_axiom_is_not_exported(self):
        with self.assertRaisesRegex(export.CannotRender, "native-premise"):
            self.article(NATIVE_PACKAGE, "native-from-premise")

    def test_naproche_curriculum_every_theorem(self):
        names = naproche_theorems(NAPROCHE)
        self.assertGreater(len(names), 0)
        self.assertTrue({"russell", "kpair-inj", "cantor", "burali-forti"} <= set(names))
        rejected = []
        for name in names:
            run = self.check(self.naproche_article(name))
            if run.returncode != 0 or run.stdout + run.stderr:
                rejected.append((name, (run.stdout + run.stderr)[:300]))
        self.assertEqual(rejected, [])

    def test_naproche_choice_is_the_foundation_axiom(self):
        article = self.naproche_article("kpair-inj")
        self.assertEqual(article.cited_axioms, ("EpsI",))
        # The package of kpair-inj cites its two lemmas by name; the choice law
        # is reached through them, and the article writes them before it.
        known = self.known(NAPROCHE, naproche_theorems(NAPROCHE), "&thy")
        self.assertEqual(known.theorems["kpair-inj"].assumptions,
                         ("kpair-inj-first", "kpair-inj-second"))
        self.assertEqual(article.theorems[-1], "kpair-inj")
        self.assertIn("kpair-inj-first", article.theorems[:-1])
        self.assertIn("kpair-inj-second", article.theorems[:-1])
        self.assertIn("Theorem prime_axiom_EpsI : forall P:set->prop, forall x:set, P x -> "
                      "P (Eps_i P).\nexact Eps_i_ax.", article.text)
        self.assertIn("prime_kpair_inj_first", article.text)
        # Russell's antinomy and Cantor's theorem use no choice.
        self.assertEqual(self.naproche_article("russell").cited_axioms, ())
        self.assertEqual(self.naproche_article("cantor").cited_axioms, ())
        # A definition may not use Megalodon's polymorphic existence or equality.
        self.assertIn("Definition prime_ex : (set->prop)->prop := fun (P:set->prop) => "
                      "forall p:prop, (forall x:set, P x -> p) -> p.",
                      self.naproche_article("no-universal-set").text)

    def test_naproche_wrong_choice_citation_is_rejected(self):
        cite = dict(export.SEED_LAWS, EpsI="law_empty")
        article = self.naproche_article("kpair-inj", cite=cite)
        lines = article.text.splitlines()
        block = lines.index("Theorem prime_axiom_EpsI : forall P:set->prop, forall x:set, "
                            "P x -> P (Eps_i P).") + 1
        self.assertEqual(lines[block], "exact law_empty.")
        run = self.check(article)
        self.assertNotEqual(run.returncode, 0, "Megalodon accepted a wrong choice law")
        self.assertEqual(export.failure_line(run), block + 1, run.stdout + run.stderr)

    def test_naproche_other_member_is_rejected(self):
        # The proof that pairs agree in their first members, stated of the
        # second members.
        other, = sx.parse_sexprs(
            "(all set (lam a (all set (lam b (all set (lam c (all set (lam d "
            "(imp (same (kpair a b) (kpair c d)) (same b d))))))))))")
        article = self.naproche_article("kpair-inj-first", statement=other)
        self.assertIn("prime_same b d.", self.theorem_text(article))
        self.rejected_inside_theorem(article)
        # C refuses the same tree for that statement.
        tree = self.known(NAPROCHE, naproche_theorems(),
                          "&thy").theorems["kpair-inj-first"].proof
        answer, = export.run_queries(self.cetta, NAPROCHE,
                                     [f"!(set:proves &thy {sx.render(tree)} {sx.render(other)})"])
        self.assertNotEqual(answer, sx.Symbol("True"))

    def test_naproche_eq_curriculum_every_theorem(self):
        names = naproche_theorems(NAPROCHE_EQ)
        self.assertTrue({"eq-sym", "eq-trans", "kpair-inj", "cantor-fn", "eq-in-empty",
                         "set-ext-identity"} <= set(names))
        self.assertNotIn("same-to-eq", names)
        rejected = []
        for name in names:
            run = self.check(self.naproche_article(name, NAPROCHE_EQ))
            if run.returncode != 0 or run.stdout + run.stderr:
                rejected.append((name, (run.stdout + run.stderr)[:300]))
        self.assertEqual(rejected, [])

    def test_naproche_eq_substitution_is_megalodon_equality(self):
        article = self.naproche_article("eq-in-empty", NAPROCHE_EQ)
        # substitution rests on no law and no axiom
        self.assertEqual(article.cited_seeds, ())
        self.assertEqual(article.cited_axioms, ())
        body = self.theorem_text(article)
        self.assertIn("Theorem prime_eq_in_empty : forall x:set, forall y:set, "
                      "x = y -> x :e Empty -> y :e Empty.", body)
        self.assertRegex(body, r"exact e \(fun (\w+):set => fun (\w+):set => \2 :e Empty -> "
                               r"y :e Empty\) \(fun (\w+):\(y :e Empty\) => \3\) h\.")
        self.accepted(article)
        # Symmetry rests on the reflexivity extensionality gives.
        self.assertEqual(self.naproche_article("eq-sym", NAPROCHE_EQ).cited_seeds,
                         ("extensionality",))

    def test_naproche_eq_membership_moved_back_is_rejected(self):
        # The substitution of eq-in-empty, stated with the membership moved
        # from y to x.
        back, = sx.parse_sexprs(
            "(all set (lam x (all set (lam y (imp (eq set x y) (imp (In y Empty) (In x Empty)))))))")
        article = self.naproche_article("eq-in-empty", NAPROCHE_EQ, statement=back)
        self.assertIn("x = y -> y :e Empty -> x :e Empty.", self.theorem_text(article))
        self.rejected_inside_theorem(article)
        # C refuses the same tree for that statement.
        tree = self.known(NAPROCHE_EQ, naproche_theorems(NAPROCHE_EQ),
                          "&thy").theorems["eq-in-empty"].proof
        answer, = export.run_queries(self.cetta, NAPROCHE_EQ,
                                     [f"!(set:proves &thy {sx.render(tree)} {sx.render(back)})"])
        self.assertNotEqual(answer, sx.Symbol("True"))

    def test_naproche_theorems_live_in_their_space(self):
        with self.assertRaisesRegex(export.CannotRender, "no native package of russell"):
            export.write_article(
                self.known(NAPROCHE, ["russell"]), "russell",
                self.development.read_text(encoding="utf-8"), export.source_label(NAPROCHE))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--megalodon", type=Path, required=True)
    parser.add_argument("--hotg-preamble", type=Path, required=True)
    parser.add_argument("--development", type=Path, required=True)
    args = parser.parse_args()
    SetProofExportTests.cetta = args.cetta
    SetProofExportTests.megalodon = args.megalodon
    SetProofExportTests.preamble = args.hotg_preamble
    SetProofExportTests.development = args.development
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(SetProofExportTests))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
