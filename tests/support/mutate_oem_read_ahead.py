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
elif kind in ("root-reorder", "root-collapse"):
    start = source.index("static Atom **oem_collect_slots(")
    end = source.index("return copied;", source.index("for (uint32_t index = 0u; index < count; index++)", start))
    old = "oem_collect_atom(cursor, collect, slots[index])"
    index = "(index + 1u) % count" if kind == "root-reorder" else "0u"
    new = f"oem_collect_atom(cursor, collect, slots[{index}])"
elif kind == "child-reorder":
    start = source.index("static Atom *oem_collect_atom(")
    end = source.index("/* A slot vector's copy:", start)
    old = "            result = atom_expr(collect->to,\n"
    new = ("            if (item->atom->expr.len > 1u) {\n"
           "                Atom *first = cursor->resolve_results[item->base];\n"
           "                cursor->resolve_results[item->base] = cursor->resolve_results[results - 1u];\n"
           "                cursor->resolve_results[results - 1u] = first;\n"
           "            }\n" + old)
elif kind == "spelled-inline":
    start = source.index("static Atom *oem_collect_atom(")
    end = source.index("/* A slot vector's copy:", start)
    old = "atom->kind != ATOM_VAR && !atom_symbol_is_spelled(atom) &&"
    new = "atom->kind != ATOM_VAR &&"
elif kind == "stale-alias":
    start = source.index("static Atom *oem_collect_atom(")
    end = source.index("/* A slot vector's copy:", start)
    old = "if (!oem_collect_put(collect, atom, result))"
    new = "if (!oem_collect_value_at(collect, atom))"
else:
    raise SystemExit(f"unknown worker mutation: {kind}")
body = source[start:end]
if body.count(old) != 1:
    raise SystemExit(f"expected one {kind} mutation site")
body = body.replace(old, new, 1)
output_path.parent.mkdir(parents=True, exist_ok=True)
output_path.write_text(source[:start] + body + source[end:])
