#!/usr/bin/env python3
"""Create isolated defects in the native query path."""
from pathlib import Path
import sys

source_path, kind, output_path = sys.argv[1:]
source = Path(source_path).read_text()
mutations = {
    "copies": ("uint64_t copies = made->removal || space_is_set(made->target)\n            ? 1u : weight;",
               "uint64_t copies = made->removal || space_is_set(made->target)\n            ? 1u : 1u;"),
    "collected-copies": ("for (uint64_t copy = 0u; copy < weight; copy++)\n        run->items",
                         "for (uint64_t copy = 0u; copy < 1u; copy++)\n        run->items"),
    "instance-slot": ("        return values[node->slot];",
                      "        return values[0];"),
    "atom-slot": ("return term_universe_get_atom(universe, values[node->slot]);",
                  "return term_universe_get_atom(universe, values[0]);"),
    "flush": ("         effect++)\n        done = native_query_flush(&run, effect);",
              "         effect++)\n        done = done || native_query_flush(&run, effect);"),
    "disjoint": ("static bool native_query_disjoint(const NativeQueryRun *run) {\n",
                 "static bool native_query_disjoint(const NativeQueryRun *run) {\n    return true;\n"),
    "level-key": ("cursor->values[depth] = key;",
                  "cursor->values[depth] = cursor->values[0];"),
    "loop-bound": ("matched = child == loops->values[slot];\n            } else if (!native_query_exact_value(loops, child)) {",
                   "matched = child != CETTA_ATOM_ID_NONE;\n            } else if (!native_query_exact_value(loops, child)) {"),
    "loop-release": ("ok = native_query_loops_leg(loops, depth + 1u);\n        for (size_t i = 0u; i < fresh_len; i++)\n            loops->bound[fresh[i]] = false;",
                     "ok = native_query_loops_leg(loops, depth + 1u);\n        for (size_t i = 0u; i < fresh_len; i++)\n            loops->bound[fresh[i]] = true;"),
    "rows-fixed": ("} else if (slot == SIZE_MAX) {\n                matched = child == leg->fixed[c];",
                   "} else if (slot == SIZE_MAX) {\n                matched = true;"),
    "dense-summed": ("run->summed[w] |= row[w];",
                     "run->summed[w] &= row[w];"),
    "dense-link": (".matrix = first ? relation->backward : relation->forward,",
                   ".matrix = relation->forward,"),
    "dense-repeat": ("        if (dense_bit(row, second))\n            return false;\n",
                     ""),
    "dense-popcount": ("bits += dense_bits(row[w]);",
                       "bits += row[w] != 0u;"),
    "dense-diagonal": ("base[key >> 6u] &= ~(UINT64_C(1) << (key & 63u));",
                       "(void)key;"),
}
old, new = mutations[kind]
assert source.count(old) == 1, (kind, source.count(old))
Path(output_path).write_text(source.replace(old, new))
