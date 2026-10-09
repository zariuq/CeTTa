#!/usr/bin/env python3
"""Measure durable bulk insertion, queries and large values over CSP1."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import tempfile
import time

from space_service_client import Client, E, S, Var


def milliseconds(action):
    start=time.perf_counter()
    result=action()
    return result,(time.perf_counter()-start)*1000


def trial(server, backend, count):
    with tempfile.TemporaryDirectory(prefix="space-cost-") as temporary:
        root=Path(temporary)
        endpoint=str(root / "socket")
        with (root / "stderr").open("w") as log:
            process=subprocess.Popen([server,"--socket",endpoint,"--data",str(root / "data")],
                                     stdout=subprocess.DEVNULL,stderr=log)
            try:
                deadline=time.monotonic()+15
                while not Path(endpoint).exists():
                    if process.poll() is not None or time.monotonic()>deadline:
                        raise RuntimeError((root / "stderr").read_text())
                    time.sleep(.005)
                with Client(endpoint) as client:
                    space=client.open("cost",backend)
                    def bulk():
                        for begin in range(0,count,1000):
                            space.batch(*(E("add",E("fact",i,i%17))
                                          for i in range(begin,min(begin+1000,count))))
                    _,bulk_ms=milliseconds(bulk)
                    if space.match_count(E("fact",Var(0),Var(1)))!=count:
                        raise RuntimeError("bulk count disagrees")
                    latencies=[]
                    for i in range(100):
                        rows,elapsed=milliseconds(lambda: space.match(E("fact",i,Var(0)),Var(0)))
                        if rows!=[i%17]:
                            raise RuntimeError("point query disagrees")
                        latencies.append(elapsed)
                    rows,scan_ms=milliseconds(lambda: space.match(E("fact",Var(0),Var(1)),Var(0),limit=count))
                    if len(rows)!=count:
                        raise RuntimeError("paged scan disagrees")
                    payload="x"*65536
                    _,large_add=milliseconds(lambda: space.add(E("large",payload)))
                    rows,large_query=milliseconds(lambda: space.match(E("large",Var(0)),Var(0)))
                    if rows!=[payload]:
                        raise RuntimeError("large value disagrees")
                    _,compact_ms=milliseconds(lambda: client.call("compact",space.uuid))
                    status=Path(f"/proc/{process.pid}/status").read_text()
                    rss=next(int(line.split()[1]) for line in status.splitlines() if line.startswith("VmHWM:"))
                    result={"backend":backend,"facts":count,"bulk_ms":bulk_ms,
                            "point_query_median_ms":statistics.median(latencies),
                            "paged_scan_ms":scan_ms,"large_add_ms":large_add,
                            "large_query_ms":large_query,"compact_ms":compact_ms,
                            "peak_rss_kib":rss,"journal_bytes":(root / "data/journal.csl").stat().st_size}
                return result
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=15)
                if process.returncode:
                    raise RuntimeError((root / "stderr").read_text())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server")
    parser.add_argument("--facts",type=int,default=10000)
    parser.add_argument("--runs",type=int,default=3)
    args=parser.parse_args()
    if not 1<=args.facts<=60000 or not 1<=args.runs<=100:
        parser.error("facts must be 1..60000 and runs 1..100")
    records=[]
    for _ in range(args.runs):
        for backend in ("native","pathmap","mork"):
            records.append(trial(str(Path(args.server).resolve()),backend,args.facts))
    medians={}
    for backend in ("native","pathmap","mork"):
        rows=[record for record in records if record["backend"]==backend]
        medians[backend]={key:statistics.median(row[key] for row in rows)
                          for key in rows[0] if key!="backend"}
    print(json.dumps({"runs":records,"medians":medians},indent=2))


if __name__=="__main__":
    main()
