#!/usr/bin/env python3
"""Independent recursive graph instantiation and native/reference controls."""
from __future__ import annotations
import argparse
from dataclasses import dataclass,replace
import hashlib
import json
from pathlib import Path
import random
import re
import subprocess

@dataclass
class Case:
    name:str
    nodes:list[tuple[int,int,int,int]]
    edges:list[int]
    keys:list[tuple[int,int]]
    bindings:list[int]
    root:int
    output:int=128
    out_edges:int=1024
    stack:int=128
    memo:int|None=None
    budget:int=1000000
    used:int=0
    expected:tuple[str,int]|None=None

class Refusal(Exception):
    def __init__(self,reason):self.reason=reason

def oracle(c:Case):
    if not c.output or not c.stack:return "INVALID",3,None,None
    if not 0<c.root<len(c.nodes):return "UNSUPPORTED",2,None,None
    if len(c.keys)!=len(c.bindings):return "UNSUPPORTED",3,None,None
    if c.memo is not None and c.memo<len(c.nodes)+len(c.bindings):return "UNSUPPORTED",6,None,None
    if any((a[1],a[0])>=(b[1],b[0]) for a,b in zip(c.keys,c.keys[1:])):
        return "UNSUPPORTED",3,None,None
    out=[(0,0,0,0,0)];edges=[];memo={};variables={};active=set();var_active=set()
    def allocate(node,origin):
        if len(out)>=c.output:raise Refusal(4)
        out.append((*node,origin));return len(out)-1
    def emit(i,depth):
        if depth>c.stack:raise Refusal(7)
        if not 0<i<len(c.nodes):raise Refusal(2)
        if i in memo:return memo[i]
        if i in active:raise Refusal(8)
        active.add(i);kind,a,b,d=c.nodes[i]
        if kind==0:
            if b or d:raise Refusal(3)
            result=allocate((kind,a,b,d),i)
        elif kind==1:
            if a>=len(c.bindings) or c.keys[a]!=(b,d):raise Refusal(3)
            if a in var_active:raise Refusal(8)
            if a in variables:result=variables[a]
            else:
                var_active.add(a)
                result=emit(c.bindings[a],depth+1) if c.bindings[a] else allocate((kind,a,b,d),i)
                variables[a]=result;var_active.remove(a)
        elif kind==2:
            if d or a>len(c.edges) or b>len(c.edges)-a:raise Refusal(3)
            if b>c.out_edges-len(edges):raise Refusal(5)
            start=len(edges);edges.extend([0]*b)
            for j,child in enumerate(c.edges[a:a+b]):
                if not 0<child<i:raise Refusal(3)
                edges[start+j]=emit(child,depth+1)
            result=allocate((kind,start,b,0),i)
        else:raise Refusal(3)
        active.remove(i);memo[i]=result;return result
    try:root=emit(c.root,1)
    except Refusal as error:return "UNSUPPORTED",error.reason,None,None
    return "FIRED",root,out,edges

def fixed():
    n=[(0,0,0,0),(1,0,41,9),(1,1,41,10),(0,77,0,0),
       (1,0,41,9),(2,0,2,0),(2,2,3,0)]
    e=[1,2,5,5,4];keys=[(41,9),(41,10)]
    base=Case("changed_bundle",n,e,keys,[3,0],6)
    cases=[base,replace(base,name="identity_context",bindings=[0,0]),
        replace(base,name="scoped_alias",bindings=[2,3]),
        replace(base,name="cyclic_alias",bindings=[2,1]),
        replace(base,name="occurs_cycle",bindings=[5,0]),
        replace(base,name="self_alias",bindings=[4,0]),
        replace(base,name="bad_root",root=0),replace(base,name="bad_binding",bindings=[999,0]),
        replace(base,name="output_capacity",output=2),replace(base,name="edge_capacity",out_edges=1),
        replace(base,name="stack_capacity",stack=1),replace(base,name="memo_capacity",memo=1),
        replace(base,name="zero_fuel",budget=0,expected=("EXHAUSTED",4)),
        replace(base,name="unit",nodes=[(0,0,0,0),(2,0,0,0)],edges=[],keys=[],bindings=[],root=1),
        replace(base,name="zero_key",nodes=[(0,0,0,0),(1,0,0,0)],edges=[],keys=[(0,0)],bindings=[0],root=1)]
    for field in ["key","kind","child","scope"]:
        nodes=n.copy();edges=e.copy();ks=keys.copy()
        if field=="key":ks[1]=ks[0]
        if field=="kind":nodes[5]=(9,0,0,0)
        if field=="child":edges[0]=6
        if field=="scope":nodes[2]=(1,1,41,9)
        cases.append(replace(base,name="bad_"+field,nodes=nodes,edges=edges,keys=ks))
    for depth,roots in [(40,64),(128,256)]:
        nodes=[(0,0,0,0),(1,0,1,9),(0,7,0,0)];edges=[]
        child=1
        for _ in range(depth):
            nodes.append((2,len(edges),2,0));edges.extend([child,child]);child=len(nodes)-1
        nodes.append((2,len(edges),roots,0));edges.extend([child]*roots)
        cases.append(Case(f"shared_bundle_{depth}_{roots}",nodes,edges,[(1,9)],[2],len(nodes)-1,
            output=depth+4,out_edges=2*depth+roots,stack=depth+4,budget=1000000))
    return cases

