#!/usr/bin/env python3
"""Create a test-only cache defect that forgets the source frame's identity."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
hash_call = 'bindings_apply_graph_hash(source, scope.epoch, scope.kind)'
epoch_test = 'graph->entries[slot].epoch != scope.epoch ||'
assert source.count(hash_call) == source.count(epoch_test) == 1
source = source.replace(hash_call, 'bindings_apply_graph_hash(source, 0u, scope.kind)')
source = source.replace(epoch_test, '')
Path(sys.argv[2]).write_text(source)
