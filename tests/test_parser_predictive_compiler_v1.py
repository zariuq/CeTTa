import subprocess
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from parser_predictive_v1 import DEFAULT_COMPILER, PredictiveConflict, analyze


def rule(category, *rhs):
    return {"category": category, "rhs": list(rhs)}


class PredictiveCompilerTest(unittest.TestCase):
    def test_non_json_nullable_grammar(self):
        rows = [rule("Document", ("n", "Items")),
                rule("Items", ("t", "word"), ("n", "Items")), rule("Items")]
        first, follow, decisions = analyze(rows, "Document")
        self.assertEqual(first, {"Document": {"word"}, "Items": {"word"}})
        self.assertEqual(follow, {"Document": {"$EOF"}, "Items": {"$EOF"}})
        self.assertEqual(decisions, {("Document", "word"): 0, ("Document", "$EOF"): 0,
                                     ("Items", "word"): 1, ("Items", "$EOF"): 2})

    def test_disjoint_occurrences_keep_enumeration(self):
        _, _, decisions = analyze([rule("Choice", ("t", "right")),
                                    rule("Choice", ("t", "left"))], "Choice")
        self.assertEqual(decisions, {("Choice", "right"): 0, ("Choice", "left"): 1})

    def test_text_identical_distinct_occurrences_conflict(self):
        row = rule("Choice", ("t", "word"))
        with self.assertRaisesRegex(PredictiveConflict, r"occurrences \[0, 1\]"):
            analyze([row, dict(row)], "Choice")

    def test_first_follow_conflict(self):
        rows = [rule("Root", ("n", "Optional"), ("t", "word")),
                rule("Optional"), rule("Optional", ("t", "word"))]
        with self.assertRaisesRegex(PredictiveConflict, "FIRST/FOLLOW conflict"):
            analyze(rows, "Root")

    def test_nullable_cycle_and_left_recursion(self):
        for rows in ([rule("A", ("n", "B")), rule("B", ("n", "A"))],
                     [rule("A", ("n", "A"), ("t", "word"))]):
            with self.subTest(rows=rows):
                with self.assertRaisesRegex(PredictiveConflict, "zero-progress cycle"):
                    analyze(rows, "A")

    def test_layout_is_not_silently_erased(self):
        rows = [rule("Choice", ("n", "Layout"), ("t", "a")),
                rule("Choice", ("n", "Layout"), ("t", "b")),
                rule("Layout"), rule("Layout", ("t", "space"), ("n", "Layout"))]
        with self.assertRaisesRegex(PredictiveConflict, "FIRST/FOLLOW conflict"):
            analyze(rows, "Choice")

    def test_bad_symbolic_inputs(self):
        with self.assertRaisesRegex(ValueError, "start category"):
            analyze([rule("A")], "Missing")
        with self.assertRaisesRegex(ValueError, "unbound"):
            analyze([rule("A", ("n", "Missing"))], "A")

    def test_malformed_native_input_fails_closed(self):
        for source in ("", "0 0\n", "0 1\n0 1\n", "0 1\n0 0\ntrailing\n"):
            with self.subTest(source=source):
                result = subprocess.run([str(DEFAULT_COMPILER)], input=source, text=True,
                                        capture_output=True, check=False)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertIn("refused", result.stderr)


if __name__ == "__main__":
    unittest.main()