def random_case(rng,index):
    nv=rng.randrange(7);keys=[(rng.randrange(9),i+1) for i in range(nv)]
    nodes=[(0,0,0,0)]+[(1,i,*key) for i,key in enumerate(keys)]
    nodes.extend((0,rng.randrange(8),0,0) for _ in range(rng.randrange(1,6)))
    edges=[]
    for _ in range(rng.randrange(2,24)):
        if nv and rng.randrange(6)==0:
            slot=rng.randrange(nv);nodes.append((1,slot,*keys[slot]))
        else:
            children=[rng.randrange(1,len(nodes)) for _ in range(rng.randrange(5))]
            nodes.append((2,len(edges),len(children),0));edges+=children
    constants=[i for i,node in enumerate(nodes) if i and node[0]==0]
    bindings=[rng.choice([0,0,*constants,*range(i+2,nv+1)]) for i in range(nv)]
    if nv and index%17==0:bindings[0]=1
    return Case("random_"+str(index),nodes,edges,keys,bindings,rng.randrange(1,len(nodes)))

def fixtures(cases,path):
    lines=[];runs=0
    for c in cases:
        outcome,payload,nodes,edges=oracle(c)
        if c.expected is not None:outcome,payload=c.expected;nodes=edges=None
        sizes=[32*len(c.nodes),8*len(c.edges),8*len(c.bindings),40*c.output,8*c.out_edges,
            8*(c.memo if c.memo is not None else len(c.nodes)+len(c.bindings)),32*c.stack,320,16*len(c.keys)]
        wants=[]
        if outcome=="FIRED":
            wants.extend(f"want 3 {8*i} {x}" for i,x in enumerate(x for n in nodes for x in n))
            wants.extend(f"want 4 {8*i} {x}" for i,x in enumerate(edges))
            wants.extend([f"want 7 {4*8} {len(nodes)}",f"want 7 {6*8} {len(edges)}"])
        wants.extend(f"want 2 {8*i} {x}" for i,x in enumerate(c.bindings))
        chunks=[wants[i:i+64] for i in range(0,len(wants),64)] or [[]]
        for part,wanted in enumerate(chunks):
            lines.append("case "+c.name+"_chunk"+str(part));runs+=1
            lines.extend(f"region {r} {size}" for r,size in enumerate(sizes))
            for r,words in [(0,[x for n in c.nodes for x in n]),(1,c.edges),(2,c.bindings),
                             (8,[x for k in c.keys for x in k])]:
                if words:lines.append(f"words {r} 0 "+" ".join(map(str,words)))
            lines.extend(["fill 3 165","fill 4 90","fill 5 195","fill 6 85","fill 7 170",
                f"arg0 {c.root}",f"arg1 {c.output}",f"budget {c.budget}",f"used {c.used}",f"expect {outcome} {payload}"])
            lines.extend(wanted);lines.append("run")
    path.write_text("\n".join(lines)+"\n");return runs

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--work",type=Path,required=True)
    p.add_argument("--cases",type=int,default=800);p.add_argument("--cetta",default="cetta");args=p.parse_args()
    here=Path(__file__).resolve().parent;repo=here.parents[1];work=args.work.resolve();work.mkdir(parents=True,exist_ok=False)
    runner=repo/"native/p1/build/p1run"
    def run(label,command,status=0):
        result=subprocess.run(list(map(str,command)),cwd=repo,text=True,capture_output=True)
        (work/(label+".log")).write_text(result.stdout+result.stderr)
        assert result.returncode==status,(label,result.returncode,(result.stdout+result.stderr)[-5000:])
        return result.stdout+result.stderr
    program=work/"emit_prog.txt";run("author",[args.cetta,"--no-results",here/"emit.metta",program,work/"notes"])
    rng=random.Random(29);cases=fixed()+[random_case(rng,i) for i in range(args.cases)]
    runs=fixtures(cases,work/"cases.p1t")
    result=run("native",[runner,"--image",work/"native_emit.hol","native_emit",program,work/"cases.p1t"])
    assert f"{runs} SAME, 0 DIFF" in result and "0 missed" in result.splitlines()[-1]
    costs={name.removesuffix("_chunk0"):(out,int(pay),int(cost)) for name,out,pay,cost in re.findall(
        r"(?m)^([^/\n]+)/1 SAME expected (\w+) ret=(\d+) used=(\d+)$",result) if name.endswith("_chunk0")}
    boundaries=[]
    for c in cases[:120]:
        outcome,payload,cost=costs[c.name]
        if not cost or c.expected is not None:continue
        for delta in [-1,0,1]:
            expected=("EXHAUSTED",4) if delta<0 else (outcome,payload)
            boundaries.append(replace(c,name=c.name+"_fuel_"+str(delta),budget=cost+delta,expected=expected if delta<0 else None))
        boundaries.append(replace(c,name=c.name+"_prepaid",used=7,budget=cost+7))
    fixtures(boundaries,work/"fuel.p1t");run("fuel",[runner,"--quiet",program,work/"fuel.p1t"])
    source=(here/"emit.metta").read_text()
    mutations={
        "scope_check":("(ne:get v3 c) (Cmp Eq v8 v2 v3)","(ne:get v3 c) (Imm TyBool v8 1)"),
        "scope_output":("(ne:get v1 c) (Store output v4 24 v1)","(Imm v1 0) (Store output v4 24 v1)"),
        "binding_lookup":("(Load bindings v1 v4 0)","(Imm v1 0)"),
        "child_offset":("(Load edges v1 v4 0)","(Load edges v1 v4 8)"),
        "node_capacity":("(Cmp Ult v7 v2 v3)\n     (If v7 Skip (ne:fail 4))",
            "(Cmp Ule v7 v2 v3)\n     (If v7 Skip (ne:fail 4))"),
        "source_origin":("(ne:get v1 source) (Store output v4 32 v1)","(Imm v1 0) (Store output v4 32 v1)"),
        "variable_sharing":("(Addr memo v4 v4 v3 3 0) (Load memo v1 v4 0)",
            "(Addr memo v4 v4 v3 3 0) (Imm v1 0)"),
        "fuel_charge":("(TFund 1)","Skip"),
    }
    controls=[c for c in fixed() if not c.name.startswith("shared_bundle")]
    fixtures(controls,work/"controls.p1t");caught={}
    for name,(old,new) in mutations.items():
        assert source.count(old)==1,(name,source.count(old))
        text=source.replace(old,new).replace('"../p1/p1.metta"',json.dumps(str(repo/"native/p1/p1.metta")))
        text=text.replace('"emit_layout.metta"',json.dumps(str(here/"emit_layout.metta")))
        authored=work/(name+".metta");authored.write_text(text);mutant=work/(name+".txt")
        run(name+"-author",[args.cetta,"--no-results",authored,mutant,work/(name+".notes")])
        caught[name]=run(name,[runner,"--quiet",mutant,work/"controls.p1t"],1).splitlines()[-1]
    encoded=re.findall(r'"([0-9a-f]{64,})"',(work/"native_emit.hol").read_text());assert len(encoded)==1
    image=bytes.fromhex(encoded[0])
    (work/"result.json").write_text(json.dumps({"passed":True,"cases":len(cases),"runs":runs,"fuel_cases":len(boundaries),
        "image_bytes":len(image),"image_sha256":hashlib.sha256(image).hexdigest(),"mutations_caught":caught,
        "native":result.splitlines()[-1],"sharing":{k:v for k,v in costs.items() if k.startswith("shared_bundle")},
        "scope":"scoped first-order graph instantiation; executable comparison, no machine-code proof"},indent=2)+"\n")
    print(result.splitlines()[-1]);print(f"native output: {len(image)} bytes; {len(boundaries)} fuel boundaries")

if __name__=="__main__":main()
