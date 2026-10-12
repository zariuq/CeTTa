"""Measure indexed mailbox work with retained terminal history, on real C storage."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"tests")); sys.path.insert(0,str(ROOT/"tools"))
import test_native_mailbox as fixtures
from native_mailbox import Mailbox
from space_service_client import E, S, Var


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server",required=True)
    parser.add_argument("--worker",required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--completed",type=int,default=10000)
    parser.add_argument("--profile-queries",action="store_true")
    args=parser.parse_args()
    if args.completed<1: parser.error("positive completed count required")
    fixtures.SERVER=str(Path(args.server).resolve()); fixtures.WORKER=str(Path(args.worker).resolve())
    fixture=fixtures.Fixture(); box=None
    try:
        box=Mailbox(fixture.socket,"mailbox",fixtures.WORKER)
        scoped={}
        work={}
        if args.profile_queries:
            def profile_query(pattern,template,limit=2,lease_ms=30000,first=False):
                value=box.space.profile_query(pattern,template,limit,lease_ms,first)
                work.setdefault(pattern[0].name,[]).append(value[2][1:])
                return value[1]
            box.space.query=profile_query
            box.space.query_first=lambda pattern,template:profile_query(pattern,template,first=True)
        original_rows=box._rows
        def measured_rows(pattern,revision,first=False):
            started=time.perf_counter_ns()
            try: return original_rows(pattern,revision,first)
            finally: scoped.setdefault(pattern[0].name,[]).append((time.perf_counter_ns()-started)/1e6)
        box._rows=measured_rows
        results=[]
        for history in (0,args.completed):
            if history:
                started=time.monotonic()
                for offset in range(0,history,100):
                    ops=[]
                    for i in range(offset,min(history,offset+100)):
                        identity=f"old-wait-{i}"; message=f"old-message-{i}"
                        continuation=E("MailboxResume",1,"completed",identity)
                        wait=E("MailboxWait",1,identity,"archive",identity,"producer","owner",i+1,continuation)
                        records=[E("MailboxMessage",1,message,"archive",identity,"producer","retained terminal body"),
                                 E("MailboxRegistration",1,identity,wait),E("MailboxOwner",1,"owner",i+1,identity),
                                 E("MailboxRoute",1,"archive",identity,"producer",identity),
                                 E("MailboxSettled",1,identity,"owner",i+1,message)]
                        ops.extend(E("add",record) for record in records)
                    box.space.batch(*ops)
                import_ms=(time.monotonic()-started)*1000
            else: import_ms=0
            identity=f"probe-{history}"
            box.register(identity,"review",identity,"codex","probe",history+1,E("MailboxResume",1,"result",identity))
            scoped.clear()
            work.clear()
            samples=[]; before=dict(box.metrics)
            for _ in range(101):
                started=time.perf_counter_ns()
                assert box.resolve(identity)[0]==S("MailboxWaiting")
                samples.append((time.perf_counter_ns()-started)/1e6)
            reads_per_probe=(box.metrics["reads"]-before["reads"])/101
            started=time.perf_counter_ns()
            box.send("arrival-"+identity,"review",identity,"codex","answer")
            assert box.resolve(identity)[6][-1]=="answer"
            ready_ms=(time.perf_counter_ns()-started)/1e6
            results.append({"completed_history":history,"native_facts":box.space.describe()[7],
                            "import_ms":round(import_ms,3),"waiting_probe_ms":{"median":round(statistics.median(samples),3),
                            "p95":round(sorted(samples)[95],3),"max":round(max(samples),3)},
                            "send_and_resume_ms":round(ready_ms,3),
                            "scoped_read_medians_ms":{key:round(statistics.median(value),3) for key,value in scoped.items()},
                            "query_work_last":{key:value[-1] for key,value in work.items()},
                            "scoped_reads_per_waiting_probe":reads_per_probe,
                            "model_calls":0})
        evidence={"status":"passed","scope":"native mailbox fixtures; existing activity journal and live agent loop unchanged",
                  "server_sha256":hashlib.sha256(Path(fixtures.SERVER).read_bytes()).hexdigest(),
                  "worker_sha256":hashlib.sha256(Path(fixtures.WORKER).read_bytes()).hexdigest(),
                  "measurements":results}
        args.output.write_text(json.dumps(evidence,indent=2)+"\n")
        print(json.dumps(evidence,indent=2))
    finally:
        if box is not None: box.close()
        fixture.close()


if __name__=="__main__": main()
