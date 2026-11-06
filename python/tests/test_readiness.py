import tempfile
import unittest
import math
import random
import struct
import sys
from pathlib import Path

from cetta import AtomKind, Callback, Engine, EvaluationError, Symbol, __version__


class ReadinessTests(unittest.TestCase):
    def test_worker_publication_cleanup_and_answers_survive_engine_close(self):
        for threads in (1, 2):
            with self.subTest(threads=threads):
                with Engine() as engine:
                    answers = engine.run(f"""
                        !(pragma! num-threads {threads})
                        !(bind! &out (new-state (new-space)))
                        !(let $o (get-state &out)
                          (collapse (hyperpose
                            ((let $s (new-space)
                               (let $_ (add-atom $s (value 11)) (add-atom $o (kept 1 $s))))
                             (let $s (new-space)
                               (let $_ (add-atom $s (value 12)) (add-atom $o (kept 2 $s))))))))
                        !(collapse (match (get-state &out) (kept $tag $s)
                          ($tag (collapse (match $s (value $x) $x)))))
                        !(+ 20 22)
                    """)
                self.assertEqual([atom.to_python() for atom in answers.groups[-1]], [42])
                self.assertCountEqual(answers.groups[-2][0].to_python(),
                                      [(1, (11,)), (2, (12,))])

    def test_generated_libraries_import_the_reference_evalc_interface(self):
        with Engine() as engine:
            engine.import_("(library lib_tptp)")
            answers = engine.eval("(tptp:evaluate &self ((+ 1 2) (+ 4 5)))")
            self.assertEqual(answers.to_python(), [((3,), (9,))])
        with Engine() as engine:
            engine.import_("(library lang)")
            self.assertEqual(engine.eval("(+ 20 22)").to_python(), [42])

    def test_nested_memo_replay_preserves_shared_variable_identity(self):
        root = Path(__file__).resolve().parents[2]
        fixture = root / "tests/petta/search_machine_memo_nested_replay.metta"
        expected = fixture.with_suffix(".disabled.expected").read_text().splitlines()[3:]
        with Engine() as engine:
            answers = list(engine.load(fixture))
            self.assertEqual(len(answers), 5)
            self.assertCountEqual([str(answer) for answer in answers[3:]], expected)

    def test_ported_petta_inequality_and_native_math(self):
        with Engine() as engine:
            self.assertEqual(engine.eval("(!= 2 2)").to_python(), [Symbol("false")])
            self.assertEqual(engine.eval("(!= 2 3)").to_python(), [Symbol("true")])
            engine.import_("(library math)")
            self.assertEqual(engine.eval("(math:exp 0.0)").to_python(), [1.0])
            self.assertEqual(engine.eval("(math:ln 1.0)").to_python(), [0.0])

    def test_ported_bounded_dot_preserves_the_cancellation_enclosure(self):
        for language in ("petta", "he"):
            with self.subTest(language=language), Engine(language=language) as engine:
                engine.import_("(library tensor)" if language == "petta" else "tensor")
                answers = engine.eval("""(tensor:dot-bounds
                    (tensor:from-values f64 (3) (1.0e16 1.0 -1.0e16))
                    (tensor:from-values f64 (3) (1.0 1.0 1.0)))""").to_python()
                self.assertEqual(len(answers), 1)
                tag, center, lower, upper = answers[0]
                self.assertEqual(tag, Symbol("Tensor:Bounds"))
                self.assertEqual(center, 0.0)
                self.assertLess(lower, 1.0)
                self.assertGreater(upper, 1.0)

    def test_memory_limit_accounts_for_retained_native_values(self):
        from cetta import CettaError
        import gc
        for language in ("petta", "he"):
            with self.subTest(language=language), Engine(language=language, memory_limit=16 * 1024**2) as engine:
                kept = []
                with self.assertRaises(CettaError) as caught:
                    for _ in range(256):
                        kept.append(engine.atom("x" * (256 * 1024)))
                self.assertEqual(caught.exception.status, "out-of-memory")
                self.assertTrue(kept)
                self.assertEqual(kept[0].to_python(), "x" * (256 * 1024))
                kept.clear()
                gc.collect()
                self.assertEqual(engine.eval("(+ 20 22)").to_python(), [42])
                last = engine.atom("retained")
                engine.close()
                self.assertEqual(last.to_python(), "retained")
            with Engine(language=language) as other:
                self.assertEqual(other.eval("(+ 20 22)").to_python(), [42])

    def test_memory_limit_validation_and_construction_refusal(self):
        from cetta import CettaError
        for value in (True, 1.5, "1000"):
            with self.assertRaises(TypeError):
                Engine(memory_limit=value)
        for value in (-1, 2**64):
            with self.assertRaises(ValueError):
                Engine(memory_limit=value)
        with self.assertRaises(CettaError) as caught:
            Engine(memory_limit=0)
        self.assertEqual(caught.exception.status, "out-of-memory")

    def test_host_display_keeps_strings_and_nul_visible(self):
        value = 'first\0second"\\\nž'
        for language in ("petta", "he"):
            with self.subTest(language=language), Engine(language=language) as engine:
                atom = engine.atom(value)
                rendered = str(atom)
                self.assertTrue(rendered.startswith('"') and rendered.endswith('"'))
                self.assertNotIn("\0", rendered)
                self.assertIn("\\x00second", rendered)
                self.assertEqual(atom.to_python(), value)
                self.assertNotEqual(repr(atom), repr(engine.symbol("first")))
                if language == "he":
                    self.assertEqual(engine.parse(rendered).to_python(), value)

    def test_finite_float_display_round_trips_scalar_bits(self):
        rng = random.Random(20261104)
        values = [0., -0., .1, .1 + .2, 1e20, 1e200, 5e-324, -5e-324]
        for _ in range(128):
            value = struct.unpack("d", rng.getrandbits(64).to_bytes(8, sys.byteorder))[0]
            if math.isfinite(value):
                values.append(value)
        for language in ("petta", "he"):
            with self.subTest(language=language), Engine(language=language) as engine:
                for value in values:
                    atom = engine.parse(str(engine.atom(value)))
                    self.assertEqual(atom.kind, AtomKind.FLOAT)
                    self.assertEqual(struct.pack("d", atom.to_python()), struct.pack("d", value))
        self.assertIsInstance(__version__, str)
        self.assertTrue(__version__)

    def test_petta_document_and_term_boolean_reading_agree(self):
        for profile in (None, "extended"):
            with self.subTest(profile=profile), Engine(profile=profile) as engine:
                for text in ("(== True true)", "(== False false)", '(== "True" "true")'):
                    expected = engine.eval(text).to_python()
                    self.assertEqual(engine.run("!" + text).to_python(), expected)
                self.assertEqual(engine.run("!(== True true)").to_python(), [Symbol("true")])
                self.assertEqual(engine.symbol("True").to_python(), Symbol("True"))

    def test_missing_host_import_is_distinct_from_empty_answers(self):
        with Engine() as engine:
            self.assertEqual(engine.eval("(superpose ())").to_python(), [])
            with self.assertRaises(EvaluationError) as caught:
                engine.import_("(library missing-readiness-module)")
            self.assertEqual(caught.exception.status, "io-error")
            self.assertIn("missing-readiness-module", caught.exception.diagnostic["message"])
            self.assertEqual(engine.eval("(+ 1 2)").to_python(), [3])

    def test_parse_diagnostic_and_file_origin(self):
        for language in ("petta", "he"):
            with self.subTest(language=language), Engine(language=language) as engine:
                try:
                    engine.parse("(a\n")
                except Exception as error:
                    self.assertEqual(error.status, "parse-error")
                    self.assertEqual((error.diagnostic["line"], error.diagnostic["column"]), (2, 1))
                    self.assertTrue(error.diagnostic["message"])
                else:
                    self.fail("malformed source was accepted")
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / "bad.metta"
                    path.write_text("(a\n")
                    with self.assertRaises(EvaluationError) as caught:
                        engine.load(path)
                    diagnostic = caught.exception.diagnostic
                    self.assertEqual(diagnostic["source"], str(path.resolve()))
                    self.assertEqual((diagnostic["line"], diagnostic["column"]), (2, 1))

    def test_petta_load_and_import_use_the_same_boolean_reader(self):
        with tempfile.TemporaryDirectory() as directory, Engine() as engine:
            path = Path(directory) / "booleans.metta"
            path.write_text("(= (from-file) (== True true))\n!(from-file)\n")
            self.assertEqual(engine.load(path).to_python(), [Symbol("true")])
            module = Path(directory) / "imported.metta"
            module.write_text("(= (from-import) (== False false))\n")
            engine.import_(module)
            self.assertEqual(engine.call("from-import").to_python(), [Symbol("true")])

    def test_he_space_reads_data_and_explicit_evaluation_runs_it(self):
        with Engine(language="he") as engine:
            effects = []
            engine.register_callback("host:effect", Callback(lambda: effects.append(1) or 7, 0))
            space = engine.new_space()
            space.add_atom(engine.parse("(host:effect)"))
            rows = list(space)
            self.assertEqual(effects, [])
            self.assertEqual(rows[0].to_python(), (Symbol("host:effect"),))
            self.assertEqual(list(space.match("$row"))[0], rows[0])
            self.assertEqual(effects, [])
            self.assertEqual(engine.eval(rows[0]).to_python(), [7])
            self.assertEqual(effects, [1])
            with space.match("$row", "(host:effect)") as query:
                self.assertEqual(query.to_python(), [7])
            self.assertEqual(effects, [1, 1])

    def test_failed_test_reports_halt_and_the_engine_can_be_used_again(self):
        with Engine() as engine:
            with self.assertRaises(EvaluationError) as caught:
                engine.run("!(+ 1 1) !(test 1 2) !(add-atom &self forbidden)")
            self.assertEqual(caught.exception.status, "halted")
            self.assertEqual(caught.exception.exit_code, 1)
            self.assertEqual(caught.exception.answers.to_python(), [2])
            self.assertEqual(engine.eval("(+ 1 1)").to_python(), [2])
            self.assertEqual(engine.eval("(match &self forbidden bad)").to_python(), [])

    def test_nested_halt_is_not_lost_if_a_callback_catches_the_exception(self):
        with Engine() as engine:
            def host():
                try:
                    engine.run("!(test 1 2)")
                except EvaluationError:
                    return 7
            engine.register_callback("host:halt", Callback(host, 0))
            with self.assertRaises(EvaluationError) as caught:
                engine.call("host:halt")
            self.assertEqual(caught.exception.status, "halted")
            self.assertEqual(engine.eval("(+ 2 3)").to_python(), [5])
