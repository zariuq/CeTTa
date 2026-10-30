#!/usr/bin/env python3
"""Write a copy of the term universe with one fault in its reference counts,
for the counting checks to catch.

usage: mutate_term_universe_counts.py SOURCE KIND OUTPUT

KIND is one of:
  hold     a hold now and then takes no reference;
  release  a release now and then drops none;
  cascade  a freed entry now and then keeps its children's references;
  reuse    an entry born in a reused slot is not made a candidate."""
import sys
from pathlib import Path

HOLD = """static inline void term_universe_count_hold_at(TermUniverse *universe,
                                               size_t index) {
"""
RELEASE = """static inline void term_universe_count_release_at(TermUniverse *universe,
                                                  size_t index) {
"""
SKIP = """    if ((index & 63u) == 17u)
        return;
"""
CASCADE = """        term_universe_record_children(universe, index,
                                      term_universe_release_child, universe);
"""
SOMETIMES_CASCADE = """        if ((index & 7u) != 3u)
            term_universe_record_children(universe, index,
                                          term_universe_release_child,
                                          universe);
"""
REUSED_BIRTH = """    if (index < universe->collection_len_after)
        term_universe_zero_push(universe, index);
"""
MUTANTS = {
    'hold': (HOLD, HOLD + SKIP),
    'release': (RELEASE, RELEASE + SKIP),
    'cascade': (CASCADE, SOMETIMES_CASCADE),
    'reuse': (REUSED_BIRTH, ""),
}

source, kind, output = sys.argv[1:4]
text = Path(source).read_text()
old, new = MUTANTS[kind]
if text.count(old) != 1:
    sys.exit(f"mutate_term_universe_counts: the {kind} site is not unique")
Path(output).write_text(text.replace(old, new))
