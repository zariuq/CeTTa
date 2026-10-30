#!/usr/bin/env python3
"""Positive and adversarial controls for the interpreter-bridge receipt gate."""

import json
from pathlib import Path
import unittest
from unittest.mock import patch

from check_ic_interpreter_bridge import ENTRY_WORKER_THEOREMS, audit_records, run_logged, source_closure


class InterpreterBridgeGateTests(unittest.TestCase):
    def test_original_record(self):
        record = {"originalInterpreterBridgeDeclarations": 1,
                  "auditedDeclarations": [{"name": "Example.result", "axioms": ["propext"]}]}
        self.assertEqual(audit_records(json.dumps(record), "originalInterpreterBridgeDeclarations"), record)

    def test_shared_record_with_lean_prefix(self):
        record = {"sharedInterpreterInterfaceDeclarations": 1,
                  "theorems": 1,
                  "auditedTheorems": [{"name": "Example.result", "axioms": []}]}
        output = "info: Example.lean:1:0: " + json.dumps(record)
        self.assertEqual(audit_records(output, "sharedInterpreterInterfaceDeclarations"), record)

    def test_missing_record(self):
        with self.assertRaises(AssertionError):
            audit_records("Build completed successfully.", "originalInterpreterBridgeDeclarations")

    def test_duplicate_record(self):
        record = {"originalInterpreterBridgeDeclarations": 1,
                  "auditedDeclarations": [{"name": "Example.result", "axioms": []}]}
        with self.assertRaises(AssertionError):
            audit_records(json.dumps(record) + "\n" + json.dumps(record), "originalInterpreterBridgeDeclarations")

    def test_empty_audit(self):
        with self.assertRaises(AssertionError):
            audit_records(json.dumps({"originalInterpreterBridgeDeclarations": 0, "auditedDeclarations": []}),
                          "originalInterpreterBridgeDeclarations")

    def test_unproven_dependency(self):
        record = {"originalInterpreterBridgeDeclarations": 1,
                  "auditedDeclarations": [{"name": "Example.result", "axioms": ["sorryAx"]}]}
        with self.assertRaises(AssertionError):
            audit_records(json.dumps(record), "originalInterpreterBridgeDeclarations")

    def test_incomplete_count(self):
        record = {"originalInterpreterBridgeDeclarations": 2,
                  "auditedDeclarations": [{"name": "Example.result", "axioms": []}]}
        with self.assertRaises(AssertionError):
            audit_records(json.dumps(record), "originalInterpreterBridgeDeclarations")

    def test_prefix_record(self):
        record = {"originalPrefixBridgeDeclarations": 1,
                  "auditedDeclarations": [{"name": "Prefix.result", "axioms": []}]}
        self.assertEqual(audit_records(json.dumps(record), "originalPrefixBridgeDeclarations"), record)

    def test_incomplete_prefix_count(self):
        record = {"originalPrefixBridgeDeclarations": 2,
                  "auditedDeclarations": [{"name": "Prefix.result", "axioms": []}]}
        with self.assertRaises(AssertionError):
            audit_records(json.dumps(record), "originalPrefixBridgeDeclarations")

    def test_hierarchical_record(self):
        record = {"hierarchicalSearchDeclarations": 3, "theorems": 1,
                  "auditedTheorems": [{"name": "Hierarchy.result", "axioms": []}],
                  "separateCompilerImplementations": 1,
                  "compilerImplementations": [{"name": "Hierarchy.run._unsafe_rec", "logicalOwner": "Hierarchy.run",
                                               "matchingTypeLevelsAndOwner": True}]}
        self.assertEqual(audit_records(json.dumps(record), "hierarchicalSearchDeclarations"), record)

    def test_source_machine_time_record(self):
        record = {"sourceMachineTimeDeclarations": 3, "theorems": 1,
                  "auditedTheorems": [{"name": "SourceTime.result", "axioms": ["propext"]}],
                  "separateCompilerImplementations": 0, "compilerImplementations": []}
        self.assertEqual(audit_records(json.dumps(record), "sourceMachineTimeDeclarations"), record)

    def test_entry_claim_has_its_required_theorem_evidence(self):
        record = {"sourceMachineTimeDeclarations": len(ENTRY_WORKER_THEOREMS),
                  "theorems": len(ENTRY_WORKER_THEOREMS),
                  "auditedTheorems": [{"name": name, "axioms": ["propext"]}
                                      for name in ENTRY_WORKER_THEOREMS]}
        self.assertEqual(audit_records(json.dumps(record), "sourceMachineTimeDeclarations",
                                       required=ENTRY_WORKER_THEOREMS), record)

    def test_entry_claim_refuses_one_missing_required_theorem(self):
        for absent in ENTRY_WORKER_THEOREMS:
            with self.subTest(absent=absent):
                names = [name for name in ENTRY_WORKER_THEOREMS if name != absent]
                record = {"sourceMachineTimeDeclarations": len(names), "theorems": len(names),
                          "auditedTheorems": [{"name": name, "axioms": []} for name in names]}
                with self.assertRaisesRegex(AssertionError, "missing required theorem evidence"):
                    audit_records(json.dumps(record), "sourceMachineTimeDeclarations",
                                  required=ENTRY_WORKER_THEOREMS)

    def test_incomplete_theorem_count(self):
        record = {"hierarchicalSearchDeclarations": 3, "theorems": 2,
                  "auditedTheorems": [{"name": "Hierarchy.result", "axioms": []}]}
        with self.assertRaises(AssertionError):
            audit_records(json.dumps(record), "hierarchicalSearchDeclarations")

    def test_missing_theorem_count(self):
        record = {"hierarchicalSearchDeclarations": 1,
                  "auditedTheorems": [{"name": "Hierarchy.result", "axioms": []}]}
        with self.assertRaises(AssertionError):
            audit_records(json.dumps(record), "hierarchicalSearchDeclarations")

    def test_boolean_declaration_count(self):
        record = {"hierarchicalSearchDeclarations": True, "theorems": 1,
                  "auditedTheorems": [{"name": "Hierarchy.result", "axioms": []}]}
        with self.assertRaises(AssertionError):
            audit_records(json.dumps(record), "hierarchicalSearchDeclarations")

    def test_incomplete_compiler_implementation_count(self):
        record = {"hierarchicalSearchDeclarations": 1, "theorems": 1,
                  "auditedTheorems": [{"name": "Hierarchy.result", "axioms": []}],
                  "separateCompilerImplementations": 1, "compilerImplementations": []}
        with self.assertRaises(AssertionError):
            audit_records(json.dumps(record), "hierarchicalSearchDeclarations")

    def test_unmatched_compiler_implementation_owner(self):
        record = {"hierarchicalSearchDeclarations": 1, "theorems": 1,
                  "auditedTheorems": [{"name": "Hierarchy.result", "axioms": []}],
                  "separateCompilerImplementations": 1,
                  "compilerImplementations": [{"name": "Hierarchy.unsafe", "logicalOwner": "Hierarchy.run"}]}
        with self.assertRaises(AssertionError):
            audit_records(json.dumps(record), "hierarchicalSearchDeclarations")

    def test_duplicate_compiler_implementation(self):
        record = {"hierarchicalSearchDeclarations": 1, "theorems": 1,
                  "auditedTheorems": [{"name": "Hierarchy.result", "axioms": []}],
                  "separateCompilerImplementations": 2,
                  "compilerImplementations": [{"name": "Hierarchy.run._unsafe_rec", "logicalOwner": "Hierarchy.run"}] * 2}
        with self.assertRaises(AssertionError):
            audit_records(json.dumps(record), "hierarchicalSearchDeclarations")

    def test_missing_compiler_provenance_check(self):
        record = {"hierarchicalSearchDeclarations": 1, "theorems": 1,
                  "auditedTheorems": [{"name": "Hierarchy.result", "axioms": []}],
                  "separateCompilerImplementations": 1,
                  "compilerImplementations": [{"name": "Hierarchy.run._unsafe_rec", "logicalOwner": "Hierarchy.run"}]}
        with self.assertRaises(AssertionError):
            audit_records(json.dumps(record), "hierarchicalSearchDeclarations")

    def test_invalid_count(self):
        record = {"sharedInterpreterInterfaceDeclarations": 0,
                  "auditedTheorems": [{"name": "Example.result", "axioms": []}]}
        with self.assertRaises(AssertionError):
            audit_records(json.dumps(record), "sharedInterpreterInterfaceDeclarations")

    def test_duplicate_declaration(self):
        record = {"originalInterpreterBridgeDeclarations": 2,
                  "auditedDeclarations": [{"name": "Example.result", "axioms": []}] * 2}
        with self.assertRaises(AssertionError):
            audit_records(json.dumps(record), "originalInterpreterBridgeDeclarations")

    def test_new_warning_is_not_allowed(self):
        with patch("check_ic_interpreter_bridge.subprocess.run") as command, \
                patch.object(Path, "open"), patch.object(Path, "read_text", return_value="warning: style"):
            command.return_value.returncode = 0
            with self.assertRaises(RuntimeError):
                run_logged(["lean"], Path("root"), Path("log"))

    def test_reference_warning_remains_visible(self):
        with patch("check_ic_interpreter_bridge.subprocess.run") as command, \
                patch.object(Path, "open"), patch.object(Path, "read_text", return_value="warning: style"):
            command.return_value.returncode = 0
            self.assertEqual(run_logged(["lean"], Path("root"), Path("log"), reference_warnings=True),
                             "warning: style")

    def test_reference_error_is_not_allowed(self):
        with patch("check_ic_interpreter_bridge.subprocess.run") as command, \
                patch.object(Path, "open"), patch.object(Path, "read_text", return_value="error: invalid proof"):
            command.return_value.returncode = 0
            with self.assertRaises(RuntimeError):
                run_logged(["lean"], Path("root"), Path("log"), reference_warnings=True)

    def test_closure_follows_imports_once(self):
        sources = {Path("root/Example/A.lean"): b"import Example.B\nimport Mathlib.Data.Nat.Basic\n",
                   Path("root/Example/B.lean"): b"public import Example.A\n"}
        with patch.object(Path, "read_bytes", lambda path: sources[path]):
            closure = source_closure(Path("root"), ("Example.A",), "Example")
        self.assertEqual(set(closure), {Path("Example/A.lean"), Path("Example/B.lean")})


if __name__ == "__main__":
    unittest.main()
