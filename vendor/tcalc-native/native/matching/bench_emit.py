#!/usr/bin/env python3
"""Record checked native-output experiments against repeated and batched C."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--binary",type=Path,required=True);p.add_argument("--out",type=Path,required=True)
p.add_argument("--processes",type=int,default=3);args=p.parse_args()
assert args.processes>0
binary=args.binary.resolve();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
here=Path(__file__).resolve().parent;samples=[]
inputs={name:hashlib.sha256((here/name).read_bytes()).hexdigest() for name in
    ["test_emit_tcalc.c","native_emit.S","emit.metta","native_emit.c","native_component.c"]}
for i in range(args.processes):
    result=subprocess.run([str(binary),"--bench"],cwd=here,text=True,capture_output=True)
    (out/f"process-{i}.log").write_text(result.stdout+result.stderr)
    assert result.returncode==0 and not result.stderr and "144 complete captured-template bundles agree" in result.stdout
    cases=[]
    for line in result.stdout.splitlines():
        if not line.startswith("BENCH "):continue
        case={k:(float(v) if "." in v else int(v)) for k,v in (field.split("=",1) for field in line.split()[1:])}
        cases.append(case)
    assert len(cases)==4;samples.append(cases)
summary=[]
for index in range(4):
    rows=[sample[index] for sample in samples];first=rows[0];repeats=first["repeats"]
    med={key:statistics.median(row[key]/row["repeats"] for row in rows) for key in
        ["C_ns","C_batched_ns","native_total_ns","prepare_ns","execute_ns","adopt_cleanup_ns"]}
    summary.append({"depth":first["depth"],"roots":first["roots"],"repeats_per_process":repeats,
        "median_ns_per_bundle":med,"vs_current":med["C_ns"]/med["native_total_ns"],
        "vs_batched":med["C_batched_ns"]/med["native_total_ns"],
        "nodes":first["nodes"],"edges":first["edges"],"expanded":first["expanded"]})
assert inputs=={name:hashlib.sha256((here/name).read_bytes()).hexdigest() for name in inputs}
packet={"passed":True,"processes":args.processes,"binary_sha256":hashlib.sha256(binary.read_bytes()).hexdigest(),
    "inputs":inputs,"samples":samples,"summary":summary,
    "scope":"synthetic warm captured-output bundles; native includes import, allocation, execution, C adoption and cleanup; not a MeTTa program benchmark"}
(out/"result.json").write_text(json.dumps(packet,indent=2)+"\n")
for row in summary:
    print(f"depth={row['depth']} roots={row['roots']}: native/current-C={row['vs_current']:.2f}x; native/batched-C={row['vs_batched']:.2f}x")
