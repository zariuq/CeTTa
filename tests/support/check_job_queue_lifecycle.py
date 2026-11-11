"""Competing Space queue decisions, durable retry outcomes and process restart."""
import argparse
import json
from pathlib import Path
import socket
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, MAX_BYTES
from check_native_kb_service import Client, await_resources
from check_job_queue import NativeStore
from value_worker_client import Worker


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("worker", type=Path)
    p.add_argument("--bulk-jobs", type=int, default=130)
    p.add_argument("--rounds", type=int, default=100)
    p.add_argument("--output", type=Path)
    args = p.parse_args(); assert args.bulk_jobs > 128 and args.rounds > 0
    checks=[]
    guard=Worker(args.worker.resolve(),ROOT/"tests/support/job_queue_fixture.metta","job-fixture:run",language="petta")
    try:
        assert guard.call(E("RejectRecoveryRow",S("malformed-row")))==E("GatewayRefusedV1",S("recovery-row"))
        for references in [(),(S("left"),S("right"))]:
            malformed=E("SpaceAnswerV1",E("GatewayJobV1","bad-job",S("running"),1,S("inactive"),1,S("read"),S("bad-fields")),references)
            assert guard.call(E("RejectRecoveryRow",malformed))==E("GatewayRefusedV1",S("recovery-row"),"bad-job")
        checks.append("malformed-recovery-rows-refuse-explicitly-and-name-job-when-available")
    finally:guard.close()
    with tempfile.TemporaryDirectory(prefix="queue-lifecycle-") as directory:
        root=Path(directory);native=NativeStore(args.worker.resolve(),root,"example-agent",service=True)
        other=Client(native.server.path);extra=[]
        def apply_at(client,view,identifier,command):
            return client.route("Apply",E("GatewayApplyV1",identifier,command),view)
        def fresh(client,identifier,command):
            view,_=client.snapshot()
            try:return apply_at(client,view,identifier,command)
            finally:client.drop(view)
        def submit(identifier,effect="read",payload=None):
            request=dict(id=identifier,tool="synthetic",arguments=[],input=payload,deadline=1000000.0)
            native.now+=1;native.submit(request,"fingerprint-"+identifier,effect)
        def race(commands):
            peers=[native.client,other];captured=[peer.snapshot() for peer in peers]
            assert captured[0][1]==captured[1][1]
            barrier=threading.Barrier(2);answers=[None,None];errors=[]
            def run(i):
                try:
                    barrier.wait(timeout=5)
                    answers[i]=apply_at(peers[i],captured[i][0],*commands[i])
                except Exception as error:errors.append(error)
            threads=[threading.Thread(target=run,args=(i,)) for i in range(2)]
            for thread in threads:thread.start()
            for thread in threads:thread.join(timeout=10)
            for peer,(view,_) in zip(peers,captured):peer.drop(view)
            assert not errors and not any(thread.is_alive() for thread in threads),errors
            assert sum(answer[0]==S("GatewayCommittedV1") for answer in answers)==1,answers
            assert sum(answer==E("StoreRefused",S("conflict")) for answer in answers)==1,answers
            return answers
        try:
            submit("write-job","write");submit("read-job","read")
            original_ref=native.inspect("write-job")[0][2][0]
            claim=E("GatewayClaimV1",10.0)
            commands=[("left-claim",claim),("right-claim",claim)]
            answers=race(commands)
            winner=next(i for i,answer in enumerate(answers) if answer[0]==S("GatewayCommittedV1"));loser=1-winner
            assert answers[winner][1]==E("GatewayClaimedV1","write-job")
            retried=fresh(other,commands[loser][0],E("GatewayClaimV1",11.0))
            assert retried[0]==S("GatewayCommittedV1") and retried[1]==E("GatewayClaimedV1","read-job"),retried
            replay=fresh(other,commands[loser][0],E("GatewayClaimV1",999999.0))
            assert replay[0]==S("GatewayReplayedV1") and replay[1]==retried[1],replay
            collision=fresh(other,commands[loser][0],E("GatewayCancelV1","read-job",12.0))
            assert collision==E("KnowledgeRefusedV1",S("receipt-collision")),collision
            assert len(native.get("write-job")["receipts"])==len(native.get("read-job")["receipts"])==2
            checks.append("same-view-competing-claims-conflict-and-fresh-retry-claims-next")
            checks.append("observation-time-not-intent-and-changed-intent-cannot-replay")

            commands=[("unique-left",E("GatewaySubmitV1","one-id","left","{}",S("read"),10000.0,12.0)),
                      ("unique-right",E("GatewaySubmitV1","one-id","right","{}",S("read"),10000.0,12.0))]
            answers=race(commands);loser=next(i for i,a in enumerate(answers) if a==E("StoreRefused",S("conflict")))
            assert fresh(other,*commands[loser])==E("GatewayRefusedV1",S("fingerprint-conflict"))
            checks.append("unique-job-id-invariant-survives-two-client-race")

            readonly=Client(native.server.path);extra.append(readonly)
            view,_=readonly.snapshot();readonly.call(E("ReadOnlyV1"))
            refused=readonly.route("Apply",E("GatewayApplyV1","readonly",E("GatewayClaimV1",13.0)),view,ok=False)
            assert refused==E("ServiceRefusedV1",S("read-only")),refused
            readonly.drop(view);readonly.close();extra.remove(readonly)
            assert native.client.route("Commit",(),ok=False)==E("ServiceRefusedV1",S("unknown-operation"))
            checks.append("read-only-and-closed-write-boundary")

            lost=Client(native.server.path);extra.append(lost);view,_=lost.snapshot()
            command=E("GatewaySubmitV1","lost-reply","lost-fp","{}",S("write"),10000.0,14.0)
            lost.send(E("UseV1",view,S("Apply"),E("GatewayApplyV1","lost-reply-command",command)))
            lost.socket.shutdown(socket.SHUT_WR)
            def resolve():
                v,_=native.client.snapshot()
                try:return native.client.route("Resolve",E("GatewayResolveV1","lost-reply-command",command),v)
                finally:native.client.drop(v)
            until=time.monotonic()+10
            while time.monotonic()<until:
                settled=resolve()
                if isinstance(settled,tuple) and settled[0]==S("GatewayResolvedV1"):break
                assert settled==S("GatewayUnresolvedV1"),settled
                time.sleep(.005)
            else:raise AssertionError("lost reply did not resolve")
            lost.close();extra.remove(lost);other.close();native.client.close()
            native.server.stop(crash=True);native.server.start()
            native.client=Client(native.server.path, maximum_reply_bytes=MAX_BYTES)
            other=Client(native.server.path)
            assert resolve()==settled
            assert native.get("write-job")["state"]==native.get("read-job")["state"]=="running"
            assert native.inspect("write-job")[0][2][0]==original_ref
            native.now=20.0;native.recover()
            assert native.get("write-job")["state"]=="uncertain"
            assert native.get("read-job")["state"]=="failed"
            assert native.get("lost-reply")["state"]=="queued"
            assert native.inspect("write-job")[0][2][0]==original_ref
            checks.append("lost-reply-reopen-resolves-same-outcome-and-preserves-membership")
            checks.append("storage-restart-does-not-infer-dispatch-recovery-or-replay-actions")

            # Drain the two existing queued jobs, then keep terminal history
            # while admitting more than the old 128-task failure boundary.
            while native.claim() is not None:
                pass
            native.recover()
            for index in range(args.bulk_jobs):submit("bulk-"+str(index))
            for index in range(args.bulk_jobs):
                claimed=native.claim();assert claimed and claimed["id"]=="bulk-"+str(index),claimed
                native.finish(claimed["id"],"succeeded",result=index)
            assert native.claim() is None
            assert native.get("bulk-"+str(args.bulk_jobs-1))["result"]==args.bulk_jobs-1
            checks.append("more-than-128-jobs-with-terminal-history-preserve-fifo")

            for index in range(4):submit("medium-"+str(index),"write","x"*300000)
            for index in range(4):assert native.claim()["id"]=="medium-"+str(index)
            native.recover()
            assert all(native.get("medium-"+str(index))["state"]=="uncertain" for index in range(4))
            checks.append("atomic-recovery-of-four300kb-jobs-exceeds-single-wire-envelope")

            other.close();other=None
            samples=[]
            for index in range(args.rounds+24):
                if index%2:
                    assert native.get("write-job")["state"]=="uncertain"
                else:
                    v,_=native.client.snapshot();opened=native.client.route("EventsOpen","write-job",v);native.client.drop(v)
                    assert opened[0]==S("ServiceResourceV1");native.client.call(E("DropV1",opened[1]))
                await_resources(native.client,0)
                if index>=23 and ((index-23)%25==0 or index==args.rounds+23):samples.append(native.server.sample(native.client,index-23))
            assert len({s["descriptors"] for s in samples})==1,samples
            assert len({s["stats"][2] for s in samples})==1,samples
            report=dict(checks=checks,bulk_jobs=args.bulk_jobs,rounds=args.rounds,samples=samples,
                        external_dispatches=0,scope="Temporary stores; metadata receipts are not authority to repeat an external action")
            if args.output:args.output.write_text(json.dumps(report,indent=2)+"\n")
            print(json.dumps(report,indent=2))
        finally:
            if other:other.close()
            for client in extra:client.close()
            native.close()


if __name__=="__main__":main()
