import concurrent.futures
import gc
import subprocess
import sys
import unittest
from fractions import Fraction

from cetta import AtomKind, CettaError, Engine, EvaluationError, Symbol


class EngineTests(unittest.TestCase):
    def test_ordinary_use_and_grouping(self):
        with Engine() as engine:
            self.assertEqual(engine.run("(= (double $x) (* $x 2)) !(double 21)").to_python(), [42])
            self.assertEqual(engine.call("double", 3).to_python(), [6])
            answers = engine.run("(= (pick) a) (= (pick) a) !(pick) !(superpose ())")
            self.assertEqual([len(group) for group in answers.groups], [2, 0])
            self.assertEqual(answers.to_python(), [Symbol("a"), Symbol("a")])

    def test_value_distinctions(self):
        with Engine() as engine:
            self.assertEqual(engine.atom(True).kind, AtomKind.BOOLEAN)
            self.assertEqual(engine.atom(1).kind, AtomKind.INTEGER)
            self.assertNotEqual(engine.atom(True), engine.atom(1))
            self.assertEqual(engine.atom("hello\0world").to_python(), "hello\0world")
            self.assertNotEqual(engine.atom("x"), engine.symbol("x"))
            self.assertNotEqual(engine.symbol("x").to_python(), "x")
            self.assertTrue(engine.eval("(== 1 1)")[0].as_bool())
            self.assertFalse(engine.eval("(== 1 2)")[0].as_bool())
            self.assertEqual(engine.symbol("true").to_python(), Symbol("true"))
            with self.assertRaises(TypeError):
                engine.symbol("unknown").as_bool()
            number = 2**100 + 7
            self.assertEqual(engine.atom(number).to_python(), number)
            self.assertEqual(engine.atom(Fraction(2, 3)).to_python(), Fraction(2, 3))
            variable = engine.variable("same")
            other = engine.variable("same")
            self.assertNotEqual(variable, other)
            pair = engine.expression(variable, variable)
            self.assertEqual(pair.children[0], pair.children[1])
            self.assertEqual(pair.children[0], variable)
            self.assertEqual(engine.expression(variable).children[0], pair.children[0])
            self.assertNotEqual(engine.parse("$x"), engine.parse("$x"))
            parsed = engine.parse("($x $x)")
            self.assertEqual(parsed.children[0], parsed.children[1])
            self.assertNotEqual(engine.expression(variable, other).children[0], other)

    def test_calls_accept_values_and_eval_accepts_code(self):
        for language in ("petta", "he"):
            with self.subTest(language=language), Engine(language=language) as engine:
                engine.run("(= (identity $x) $x) (= (looks-like-code) 9)")
                datum = engine.expression(Symbol("looks-like-code"))
                for fuel in (-1, 100):
                    self.assertEqual(engine.call("identity", datum, fuel=fuel).to_python(),
                                     [(Symbol("looks-like-code"),)])
                    self.assertEqual(engine.call("unknown", datum, fuel=fuel).to_python(),
                                     [(Symbol("unknown"), (Symbol("looks-like-code"),))])
                self.assertEqual(engine.eval("(identity (looks-like-code))").to_python(), [9])
                self.assertEqual(engine.call("+", 20, 22).to_python(), [42])
        with Engine() as engine:
            engine.run("(= (truth true) yes) (= (pair-truth (pair true)) yes)")
            self.assertEqual(engine.call("truth", True).to_python(), [Symbol("yes")])
            pair = engine.expression(Symbol("pair"), True)
            self.assertEqual(engine.call("pair-truth", pair).to_python(), [Symbol("yes")])

    def test_shared_function_delimiter_agrees_at_root_and_query_entry(self):
        for language in ("petta", "he"):
            with self.subTest(language=language), Engine(language=language) as engine:
                source = "(function (return (+ 1 2)))"
                expected = [(Symbol("+"), 1, 2)]
                self.assertEqual(engine.eval(source).to_python(), expected)
                with engine.query(source) as query:
                    self.assertEqual(query.to_python(), expected)
                source = "(function (eval missing))"
                expected = [(Symbol("Error"), (Symbol("function"), (Symbol("eval"), Symbol("missing"))),
                             Symbol("NoReturn"))]
                self.assertEqual(engine.eval(source).to_python(), expected)
                with engine.query(source) as query:
                    self.assertEqual(query.to_python(), expected)

    def test_ready_call_effects_and_raised_control(self):
        for language in ("petta", "he"):
            with self.subTest(language=language), Engine(language=language) as engine:
                engine.run("(= (touch) (add-atom &self touched)) (= (identity $x) $x)")
                datum = engine.expression(Symbol("touch"))
                for fuel in (-1, 100):
                    engine.call("identity", datum, fuel=fuel)
                    engine.call("unknown", datum, fuel=fuel)
                self.assertEqual(engine.eval("(match &self touched yes)").to_python(), [])
                engine.eval("(touch)")
                self.assertEqual(engine.eval("(match &self touched yes)").to_python(), [Symbol("yes")])
        with Engine() as engine:
            for fuel in (-1, 100):
                with self.assertRaises(EvaluationError) as caught:
                    engine.call("/", 1, 0, fuel=fuel)
                self.assertEqual(caught.exception.status, "language-raised")
            data = engine.parse("(Error data reason)")
            engine.run("(= (identity $x) $x)")
            self.assertEqual(engine.call("identity", data).to_python(),
                             [(Symbol("Error"), Symbol("data"), Symbol("reason"))])

    def test_unknown_data_and_retained_answers(self):
        engine = Engine()
        answers = engine.eval("(unknown 42)")
        answer = answers[0]
        del answers
        engine.close()
        del engine
        gc.collect()
        self.assertEqual(str(answer), "(unknown 42)")
        self.assertEqual(answer.to_python(), (Symbol("unknown"), 42))

    def test_document_entry_preserves_errors_budgets_and_plans(self):
        with Engine() as engine:
            answers = engine.run("!(Error data reason) ! 42 !(superpose ())")
            self.assertEqual([len(group) for group in answers.groups], [1, 1, 0])
            self.assertEqual(answers[1].to_python(), 42)
            with self.assertRaises(EvaluationError) as caught:
                engine.run("! 42 !(/ 1 0) !(add-atom &self after-error)")
            self.assertEqual(caught.exception.status, "language-raised")
            self.assertEqual(caught.exception.answers[0].to_python(), 42)
            self.assertEqual(engine.eval("(match &self after-error yes)").to_python(), [])
            with self.assertRaises(EvaluationError) as caught:
                engine.run("!(+ 1 2)", fuel=0)
            self.assertEqual(caught.exception.status, "fuel-exhausted")
            with self.assertRaises(EvaluationError) as caught:
                engine.run("(= (+ $x $y) rejected)")
            self.assertEqual(caught.exception.status, "language-raised")
        with Engine(language="he") as engine:
            answers = engine.run("!(bind! held 7) (= (saved) held) !(bind! held 9) !(saved)")
            self.assertEqual(answers[-1].to_python(), 7)
        with Engine() as engine:
            for source in ("!(add-atom &self parsed) (", "!(add-atom &self parsed) !"):
                with self.assertRaises(EvaluationError) as caught:
                    engine.run(source)
                self.assertEqual(caught.exception.status, "parse-error")
            self.assertEqual(engine.eval("(match &self parsed yes)").to_python(), [])
            for source in ("(with_mutex lock 42)", "(transaction 42)"):
                self.assertEqual(engine.run("!" + source).to_python(), engine.eval(source).to_python())

    def test_foreign_engine_is_rejected(self):
        with Engine() as left, Engine() as right:
            with self.assertRaisesRegex(RuntimeError, "foreign-engine"):
                right.eval(left.parse("(+ 1 2)"))
            with self.assertRaisesRegex(RuntimeError, "foreign-engine"):
                right.expression(left.symbol("foreign"))

    def test_closed_engine_reports_a_typed_status(self):
        engine = Engine()
        engine.close()
        with self.assertRaises(CettaError) as caught:
            engine.parse("42")
        self.assertEqual(caught.exception.status, "engine-closed")
        with self.assertRaises(CettaError) as caught:
            with engine:
                self.fail("a closed engine was entered")
        self.assertEqual(caught.exception.status, "engine-closed")

    def test_budget_is_not_logical_failure(self):
        with Engine() as engine:
            with self.assertRaises(EvaluationError) as caught:
                engine.eval("(+ 1 2)", fuel=0)
            self.assertEqual(caught.exception.status, "fuel-exhausted")
            self.assertEqual(engine.eval("(+ 1 2)").to_python(), [3])

    def test_displays_are_bounded_and_values_are_preserved(self):
        with Engine() as engine:
            text = "x" * 10000
            value = engine.atom(text)
            self.assertLess(len(repr(value)), 100)
            self.assertEqual(value.to_python(), text)
            expression = engine.parse("(" + " ".join(f"name{i}" for i in range(100)) + ")")
            self.assertLess(len(repr(expression)), 550)
            self.assertEqual(len(expression.children), 100)
            answers = engine.eval("(superpose (" + " ".join(map(str, range(100))) + "))")
            self.assertLess(len(repr(answers)), 600)
            self.assertCountEqual(answers.to_python(), range(100))

    def test_concurrent_independent_engines(self):
        def worker(value):
            with Engine() as engine:
                engine.run(f"(= (private) {value})")
                for _ in range(20):
                    self.assertEqual(engine.call("private").to_python(), [value])
                return engine.call("private")[0]

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            answers = list(pool.map(worker, [101, 202]))
        self.assertEqual([answer.to_python() for answer in answers], [101, 202])

    def test_he_catalog_is_owned_by_its_engine(self):
        with Engine(language="he") as first, Engine(language="he") as second:
            for index in range(30):
                first.symbol(f"only-first-{index}")
            first.run("(: double (-> Number Number)) (= (double $x) (* $x 2))")
            second.run("(: triple (-> Number Number)) (= (triple $x) (* $x 3))")
            for _ in range(10):
                self.assertEqual(first.call("double", 21).to_python(), [42])
                self.assertEqual(second.call("triple", 7).to_python(), [21])
                self.assertEqual(str(first.eval("(get-type +)")[0]), "(-> Number Number Number)")
                self.assertEqual(str(second.eval("(get-type +)")[0]), "(-> Number Number Number)")

    def test_optional_libraries_use_the_shared_module_loader(self):
        for language in ("petta", "he"):
            with self.subTest(language=language), Engine(language=language) as engine:
                for library in ("str", "math"):
                    reference = f"(library {library})" if language == "petta" else library
                    engine.run(f"!(import! &self {reference})")
                self.assertTrue(engine.call("str:starts-with", "hello", "he")[0].as_bool())
                self.assertEqual(engine.call("math:sqrt", 4.0).to_python(), [2.0])

    def test_waiting_metadata_entries_do_not_hold_the_python_lock(self):
        program = r'''
from cetta import Engine, CettaError
import sys, threading, time
operation = sys.argv[1]
engine = Engine()
engine.run('(= (spin) (spin))')
expression = engine.parse('(spin)')
value, other = engine.atom('ready'), engine.atom('ready')
held = [engine.atom('drop')]
spin_errors, metadata_errors = [], []
started, metadata_started = threading.Event(), threading.Event()
def spin():
    started.set()
    try:
        engine.eval(expression)
    except CettaError as error:
        spin_errors.append(error.status)
def metadata():
    metadata_started.set()
    try:
        if operation == 'construct': engine.atom(42)
        elif operation == 'format': str(value)
        elif operation == 'inspect': assert value.to_python() == 'ready'
        elif operation == 'equal': assert value == other
        elif operation == 'release':
            last = held.pop()
            del last
    except CettaError as error:
        metadata_errors.append(error.status)
worker = threading.Thread(target=spin)
worker.start()
assert started.wait(2)
clock = time.pthread_getcpuclockid(worker.ident)
until = time.monotonic() + 2
while time.clock_gettime(clock) < .005:
    assert worker.is_alive() and time.monotonic() < until
    time.sleep(.001)
reader = threading.Thread(target=metadata)
reader.start()
assert metadata_started.wait(2)
time.sleep(.01)
engine.close()
worker.join(2)
reader.join(2)
assert not worker.is_alive() and not reader.is_alive()
assert spin_errors == ['cancelled'], spin_errors
assert metadata_errors == (['engine-closed'] if operation == 'construct' else []), metadata_errors
'''
        for operation in ("construct", "format", "inspect", "equal", "release"):
            with self.subTest(operation=operation):
                result = subprocess.run([sys.executable, "-c", program, operation],
                                        capture_output=True, text=True, timeout=8)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_created_spaces_follow_their_engine(self):
        first, second = Engine(), Engine()
        left = first.eval("(new-space)")[0]
        right = second.eval("(new-space)")[0]
        first.call("add-atom", left, Symbol("left-row"))
        second.call("add-atom", right, Symbol("right-row"))
        first.close()
        del first, left
        gc.collect()
        self.assertEqual(second.call("get-atoms", right).to_python(), [Symbol("right-row")])
        second.close()
        del second, right
        gc.collect()

        def create_space(_):
            with Engine() as engine:
                return engine.eval("(new-space)")[0]

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            retained = list(pool.map(create_space, range(4)))
        self.assertEqual(len(retained), 4)
        self.assertTrue(all(atom.kind == AtomKind.RESOURCE for atom in retained))
        del retained
        gc.collect()


if __name__ == "__main__":
    unittest.main()
