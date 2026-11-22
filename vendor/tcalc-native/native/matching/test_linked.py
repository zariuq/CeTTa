#!/usr/bin/env python3
"""Check the linked native entry and registration rejection of altered bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build",type=Path,required=True)
parser.add_argument("--work",type=Path,required=True)
parser.add_argument("--cc",default="cc")
parser.add_argument("--sanitize",action="store_true")
args=parser.parse_args()
here=Path(__file__).resolve().parent
repo=here.parents[1]
build=args.build.resolve()
work=args.work.resolve()
work.mkdir(parents=True,exist_ok=False)

def run(label,command,status=0):
    p=subprocess.run(list(map(str,command)),cwd=here,text=True,capture_output=True)
    output=p.stdout+p.stderr
    (work/(label+".log")).write_text(output)
    assert p.returncode==status,(label,p.returncode,output[-2000:])
    return output

encoded=re.findall(r'"([0-9a-f]{64,})"',(here/"native_unify.hol").read_text())
assert len(encoded)==1
body=bytes.fromhex(encoded[0])
expected=bytes.fromhex("f30f1efa")+body
binary=build/"test_runtime"
symbols=run("symbols",["nm","-S","-n",binary])
locations={}
sizes={}
for line in symbols.splitlines():
    m=re.fullmatch(r"([0-9a-fA-F]+)\s+(?:([0-9a-fA-F]+)\s+)?[A-Za-z]\s+(\w+)",line)
    if m:
        locations[m[3]]=int(m[1],16)
        if m[2]:sizes[m[3]]=int(m[2],16)
sections=run("sections",["objdump","-h",binary])
m=re.search(r"(?m)^\s*\d+\s+\.text\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s",sections)
assert m
base=int(m[1],16)
text=work/"linked-text.bin"
run("extract",["objcopy","--dump-section",".text="+str(text),binary])
start=locations["nm_unify_entry"]-base
assert sizes["nm_unify_entry"]==len(expected)
assert text.read_bytes()[start:start+len(expected)]==expected
assert locations["nm_unify_image"]-locations["nm_unify_entry"]==4
assert locations["nm_unify_image_end"]-locations["nm_unify_image"]==len(body)
rosection=re.search(r"(?m)^\s*\d+\s+\.rodata\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s",sections)
assert rosection
robase=int(rosection[1],16)
rofile=work/"linked-rodata.bin"
run("extract-model",["objcopy","--dump-section",".rodata="+str(rofile),binary])
rodata=rofile.read_bytes()
a,b=locations["nm_unify_expected"]-robase,locations["nm_unify_expected_end"]-robase
assert rodata[a:b]==body
a,b=locations["nm_unify_text"]-robase,locations["nm_unify_text_end"]-robase
assert rodata[a:b]==(here/"native_unify_prog.txt").read_bytes()
disassembly=run("entry",["objdump","-d","--disassemble=nm_unify_entry",binary])
assert "endbr64" in disassembly

compiler=shlex.split(args.cc)
options=["-O2","-std=c11","-Wall","-Wextra","-Werror","-UNDEBUG","-I",here]
if args.sanitize:options += ["-fsanitize=address,undefined","-fno-omit-frame-pointer"]
assembly=(here/"native_unify.S").read_text()
controls={}
for name,old,new in [("landing",".byte 0xf3,0x0f,0x1e,0xfa",".byte 0xf3,0x0f,0x1e,0xfb"),
        ("body",".byte "+",".join(f"0x{x:02x}" for x in body[:16]),
         ".byte "+",".join(f"0x{x:02x}" for x in bytes([body[0]^1])+body[1:16]))]:
    assert assembly.count(old) >= 1
    source=work/(name+".S")
    source.write_text(assembly.replace(old,new,1))
    obj=work/(name+".o")
    run(name+"-assemble",compiler+["-c",source,"-o",obj])
    target=work/("bad-"+name)
    run(name+"-link",compiler+options+[here/"test_runtime.c",obj,build/"libnative_match.a","-o",target])
    output=run(name,[target,here/"native_unify_prog.txt"],1)
    assert "does not match the linked native-unifier image" in output
    controls[name]=output.strip()

def symbol_table(label,binary):
    found={}
    for line in run(label,["nm","-S","-n",binary]).splitlines():
        m=re.fullmatch(r"([0-9a-fA-F]+)\s+(?:([0-9a-fA-F]+)\s+)?[A-Za-z]\s+(\w+)",line)
        if m: found[m[3]]=(int(m[1],16),int(m[2],16) if m[2] else None)
    return found

def section_data(label,binary,name):
    sections=run(label+"-sections",["objdump","-h",binary])
    m=re.search(r"(?m)^\s*\d+\s+"+re.escape(name)+r"\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s",sections)
    assert m,(binary,name)
    target=work/(label+name+".bin")
    run(label+"-extract",["objcopy","--dump-section",name+"="+str(target),binary])
    return int(m[1],16),target.read_bytes()

query=build/"test_query";obj=build/"native_selection.o"
qloc=symbol_table("query-symbols",query);oloc=symbol_table("selection-symbols",obj)
qb,qt=section_data("query-code",query,".text")
rb,qr=section_data("query-copy",query,".rodata")
ob,ot=section_data("selection-code",obj,".text")
selection={}
for suffix in ["advance","allowed"]:
    fn="nm_selection_"+suffix
    address,length=qloc[fn];offset=address-qb;assert length
    code=qt[offset:offset+length]
    original=oloc[fn][0]-ob
    a,b=qloc[fn+"_expected"][0]-rb,qloc[fn+"_expected_end"][0]-rb
    assert code==ot[original:original+length]==qr[a:b] and code[:4]==bytes.fromhex("f30f1efa")
    selection[suffix]={"bytes":length,"sha256":hashlib.sha256(code).hexdigest()}
for label,fn,within in [("selection-landing","nm_selection_advance",0),
                        ("selection-body","nm_selection_allowed",4)]:
    changed=bytearray(ot);changed[oloc[fn][0]-ob+within]^=1
    raw=work/(label+".text");raw.write_bytes(changed)
    mutated=work/(label+".o")
    run(label+"-change",["objcopy","--update-section",".text="+str(raw),obj,mutated])
    target=work/("bad-"+label)
    run(label+"-link",compiler+options+[here/"test_selection_registration.c",mutated,
        build/"libnative_match.a","-o",target])
    output=run(label,[target]);assert "rejected before execution" in output
    controls[label]=output.strip()
result={"passed":True,"body_bytes":len(body),"entry_bytes":len(expected),
    "packaged_reference_text_matches":True,
    "entry_sha256":hashlib.sha256(expected).hexdigest(),"registration_controls":controls,
    "binary_sha256":hashlib.sha256(binary.read_bytes()).hexdigest(),"selection":selection,
    "scope":"actual linked bytes and before-execution registration controls; no machine-code proof"}
emit_body_text=re.findall(r'"([0-9a-f]{64,})"',(here/"native_emit.hol").read_text())
assert len(emit_body_text)==1
emit_body=bytes.fromhex(emit_body_text[0]);emit_entry=bytes.fromhex("f30f1efa")+emit_body
emit_binary=build/"test_emit";eloc=symbol_table("emit-symbols",emit_binary)
eb,et=section_data("emit-code",emit_binary,".text");erb,er=section_data("emit-model",emit_binary,".rodata")
a,length=eloc["nm_emit_entry"];assert length==len(emit_entry) and et[a-eb:a-eb+length]==emit_entry
a,b=eloc["nm_emit_expected"][0]-erb,eloc["nm_emit_expected_end"][0]-erb;assert er[a:b]==emit_body
a,b=eloc["nm_emit_text"][0]-erb,eloc["nm_emit_text_end"][0]-erb
assert er[a:b]==(here/"native_emit_prog.txt").read_bytes()
emit_assembly=(here/"native_emit.S").read_text();emit_controls={}
for label,old,new in [("emit-landing",".byte 0xf3,0x0f,0x1e,0xfa",".byte 0xf3,0x0f,0x1e,0xfb"),
        ("emit-body",".byte "+",".join(f"0x{x:02x}" for x in emit_body[:16]),
         ".byte "+",".join(f"0x{x:02x}" for x in bytes([emit_body[0]^1])+emit_body[1:16]))]:
    assert old in emit_assembly
    source=work/(label+".S");source.write_text(emit_assembly.replace(old,new,1));obj=work/(label+".o")
    run(label+"-assemble",compiler+["-c",source,"-o",obj]);exe=work/("bad-"+label)
    run(label+"-link",compiler+options+[here/"test_emit_registration.c",obj,build/"libnative_match.a","-o",exe])
    output=run(label,[exe]);assert "rejected before execution" in output;emit_controls[label]=output.strip()
result["emitter"]={"body_bytes":len(emit_body),"entry_bytes":len(emit_entry),
    "entry_sha256":hashlib.sha256(emit_entry).hexdigest(),"registration_controls":emit_controls}
(work/"result.json").write_text(json.dumps(result,indent=2)+"\n")
print(f"linked native components: matcher {len(expected)} bytes, output {len(emit_entry)} bytes, selection {sum(x['bytes'] for x in selection.values())} bytes; six changed-image controls rejected before execution")
