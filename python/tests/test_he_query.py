import gc
from itertools import product
import traceback
import unittest

from cetta import AtomKind, Callback, Engine, EvaluationError, Symbol


class HEQueryTests(unittest.TestCase):
    def first(self, query, allowance):
        for _ in range(2000):
            idle = query.step(0)
            self.assertEqual(idle.physical_steps, 0)
            step = query.step(allowance)
            self.assertLessEqual(step.physical_steps, allowance)
            if step.answers:
                self.assertEqual(len(step.answers), 1)
                return step.answers[0]
            self.assertNotEqual(step.state, "complete")
        self.fail("HE query did not publish its next answer")

    def seen(self, engine, head="seen"):
        return engine.eval(f"(match &self ({head} $x) $x)").to_python()

    def test_completed_call_prefix_and_cancellation(self):
        for profile in ("he", "he-compat"):
            for allowance in (1, 2, 10000):
                with self.subTest(profile=profile, allowance=allowance), Engine(language="he", profile=profile) as engine:
                    engine.run("(= (emit $x) (let $u (add-atom &self (seen $x)) $x))")
                    with engine.query("(emit (superpose (1 2)))") as query:
                        self.assertEqual(query.step(0).answers.to_python(), [])
                        self.assertEqual(self.first(query, allowance).to_python(), 1)
                        self.assertEqual(self.seen(engine), [1])
                        query.cancel()
                        with self.assertRaises(EvaluationError) as caught:
                            query.step(0)
                        self.assertEqual(caught.exception.status, "cancelled")
                    self.assertEqual(self.seen(engine), [1])
                    self.assertEqual(engine.call("+", 1, 2).to_python(), [3])

    def test_control_and_match_bodies_publish_one_completed_branch(self):
        sources = (
            "(let $x (superpose (1 2)) (emit $x))",
            "(if (superpose (True False)) (emit 1) (emit 2))",
            "(match &self (row $x) (emit $x))",
        )
        for source in sources:
            for allowance in (1, 2, 10000):
                with self.subTest(source=source, allowance=allowance), Engine(language="he") as engine:
                    engine.run("(row 1) (row 2) (= (emit $x) (let $u (add-atom &self (seen $x)) $x))")
                    with engine.query(source) as query:
                        first = self.first(query, allowance).to_python()
                        self.assertIn(first, (1, 2))
                        self.assertEqual(self.seen(engine), [first])
                        second = self.first(query, allowance).to_python()
                        self.assertCountEqual([first, second], [1, 2])
                        self.assertCountEqual(self.seen(engine), [1, 2])
                        self.assertEqual(query.to_python(), [])

    def test_frontier_normalization_defers_errors_and_preserves_duplicates(self):
        cases = (
            ("(superpose ((Error a e) 7 7))", [7, 7]),
            ("(superpose ((Error a e) (Error b e)))",
             [(Symbol("Error"), Symbol("a"), Symbol("e")), (Symbol("Error"), Symbol("b"), Symbol("e"))]),
            ("(superpose (Empty 7 Empty))", [7]),
            ("(superpose (Empty (Error a e) Empty))", [(Symbol("Error"), Symbol("a"), Symbol("e"))]),
        )
        with Engine(language="he") as engine:
            for source, expected in cases:
                with self.subTest(source=source):
                    self.assertEqual(engine.eval(source).to_python(), expected)
                    for allowance in (1, 2, 10000):
                        with engine.query(source) as query:
                            actual = []
                            for _ in range(2000):
                                step = query.step(allowance)
                                actual.extend(step.answers.to_python())
                                if step.state == "complete": break
                            else: self.fail("normalized HE producer did not complete")
                            self.assertEqual(actual, expected)

    def test_frontier_classifications_at_every_alternative_position(self):
        with Engine(language="he") as engine:
            for size in range(5):
                for kinds in product(range(3), repeat=size):
                    terms = ["Empty" if kind == 0 else f"(Error {i} reason)" if kind == 1
                             else str(i // 2) for i, kind in enumerate(kinds)]
                    successes = [i // 2 for i, kind in enumerate(kinds) if kind == 2]
                    errors = [(Symbol("Error"), i, Symbol("reason"))
                              for i, kind in enumerate(kinds) if kind == 1]
                    expected = successes or errors
                    source = "(superpose (" + " ".join(terms) + "))"
                    with self.subTest(source=source):
                        self.assertEqual(engine.eval(source).to_python(), expected)
                        for allowance in (1, 2, 10000):
                            with engine.query(source) as query:
                                actual = []
                                for _ in range(2000):
                                    step = query.step(allowance)
                                    actual.extend(step.answers.to_python())
                                    if step.state == "complete": break
                                else: self.fail("HE normalization did not close")
                                self.assertEqual(actual, expected)

    def test_owned_function_returns_stop_before_later_body_effects(self):
        sources = (
            "(function (chain (metta (superpose (1 2)) %Undefined% &self) $x "
            "(chain (metta (emit $x) %Undefined% &self) $r (return $r))))",
            "(function (let $x (superpose (1 2)) (let $u (add-atom &self (seen $x)) (return $x))))",
            "(function (superpose ((let $u (add-atom &self (seen 1)) (return 1)) "
            "(let $u (add-atom &self (seen 2)) (return 2)))))",
        )
        for profile in ("he", "he-compat"):
            for allowance in (1, 2, 10000):
                for source in sources:
                    with self.subTest(profile=profile, allowance=allowance, source=source), Engine(language="he", profile=profile) as engine:
                        engine.run("(= (emit $x) (let $u (add-atom &self (seen $x)) $x))")
                        with engine.query(source) as query:
                            self.assertEqual(self.first(query, allowance).to_python(), 1)
                            self.assertEqual(self.seen(engine), [1])
                        self.assertEqual(self.seen(engine), [1])

    def test_function_return_syntax_keeps_its_one_step_observation(self):
        cases = (
            ("(function (return (+ 1 2)))", [(Symbol("+"), 1, 2)]),
            ("(metta (function (return (+ 1 2))) Atom &self)", [(Symbol("function"), (Symbol("return"), (Symbol("+"), 1, 2)))]),
        )
        with Engine(language="he") as engine:
            for source, expected in cases:
                with self.subTest(source=source):
                    self.assertEqual(engine.eval(source).to_python(), expected)
                    with engine.query(source) as query:
                        self.assertEqual(query.to_python(), expected)

    def test_held_code_shaped_values_stay_data_after_close(self):
        engine = Engine(language="he")
        engine.run("(: held (-> Atom Atom)) (= (held $x) $x)")
        with engine.query("(held (add-atom &self forbidden))") as query:
            answer = next(query)
            self.assertEqual(query.to_python(), [])
        self.assertEqual(engine.eval("(match &self forbidden yes)").to_python(), [])
        engine.close()
        del engine, query
        gc.collect()
        self.assertEqual(answer.children[0].to_python(), Symbol("add-atom"))

    def test_input_frontier_closes_before_outer_body_runs(self):
        with Engine(language="he") as engine:
            engine.run("""
(= (produce $x) (let $u (add-atom &self (produced $x)) $x))
(= (consume $x) (let $u (add-atom &self (consumed $x)) $x))
""")
            with engine.query("(consume (produce (superpose (1 2))))") as query:
                self.assertEqual(next(query).to_python(), 1)
                self.assertCountEqual(self.seen(engine, "produced"), [1, 2])
                self.assertEqual(self.seen(engine, "consumed"), [1])

    def test_scoped_capture_and_interleaved_queries(self):
        with Engine(language="he") as engine:
            engine.run("(= (emit $x) (let $u (add-atom &self (seen $x)) $x))")
            left = engine.query("(emit (superpose (1 2)))")
            right = engine.query("(emit (superpose (10 20)))")
            self.assertEqual(self.first(left, 1).to_python(), 1)
            self.assertEqual(self.first(right, 2).to_python(), 10)
            self.assertCountEqual(self.seen(engine), [1, 10])
            left.close()
            self.assertEqual(self.first(right, 1).to_python(), 20)
            right.close()
            self.assertCountEqual(self.seen(engine), [1, 10, 20])

    def test_owned_callback_stream_pulls_once_and_closes_on_refusal(self):
        for allowance in (1, 2, 10000):
            with self.subTest(allowance=allowance), Engine(language="he") as engine:
                pulls, closes = [], []
                def values():
                    try:
                        for i in range(3):
                            pulls.append(i)
                            yield i
                    finally:
                        closes.append(1)
                engine.register_callback("host:values", Callback(values, 0, generator=True))
                with engine.query("(host:values)") as query:
                    self.assertEqual(self.first(query, allowance).to_python(), 0)
                    self.assertEqual(pulls, [0])
                self.assertEqual(pulls, [0])
                self.assertEqual(closes, [1])

    def test_pure_function_fallthrough_does_not_block_later_returns(self):
        for profile in ("he", "he-compat"):
            for allowance in (1, 2, 10000):
                with self.subTest(profile=profile, allowance=allowance), Engine(language="he", profile=profile) as engine:
                    pulls, closes = [], []

                    def values():
                        try:
                            for value in (7, (Symbol("return"), 11), (Symbol("return"), 13)):
                                pulls.append(value)
                                yield value
                        finally:
                            closes.append(1)

                    engine.register_callback("host:mixed", Callback(values, 0, generator=True))
                    if profile == "he":
                        self.assertEqual(engine.eval("(function (host:mixed))").to_python(), [11, 13])
                        pulls.clear()
                        closes.clear()
                    with engine.query("(function (host:mixed))") as query:
                        if profile == "he-compat":
                            self.assertEqual(self.first(query, allowance).kind, AtomKind.VARIABLE)
                            self.assertEqual(pulls, [7])
                        self.assertEqual(self.first(query, allowance).to_python(), 11)
                        self.assertEqual(pulls, [7, (Symbol("return"), 11)])
                    self.assertEqual(closes, [1])

    def test_open_call_keeps_each_returned_binding(self):
        with Engine(language="he") as engine:
            engine.run("(= (choose a) 1) (= (choose b) 2)")
            with engine.query("(choose $x)") as query:
                a, b = next(query), next(query)
            self.assertEqual((a.to_python(), b.to_python()), (1, 2))
            self.assertEqual(a.bindings["x"].to_python(), Symbol("a"))
            self.assertEqual(b.bindings["x"].to_python(), Symbol("b"))

    def test_callback_exception_preserves_its_prefix_and_traceback(self):
        for allowance in (1, 2, 10000):
            with self.subTest(allowance=allowance), Engine(language="he") as engine:
                failure = ValueError("HE stream after its first answer")
                closes = []

                def broken():
                    try:
                        yield 7
                        raise failure
                    finally:
                        closes.append(1)

                engine.register_callback("host:broken", Callback(broken, 0, generator=True))
                with engine.query("(host:broken)") as query:
                    self.assertEqual(self.first(query, allowance).to_python(), 7)
                    try:
                        query.to_python()
                    except ValueError as error:
                        self.assertIs(error, failure)
                        self.assertIn("broken", [f.name for f in traceback.extract_tb(error.__traceback__)])
                    else:
                        self.fail("HE callback exception was discarded")
                self.assertEqual(closes, [1])
                self.assertFalse(engine._invocations)
                self.assertEqual(engine.eval("(+ 20 22)").to_python(), [42])

    def test_callback_keyboard_interrupt_closes_the_query(self):
        with Engine(language="he") as engine:
            closes = []

            def interrupted():
                try:
                    yield 7
                    raise KeyboardInterrupt
                finally:
                    closes.append(1)

            engine.register_callback("host:interrupted", Callback(interrupted, 0, generator=True))
            with engine.query("(host:interrupted)") as query:
                self.assertEqual(next(query).to_python(), 7)
                with self.assertRaises(KeyboardInterrupt):
                    next(query)
            self.assertEqual(closes, [1])
            self.assertFalse(engine._invocations)
            self.assertEqual(engine.eval("(+ 1 2)").to_python(), [3])


if __name__ == "__main__":
    unittest.main()
