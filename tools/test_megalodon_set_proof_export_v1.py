#!/usr/bin/env python3
"""Megalodon checks again the set proofs C has checked.

The development with the eleven laws (Part 7) is checked alone first.  Then
C's `power-in-universe-term` is exported and must be accepted; the same tree
with a wrong citation, and the same tree under the converse statement, must be
rejected inside the exported theorem.  A second rule-form theorem
(`power-self`, from the law of the power set), a theorem citing a theorem, a
theorem over seven definitions of the space, and the eleven seeds as C states
them go through the same exporter.  The readable proof and a citation of an
axiom are not exported.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import tempfile
import unittest

import gslt2parse_schema_v1 as sx
import megalodon_set_proof_export_v1 as export

DRAFT = Path(__file__).resolve().parents[1]
POWER_IN_UNIVERSE = DRAFT / "tests/prime/trinity/power-in-universe.metta"
NATIVE_PACKAGE = DRAFT / "tests/prime/scoped/native_proof_package.metta"
UNIVERSE_LAWS = DRAFT / "tests/prime/scoped/hotg_universe_native_use.metta"


class SetProofExportTests(unittest.TestCase):
    cetta: Path
    megalodon: Path
    preamble: Path
    development: Path
    known_cache: dict = {}

    def known(self, source: Path, names: list[str]) -> export.Known:
        key = (source, tuple(names))
        if key not in self.known_cache:
            self.known_cache[key] = export.gather(self.cetta, source, names)
        return self.known_cache[key]

    def article(self, source: Path, theorem: str | None, **options) -> export.Article:
        return export.write_article(
            self.known(source, [theorem] if theorem else []), theorem,
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

    def test_readable_proof_is_not_exported(self):
        with self.assertRaisesRegex(export.CannotRender, "pf:have"):
            self.article(POWER_IN_UNIVERSE, "power-in-universe")

    def test_cited_axiom_is_not_exported(self):
        with self.assertRaisesRegex(export.CannotRender, "native-premise"):
            self.article(NATIVE_PACKAGE, "native-from-premise")


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
