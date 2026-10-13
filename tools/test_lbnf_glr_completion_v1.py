import json
import subprocess
import unittest

from lbnf_glr_completion_v1 import classify_glr_completion


class GlrCompletionVerdictTests(unittest.TestCase):
    def result(self, status, stdout=b"", stderr=b""):
        return subprocess.CompletedProcess(["typed-ast", "input"], status,
                                           stdout=stdout, stderr=stderr)

    def test_typed_acceptance(self):
        tree = {"format": "BnfcTypedAstV1", "int_bits": 32,
                "double_bytes": 8, "ast": ["E", "X", []]}
        observed = classify_glr_completion(self.result(0, json.dumps(tree).encode()))
        self.assertEqual(observed["typed"], tree)
        self.assertFalse(observed["ambiguous"])

    def test_syntax_rejection(self):
        observed = classify_glr_completion(self.result(4, stderr=b"syntax error at x"))
        self.assertEqual(observed["status"], 4)
        self.assertIsNone(observed["typed"])
        self.assertFalse(observed["ambiguous"])

    def test_unresolved_ambiguity_is_not_rejection(self):
        observed = classify_glr_completion(self.result(4, stderr=b"syntax is ambiguous"))
        self.assertTrue(observed["ambiguous"])
        self.assertIsNone(observed["typed"])

    def test_token_named_ambiguous_does_not_change_verdict(self):
        for token in [b"ambiguous", b"syntax is ambiguous", b"memory exhausted"]:
            with self.subTest(token=token):
                observed = classify_glr_completion(self.result(
                    4, stderr=b"error: 1,8: syntax error at " + token))
                self.assertFalse(observed["ambiguous"])

    def test_reference_failures_do_not_count_as_rejections(self):
        for status in [-11, -9, 1, 2, 3, 5, 127]:
            with self.subTest(status=status), self.assertRaises(ValueError):
                classify_glr_completion(self.result(status, stderr=b"syntax error"))

    def test_no_tree_without_parse_diagnostic_is_not_a_verdict(self):
        for diagnostic in [b"", b"invalid AST constructor", b"memory exhausted",
                           b"syntax error: memory exhausted", b"error: 1,8: memory exhausted at x"]:
            with self.subTest(diagnostic=diagnostic), self.assertRaises(ValueError):
                classify_glr_completion(self.result(4, stderr=diagnostic))

    def test_corrupt_success_is_not_acceptance(self):
        for payload in [b"", b"not JSON", b"null", b"{}"]:
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                classify_glr_completion(self.result(0, stdout=payload))

    def test_success_with_diagnostics_is_not_acceptance(self):
        with self.assertRaises(ValueError):
            classify_glr_completion(self.result(0, stdout=b"{}", stderr=b"memory exhausted"))


if __name__ == "__main__":
    unittest.main()
