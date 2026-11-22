#!/usr/bin/env python3
"""Create isolated defects in merge order, cursor cloning or read ceilings."""
from pathlib import Path
import sys

source_path, kind, output_path = sys.argv[1:]
source = Path(source_path).read_text()
if kind == "order":
    edits = [("cursor->heap[right] < cursor->heap[left]",
              "cursor->heap[right] > cursor->heap[left]")]
elif kind == "clone":
    edits = [("memcpy(dst->leaf_pos, src->leaf_pos,", "memset(dst->leaf_pos, 0,")]
elif kind == "ceiling":
    # The row at the ceiling, added after the cursor opened, is read in
    # either direction.
    edits = [("if (leaf >= cursor->ceiling) {", "if (leaf > cursor->ceiling) {"),
             ("if (leaf < cursor->ceiling) {", "if (leaf <= cursor->ceiling) {")]
else:
    raise SystemExit(f"unknown mutation: {kind}")
for old, new in edits:
    assert source.count(old) == 1, (kind, old, source.count(old))
    source = source.replace(old, new)
Path(output_path).write_text(source)
