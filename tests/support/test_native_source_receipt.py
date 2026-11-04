#!/usr/bin/env python3
"""Boundary controls for source-snapshot receipt admission."""
import unittest
from check_native_source_receipt import acceptable_missing, required_build_paths


class SourceReceiptControls(unittest.TestCase):
    def test_only_tests_may_be_outside_the_reconstructed_index(self):
        self.assertTrue(acceptable_missing(["tests/a.metta", "tests/support/check.py"]))
        self.assertFalse(acceptable_missing(["tests/a.metta", "src/space.c"]))
        self.assertFalse(acceptable_missing(["Makefile"]))

    def test_build_paths_include_runtime_sources_and_configuration(self):
        paths = {"Makefile", "src/space.c", "native/module.c",
                 "runtime/bootstrap/build_config.example.h", "tests/a.metta", "README"}
        self.assertEqual(required_build_paths(paths),
                         {"Makefile", "src/space.c", "native/module.c",
                          "runtime/bootstrap/build_config.example.h"})


if __name__ == "__main__":
    unittest.main()
