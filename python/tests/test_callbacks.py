import gc
import traceback
import unittest
import weakref
import sys
import subprocess
import os
from unittest.mock import patch
from concurrent.futures import ThreadPoolExecutor

from cetta import Atom, Callback, CettaError, Engine, Symbol


class CallbackTests(unittest.TestCase):
    def test_callback_can_close_its_entered_query_or_engine(self):
        for language in ("petta", "he"):
            for close_engine in (False, True):
                with self.subTest(language=language, close_engine=close_engine), Engine(language=language) as engine:
                    variable = engine.variable("same")
                    retained = engine.expression(Symbol("kept"), variable, variable)
                    query = None

                    def close_current():
                        if close_engine:
                            engine.close()
                        else:
                            query.close()
                        return 7

                    engine.register_callback("host:close-current", Callback(close_current, 0))
                    query = engine.query("(host:close-current)")
                    with self.assertRaises(CettaError) as caught:
                        next(query)
                    self.assertEqual(caught.exception.status, "engine-closed" if close_engine else "cancelled")
                    self.assertTrue(query._closed)
                    self.assertFalse(engine._invocations)
                    self.assertEqual(retained.children[0].to_python(), Symbol("kept"))
                    self.assertEqual(retained.children[1], retained.children[2])
                    if not close_engine:
                        self.assertEqual(engine.call("+", 20, 22).to_python(), [42])

    def test_parallel_dynamic_callback_from_space(self):
        program = '''from cetta import Engine, Callback, Symbol
import threading
with Engine() as engine:
    calls = []
    def identity(value):
        calls.append(threading.get_ident())
        return value
    callback = engine.register_callback("host:id", Callback(identity, 1))
    engine.space.add_atom(engine.expression(Symbol("host-value"), callback))
    engine.run("!(pragma! num-threads 2)")
    answers = engine.eval("(collapse (hyperpose ((match &self (host-value $f) ($f 1)) (match &self (host-value $g) ($g 2)))))").to_python()
    assert len(answers) == 1 and sorted(answers[0]) == [1, 2], answers
    assert calls == [threading.get_ident()] * 2, calls
    assert not engine._invocations
'''
        completed = subprocess.run([sys.executable, "-c", program], env=os.environ.copy(),
                                   capture_output=True, text=True, timeout=10)
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)

    def test_single_calls_and_value_conversion(self):
        for language in ("petta", "he"):
            with self.subTest(language=language), Engine(language=language) as engine:
                function = engine.register_callback("host:twice", Callback(lambda x: 2 * x, 1))
                self.assertEqual(engine.call("host:twice", 21).to_python(), [42])
                self.assertEqual(engine.call(function, 11).to_python(), [22])
                self.assertEqual(engine.run("!(host:twice 5)").to_python(), [10])
        with Engine() as engine:
            engine.register_callback("host:truth", Callback(lambda: True, 0))
            with engine.query("(host:truth)") as query:
                self.assertEqual(query.to_python(), [Symbol("true")])

    def test_generator_prefix_and_close(self):
        events = []

        def values(count):
            events.append("start")
            try:
                for i in range(count):
                    events.append(i)
                    yield i // 2
            finally:
                events.append("close")

        with Engine() as engine:
            engine.register_callback("host:values", Callback(values, 1, generator=True))
            query = engine.query("(host:values 6)")
            self.assertEqual(query.step(0).state, "paused")
            self.assertEqual(events, [])
            self.assertEqual(next(query).to_python(), 0)
            self.assertEqual(events, ["start", 0])
            query.close()
            self.assertEqual(events, ["start", 0, "close"])
            self.assertFalse(engine._invocations)
            self.assertEqual(engine.call("host:values", 4).to_python(), [0, 0, 1, 1])
            self.assertEqual(engine.eval("(+ 20 22)").to_python(), [42])

    def test_code_and_error_shapes_remain_data(self):
        with Engine() as engine:
            engine.register_callback("host:code", Callback(lambda: (Symbol("+"), 2, 3), 0))
            engine.register_callback("host:error-data", Callback(lambda: (Symbol("Error"), "data", "reason"), 0))
            self.assertEqual(engine.call("host:code").to_python(), [(Symbol("+"), 2, 3)])
            with engine.query("(host:code)") as query:
                self.assertEqual(query.to_python(), [(Symbol("+"), 2, 3)])
            self.assertEqual(engine.call("host:error-data").to_python(), [(Symbol("Error"), "data", "reason")])

    def test_exception_identity_and_traceback(self):
        failure = ValueError("callback witness")

        def broken(value):
            raise failure

        with Engine() as engine:
            engine.register_callback("host:broken", Callback(broken, 1))
            try:
                engine.call("host:broken", 0)
            except ValueError as error:
                self.assertIs(error, failure)
                self.assertIn("broken", [frame.name for frame in traceback.extract_tb(error.__traceback__)])
            else:
                self.fail("callback exception was discarded")
            self.assertEqual(engine.eval("(+ 1 2)").to_python(), [3])

    def test_generator_exception_keeps_published_prefix(self):
        failure = ValueError("after the prefix")
        closed = []

        def values():
            try:
                yield 7
                raise failure
            finally:
                closed.append(True)

        with Engine() as engine:
            engine.register_callback("host:broken-stream", Callback(values, 0, generator=True))
            with engine.query("(host:broken-stream)") as query:
                with self.assertRaises(ValueError) as caught:
                    query.all()
            self.assertIs(caught.exception, failure)
            self.assertEqual(failure.answers.to_python(), [7])
            self.assertEqual(closed, [True])
            self.assertFalse(engine._invocations)

    def test_reentry_busy_and_independent_engines(self):
        with Engine() as engine, Engine() as other, ThreadPoolExecutor(max_workers=2) as pool:
            def nested(value):
                own = engine.call("+", value, 1)[0].to_python()
                blocked = pool.submit(engine.eval, "(+ 1 2)")
                independent = pool.submit(other.eval, "(+ 20 22)")
                try:
                    blocked.result(timeout=5)
                except CettaError as error:
                    self.assertEqual(error.status, "busy")
                else:
                    self.fail("callback-originated conflicting entry did not report busy")
                self.assertEqual(independent.result(timeout=5).to_python(), [42])
                return own

            engine.register_callback("host:nested", Callback(nested, 1))
            self.assertEqual(engine.call("host:nested", 4).to_python(), [5])

    def test_atom_arguments_preserve_scoped_sharing(self):
        def identity(value):
            self.assertIsInstance(value, Atom)
            self.assertEqual(value.children[1], value.children[2])
            return value

        with Engine() as engine:
            engine.register_callback("host:identity", Callback(identity, 1, unpack=False))
            variable = engine.variable("x")
            value = engine.expression(Symbol("pair"), variable, variable)
            answer = engine.call("host:identity", value)[0]
            self.assertEqual(answer.children[1], answer.children[2])

    def test_deferred_close_errors_keep_their_own_error_channel(self):
        for raise_primary in (False, True):
            with self.subTest(raise_primary=raise_primary), Engine() as engine, ThreadPoolExecutor(max_workers=1) as pool:
                failure = ValueError("deferred generator close")
                primary = RuntimeError("callback's primary error")
                def generator():
                    try:
                        yield 1
                    finally:
                        raise failure
                engine.register_callback("host:pending", Callback(generator, 0, generator=True))
                query = engine.query("(host:pending)")
                self.assertEqual(next(query).to_python(), 1)
                def closer():
                    pool.submit(query.close).result(timeout=2)
                    if raise_primary:
                        raise primary
                    return 7
                engine.register_callback("host:close-pending", Callback(closer, 0))
                unraisable = []
                with patch.object(sys, "unraisablehook", unraisable.append):
                    if raise_primary:
                        with self.assertRaises(RuntimeError) as caught:
                            engine.call("host:close-pending")
                        self.assertIs(caught.exception, primary)
                    else:
                        self.assertEqual(engine.call("host:close-pending").to_python(), [7])
                self.assertEqual(len(unraisable), 1)
                self.assertIs(unraisable[0].exc_value, failure)
                self.assertIn("generator", [frame.name for frame in traceback.extract_tb(unraisable[0].exc_traceback)])
                self.assertFalse(engine._invocations)
                self.assertEqual(engine.call("+", 20, 22).to_python(), [42])

    def test_cross_thread_inspection_and_cleanup_do_not_wait_on_callback(self):
        with Engine() as engine, ThreadPoolExecutor(max_workers=2) as pool:
            symbol = engine.symbol("retained-name")
            discarded = [engine.atom(17)]
            query = engine.query("(superpose (1 2))")

            def callback():
                for inspect in (lambda: str(symbol), lambda: symbol.to_python(),
                                lambda: symbol == symbol):
                    with self.assertRaises(CettaError) as caught:
                        pool.submit(inspect).result(timeout=2)
                    self.assertEqual(caught.exception.status, "busy")
                pool.submit(discarded.clear).result(timeout=2)
                pool.submit(query.close).result(timeout=2)
                return 7

            engine.register_callback("host:retained", Callback(callback, 0))
            self.assertEqual(engine.call("host:retained").to_python(), [7])
            self.assertEqual(str(symbol), "retained-name")
            self.assertFalse(discarded)
            self.assertTrue(query._closed)

    def test_nested_explicit_close_has_its_own_error_channel(self):
        for same_engine in (True, False):
            with self.subTest(same_engine=same_engine), Engine() as engine, Engine() as other, ThreadPoolExecutor(max_workers=1) as pool:
                nested = engine if same_engine else other
                outer_error, inner_error = ValueError("outer deferred close"), RuntimeError("inner direct close")
                caught = []
                def inner():
                    try:
                        yield 2
                    finally:
                        raise inner_error
                nested.register_callback("host:inner", Callback(inner, 0, generator=True))
                inner_query = nested.query("(host:inner)")
                next(inner_query)
                def outer():
                    try:
                        yield 1
                    finally:
                        try:
                            inner_query.close()
                        except RuntimeError as error:
                            caught.append(error)
                        raise outer_error
                engine.register_callback("host:outer", Callback(outer, 0, generator=True))
                outer_query = engine.query("(host:outer)")
                next(outer_query)
                def closer():
                    pool.submit(outer_query.close).result(timeout=2)
                    return 7
                engine.register_callback("host:close-outer", Callback(closer, 0))
                unraisable = []
                with patch.object(sys, "unraisablehook", unraisable.append):
                    self.assertEqual(engine.call("host:close-outer").to_python(), [7])
                self.assertEqual(caught, [inner_error])
                self.assertEqual([event.exc_value for event in unraisable], [outer_error])

    def test_engine_callback_cycle_is_collectable(self):
        engine = Engine()

        def capture(value, owner=engine):
            return owner.call("+", value, 1)[0]

        atom = engine.register_callback("host:cycle", Callback(capture, 1))
        reference = weakref.ref(engine)
        del engine, capture, atom
        gc.collect()
        self.assertIsNone(reference())

    def test_generator_query_cycle_closes_once(self):
        closed = []
        holder = {}
        engine = Engine()

        def values(owner=holder):
            try:
                yield len(owner)
                yield 2
            finally:
                closed.append(True)

        engine.register_callback("host:cycle-stream", Callback(values, 0, generator=True))
        query = engine.query("(host:cycle-stream)")
        holder["query"] = query
        next(query)
        reference = weakref.ref(engine)
        del query, engine, values, holder
        gc.collect()
        self.assertEqual(closed, [True])
        self.assertIsNone(reference())

    def test_close_exception_and_primary_error(self):
        def close_raises():
            try:
                yield 1
            finally:
                raise RuntimeError("close witness")

        def conversion_fails():
            try:
                yield object()
            finally:
                raise RuntimeError("secondary close witness")

        with Engine() as engine:
            engine.register_callback("host:close-error", Callback(close_raises, 0, generator=True))
            query = engine.query("(host:close-error)")
            next(query)
            with self.assertRaisesRegex(RuntimeError, "close witness"):
                query.close()
            self.assertFalse(engine._invocations)
            engine.register_callback("host:conversion-error", Callback(conversion_fails, 0, generator=True))
            unraisable = []
            with patch.object(sys, "unraisablehook", unraisable.append):
                with engine.query("(host:conversion-error)") as query:
                    with self.assertRaises(TypeError):
                        next(query)
            self.assertEqual(len(unraisable), 1)
            self.assertIs(unraisable[0].exc_type, RuntimeError)
            self.assertFalse(engine._invocations)
            self.assertEqual(engine.eval("(+ 1 2)").to_python(), [3])

    def test_keyboard_interrupt_closes_callback(self):
        def interrupted():
            raise KeyboardInterrupt("callback interrupt")

        with Engine() as engine:
            engine.register_callback("host:interrupt", Callback(interrupted, 0))
            with engine.query("(host:interrupt)") as query:
                with self.assertRaises(KeyboardInterrupt):
                    next(query)
            self.assertFalse(engine._invocations)
            self.assertEqual(engine.eval("(+ 1 2)").to_python(), [3])

    def test_deferred_close_preserves_primary_exception(self):
        class Primary(ValueError):
            def add_note(self, _):
                raise RuntimeError("annotation override")

        primary = Primary("primary after close request")

        with Engine() as engine:
            class Iterator:
                def __iter__(self):
                    return self

                def __next__(self):
                    engine.close()
                    raise primary

                def close(self):
                    raise RuntimeError("secondary deferred close")

            engine.register_callback("host:deferred", Callback(Iterator, 0, generator=True))
            with engine.query("(host:deferred)") as query:
                with self.assertRaises(ValueError) as caught:
                    next(query)
            self.assertIs(caught.exception, primary)
            self.assertTrue(any("Deferred" in note for note in primary.__notes__))
            self.assertFalse(engine._invocations)

    def test_stop_iteration_is_not_logical_failure(self):
        def broken():
            raise StopIteration("unexpected host control")

        with Engine() as engine:
            engine.register_callback("host:stop", Callback(broken, 0))
            with self.assertRaises(StopIteration):
                engine.call("host:stop")
            with engine.query("(host:stop)") as query:
                with self.assertRaises(RuntimeError) as caught:
                    list(query)
            self.assertIsInstance(caught.exception.__cause__, StopIteration)

    def test_bad_descriptor_and_closed_engine(self):
        with self.assertRaises(ValueError):
            Callback(lambda: None, -1)
        engine = Engine()
        engine.close()
        with self.assertRaises(CettaError) as caught:
            engine.register_callback("host:closed", Callback(lambda: 1, 0))
        self.assertEqual(caught.exception.status, "engine-closed")


if __name__ == "__main__":
    unittest.main()
