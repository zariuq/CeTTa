#!/usr/bin/env python3
"""Run the shared native match/query qualification on unchanged sources."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import time

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--out",type=Path,required=True)
parser.add_argument("--cc",default="cc")
parser.add_argument("--sanitize",action="store_true")
parser.add_argument("--controls",action="store_true")
parser.add_argument("--cetta",type=Path)
args=parser.parse_args()
here=Path(__file__).resolve().parent;repo=here.parents[1]
out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
compiler=shlex.split(args.cc)
assert compiler and len(compiler)==1,"--cc names one compiler executable"
if args.sanitize and args.controls:parser.error("run semantic mutation controls in a separate ordinary build")
flags="-O2 -std=c11 -Wall -Wextra -Werror -fstrict-aliasing"
if args.sanitize:
    flags="-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer"

def inputs():
    paths={str(p.relative_to(repo)):p for p in here.iterdir() if p.is_file() and
        (p.suffix in {".c",".h",".S",".py",".metta",".txt",".hol"} or p.name=="Makefile")}
    for directory in [repo/"tisa",repo/"native/p1"]:
        for p in directory.iterdir():
            if p.is_file() and p.suffix in {".c",".h"}:paths[str(p.relative_to(repo))]=p
    if args.cetta:
        host=args.cetta.resolve()
        for p in (host/"native/tcalc").iterdir():
            if p.is_file() and p.suffix in {".c",".h",".export",".data"}:
                paths["host/"+str(p.relative_to(host))]=p
        for name in ["src/runner.h","src/runner_axes.export","src/generated/runner_axes.generated.h",
                     "src/generated/runner_lists.generated.h","lib/runner_axes.data"]:
            paths["host/"+name]=host/name
    return {name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in sorted(paths.items())}

def run(label,command):
    started=time.monotonic()
    p=subprocess.run(list(map(str,command)),cwd=here,text=True,capture_output=True)
    (out/(label+".log")).write_text(p.stdout+p.stderr)
    result={"exit":p.returncode,"seconds":time.monotonic()-started}
    if p.returncode:raise RuntimeError(f"{label} failed; inspect {label}.log")
    return result

before=inputs();(out/"inputs.json").write_text(json.dumps(before,indent=2)+"\n")
result={"passed":False,"compiler":args.cc,"sanitize":args.sanitize,
        "tcalc_client":bool(args.cetta),"checks":{}}
try:
    result["checks"]["compiler"]=run("compiler",compiler+["--version"])
    make=["make","DEVELOPMENT_REFERENCE=1","CC="+args.cc,"CFLAGS="+flags,"BUILD="+str(out/"build")]
    result["checks"]["runtime"]=run("runtime",make+["check"])
    if args.cetta:
        result["checks"]["tcalc"]=run("tcalc",make+["check-tcalc","CETTA="+str(args.cetta.resolve())])
    linked=["python3",here/"test_linked.py","--build",out/"build","--work",out/"linked","--cc",args.cc]
    if args.sanitize:linked.append("--sanitize")
    result["checks"]["linked"]=run("linked",linked)
    if args.controls:
        result["checks"]["controls"]=run("controls",["python3",here/"test_matching_controls.py",
            "--build",out/"build","--work",out/"controls","--cc",args.cc])
        result["checks"]["output_controls"]=run("output-controls",["python3",here/"test_output_controls.py",
            "--build",out/"build","--work",out/"output-controls","--cc",args.cc])
    after=inputs();result["sources_unchanged"]=before==after
    (out/"outputs.json").write_text(json.dumps(after,indent=2)+"\n")
    assert result["sources_unchanged"],"sources changed during qualification"
    result["passed"]=True
finally:
    (out/"result.json").write_text(json.dumps(result,indent=2)+"\n")
print("native matching qualification passed: runtime, contextual views, queries, allocation controls, linked bytes"+
      (", actual tcalc client" if args.cetta else "")+(", semantic mutation controls" if args.controls else ""))
