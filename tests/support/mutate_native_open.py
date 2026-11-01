#!/usr/bin/env python3
"""Create isolated defects in the joins over facts holding variables."""
from pathlib import Path
import sys

source_path, kind, output_path = sys.argv[1:]
source = Path(source_path).read_text()
mutations = {
    # Bindings without the occurs check.
    "occurs-unchecked": ("    CettaMolResult occurs = mol_occurs(store, cell, x, base);\n",
                         "    CettaMolResult occurs = CETTA_MOL_FAIL;\n    (void)mol_occurs;\n"),
    # A kept fact's holes each read under a frame of its own, not one
    # variable scope across the fact.
    "frame-per-hole": ("                store, part, (CettaMol){holes[h++], frame});",
                       "                store, part, (CettaMol){holes[h++], cetta_mol_frame(store)});"),
    # Additions admitted though an instance may meet a pattern read in its
    # target.
    "meets-unchecked": ("    return met != CETTA_MOL_FAIL;\n}",
                        "    return met != CETTA_MOL_FAIL && false;\n}"),
    # The patterns stored apart from the effects' data, each numbering its
    # variables alone.
    "store-apart": ("        run.patterns[i] = tu_child(universe, stored, (CettaExprIndex)(1u + i));",
                    "        run.patterns[i] = term_universe_store_atom_id(universe, arena, query->patterns[i]);"),
    # A let* over a producer that reads a space read as a pure stream.
    "stream-impure": ("                                    &defined) !=\n            CETTA_GSLT_QUERY_EFFECT_PURE ||",
                      "                                    &defined) ==\n            (CettaGsltQueryEffect)0xffu ||"),
    # A stored part's variable taken as new to an instance's numbering
    # though an earlier read of it numbered it already.
    "own-renumbered": ("                while (at < numbering->len && numbering->cells[at] != cell)\n                    at++;\n",
                       "                at = numbering->len;\n"),
    # A set's batch of instances given as alpha keys checked against its
    # relations only, not its rows.
    "keyed-unchecked": ("            if (id_present_contains(&s->native, id) ||\n                space_dense_holds_id(s, id))\n",
                        "            if (space_dense_holds_id(s, id))\n"),
    # An aligned pattern's first hole left unread.
    "aligned-hole": ("    for (uint32_t h = 0u; h < aligned->holes; h++) {\n        CettaMolResult unified = cetta_mol_unify(",
                     "    for (uint32_t h = 1u; h < aligned->holes; h++) {\n        CettaMolResult unified = cetta_mol_unify("),
}
old, new = mutations[kind]
assert source.count(old) == 1, (kind, source.count(old))
Path(output_path).write_text(source.replace(old, new))
