"""Explicit Int64 index order against a complete MeTTa scan/order reference."""
import argparse
from pathlib import Path
import json
import random
import sys
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var, FloatBits, encode
from value_worker_client import Worker
from check_native_kb_service import Server, Client, await_resources
from check_space_query import rows, open_query, drain


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("worker", type=Path)
    p.add_argument("--rounds", type=int, default=100)
    p.add_argument("--output", type=Path)
    args = p.parse_args(); assert args.rounds > 0
    with tempfile.TemporaryDirectory(prefix="indexed-i64-") as directory:
        root = Path(directory); path = root / "store"; path.mkdir(mode=0o700)
        module = ROOT / "tests/support/ordered_i64_fixture.metta"
        seed = Worker(args.worker.resolve(), ROOT / "tests/support/indexed_space_fixture.metta", "indexed-fixture:run", language="petta")
        embedded = Worker(args.worker.resolve(), module, "space-query:run", language="petta")
        guard = Worker(args.worker.resolve(), module, "kb:integer?", language="petta")
        server = Server(args.worker.resolve(), root, module=module, entry="query-test:configuration")
        client = None
        def plan(lower=S("NoBound"), upper=S("NoBound")):
            return E("KqIndexedI64Range", S("sample"), 3, 2, lower, upper,
                     E("metric", Var(0), Var(1)), E("Metric", Var(0), Var(1)))
        def query(candidate, count=1, fuel=2):
            return embedded.call(E("At", str(path), candidate, count, fuel))
        try:
            for value in [True, False, "text", S("name"), E("data", 1), Var(0), FloatBits.of(1.0)]:
                assert guard.call(value) in (False, S("false"), S("False")), value
            assert guard.call(1) in (True, S("true"), S("True"))
            assert seed.call(E("Create", str(path), (E("SpaceRangeIndexV1", 3, 2),)))[0] == S("KnowledgeCommittedV1")
            integers = [-(2**63), -(2**63)+1, -25, -1, 0, 0, 1, 2, 25, 2**63-2, 2**63-1]
            rng = random.Random(91017); values = integers + [rng.randrange(-50, 51) for _ in range(24)]
            for index, value in enumerate(values):
                # Random physical IDs make accidental membership enumeration
                # observably different from the declared numeric ordering.
                out = seed.call(E("At", str(path), E("Commit", uuid.uuid4().hex,
                    (E("SpaceInsertV1", 0, E("metric", "row-"+str(index), value)),))))
                assert out[0] == S("KnowledgeCommittedV1"), out
            others = [FloatBits.of(0.0), FloatBits.of(-0.0), FloatBits.of(1.0), True, False,
                      S("zero"), "zero", E("payload", 0), Var(0)]
            out = seed.call(E("At", str(path), E("Commit", "other-values",
                tuple(E("SpaceInsertV1", i, E("metric", "other-"+str(i), value)) for i, value in enumerate(others)))))
            assert out[0] == S("KnowledgeCommittedV1"), out
            reference = E("KqOrder", E("KqSelect", E("KqSpace", S("sample"), E("metric", Var(0), Var(1)),
                E("Metric", Var(0), Var(1))), S("ordered-i64:integer?")), S("ordered-i64:less"), 131072)
            expected = rows(query(reference))
            assert [row[1][2] for row in expected] == sorted(values), expected
            server.start(); client = Client(server.path)
            checks = 0
            intervals = [(S("NoBound"), S("NoBound")), (E("Inclusive", -1), E("Inclusive", 2)),
                         (E("Exclusive", 0), E("Exclusive", 25)),
                         (E("Inclusive", FloatBits.of(-0.5)), E("Inclusive", FloatBits.of(2.5))),
                         (E("Inclusive", 2**63-1), S("NoBound")),
                         (E("Exclusive", 2**63-1), S("NoBound")),
                         (S("NoBound"), E("Exclusive", -(2**63)))]
            def in_bounds(value, lower, upper):
                def bound(b):
                    n=b[1]
                    if isinstance(n, FloatBits):
                        import struct
                        n=struct.unpack("<d",struct.pack("<Q",n.bits))[0]
                    return n
                return ((lower==S("NoBound") or (value>=bound(lower) if lower[0]==S("Inclusive") else value>bound(lower))) and
                        (upper==S("NoBound") or (value<=bound(upper) if upper[0]==S("Inclusive") else value<bound(upper))))
            for lower, upper in intervals:
                wanted = [row for row in expected if in_bounds(row[1][2], lower, upper)]
                for count, fuel in [(1,1),(16,128)]:
                    candidate=plan(lower,upper); local=rows(query(candidate,count,fuel))
                    view,_=client.snapshot();token=open_query(client,view,candidate);client.drop(view)
                    remote,_=drain(client,token,count,fuel)
                    assert tuple(map(encode,local))==tuple(map(encode,remote))==tuple(map(encode,wanted)),(lower,upper,local,remote,wanted)
                    checks+=1
            view,_=client.snapshot(); old=open_query(client,view,plan()); client.drop(view)
            out=seed.call(E("At",str(path),E("Commit","new-minimum",(E("SpaceInsertV1",0,E("metric","new-min",-(2**63))),))))
            assert out[0]==S("KnowledgeCommittedV1")
            assert client.route("Compact")[0]==S("StoreCompacted")
            captured,_=drain(client,old,1,2)
            assert tuple(map(encode,captured))==tuple(map(encode,expected))
            fresh=rows(query(plan()));assert len(fresh)==len(expected)+1
            assert len({encode(row[2][0]) for row in fresh})==len(fresh)
            # The indexed operation is explicit about requiring an index.
            # Ordinary KqRange keeps its complete-scan fallback separately.
            missing = root / "missing-index"; missing.mkdir(mode=0o700)
            assert seed.call(E("Create", str(missing), ()))[0] == S("KnowledgeCommittedV1")
            refused = embedded.call(E("At", str(missing), plan(), 1, 1))
            assert refused == E("KbRefused", S("ordered-integer-index-required")), refused
            other_path = root / "noninteger"; other_path.mkdir(mode=0o700)
            assert seed.call(E("Create", str(other_path), (E("SpaceRangeIndexV1", 3, 2),)))[0] == S("KnowledgeCommittedV1")
            more = tuple(E("SpaceInsertV1", i, E("metric", i, "not-an-integer")) for i in range(512))
            inserted = seed.call(E("At", str(other_path), E("Commit", "noninteger", more)))
            assert inserted[0] == S("KnowledgeCommittedV1"), inserted
            excluded = embedded.call(E("At", str(other_path), plan(), 1, 1))
            assert rows(excluded) == [] and excluded[2][2] <= 1 and excluded[2][3] == 0, excluded
            samples=[]
            for index in range(args.rounds+24):
                view,_=client.snapshot();token=open_query(client,view,plan());client.drop(view)
                assert client.call(E("DropV1",token))==S("ServiceClosedV1")
                await_resources(client,0)
                if index>=23 and ((index-23)%25==0 or index==args.rounds+23):samples.append(server.sample(client,index-23))
            assert len({s["descriptors"] for s in samples})==1,samples
            assert len({s["stats"][2] for s in samples})==1,samples
            report=dict(ordered_comparisons=checks,int64_records=len(values),non_integer_records=len(others),
                        reference="complete MeTTa scan + explicit integer predicate + value/occurrence comparator",
                        declared_index_required=True, noninteger_bucket_excluded_without_decode=512,
                        captured_publication_compaction=True,rounds=args.rounds,samples=samples)
            if args.output:args.output.write_text(json.dumps(report,indent=2)+"\n")
            print(json.dumps(report,indent=2))
        finally:
            if client:client.close()
            server.stop();embedded.close();seed.close();guard.close()


if __name__=="__main__":main()
