"""Large portable facts through the same embedded and served Space operations."""
import argparse
from collections import Counter
import importlib.util
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker, WorkerEvaluationError
from space_service_client import E, S, Var, encode
from check_native_kb_service import Server, Client, await_resources
from check_space_query import open_query, drain, rows
from check_job_queue import NativeStore
from check_space_graph import open_graph, service_rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("worker", type=Path)
    p.add_argument("--maximum-bytes", type=int, default=8*1024*1024)
    p.add_argument("--payload-bytes", type=int, default=2*1024*1024)
    p.add_argument("--rounds", type=int, default=12)
    p.add_argument("--reference-store", type=Path)
    p.add_argument("--cli", type=Path)
    p.add_argument("--output", type=Path)
    args=p.parse_args(); assert args.rounds>0 and args.payload_bytes>1024*1024
    maximum=args.maximum_bytes
    packet=lambda value:encode(value,maximum_bytes=maximum)
    bag=lambda values:Counter(map(packet,values))
    module=ROOT/"tests/support/large_value_space_fixture.metta"
    checks=[]
    with tempfile.TemporaryDirectory(prefix="large-space-") as directory:
        root=Path(directory);path=root/"store";path.mkdir(mode=0o700)
        payload=(bytes(range(251))*((args.payload_bytes+250)//251))[:args.payload_bytes]
        fact=E("metric","same",7,payload)
        pattern=E("metric",Var(0),Var(1),Var(2));template=E("Metric",Var(0),Var(1),Var(2))
        plan=E("KqSpace",S("sample"),pattern,template)
        changes=(E("SpaceInsertV1","a",fact),E("SpaceInsertV1","b",fact))
        seed=Worker(args.worker.resolve(),module,"large-value:run",language="petta",maximum_bytes=maximum)
        reader=server=client=None
        def commit(identifier,edits):
            answer=reader.call(E("Seed",E("At",str(path),E("Commit",identifier,edits))))
            assert answer[0]==S("KnowledgeCommittedV1"),answer[0]
            return answer
        def query(candidate=plan,count=1,limit=maximum):
            return reader.call(E("At",str(path),candidate,count,limit))
        try:
            assert seed.call(E("Budget"))==maximum
            specs=(E("SpaceIndexV1",4,0),E("SpaceIndexV1",4,1),E("SpaceRangeIndexV1",4,2))
            created=seed.call(E("Seed",E("Create",str(path),specs)));assert created[0]==S("KnowledgeCommittedV1")
            memory=seed.call(E("Memory",(fact,fact),pattern,template));assert memory[0]==S("MemoryRows")
            inserted=seed.call(E("Seed",E("At",str(path),E("Commit","large-seed",changes))));assert inserted[0]==S("KnowledgeCommittedV1")
            seed.close();seed=None
            reader=Worker(args.worker.resolve(),module,"large-value:run",language="petta",maximum_bytes=maximum)
            local=rows(query());assert len(local)==2 and bag(r[1] for r in local)==bag(memory[1])
            references=[r[2][0] for r in local];assert len(set(map(packet,references)))==2
            raw=reader.call(E("Raw",str(path),pattern,template));assert bag(raw[1])==bag(memory[1])
            assert bag(rows(query(count=32)))==bag(local)
            checks.append("large-facts-reopen-in-fresh-process-match-memory-and-ordinary-captured-space")
            replay=reader.call(E("Seed",E("At",str(path),E("Commit","large-seed",changes))));assert replay[0]==S("KnowledgeReplayedV1")
            assert bag(rows(query()))==bag(local)
            checks.append("durable-receipt-replay-preserves-two-distinct-occurrences")

            small_worker=Worker(args.worker.resolve(),module,"large-value:run",language="petta")
            try:
                assert small_worker.call(E("Budget"))==1024*1024
                before=small_worker.call(E("Seed",E("At",str(path),E("Info"))))
                rejected=E("Seed",E("At",str(path),E("Commit","too-large-before-send",changes)))
                fingerprint=hashlib.sha256(packet(rejected)).hexdigest()
                try:small_worker.call(rejected)
                except ValueError as error:assert "byte limit" in str(error)
                else:raise AssertionError("small transport unexpectedly admitted a large request")
                assert hashlib.sha256(packet(rejected)).hexdigest()==fingerprint
                assert small_worker.call(E("Seed",E("At",str(path),E("Info"))))==before
                try:small_worker.call(E("Raw",str(path),pattern,template))
                except WorkerEvaluationError as error:assert error.completion==S("capacity"),error.completion
                else:raise AssertionError("small source policy became a successful match")
                assert small_worker.call(E("Budget"))==1024*1024 and reader.call(E("Budget"))==maximum
            finally:small_worker.close()
            checks.append("small-policy-refusal-preserves-input-and-revision-and-is-not-empty-match")

            server=Server(args.worker.resolve(),root,module=module,entry="large-value:configuration",maximum_bytes=maximum)
            server.start();client=Client(server.path,maximum_reply_bytes=maximum,maximum_request_bytes=maximum)
            for operation in ["Remote","RemoteAsync"]:
                identifier="native-large-"+operation
                reply=reader.call(E(operation,str(server.path),E("request",identifier,E("CallV1",S("LargeEcho"),payload))))
                assert reply[:3]==(S("reply"),identifier,S("ok"))
                assert packet(reply[3])==packet(payload)
            checks.append("native-metta-sync-and-async-clients-roundtrip-large-values")
            native_rows=reader.call(E("RemoteQuery",str(server.path),pattern,template))
            assert native_rows[0]==S("ClientRows")
            assert bag((r[1],r[2]) for r in native_rows[1])==bag((r[1],r[2]) for r in local)
            checks.append("native-persistent-client-reads-large-database-facts-with-owned-views")
            def remote(candidate=plan,count=1):
                view,_=client.snapshot();token=open_query(client,view,candidate,maximum);client.drop(view)
                return drain(client,token,count,128,replay=True)[0]
            assert bag(remote())==bag(remote(count=32))==bag(local)
            view,_=client.snapshot();basic=client.query(view,pattern,template);client.drop(view)
            basic_rows=client.drain(basic);client.drop(basic)
            assert bag(basic_rows)==bag((r[1],r[2]) for r in local)
            checks.append("basic-space-query-next-and-relational-pages-share-the-value-policy")
            view,_=client.snapshot();graph=open_graph(client,view,
                E("KgNeighbours",E("KgRelation",S("metric"),0,1),"same",S("out")),arity=4);client.drop(view)
            graph_rows,_=service_rows(client,graph,1,128,replay=True)
            assert bag(graph_rows)==bag(E("KgEdge",r,"same",7) for r in references)
            checks.append("graph-neighbours-read-large-property-bearing-atoms")
            # Use closed values: this tests retained bytes independently of
            # stored occurrences and their intentionally different identities.
            value=E("Metric","same",7,payload)
            distinct=E("KqDistinct",E("KqRows",(value,value)),S("kb:alpha-equal"),maximum)
            assert bag(remote(distinct))==bag([value])
            checks.append("retained-distinct-state-can-use-the-selected-large-byte-budget")
            ordered=E("KqIndexedI64Range",S("sample"),4,2,S("NoBound"),S("NoBound"),pattern,template)
            assert list(map(packet,remote(ordered)))==list(map(packet,rows(query(ordered))))
            checks.append("served-pages-replay-and-ordered-range-equal-embedded-answers")

            view,_=client.snapshot();held=open_query(client,view,plan,maximum);client.drop(view)
            update=E("SpaceUpdateV1",references[0],E("metric","same",9,payload))
            commit("large-update",(update,))
            assert client.route("Compact")[0]==S("StoreCompacted")
            captured,_=drain(client,held,1,128);assert bag(captured)==bag(local)
            current=rows(query());assert set(map(packet,(r[2][0] for r in current)))==set(map(packet,references))
            assert sorted(r[1][2] for r in current)==[7,9]
            checks.append("large-update-maintains-indexes-and-identities-held-reader-survives-compaction")

            view,_=client.snapshot();opened=client.route("ReindexOpen",E("ReindexOpenV1",S("sample"),"large-reindex",maximum),view);client.drop(view)
            for _ in range(10000):
                if opened[0]!=S("ServiceResourceV1"):break
                opened=client.call(E("UseValueV1",opened[1],S("ReindexPage"),E("ReindexPageV1",4)))
            else:raise AssertionError("reindex did not finish")
            assert opened[0] in (S("KnowledgeCommittedV1"),S("KnowledgeReplayedV1")),opened[0]
            assert bag(remote(ordered))==bag(current)
            checks.append("reindex-streams-large-original-atoms-with-the-same-policy")

            commit("large-delete",(E("SpaceDeleteV1",references[1]),))
            assert len(rows(query()))==1 and len(remote())==1
            checks.append("large-delete-removes-postings-without-changing-other-identity")
            view,_=client.snapshot();created=client.route("Create",E("CreateV1",S("bulk"),"create-bulk"),view);client.drop(view)
            assert created[0]==S("KnowledgeCommittedV1")
            edits=tuple(E("SpaceInsertV1",i,E("row",i)) for i in range(129))
            view,_=client.snapshot();written=client.route("Commit",E("CommitV1",S("bulk"),"bulk129",edits),view);client.drop(view)
            assert written[0]==S("KnowledgeCommittedV1"),written[0]
            bulk=E("KqSpace",S("bulk"),E("row",Var(0)),Var(0))
            assert sorted(r[1] for r in remote(bulk,count=32))==list(range(129))
            checks.append("129-edit-service-batch-uses-native-byte-and-work-budgets-without-extra-count-cap")
            view,_=client.snapshot();created=client.route("Create",E("CreateV1",S("bulk-read"),"create-bulk-read"),view);client.drop(view)
            assert created[0]==S("KnowledgeCommittedV1")
            view,_=client.snapshot();session=client.route("RelOpenRead",E("RelOpenV1",plan,maximum),view);client.drop(view)
            assert session[0]==S("ServiceResourceV1")
            for _ in range(10000):
                session=client.call(E("UseValueV1",session[1],S("RelReadPage"),E("RelPageV1",1,128)))
                assert session[0]==S("ServiceResourceV1")
                if isinstance(session[2][2],tuple) and session[2][2][0]==S("Complete"):break
            else:raise AssertionError("read context did not complete")
            written=client.call(E("UseValueV1",session[1],S("RelReadCommit"),
                E("RelReadCommitV1",S("bulk-read"),"observed129",E("ObservedBatchV1"),edits)))
            assert written[0]==S("KnowledgeCommittedV1"),written[0]
            read_bulk=E("KqSpace",S("bulk-read"),E("row",Var(0)),Var(0))
            assert sorted(r[1] for r in remote(read_bulk,count=32))==list(range(129))
            checks.append("completed-read-context-publishes-129-edits-with-original-validation")
            small=query(limit=65536);assert small[0]==S("KqFailedPages"),small[0]
            assert small[1]==((),) or not any(small[1])
            checks.append("smaller-row-budget-fails-explicitly-instead-of-complete-empty")
            client.close();client=None;server.stop(crash=True);server.start()
            client=Client(server.path,maximum_reply_bytes=maximum,maximum_request_bytes=maximum)
            assert bag(remote())==bag(rows(query()))
            checks.append("service-kill-and-reopen-preserve-large-committed-state")
            samples=[]
            for index in range(args.rounds+24):
                assert len(remote())==1;await_resources(client,0)
                if index>=23 and ((index-23)%10==0 or index==args.rounds+23):samples.append(server.sample(client,index-23))
            assert len({s["descriptors"] for s in samples})==1,samples
            assert len({s["stats"][2] for s in samples})==1,samples
        finally:
            if seed:seed.close()
            if reader:reader.close()
            if client:client.close()
            if server:server.stop()

        gateway=[]
        cli=[]
        if args.cli:
            cli_path=root/"cli-store";cli_path.mkdir(mode=0o700)
            for mode in ["seed","verify"]:
                command=[str(args.cli.resolve()),"--lang","petta","--value-bytes",str(maximum),"--run-contract",
                         str(ROOT/"tests/support/large_value_cli_fixture.metta"),str(cli_path),mode]
                process=subprocess.run(command,capture_output=True,text=True)
                assert process.returncode==0,(mode,process.returncode,process.stderr[-1000:])
                assert ("LargeCliCommitted" if mode=="seed" else "LargeCliReopened") in process.stdout
                cli.append(dict(mode=mode,exit_code=process.returncode,stdout=process.stdout))
        reference=None
        if args.reference_store:
            spec=importlib.util.spec_from_file_location("large_reference_store",args.reference_store)
            reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(reference)
        for service in [False,True]:
            sub=root/("gateway-service" if service else "gateway-embedded");sub.mkdir(mode=0o700)
            native=NativeStore(args.worker.resolve(),sub,"large-agent",service=service,maximum_bytes=maximum)
            original=reference.Store(sub/"reference.sqlite3","large-agent") if reference else None
            request=dict(id="large-job",tool="synthetic",arguments=[],input="x"*args.payload_bytes,deadline=1000.0)
            try:
                native.now=10.0
                first=native.submit(request,"large-fingerprint","write")
                if original:
                    with patch.object(reference.time,"time",return_value=10.0):expected=original.submit(request,"large-fingerprint","write")
                    assert first==expected
                assert native.claim()["request"]==request
                if original:
                    with patch.object(reference.time,"time",return_value=10.0):expected_claim=original.claim()
                    assert expected_claim["request"]==request
                native.recover()
                if original:
                    with patch.object(reference.time,"time",return_value=10.0):original.recover()
                    assert native.get("large-job")==original.get("large-job")
                assert native.get("large-job")["state"]=="uncertain"
                gateway.append(dict(service=service,payload_bytes=args.payload_bytes,actual_reference=bool(original)))
            finally:native.close()
        report=dict(checks=checks,maximum_bytes=maximum,payload_bytes=args.payload_bytes,rounds=args.rounds,samples=samples,gateway=gateway,cli=cli,
                    reference_sha256=hashlib.sha256(args.reference_store.read_bytes()).hexdigest() if args.reference_store else None,
                    scope="Temporary stores; original native storage owner and MeTTa Space matcher. No live migration or external dispatch.")
        if args.output:args.output.write_text(json.dumps(report,indent=2)+"\n")
        print(json.dumps(report,indent=2))


if __name__=="__main__":main()
