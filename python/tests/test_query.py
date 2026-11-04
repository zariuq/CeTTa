import gc
import subprocess
import sys
import threading
import unittest

from cetta import CettaError, Engine, EvaluationError, Symbol


class QueryTests(unittest.TestCase):
    def test_iteration_batching_and_multiplicity(self):
        with Engine() as engine, engine.query("(superpose (1 2 2 3))") as query:
            self.assertEqual(query.step(0).state, "paused")
            self.assertEqual(query.batch(2).to_python(), [1, 2])
            self.assertEqual(query.to_python(), [2, 3])
            self.assertEqual(query.batch(2).to_python(), [])

    def test_bindings_keep_scoped_variable_identity_and_storage(self):
        engine = Engine()
        engine.run("(= (pick a) 11) (= (pick b) 22)")
        variable = engine.variable("x")
        source = engine.expression(Symbol("pick"), variable)
        with engine.query(source) as query:
            first = next(query)
            second = next(query)
        engine.close()
        del engine, query
        gc.collect()
        self.assertEqual(first.to_python(), 11)
        self.assertEqual(second.to_python(), 22)
        self.assertEqual(first.bindings[variable].to_python(), Symbol("a"))
        self.assertEqual(second.bindings["x"].to_python(), Symbol("b"))
        self.assertEqual(first.bindings.constraints, ())

    def test_independent_query_cleanup(self):
        with Engine() as engine:
            left = engine.query("(superpose (1 2))")
            right = engine.query("(superpose (10 20))")
            self.assertEqual(next(left).to_python(), 1)
            self.assertEqual(next(right).to_python(), 10)
            left.close()
            self.assertEqual(right.to_python(), [20])
            right.close()

    def test_pull_prefix_and_committed_effects(self):
        with Engine() as engine:
            engine.run("(= (emit $x) (let $u (add-atom &self (seen $x)) $x))")
            with engine.query("(emit (superpose (1 2)))") as query:
                self.assertEqual(next(query).to_python(), 1)
                self.assertEqual(engine.eval("(match &self (seen $x) $x)").to_python(), [1])
                query.cancel()
                with self.assertRaises(EvaluationError) as caught:
                    query.step(0)
                self.assertEqual(caught.exception.status, "cancelled")
            self.assertEqual(engine.eval("(match &self (seen $x) $x)").to_python(), [1])
            self.assertEqual(engine.call("+", 1, 2).to_python(), [3])

    def test_source_credit_is_independent_of_slices(self):
        with Engine() as engine:
            outcomes = []
            for allowance in (1, 2, 10000):
                with engine.query("(superpose (1 2 3))", fuel=12) as query:
                    values = []
                    spent = 0
                    status = "complete"
                    for _ in range(1000):
                        idle = query.step(0)
                        self.assertEqual(idle.source_steps, spent)
                        self.assertEqual(idle.physical_steps, 0)
                        try:
                            step = query.step(allowance)
                        except EvaluationError as error:
                            status = error.status
                            values.extend(error.answers.to_python())
                            break
                        self.assertLessEqual(step.physical_steps, allowance)
                        self.assertLessEqual(step.source_steps, 12)
                        spent = step.source_steps
                        values.extend(step.answers.to_python())
                        if step.state == "complete":
                            break
                    else:
                        self.fail("query did not complete or exhaust its source credit")
                    outcomes.append((values, status))
            self.assertEqual(outcomes[0], outcomes[1])
            self.assertEqual(outcomes[1], outcomes[2])

    def test_zero_fuel_and_error_value_are_distinct(self):
        with Engine() as engine:
            with engine.query("(+ 1 2)", fuel=0) as query:
                with self.assertRaises(EvaluationError) as caught:
                    next(query)
                self.assertEqual(caught.exception.status, "fuel-exhausted")
            with engine.query("(Error data reason)") as query:
                self.assertEqual(next(query).to_python(),
                                 (Symbol("Error"), Symbol("data"), Symbol("reason")))
            with engine.query("(/ 1 0)") as query:
                with self.assertRaises(EvaluationError) as caught:
                    next(query)
                self.assertEqual(caught.exception.status, "language-raised")

    def test_collecting_preserves_answers_before_resource_stop(self):
        with Engine() as engine:
            for collect in (lambda q: q.all(), lambda q: q.batch(3)):
                with engine.query("(superpose (1 2 3))", fuel=3) as query:
                    with self.assertRaises(EvaluationError) as caught:
                        collect(query)
                    self.assertEqual(caught.exception.status, "fuel-exhausted")
                    self.assertEqual(caught.exception.answers.to_python(), [1])

    def test_engine_close_and_query_close(self):
        engine = Engine()
        query = engine.query("(superpose (1 2))")
        answer = next(query)
        engine.close()
        with self.assertRaises(CettaError) as caught:
            next(query)
        self.assertEqual(caught.exception.status, "engine-closed")
        query.close()
        self.assertEqual(answer.to_python(), 1)

    def test_foreign_values_and_invalid_allowance(self):
        with Engine() as left, Engine() as right:
            with self.assertRaises(CettaError) as caught:
                right.query(left.parse("42"))
            self.assertEqual(caught.exception.status, "foreign-engine")
            with left.query("42") as query:
                for value in (-1, 2**64, 1.5):
                    with self.assertRaises(ValueError):
                        query.step(value)

    def test_interrupt_closes_query_and_keeps_engine_usable(self):
        program = r'''
import os, signal, threading
from cetta import Engine
engine = Engine(language=LANGUAGE)
engine.run('!(pragma! max-stack-depth 100000000)')
engine.run('(= (spin) (spin))')
query = engine.query('(spin)')
timer = threading.Timer(.05, lambda: os.kill(os.getpid(), signal.SIGINT))
timer.start()
try:
    next(query)
except KeyboardInterrupt:
    pass
else:
    raise AssertionError('interrupt was swallowed')
finally:
    timer.cancel()
    timer.join()
assert query._closed
assert engine.call('+', 20, 22).to_python() == [42]
engine.close()
'''
        for language in ("petta", "he"):
            with self.subTest(language=language):
                result = subprocess.run([sys.executable, "-c", program.replace("LANGUAGE", repr(language))],
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_cross_thread_cancel_and_close(self):
        for close in (False, True):
            with self.subTest(close=close), Engine() as engine:
                engine.run("(= (spin) (spin))")
                query = engine.query("(spin)")
                started = threading.Event()
                errors = []

                def worker():
                    started.set()
                    try:
                        next(query)
                    except CettaError as error:
                        errors.append(error.status)

                thread = threading.Thread(target=worker)
                thread.start()
                self.assertTrue(started.wait(2))
                if close:
                    query.close()
                else:
                    query.cancel()
                thread.join(2)
                self.assertFalse(thread.is_alive())
                self.assertEqual(len(errors), 1)
                self.assertIn(errors[0], ("cancelled", "query-closed"))
                self.assertEqual(engine.call("+", 20, 22).to_python(), [42])


if __name__ == "__main__":
    unittest.main()
