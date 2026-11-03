#!/usr/bin/env python3
"""Create a test-only cache defect that forgets the source frame's identity:
node images keyed without the reading scope's epoch."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
hash_epoch = '    hash ^= scope.epoch.word * UINT64_C(0x9e3779b97f4a7c15);\n'
epoch_test = '           cetta_frame_identity_eq(left.epoch, right.epoch) &&\n'
assert source.count(hash_epoch) == source.count(epoch_test) == 1
source = source.replace(hash_epoch, '')
source = source.replace(epoch_test, '')
Path(sys.argv[2]).write_text(source)
