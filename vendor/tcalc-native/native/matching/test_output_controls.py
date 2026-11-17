#!/usr/bin/env python3
"""Check that output-bundle controls reject damaged publication and receipts."""
import argparse
import json
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--build",type=Path,required=True);p.add_argument("--work",type=Path,required=True)
p.add_argument("--cc",default="cc");args=p.parse_args()
here=Path(__file__).resolve().parent;work=args.work.resolve();work.mkdir(parents=True,exist_ok=False)
source=(here/"native_output.c").read_text()
changes={
    "after_execution_pin":("r.has_core=true;r.fuel_used=r.core.fuel_used;\n    if (!valid(o))",
        "r.has_core=true;r.fuel_used=r.core.fuel_used;\n    if (false)"),
    "read_pin":("return o && valid(o) ? &o->graph : NULL;","return o ? &o->graph : NULL;"),
    "activation_receipt":("++r.fuel_used;r.activated=true;","r.activated=true;"),
    "root_multiplicity":("o->children+root.a,count};","o->children+root.a,count ? 1 : 0};"),
}
caught={}
for name,(old,new) in changes.items():
    assert source.count(old)==1,(name,source.count(old))
    path=work/(name+".c");path.write_text(source.replace(old,new));binary=work/name
    command=[args.cc,"-O2","-std=c11","-Wall","-Wextra","-Werror","-UNDEBUG","-I",str(here),
        str(here/"test_output.c"),str(path),str(args.build.resolve()/"libnative_match.a"),"-o",str(binary)]
    compiled=subprocess.run(command,text=True,capture_output=True)
    (work/(name+"-compile.log")).write_text(compiled.stdout+compiled.stderr)
    assert compiled.returncode==0,(name,compiled.stderr)
    result=subprocess.run([str(binary)],text=True,capture_output=True,cwd=work)
    (work/(name+".log")).write_text(result.stdout+result.stderr)
    assert result.returncode!=0 and "Assertion" in result.stderr,(name,result.returncode,result.stderr)
    caught[name]={"exit":result.returncode,"caught":True}
(work/"result.json").write_text(json.dumps({"passed":True,"mutations_caught":caught},indent=2)+"\n")
print(f"output-bundle semantic controls: {len(caught)} damaged components rejected")
