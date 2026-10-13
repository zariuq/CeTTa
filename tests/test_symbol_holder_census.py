#!/usr/bin/env python3
"""Exercise the declaration guard with compiler-expanded C holder mutations."""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_symbol_lifetimes import ast_holders, classify, declarations


class HolderCensusTest(unittest.TestCase):
    def setUp(self):
        (ROOT / "runtime").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="symbol-census-test-", dir=ROOT / "runtime")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "src").mkdir()
        self.file = self.root / "src/holders.c"
        self.clang = sys.argv[1] if len(sys.argv) > 1 else "clang"

    def scan(self, source):
        self.file.write_text(source)
        result = subprocess.run(
            [self.clang, "-std=c11", "-fsyntax-only", "-Xclang", "-ast-dump=json", str(self.file)],
            check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return ast_holders(json.loads(result.stdout), self.root)

    def test_compiler_expands_and_distinguishes_storage(self):
        rows = self.scan("""
typedef unsigned SymbolId;
typedef SymbolId Alias;
typedef Alias Row[3];
typedef SymbolId (*Callback)(SymbolId);
#define ID_FIELD(name) Alias name;
typedef struct {
    ID_FIELD(macro_id)
    Row rows;
    SymbolId (*matrix)[3];
    _Atomic(SymbolId) atomic_id;
    Callback callback;
    SymbolId (*callback_direct)(SymbolId);
    unsigned erased_integer;
} Holders;
static Alias global_id;
static _Thread_local SymbolId thread_id;
void compute(void) { static Alias cached_id; SymbolId local_id; (void)local_id; }
""")
        expected = {"Holders.macro_id", "Holders.rows", "Holders.matrix", "Holders.atomic_id",
                    "static:.global_id", "static:.thread_id", "static:compute:.cached_id"}
        self.assertEqual({row["member"] for row in rows.values()}, expected)

    def test_new_holder_cannot_inherit_a_file_default(self):
        before = self.scan("typedef unsigned SymbolId; typedef struct { SymbolId known; } Owner;")
        rules = {key: "owned-vocabulary" for key in before}
        classify(before, rules)
        after = self.scan("typedef unsigned SymbolId; typedef struct { SymbolId known, added; } Owner;")
        with self.assertRaisesRegex(ValueError, "unclassified.*[\\s\\S]*Owner.added"):
            classify(after, rules)

    def test_included_fragment_change_invalidates_cached_declarations(self):
        scripts = self.root / "scripts"
        scripts.mkdir()
        for name in ("check_symbol_lifetimes.py", "gen_semiring_fixtures.py"):
            shutil.copy2(ROOT / "scripts" / name, scripts / name)
        (self.root / "lib").mkdir()
        (self.root / "lib/symbol_lifetimes.metta").write_text(
            "(gslt symbol-holder-lifetimes "
            "(holder src/holders.inc Owner.known transient))")
        self.file.write_text('#include "holders.inc"\n')
        fragment = self.root / "src/holders.inc"
        fragment.write_text(
            "typedef unsigned SymbolId; "
            "typedef struct { SymbolId known; } Owner;")
        command = [sys.executable, str(scripts / "check_symbol_lifetimes.py"),
                   "--clang", self.clang, "--sources", "src/holders.c"]
        first = subprocess.run(command, cwd=self.root, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        fragment.write_text(
            "typedef unsigned SymbolId; "
            "typedef struct { SymbolId known, added; } Owner;")
        second = subprocess.run(command, cwd=self.root, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertNotEqual(second.returncode, 0)
        self.assertIn("Owner.added", second.stderr)

    def test_unresolved_is_a_destructive_collection_blocker(self):
        rows = self.scan("typedef unsigned SymbolId; typedef struct { SymbolId unknown; } Owner;")
        rules = {key: "unresolved" for key in rows}
        self.assertEqual(len(classify(rows, rules)), 1)
        with self.assertRaisesRegex(ValueError, "prohibit sweeping"):
            classify(rows, rules, require_resolved=True)

    def test_generated_vocabulary_requires_an_explicit_owner_rule(self):
        rows = self.scan("""
typedef unsigned SymbolId;
#define VOCAB(X) X(first) X(second)
#define FIELD(name) SymbolId name;
typedef struct { VOCAB(FIELD) } BuiltinSyms;
""")
        classify(rows, {"src/holders.c::BuiltinSyms.*": "owned-vocabulary"})
        with self.assertRaisesRegex(ValueError, "unclassified"):
            classify(rows, {"src/holders.c::*": "owned-vocabulary"})

    def test_declarations_reject_misspelling_and_duplicates(self):
        spec = self.root / "lifetimes.metta"
        spec.write_text("(gslt symbol-holder-lifetimes (holder src/a.c A.id unresolved))")
        self.assertEqual(declarations(spec), {"src/a.c::A.id": "unresolved"})
        spec.write_text("(gslt symbol-holder-lifetimes (holder src/a.c A.id unresovled))")
        with self.assertRaisesRegex(ValueError, "invalid holder"):
            declarations(spec)
        spec.write_text("(gslt symbol-holder-lifetimes "
                        "(holder src/a.c A.id transient) (holder src/a.c A.id transient))")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            declarations(spec)

    def test_same_shaped_anonymous_types_remain_distinct_holders(self):
        rows = self.scan("""
typedef unsigned SymbolId;
struct { SymbolId name; } first;
struct { SymbolId name; } second;
typedef struct {
    struct { SymbolId head; } one;
    struct { SymbolId head; } two;
} Container;
""")
        self.assertEqual({row["member"] for row in rows.values()},
                         {"anonymous:first.name", "anonymous:second.name", "Container.one.head", "Container.two.head"})


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
