#!/usr/bin/env python3
"""Package the MeTTa-authored matcher as source text and a linked x86 image."""
import argparse
import json
from pathlib import Path
import re
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--out",type=Path,required=True)
parser.add_argument("--cetta",default="cetta")
parser.add_argument("--component",choices=["unify","emit"],default="unify")
args=parser.parse_args()
here=Path(__file__).resolve().parent
repo=here.parents[1]
out=args.out.resolve()
out.mkdir(parents=True,exist_ok=True)
stem="native_"+args.component
symbol="nm_"+args.component
program=out/(stem+"_prog.txt")
subprocess.run([args.cetta,"--no-results",str(here/(args.component+".metta")),str(program),
    str(out/(stem+".notes"))],check=True,cwd=repo)
image=out/(stem+".hol")
subprocess.run([str(repo/"native/p1/build/p1run"),"--image",str(image),stem,str(program)],
    check=True,cwd=repo,stdout=subprocess.DEVNULL)
encoded=re.findall(r'"([0-9a-f]{64,})"',image.read_text())
assert len(encoded)==1
data=bytes.fromhex(encoded[0])
lines=["/* Generated native_unify image; export.py reproduces the body. */",
    ".text",".p2align 4",".globl nm_unify_entry",".hidden nm_unify_entry",
    ".type nm_unify_entry, @function","nm_unify_entry:",
    "/* Indirect-branch landing pad; the p1 image starts at the next label. */",
    ".byte 0xf3,0x0f,0x1e,0xfa",
    ".globl nm_unify_image",".hidden nm_unify_image","nm_unify_image:"]
for start in range(0,len(data),16):
    lines.append(".byte "+",".join(f"0x{x:02x}" for x in data[start:start+16]))
lines += [".globl nm_unify_image_end",".hidden nm_unify_image_end","nm_unify_image_end:",
    ".size nm_unify_entry, .-nm_unify_entry",'.section .rodata',".p2align 3",
    ".globl nm_unify_expected",".hidden nm_unify_expected","nm_unify_expected:"]
for start in range(0,len(data),16):
    lines.append(".byte "+",".join(f"0x{x:02x}" for x in data[start:start+16]))
lines += [".globl nm_unify_expected_end",".hidden nm_unify_expected_end","nm_unify_expected_end:",
    ".globl nm_unify_text",".hidden nm_unify_text","nm_unify_text:"]
text=program.read_text()
for start in range(0,len(text),256):
    lines.append(".ascii "+json.dumps(text[start:start+256]))
lines += [".globl nm_unify_text_end",".hidden nm_unify_text_end","nm_unify_text_end:",
    '.section .note.GNU-stack,"",@progbits']
(out/(stem+".S")).write_text("\n".join(line.replace("native_unify",stem).replace("nm_unify",symbol) for line in lines)+"\n")
assert "/home/" not in image.read_text() and "/shared/" not in image.read_text()
if args.component=="emit":
    fields=re.findall(r'\(= \(ne:h ([a-z-]+)\) (\d+)\)',(here/"emit_layout.metta").read_text())
    assert fields and len({n for _,n in fields})==len(fields)
    header=["/* Generated from emit_layout.metta by export.py. */",
        "#ifndef TCALC_NATIVE_EMIT_LAYOUT_GENERATED_H","#define TCALC_NATIVE_EMIT_LAYOUT_GENERATED_H",
        "enum {"]
    header += ["    NE_STATE_"+name.upper().replace("-","_")+"="+number+"," for name,number in fields]
    header += ["};","#endif"]
    (out/"emit_layout.generated.h").write_text("\n".join(header)+"\n")
print(f"{stem}: {len(data)} body bytes; {len(data)+4} linked entry bytes")
