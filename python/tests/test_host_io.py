import contextlib
from concurrent.futures import ThreadPoolExecutor
import gc
import io
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import tempfile
import time
import traceback
import unittest
import weakref

from cetta import Engine, EvaluationError


class HostIOTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix", "POSIX signals")
    def test_keyboard_interrupt_stops_parallel_work_and_preserves_the_engine(self):
        child = r'''
import json, signal, sys
from cetta import Engine
with Engine(language=sys.argv[1], profile="extended" if sys.argv[1] == "he" else None) as engine:
    engine.run('(= (spin $n) (if (== $n 0) done (spin (- $n 1))))')
    handler = signal.getsignal(signal.SIGINT)
    print("READY", flush=True)
    try:
        engine.eval('(hyperpose ((spin 1000000000) (spin 1000000000)))')
    except KeyboardInterrupt:
        print(json.dumps({"answer": engine.eval("(+ 20 22)").to_python(),
                          "handler": signal.getsignal(signal.SIGINT) is handler}), flush=True)
    else:
        raise AssertionError("parallel work returned without interruption")
'''
        for language in ("petta", "he"):
            with self.subTest(language=language):
                process = subprocess.Popen([sys.executable, "-u", "-c", child, language],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                try:
                    with selectors.DefaultSelector() as selector:
                        selector.register(process.stdout, selectors.EVENT_READ)
                        self.assertTrue(selector.select(10), "engine did not start")
                    self.assertEqual(process.stdout.readline().strip(), "READY")
                    time.sleep(0.2)
                    process.send_signal(signal.SIGINT)
                    output, errors = process.communicate(timeout=10)
                    self.assertEqual(process.returncode, 0, errors)
                    self.assertEqual(json.loads(output), {"answer": [42], "handler": True})
                finally:
                    if process.poll() is None:
                        process.kill(); process.communicate()

    def test_current_stdout_and_explicit_output(self):
        for language in ("petta", "he"):
            with self.subTest(language=language), Engine(language=language) as engine:
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    engine.run('!(println! "run")')
                    engine.eval('(println! "eval")')
                    engine.call("println!", "ž🙂")
                    with tempfile.TemporaryDirectory() as directory:
                        path = Path(directory) / "output.metta"
                        path.write_text('!(println! "load")')
                        engine.load(path)
                expected = '"run"\n"eval"\n"ž🙂"\n"load"\n' if language == "petta" else "run\neval\nž🙂\nload\n"
                self.assertEqual(output.getvalue(), expected)
            explicit = io.StringIO()
            with Engine(language=language, output=explicit) as engine:
                engine.call("println!", "explicit")
            self.assertEqual(explicit.getvalue(), '"explicit"\n' if language == "petta" else "explicit\n")

    def test_trace_and_test_output(self):
        output, diagnostics = io.StringIO(), io.StringIO()
        with Engine() as engine, contextlib.redirect_stdout(output):
            engine.eval('(trace! "trace" 9)')
            with self.assertRaises(EvaluationError):
                engine.run("!(test 1 2)")
        self.assertIn("trace", output.getvalue())
        self.assertIn("should 2", output.getvalue())
        with Engine(language="he") as engine, contextlib.redirect_stderr(diagnostics):
            engine.eval('(trace! "diagnostic" 9)')
        self.assertIn("diagnostic", diagnostics.getvalue())

    def test_module_inventory_uses_the_host_sink(self):
        output = io.StringIO()
        with Engine(output=output) as engine:
            engine.eval("(print-mods!)")
        self.assertTrue(output.getvalue())

    def test_writer_exception_and_traceback_are_preserved(self):
        failure = ValueError("writer failed")
        class Writer:
            def write(self, text):
                raise failure
        with Engine(output=Writer()) as engine:
            try:
                engine.eval('(println! "message")')
            except ValueError as caught:
                self.assertIs(caught, failure)
                self.assertIn("write", [frame.name for frame in traceback.extract_tb(caught.__traceback__)])
            else:
                self.fail("the writer exception was swallowed")
            self.assertEqual(engine.eval("(+ 2 3)").to_python(), [5])

    def test_short_write_is_an_error(self):
        class Writer:
            def write(self, text):
                return 0
        with Engine(output=Writer()) as engine:
            with self.assertRaises(OSError):
                engine.eval('(println! "message")')
            self.assertEqual(engine.eval("(+ 1 2)").to_python(), [3])

    def test_concurrent_engine_outputs_are_independent(self):
        def run(number):
            output = io.StringIO()
            with Engine(output=output) as engine:
                for _ in range(10):
                    engine.call("println!", str(number))
            return output.getvalue()
        with ThreadPoolExecutor(max_workers=2) as pool:
            self.assertEqual(list(pool.map(run, (1, 2))), ['"1"\n' * 10, '"2"\n' * 10])

    def test_output_cycles_can_be_collected(self):
        class Writer:
            def write(self, text):
                return len(text)
        writer = Writer()
        engine = Engine(output=writer)
        writer.engine = engine
        reference = weakref.ref(engine)
        del writer, engine
        gc.collect()
        self.assertIsNone(reference())

    @unittest.skipUnless(os.name == "posix", "POSIX signals")
    def test_keyboard_interrupt_unwinds_all_whole_result_entries(self):
        child = r'''
import json, pathlib, signal, sys, tempfile
from cetta import Callback, Engine
language, entry = sys.argv[1:]
with Engine(language=language) as engine:
    def ready():
        print("READY", flush=True)
        return 0
    engine.register_callback("host:ready", Callback(ready, 0))
    engine.run("(= (spin $n) (if (== $n 0) (let $_ (host:ready) (spin 1)) (spin (+ $n 1))))")
    handler = signal.getsignal(signal.SIGINT)
    with tempfile.TemporaryDirectory() as directory:
        path = pathlib.Path(directory) / "spin.metta"
        path.write_text("!(spin 0)")
        try:
            if entry == "eval": engine.eval("(spin 0)")
            elif entry == "call": engine.call("spin", 0)
            elif entry == "run": engine.run("!(spin 0)")
            else: engine.load(path)
        except KeyboardInterrupt:
            print(json.dumps({"answer": engine.eval("(+ 1 1)").to_python(),
                "handler": signal.getsignal(signal.SIGINT) is handler}), flush=True)
        else:
            raise AssertionError("unbounded execution returned without interruption")
'''
        for language in ("petta", "he"):
            for entry in ("run", "eval", "call", "load"):
                with self.subTest(language=language, entry=entry):
                    process = subprocess.Popen([sys.executable, "-u", "-c", child, language, entry],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                    try:
                        with selectors.DefaultSelector() as selector:
                            selector.register(process.stdout, selectors.EVENT_READ)
                            self.assertTrue(selector.select(10), "execution never reached the callback")
                        self.assertEqual(process.stdout.readline().strip(), "READY")
                        process.send_signal(signal.SIGINT)
                        output, errors = process.communicate(timeout=10)
                        self.assertEqual(process.returncode, 0, errors)
                        self.assertEqual(json.loads(output), {"answer": [2], "handler": True})
                    finally:
                        if process.poll() is None:
                            process.kill()
                            process.communicate()
