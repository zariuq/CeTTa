import errno
import os
from pathlib import Path
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor

from cetta import Callback, CettaError, Engine, EvaluationError, Symbol


class ModuleTests(unittest.TestCase):
    def write(self, directory, name, text):
        path = Path(directory) / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def test_load_definitions_groups_data_and_retained_answers(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write(directory, "definitions with spaces-λ.metta", """
(= (double $x) (* $x 2))
!(double 21)
!(superpose (7 7))
!(superpose ())
!(Error data reason)
""")
            for language in ("petta", "he"):
                with self.subTest(language=language), Engine(language=language) as engine:
                    answers = engine.load(path)
                    self.assertEqual([len(group) for group in answers.groups], [1, 2, 0, 1])
                    self.assertEqual(answers.to_python(), [42, 7, 7, (Symbol("Error"), Symbol("data"), Symbol("reason"))])
                    self.assertEqual(engine.call("double", 9).to_python(), [18])
                self.assertEqual(answers[0].to_python(), 42)

    def test_relative_imports_and_independent_engines(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = []
            for name, number in (("left", 11), ("right", 22)):
                self.write(root, name + "/helper.metta", f"(= (local-value) {number})\n")
                files.append(self.write(root, name + "/main.metta", '!(import! &self "./helper.metta")\n!(local-value)\n'))
            before = os.getcwd()
            for language in ("petta", "he"):
                with self.subTest(language=language), Engine(language=language) as left, Engine(language=language) as right, ThreadPoolExecutor(max_workers=2) as pool:
                    first = pool.submit(left.load, files[0])
                    second = pool.submit(right.load, files[1])
                    self.assertEqual(first.result(timeout=10)[-1].to_python(), 11)
                    self.assertEqual(second.result(timeout=10)[-1].to_python(), 22)
                    self.assertEqual(left.call("local-value").to_python(), [11])
                    self.assertEqual(right.call("local-value").to_python(), [22])
                    self.assertEqual(os.getcwd(), before)

    def test_nested_load_restores_directory_and_suppresses_ancestor_replay(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            outer = self.write(root, "outer/main.metta", '''!(host:tick)
!(host:nested)
!(import! &self "./shared.metta")
!(outer-value)
''')
            inner = self.write(root, "inner/main.metta", '''!(import! &self "../outer/main.metta")
!(import! &self "./shared.metta")
!(inner-value)
''')
            self.write(root, "outer/shared.metta", '(= (outer-value) 111)\n')
            self.write(root, "inner/shared.metta", '(= (inner-value) 222)\n')
            for language in ("petta", "he"):
                with self.subTest(language=language), Engine(language=language) as engine:
                    calls = []
                    def tick():
                        calls.append(1)
                        if len(calls) != 1:
                            raise RuntimeError("an active ancestor document was replayed")
                        return 1
                    def nested():
                        self.assertEqual(engine.load(inner)[-1].to_python(), 222)
                        return 2
                    engine.register_callback("host:tick", Callback(tick, 0))
                    engine.register_callback("host:nested", Callback(nested, 0))
                    self.assertEqual(engine.load(outer)[-1].to_python(), 111)
                    engine.import_(outer)
                    self.assertEqual(calls, [1])

    def test_import_matches_dialect_and_target_space(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.write(directory, "facts.metta", '(row 17)\n')
            for language, spec in (("petta", "(library tensor)"), ("he", "tensor")):
                with self.subTest(language=language), Engine(language=language) as engine:
                    engine.import_(spec)
                    space = engine.new_space()
                    imported = engine.import_(path, target=space)
                    if language == "he":
                        self.assertEqual(imported[0].children[0].to_python(), Symbol("Error"))
                        engine.import_(path, target="fresh")
                        space = engine.space_from_atom(engine.eval("&fresh")[0])
                    with space.match("(row $x)") as query:
                        self.assertEqual([answer.bindings["x"].to_python() for answer in query], [17])
                    self.assertEqual(engine.eval("(match &self (row $x) $x)").to_python(), [])
                    self.assertEqual(engine.eval("(tensor:sum (tensor:from-values f64 (2) (1.0 2.0)))").to_python(), [3.0])

    def test_entered_query_keeps_its_file_origin_across_host_slices(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = self.write(root, "first/main.metta", '!(host:open-query)\n')
            second = self.write(root, "second/main.metta", '!(host:resume-query)\n')
            self.write(root, "first/late.metta", '(= (late-value) 111)\n')
            self.write(root, "second/late.metta", '(= (late-value) 222)\n')
            for allowance in (1, 2, 10000):
                with self.subTest(allowance=allowance), Engine() as engine:
                    pending = []
                    def start():
                        query = engine.query('(superpose (7 (import! &self "./late.metta")))')
                        for _ in range(100):
                            self.assertEqual(query.step(0).physical_steps, 0)
                            step = query.step(allowance)
                            if step.answers:
                                self.assertEqual(step.answers.to_python(), [7])
                                break
                        else:
                            self.fail("query did not publish its first answer")
                        pending.append(query)
                        return 1
                    def resume():
                        with pending.pop() as query:
                            query.all()
                        return engine.call("late-value")[0]
                    engine.register_callback("host:open-query", Callback(start, 0))
                    engine.register_callback("host:resume-query", Callback(resume, 0))
                    engine.load(first)
                    self.assertEqual(engine.load(second)[-1].to_python(), 111)

    def test_io_parse_control_and_budget_remain_distinct(self):
        with tempfile.TemporaryDirectory() as directory, Engine() as engine:
            empty = self.write(directory, "empty.metta", "")
            self.assertEqual(engine.load(os.fsencode(empty)).to_python(), [])
            self.assertEqual(len(engine.load(empty).groups), 0)
            with self.assertRaises(IsADirectoryError) as caught:
                engine.load(directory)
            self.assertEqual(caught.exception.errno, errno.EISDIR)
            missing = Path(directory) / "missing.metta"
            with self.assertRaises(FileNotFoundError) as caught:
                engine.load(missing)
            self.assertEqual(caught.exception.errno, errno.ENOENT)
            self.assertEqual(caught.exception.filename, str(missing))
            bad = self.write(directory, "syntax.metta", '!(add-atom &self should-not-run)\n(\n')
            with self.assertRaises(EvaluationError) as caught:
                engine.load(bad)
            self.assertEqual(caught.exception.status, "parse-error")
            self.assertEqual(engine.eval("(match &self should-not-run yes)").to_python(), [])
            bad.write_bytes(b'! 7\n\x00! 9')
            with self.assertRaises(EvaluationError) as caught:
                engine.load(bad)
            self.assertEqual(caught.exception.status, "parse-error")
            failing = self.write(directory, "raised.metta", '! 42\n!(/ 1 0)\n!(add-atom &self after-error)\n')
            with self.assertRaises(EvaluationError) as caught:
                engine.load(failing)
            self.assertEqual(caught.exception.status, "language-raised")
            self.assertEqual(caught.exception.answers[0].to_python(), 42)
            self.assertEqual(engine.eval("(match &self after-error yes)").to_python(), [])
            limited = self.write(directory, "limited.metta", '!(add-atom &self ready-after-retry)\n')
            with self.assertRaises(EvaluationError) as caught:
                engine.load(limited, fuel=0)
            self.assertEqual(caught.exception.status, "fuel-exhausted")
            engine.import_(limited)
            self.assertEqual(engine.eval("(match &self ready-after-retry yes)").to_python(), [Symbol("yes")])
            self.assertEqual(engine.call("+", 20, 22).to_python(), [42])

    def test_foreign_values_closed_engines_and_invalid_paths(self):
        with Engine() as engine, Engine() as other:
            with self.assertRaises(CettaError) as caught:
                engine.import_(other.symbol("tensor"))
            self.assertEqual(caught.exception.status, "foreign-engine")
            with self.assertRaises(CettaError) as caught:
                engine.import_("(library tensor)", target=other.new_space())
            self.assertEqual(caught.exception.status, "foreign-engine")
            with self.assertRaises(ValueError):
                engine.load("name\0tail")
            engine.close()
            with self.assertRaises(CettaError) as caught:
                engine.load("anything.metta")
            self.assertEqual(caught.exception.status, "engine-closed")


if __name__ == "__main__":
    unittest.main()
