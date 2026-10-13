#!/usr/bin/env python3
"""Reject source drift and forged binary seals without pinning compiler bytes."""
import hashlib
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from check_generated_lock import validate


def lock(digest, source="source-v1"):
    return ('(gslt-langdef-lock-v1\n  (source-digest "' + source + '")\n'
            '  (compiled-cursor-sha256 "' + digest + '")\n)\n')


class GeneratedLockTest(unittest.TestCase):
    def setUp(self):
        self.cursor = b"locally compiled cursor"
        self.digest = hashlib.sha256(self.cursor).hexdigest()
        self.expected = lock("0" * 64)
        self.actual = lock(self.digest)

    def test_build_specific_checksum_is_validated_against_the_cursor(self):
        validate(self.expected, self.actual, self.cursor)

    def test_source_drift_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "source-derived"):
            validate(self.expected, lock(self.digest, "changed-source"), self.cursor)

    def test_forged_cursor_checksum_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "does not match"):
            validate(self.expected, lock("1" * 64), self.cursor)

    def test_changed_binary_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "does not match"):
            validate(self.expected, self.actual, self.cursor + b"changed")

    def test_missing_duplicate_and_malformed_fields_are_rejected(self):
        for bad in [lock("invalid"), lock("0" * 64).replace("compiled-cursor-sha256", "other"),
                    self.actual + '  (compiled-cursor-sha256 "' + self.digest + '")\n']:
            for expected, actual in [(bad, self.actual), (self.expected, bad)]:
                with self.subTest(expected=expected, actual=actual):
                    with self.assertRaisesRegex(ValueError, "well-formed"):
                        validate(expected, actual, self.cursor)


if __name__ == "__main__":
    unittest.main()
