import gc
import unittest

from cetta import AtomKind, CettaError, Engine, Symbol


class SpaceTests(unittest.TestCase):
    def test_facts_are_data_and_preserve_multiplicity(self):
        with Engine() as engine:
            engine.run("(= (row $x) (add-atom &self executed))")
            space = engine.new_space()
            fact = engine.expression(Symbol("row"), 7)
            space.add_atom(fact)
            space.add_atom(fact)
            self.assertEqual(len(space), 2)
            self.assertEqual([a.to_python() for a in space],
                             [(Symbol("row"), 7), (Symbol("row"), 7)])
            self.assertEqual(engine.eval("(match &self executed yes)").to_python(), [])

    def test_pattern_bindings_and_explicit_template(self):
        for language in ("petta", "he"):
            with self.subTest(language=language), Engine(language=language) as engine:
                space = engine.new_space()
                for value in (1, 2, 2):
                    space.add_atom(engine.expression(Symbol("row"), value))
                x = engine.variable("x")
                pattern = engine.expression(Symbol("row"), x)
                with space.match(pattern) as query:
                    answers = list(query)
                self.assertEqual([a.bindings[x].to_python() for a in answers], [1, 2, 2])
                with space.match(pattern, engine.symbol("hit")) as query:
                    answers = list(query)
                self.assertEqual([a.bindings[x].to_python() for a in answers], [1, 2, 2])
                self.assertEqual([a.to_python() for a in answers], [Symbol("hit")] * 3)
                with space.match("(row $x)", "(+ $x 10)") as query:
                    self.assertEqual(query.to_python(), [11, 12, 12])

    def test_repeated_and_distinct_variables(self):
        with Engine() as engine:
            space = engine.new_space()
            space.add_atom(engine.parse("(pair a a)"))
            space.add_atom(engine.parse("(pair a b)"))
            x, y = engine.variable("same"), engine.variable("same")
            with space.match(engine.expression(Symbol("pair"), x, x)) as query:
                answers = list(query)
            self.assertEqual(len(answers), 1)
            self.assertEqual(answers[0].bindings[x].to_python(), Symbol("a"))
            with space.match(engine.expression(Symbol("pair"), x, y)) as query:
                answers = list(query)
            self.assertEqual(len(answers), 2)
            self.assertEqual(answers[1].bindings[x].to_python(), Symbol("a"))
            self.assertEqual(answers[1].bindings[y].to_python(), Symbol("b"))
            with self.assertRaises(KeyError):
                answers[1].bindings["same"]

    def test_root_alias_and_space_atoms(self):
        with Engine() as engine:
            root = engine.space
            before = len(root)
            root.add_atom(engine.parse("(root-fact 42)"))
            self.assertEqual(len(engine.space), before + 1)
            self.assertEqual(engine.eval("(match &self (root-fact $x) $x)").to_python(), [42])
            space = engine.new_space()
            alias = engine.space_from_atom(space.as_atom())
            alias.add_atom(7)
            self.assertEqual(len(space), 1)
            self.assertEqual(engine.atom(space).kind, AtomKind.RESOURCE)
            self.assertEqual(engine.call("get-atoms", space).to_python(), [7])

    def test_language_removal_and_missing_fact(self):
        for language, remaining in (("petta", 0), ("he", 1)):
            with self.subTest(language=language), Engine(language=language) as engine:
                space = engine.new_space()
                space.add_atom(7)
                space.add_atom(7)
                self.assertTrue(space.remove_atom(7))
                self.assertEqual(len(space), remaining)
                self.assertFalse(space.remove_atom(99))

    def test_foreign_engine_handles_are_rejected(self):
        with Engine() as left, Engine() as right:
            space = left.new_space()
            operations = [lambda: space.add_atom(right.atom(1)),
                          lambda: space.match(right.parse("$x")),
                          lambda: right.space_from_atom(space.as_atom())]
            for operation in operations:
                with self.assertRaises(CettaError) as caught:
                    operation()
                self.assertEqual(caught.exception.status, "foreign-engine")

    def test_space_match_owns_its_target_and_returned_bindings(self):
        engine = Engine()
        space = engine.new_space()
        space.add_atom(engine.parse("(row 42)"))
        query = space.match("(row $x)")
        del space
        gc.collect()
        answer = next(query)
        query.close()
        engine.close()
        del engine, query
        gc.collect()
        self.assertEqual(answer.to_python(), (Symbol("row"), 42))
        self.assertEqual(answer.bindings["x"].to_python(), 42)

    def test_closed_engine_prevents_space_operations(self):
        engine = Engine()
        space = engine.new_space()
        engine.close()
        for operation in (lambda: len(space), lambda: space.add_atom(1), lambda: space.match("$x")):
            with self.assertRaises(CettaError) as caught:
                operation()
            self.assertEqual(caught.exception.status, "engine-closed")


if __name__ == "__main__":
    unittest.main()
