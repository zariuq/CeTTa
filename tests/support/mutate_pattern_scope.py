#!/usr/bin/env python3
"""Create a test-only cache defect that forgets the source frame's identity."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
hash_call = 'source, scope.epoch, scope.kind, code_depth'
epoch_test = 'graph->entries[slot].epoch != scope.epoch ||'
assert source.count(hash_call) == source.count(epoch_test) == 1
source = source.replace(hash_call, 'source, 0u, scope.kind, code_depth')
source = source.replace(epoch_test, '')
Path(sys.argv[2]).write_text(source)
