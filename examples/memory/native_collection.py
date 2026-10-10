#!/usr/bin/env python3
"""Two-record native collection demo; no model, corpus or Chroma dependency."""
import argparse
import os
from pathlib import Path
import sys
import uuid

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--release', type=Path, default=Path.home()/'.local/lib/cetta-memory/current')
parser.add_argument('--socket', default=str(Path(os.environ.get('XDG_RUNTIME_DIR', f'/run/user/{os.getuid()}'))/'cetta-memory.sock'))
args = parser.parse_args()
release = args.release.resolve()
sys.path.insert(0, str(release/'tools'))
from memory_collection import NativeCollection

collection = NativeCollection(args.socket, 'demo-'+uuid.uuid4().hex,
    {'embedding_output_dim':4, 'embedding_model':'handwritten-demo-v1'},
    release/'bin/cetta-value-worker', release/'lib/petta/lib_memory_collection.metta')
try:
    collection.add(['first','second'], [[1.,0.,0.,0.],[0.,1.,0.,0.]],
                   ['first example','second example'], [{'topic':'demo'},{'topic':'demo'}])
    print(collection.query([[1.,.2,0.,0.]], 2, where={'topic':'demo'}))
    collection.delete(ids=['first'])
    assert collection.get(ids=['first'])['ids'] == []
    print('PASS native query and deletion')
finally:
    collection.space.describe()
    hello = collection.client.hello()
    collection.client.call('drop', collection.space.uuid, collection.space.id,
                           collection.space.revision, hello[4])
    collection.close()
