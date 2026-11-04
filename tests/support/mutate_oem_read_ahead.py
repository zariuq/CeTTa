#!/usr/bin/env python3
"""Create isolated defects in the worker's fetch and collection controls."""
from pathlib import Path
import sys

source_path, output_path = map(Path, sys.argv[1:3])
kind = sys.argv[3] if len(sys.argv) > 3 else "unbounded"
source = source_path.read_text()
if kind == "unbounded":
    start = source.index("static __attribute__((noinline)) bool oem_lazy_rows_fetch_filtered(")
    end = source.index("static inline __attribute__((always_inline)) bool oem_lazy_rows_fetch(", start)
    old = "        if (remaining-- == 0u)\n            return true;"
    new = "        if (((void)remaining, false))\n            return true;"
elif kind == "scalar-owner":
    start = source.index("static Atom *oem_collect_copy_leaf(")
    end = source.index("/* A term's copy;", start)
    old = "if (!(atom->structural_facts & ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID) &&"
    new = "if (true &&"
elif kind == "he-mark":
    start = source.index("static Atom *oem_collect_atom(")
    end = source.index("/* A slot vector's copy:", start)
    old = ("            if (result && atom_is_he_evaluated(item->atom))\n"
           "                result = atom_he_evaluated_copy(collect->to, result);\n")
    new = ""
else:
    raise SystemExit(f"unknown worker mutation: {kind}")
body = source[start:end]
if body.count(old) != 1:
    raise SystemExit(f"expected one {kind} mutation site")
body = body.replace(old, new, 1)
output_path.parent.mkdir(parents=True, exist_ok=True)
output_path.write_text(source[:start] + body + source[end:])
