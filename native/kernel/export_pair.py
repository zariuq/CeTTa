#!/usr/bin/env python3
"""Reproduce the matching and observation component images and field declarations."""
import argparse
import json
from pathlib import Path
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--component", choices=("pair","observe","constructor","word","tcunify","tcbuild"), default="pair");p.add_argument("--cetta",required=True);p.add_argument("--p1run",required=True);p.add_argument("--out",type=Path,required=True)
a=p.parse_args();here=Path(__file__).resolve().parent;out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
component=a.component
prefix={"pair":"np","observe":"no","constructor":"nc","word":"nw","tcunify":"tu","tcbuild":"tb"}[component]
program=out/("native_"+component+"_prog.txt")
subprocess.run([a.cetta,"--no-results",str(here/(component+".metta")),str(program),str(out/("native_"+component+".notes"))],check=True)
model=out/("native_"+component+".hol")
subprocess.run([a.p1run,"--image",str(model),"native_"+component,str(program)],check=True,stdout=subprocess.DEVNULL)
encoded=re.findall(r'"([0-9a-f]{64,})"',model.read_text());assert len(encoded)==1
body=bytes.fromhex(encoded[0]);lines=["/* Generated from pair.metta by export_pair.py. */",".text",".p2align 4",
    ".globl ck_pair_entry",".hidden ck_pair_entry",".type ck_pair_entry, @function","ck_pair_entry:",
    ".byte 0xf3,0x0f,0x1e,0xfa",".globl ck_pair_image",".hidden ck_pair_image","ck_pair_image:"]
def data(raw):
    return [".byte "+",".join(f"0x{x:02x}" for x in raw[i:i+16]) for i in range(0,len(raw),16)]
lines+=data(body)+[".globl ck_pair_image_end",".hidden ck_pair_image_end","ck_pair_image_end:",
    ".size ck_pair_entry, .-ck_pair_entry", ".section .rodata", ".p2align 3",
    ".globl ck_pair_expected",".hidden ck_pair_expected","ck_pair_expected:"]
lines+=data(body)+[".globl ck_pair_expected_end",".hidden ck_pair_expected_end","ck_pair_expected_end:",
    ".globl ck_pair_text",".hidden ck_pair_text","ck_pair_text:"]
text=program.read_text()
lines += [".ascii "+json.dumps(text[i:i+256]) for i in range(0,len(text),256)]
lines += [".globl ck_pair_text_end",".hidden ck_pair_text_end","ck_pair_text_end:",'.section .note.GNU-stack,"",@progbits']
(out/("native_"+component+".S")).write_text("\n".join(line.replace("pair",component).replace("export_"+component+".py","export_pair.py") for line in lines)+"\n")
layout=here/(component+"_layout.metta")
fields=re.findall(r'\(= \('+prefix+r':([fakrn]) ([a-z-]+)\) (\d+)\)',layout.read_text() if layout.exists() else "")
if fields:
    header=["/* Generated from pair_layout.metta by export_pair.py. */","#ifndef CETTA_KERNEL_PAIR_LAYOUT_H","#define CETTA_KERNEL_PAIR_LAYOUT_H","enum {"]
    header += ["    NP_"+kind.upper()+"_"+name.upper().replace("-","_")+"="+value+"," for kind,name,value in fields]
    header += ["    NP_FACTS="+str(1+max(int(n) for k,_,n in fields if k=="f")),"};","#endif"]
    (out/(component+"_layout.generated.h")).write_text("\n".join(line.replace("PAIR",component.upper()).replace("pair",component).replace("NP_",prefix.upper()+"_").replace("export_"+component+".py","export_pair.py") for line in header)+"\n")
assert "/home/" not in model.read_text() and "/shared/" not in model.read_text()
print(f"native policy dispatcher: {len(body)} body bytes, {len(body)+4} entry bytes")
