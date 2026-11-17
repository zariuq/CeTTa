#!/usr/bin/env python3
"""Compare the tcalc fixture corpus through every requested kernel realization."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--binary",type=Path,required=True);p.add_argument("--out",type=Path,required=True)
p.add_argument("--modes",default="c,x86,hybrid");p.add_argument("--pattern",default=".")
p.add_argument("--timeout",type=float,default=120)
a=p.parse_args();repo=Path(__file__).resolve().parents[1];binary=a.binary.resolve()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
before=hashlib.sha256(binary.read_bytes()).hexdigest();rows=[];bad=0
fixtures=sorted(f for f in (repo/"tests/tcalc").iterdir() if f.suffix in {".tcalc",".rho",".chr"} and re.search(a.pattern,f.name))
for mode in a.modes.split(","):
    assert mode in {"c","x86","hybrid"}
    totals={"c":0,"x86":0}
    for f in fixtures:
        first=f.read_text().splitlines()[0];profile=re.match(r"(?:;|//|%) profile: (.*)",first)
        command=[str(binary),"--kernel",mode,"--kernel-stats","--lang","tcalc"]
        if profile:command += ["--profile",profile[1]]
        command.append(str(f));started=time.monotonic()
        try:
            r=subprocess.run(command,cwd=repo,text=True,capture_output=True,timeout=a.timeout)
            stats=re.findall(r"(?m)^kernel: (.*)$",r.stderr)
            stderr=re.sub(r"(?m)^kernel: .*\n?","",r.stderr)
            fields={k:v for k,v in (x.split("=",1) for x in stats[-1].split())} if stats else {}
            for key in totals:totals[key]+=int(fields.get(key,0))
            text=(r.stdout+stderr).strip();expected=f.with_suffix(".expected").read_text().strip()
            conflict=mode!="c" and "backend=x86-flat" in (profile[1] if profile else "")
            if not conflict and mode!="c":
                conflict=bool(re.search(r"\(backend\s+x86-flat\)",f.read_text()))
            ok=("select different execution interfaces" in text and r.returncode==2) if conflict else text==expected
            if mode == "x86" and int(fields.get("c",0)):ok=False
            record={"fixture":f.name,"mode":mode,"passed":ok,"exit":r.returncode,
                "seconds":time.monotonic()-started,"stats":fields,"legacy_conflict_control":conflict}
            if not ok:
                (out/(mode+"-"+f.name+".actual")).write_text(text+"\n")
                print("FAIL",mode,f.name,"exit",r.returncode,flush=True);bad+=1
        except subprocess.TimeoutExpired:
            record={"fixture":f.name,"mode":mode,"passed":False,"timeout":True,"seconds":a.timeout}
            print("TIMEOUT",mode,f.name,flush=True);bad+=1
        rows.append(record)
    print(mode,len(fixtures),"fixtures; calls",totals,flush=True)
    if mode == "x86":assert totals[mode]>0,"no requested native implementation ran"
assert hashlib.sha256(binary.read_bytes()).hexdigest()==before
(out/"result.json").write_text(json.dumps({"passed":bad==0,"binary_sha256":before,
    "fixtures":len(fixtures),"runs":len(rows),"failures":bad,"results":rows},indent=2)+"\n")
raise SystemExit(1 if bad else 0)
