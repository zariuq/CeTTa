#!/usr/bin/env python3
"""Create isolated defects in merge order, cursor cloning or read ceilings."""
from pathlib import Path
import sys

source_path, kind, output_path = sys.argv[1:]
source = Path(source_path).read_text()
expected_sites = 1
if kind == "order":
    old = "cursor->heap[right] < cursor->heap[left]"
    new = old.replace(" <", " >")
elif kind == "clone":
    old = "memcpy(dst->leaf_pos, src->leaf_pos,"
    new = "memset(dst->leaf_pos, 0,"
elif kind == "ceiling":
    old = "disc_leaf_at(node, position) >= cursor->ceiling"
    new = old.replace(" >=", " >")
    expected_sites = 2
else:
    raise SystemExit(f"unknown mutation: {kind}")
assert source.count(old) == expected_sites, (kind, source.count(old))
Path(output_path).write_text(source.replace(old, new))
