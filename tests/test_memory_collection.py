#!/usr/bin/env python3
"""Native collection qualification with an independent scalar cosine oracle."""
import argparse
from array import array
import hashlib
import json
import math
import os
from pathlib import Path
import random
import signal
import statistics
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from memory_collection import NativeCollection
from space_service_client import Client, E, S, Tensor, ServiceError, encode, decode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--worker", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--module", type=Path)
    parser.add_argument("--scan-tier", choices=("wide", "f32"), default="wide")
    args = parser.parse_args()
    receipt = {"status": "failed", "checks": [], "programme_complete": False}
    def check(name, condition):
        receipt["checks"].append({"name": name, "passed": bool(condition)})
        if not condition:
            raise AssertionError(name)
    def refused(name, function):
        try: function()
        except (ValueError, TypeError, ServiceError, RuntimeError): check(name, True)
        else: check(name, False)
    server = collection = None
    try:
        with tempfile.TemporaryDirectory(prefix="cetta-memory-", dir=args.work_dir) as temporary:
            root = Path(temporary); data = root / "data"; data.mkdir(mode=0o700)
            socket = root / "service.sock"
            def start():
                process = subprocess.Popen([str(args.server.resolve()), "--data", str(data), "--socket", str(socket),
                    "--max-query-bytes", str(64*1024*1024), "--max-snapshot-bytes", str(128*1024*1024)],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                for _ in range(100):
                    try:
                        c = Client(str(socket)); c.hello(); c.close(); return process
                    except (OSError, EOFError): time.sleep(.02)
                raise RuntimeError("service failed to start")
            server = start()
            profile = {"embedding_output_dim": 16, "embedding_model": "seeded-fixture", "hnsw:space": "cosine"}
            collection = NativeCollection(socket, "qualification", profile, args.worker.resolve(), args.module,
                                          profile_requests=True, scan_tier=args.scan_tier)
            raw = array('d', [1., 2., 3.]); tensor = Tensor.from_buffer(raw); raw[0] = 99.
            check("buffer-ownership-copy", tensor == Tensor.vector([1.,2.,3.]))
            check("tensor-wire-roundtrip", decode(encode(tensor)) == tensor)
            refused("noncontiguous-buffer", lambda: Tensor.from_buffer(memoryview(raw)[::2]))
            compact = Tensor.from_buffer(array('f', [1., -0., .5]))
            check("f32-buffer-wire-roundtrip", decode(encode(compact)) == compact and compact.encoded[4] == 4)
            refused("wrong-buffer-dtype", lambda: Tensor.from_buffer(array('h', [1])))
            refused("truncated-tensor", lambda: Tensor(tensor.encoded[:-1]))
            rng = random.Random(77109)
            rows = {f"r{i:04d}": [rng.uniform(-1,1) for _ in range(16)] for i in range(257)}
            def put(ids):
                collection.upsert(ids, [rows[i] for i in ids], ["literal (+ 1 2) ' text " + i for i in ids],
                                  [{"group": int(i[1:]) % 3, "time": "day"+str(int(i[1:])%2)} for i in ids])
            ids = sorted(rows)
            for index in range(0,len(ids),32): put(ids[index:index+32])
            check("bulk-ingress-count", collection.count() == len(rows))
            saved = collection.get(include=["embeddings", "documents", "metadatas"])
            check("embedding-bits-roundtrip", all(array('d',rows[i]) == v for i,v in zip(saved['ids'],saved['embeddings'])))
            check("strings-are-data", saved['documents'][0] == "literal (+ 1 2) ' text r0000")
            check("empty-id-get", collection.get(ids=[])['ids'] == [])
            check("time-lookup", len(collection.get(where={'time':'day1'})['ids']) == 128)
            check("and-filter", len(collection.get(where={'$and':[{'time':'day0'},{'group':0}]})['ids']) == 43)
            refused("unsupported-filter", lambda: collection.get(where={'time':{'$wat':3}}))
            refused("duplicate-add", lambda: collection.add(['r0000'],[rows['r0000']],['x'],[{}]))
            refused("duplicate-batch", lambda: put(['r0000','r0000']))
            before = collection.count()
            for name,v in [('zero',[0.]*16),('nan',[float('nan')]*16),('inf',[float('inf')]*16),('dimension',[1.])]:
                refused('bad-write-'+name,lambda v=v: collection.upsert(['bad'],[v],['bad'],[{}]))
                refused('bad-query-'+name,lambda v=v: collection.query([v]))
            check("failed-writes-preserve-source", before == collection.count())
            def oracle(q, group=None):
                qnorm=math.sqrt(sum(x*x for x in q))
                scored=[]
                for i,v in rows.items():
                    if group is None or int(i[1:])%3==group:
                        score=sum(a*b for a,b in zip(q,v))/(qnorm*math.sqrt(sum(x*x for x in v)))
                        scored.append((i,1-score))
                return sorted(scored,key=lambda x:(x[1],x[0]))[:7]
            times=[]
            queries = [[rng.uniform(-1,1) for _ in range(16)] for _ in range(4)]
            batch = collection.query(queries, 7)
            check("batched-query-order", batch['ids'] == [[r[0] for r in oracle(q)] for q in queries])
            check("batched-query-single-worker-frame", collection.last_query_metrics['queries'] == 4)
            check("empty-query-batch", collection.query([], 7)['ids'] == [])
            refused("invalid-batch-query", lambda: collection.query([queries[0], [0.]*16], 7))
            base_view = None
            for stage in range(3):
                for query_index in range(12):
                    q=[rng.uniform(-1,1) for _ in range(16)]
                    group=None if query_index%2==0 else query_index%3
                    expected=oracle(q,group)
                    start_time=time.perf_counter()
                    actual=collection.query([q],7,where=None if group is None else {'group':group})
                    times.append((time.perf_counter()-start_time)*1000)
                    check(f"oracle-{stage}-{query_index}", actual['ids'][0]==[r[0] for r in expected] and
                          all(abs(a-b[1])<1e-12 for a,b in zip(actual['distances'][0],expected)))
                status = collection._call(E('index-status'))
                if stage == 0:
                    base_view = status[2]
                else:
                    check(f'base-retained-after-churn-{stage}', status[2] == base_view)
                    check(f'delta-covers-changed-ids-{stage}', status[3] == 10)
                    check(f'delta-latest-version-count-{stage}', status[4][2] == (10 if stage == 1 else 0))
                if stage==0:
                    for i in ids[:10]: rows[i]=[rng.uniform(-1,1) for _ in range(16)]
                    put(ids[:10])
                if stage==1:
                    collection.delete(ids[:10])
                    for i in ids[:10]: del rows[i]
            collection.update(['r0010'],[{'linkedEpisodes':['day0','day1']}])
            check('episode-links',collection.get(ids=['r0010'])['metadatas'][0]['linkedEpisodes']==['day0','day1'])
            collection.update(['r0010'],[{'linkedEpisodes':[]}])
            check('episode-unlink-last',collection.get(ids=['r0010'])['metadatas'][0]['linkedEpisodes']==[])
            collection.update(['r0010'],[{'linkedEpisodes':None}])
            check('metadata-field-delete', 'linkedEpisodes' not in collection.get(ids=['r0010'])['metadatas'][0])
            rows['r0300'] = [rng.uniform(-1,1) for _ in range(16)]
            put(['r0300'])
            check('insert-in-delta', collection.query([rows['r0300']],1,where={'group':0})['ids'][0] == ['r0300'])
            collection.delete(['r0300'])
            del rows['r0300']
            # A large changed-ID set compacts once rather than leaving a delta
            # whose interpreter and allocation cost can grow without bound.
            put(sorted(rows)[:128])
            check('delta-compaction-requested', collection._call(E('index-status'))[2] == S('Missing'))
            q = [rng.uniform(-1,1) for _ in range(16)]
            check('delta-compaction-preserves-ranking', collection.query([q],7)['ids'][0] == [r[0] for r in oracle(q)])
            check('delta-compaction-clears-dirty-set', collection._call(E('index-status'))[3] == 0)
            real_worker = collection.worker
            class FailedCache:
                process = real_worker.process
                def call(self, request):
                    if request[0] == S('apply'):
                        raise RuntimeError('injected cache failure after durable commit')
                    return real_worker.call(request)
                def close(self):
                    real_worker.close()
                    raise RuntimeError('injected worker cleanup failure')
            collection.worker = FailedCache()
            collection.update(['r0010'],[{'cache_probe':'committed'}])
            check('cache-failure-cannot-hide-committed-write', collection.get(ids=['r0010'])['metadatas'][0]['cache_probe']=='committed')
            stale=collection.space.revision
            competitor=Client(str(socket)); other=competitor.open('qualification')
            other.add(E('other-client','unrelated'))
            collection.space.revision=stale
            refused('stale-writer-refused',lambda:collection.space.add(E('must-not-appear')))
            check('unrelated-change-refresh',collection.get(ids=['r0010'])['ids']==['r0010'])
            competitor.close()
            collection.worker.process.kill();collection.worker.process.wait()
            check('dead-query-worker-rebuilt',collection.get(ids=['r0010'])['ids']==['r0010'])
            collection.worker.close()
            check('closed-query-worker-rebuilt',collection.get(ids=['r0010'])['ids']==['r0010'])
            server.kill();server.wait();server=start()
            check('connected-client-recovers-owner-restart',collection.get(ids=['r0010'])['ids']==['r0010'])
            check('restart-retains-count',collection.count()==len(rows))
            check('restart-does-not-resurrect-delete',collection.get(ids=ids[:10])['ids']==[])
            check('restart-retains-embedding-bits',collection.get(ids=['r0010'],include=['embeddings'])['embeddings'][0]==array('d',rows['r0010']))
            collection.client.call('compact',collection.space.uuid)
            collection.close();collection=None
            server.terminate();server.wait();server=start()
            collection=NativeCollection(socket,'qualification',profile,args.worker.resolve(),args.module,
                                        scan_tier=args.scan_tier)
            check('checkpoint-recovery',collection.count()==len(rows))
            refused('profile-mismatch',lambda: NativeCollection(socket,'qualification',dict(profile,embedding_model='other'),args.worker.resolve()))
            collection.delete(where={'group':2})
            check('filtered-delete',collection.get(where={'group':2})['ids']==[])
            receipt.update(status='passed', query_p50_ms=statistics.median(times), query_p95_ms=sorted(times)[int(.95*len(times))],
                           seed=77109, records=257, dimensions=16,
                           source_hashes={str(p.relative_to(Path(__file__).resolve().parents[1])):hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in [Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'lib/petta/lib_memory_collection.metta',
                                       Path(__file__).resolve().parents[1]/'tools/memory_collection.py']})
    finally:
        if collection:collection.close()
        if server and server.poll() is None:server.terminate();server.wait()
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({k:receipt[k] for k in ('status','records','dimensions','query_p50_ms','query_p95_ms')}))

if __name__=='__main__': main()
