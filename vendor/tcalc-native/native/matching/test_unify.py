#!/usr/bin/env python3
"""Native/reference comparisons and independent recursive unification checks."""
from __future__ import annotations

import argparse
from dataclasses import dataclass, replace
import hashlib
import json
from pathlib import Path
import random
import re
import subprocess


@dataclass
class Case:
    name: str
    nodes: list[tuple[int, int, int, int]]
    edges: list[int]
    keys: list[tuple[int, int]]
    seed: list[int]
    left: int
    right: int
    pairs: int = 256
    scans: int = 256
    memo: int = 128
    result_words: int | None = None
    mark_words: int | None = None
    budget: int = 1_000_000
    used: int = 0
    expect: tuple[str, int] | None = None
    wanted: list[int] | None = None


class Mismatch(Exception):
    pass


def oracle(case: Case) -> tuple[str, int, list[int] | None]:
    """Robinson unification by recursion; no native phases or work buffers."""
    n = len(case.seed)
    mark_words = n+len(case.nodes)+case.memo if case.mark_words is None else case.mark_words
    if (case.result_words is not None and case.result_words < n or
            mark_words <= n+len(case.nodes)):
        return "UNSUPPORTED", 6, None
    slots = mark_words-n-len(case.nodes)
    if slots & (slots-1):
        return "UNSUPPORTED", 1, None
    if len(case.keys) != n:
        return "UNSUPPORTED", 4, None
    if not (0 < case.left < len(case.nodes) and 0 < case.right < len(case.nodes)):
        return "UNSUPPORTED", 2, None
    for i, value in enumerate(case.seed):
        if value and not 0 < value < len(case.nodes):
            return "UNSUPPORTED", 5, None
        if i and (case.keys[i-1][1], case.keys[i-1][0]) >= (case.keys[i][1], case.keys[i][0]):
            return "UNSUPPORTED", 4, None
    for i, (kind, a, b, c) in enumerate(case.nodes[1:], 1):
        if kind == 0:
            if b or c:
                return "UNSUPPORTED", 3, None
        elif kind == 1:
            if a >= n or case.keys[a] != (b, c):
                return "UNSUPPORTED", 4, None
        elif kind == 2:
            if c or a > len(case.edges) or b > len(case.edges) or a+b > len(case.edges):
                return "UNSUPPORTED", 3, None
            if any(not 0 < child < i for child in case.edges[a:a+b]):
                return "UNSUPPORTED", 3, None
        else:
            return "UNSUPPORTED", 3, None
    env = case.seed.copy()
    active: set[int] = set()
    finished: set[int] = set()
    visited_nodes: set[int] = set()

    def check_variable(slot: int) -> None:
        if slot in active:
            raise Mismatch
        if slot in finished or not env[slot]:
            return
        active.add(slot)
        visit(env[slot])
        active.remove(slot)
        finished.add(slot)

    def visit(node: int) -> None:
        if node in visited_nodes:
            return
        kind, a, b, _ = case.nodes[node]
        if kind == 1:
            check_variable(a)
        elif kind == 2:
            for child in case.edges[a:a+b]:
                visit(child)
        visited_nodes.add(node)

    try:
        for slot in range(n):
            check_variable(slot)
    except Mismatch:
        return "UNSUPPORTED", 5, None

    def deref(node: int) -> int:
        kind, a, _, _ = case.nodes[node]
        return deref(env[a]) if kind == 1 and env[a] else node

    def occurs(slot: int, node: int, seen: set[tuple[str,int]]) -> bool:
        kind, a, b, _ = case.nodes[node]
        if kind == 1 and a == slot:
            return True
        if ("node",node) in seen:
            return False
        seen.add(("node",node))
        if kind == 1:
            if ("variable",a) in seen or not env[a]:
                return False
            seen.add(("variable",a))
            return occurs(slot, env[a], seen)
        return kind == 2 and any(occurs(slot, child, seen) for child in case.edges[a:a+b])

    changes = 0
    scheduled: set[tuple[int,int]] = set()

    def unify(left: int, right: int) -> None:
        nonlocal changes
        left, right = deref(left), deref(right)
        lk, la, lb, _ = case.nodes[left]
        rk, ra, rb, _ = case.nodes[right]
        if left == right or lk == rk == 1 and la == ra:
            return
        if lk == 1 or rk == 1:
            slot, value = (la, right) if lk == 1 else (ra, left)
            if occurs(slot, value, set()):
                raise Mismatch
            env[slot] = value
            changes += 1
        elif lk != rk or lk == 0 and la != ra or lk == 2 and lb != rb:
            raise Mismatch
        elif lk == 2:
            if (left,right) in scheduled:
                return
            scheduled.add((left,right))
            for a, b in zip(case.edges[la:la+lb], case.edges[ra:ra+rb]):
                unify(a, b)

    try:
        unify(case.left, case.right)
        return "FIRED", changes, env
    except Mismatch:
        return "REFUSED", 0, None


def fixed_cases() -> list[Case]:
    atom = [(0,0,0,0), (0,10,0,0), (0,20,0,0), (0,10,0,0)]
    var = [(0,0,0,0), (1,0,42,7), (0,10,0,0), (2,0,1,0)]
    two = [(0,0,0,0), (1,0,42,7), (1,1,42,8), (0,10,0,0),
        (2,0,2,0), (2,2,2,0), (1,0,42,7)]
    cases = [
        Case("same_constant",atom,[],[],[],1,1),
        Case("distinct_constant",atom,[],[],[],1,2),
        Case("same_constant_distinct_id",atom,[],[],[],1,3),
        Case("bind_left",var,[1],[(42,7)],[0],1,2),
        Case("bind_right",var,[1],[(42,7)],[0],2,1),
        Case("occurs_beneath_tuple",var,[1],[(42,7)],[0],1,3),
        Case("different_scopes",two,[1,2,3,3],[(42,7),(42,8)],[0,0],4,5),
        Case("same_variable_distinct_id",two,[1,2,3,3],[(42,7),(42,8)],[0,0],1,6),
        Case("seed_chain",two,[1,2,3,3],[(42,7),(42,8)],[2,3],1,3),
        Case("seed_cycle",two,[1,2,3,3],[(42,7),(42,8)],[2,1],1,3),
        Case("seed_structural_cycle",var,[1],[(42,7)],[3],2,2),
        Case("wrong_node_scope",[(0,0,0,0),(1,0,42,8)],[],[(42,7)],[0],1,1),
        Case("duplicate_key",two,[1,2,3,3],[(42,7),(42,7)],[0,0],1,3),
        Case("keys_wrong_order",two,[1,2,3,3],[(42,8),(42,7)],[0,0],1,3),
        Case("invalid_root",atom,[],[],[],0,1),
        Case("invalid_seed",var,[1],[(42,7)],[999],1,2),
        Case("cyclic_node",[(0,0,0,0),(2,0,1,0)],[1],[],[],1,1),
        Case("unknown_node_kind",[(0,0,0,0),(99,0,0,0)],[],[],[],1,1),
        Case("constant_unused_fields",[(0,0,0,0),(0,10,1,0)],[],[],[],1,1),
        Case("overflowing_child_range",[(0,0,0,0),(2,2**64-1,1,0)],[],[],[],1,1),
        Case("result_capacity",var,[1],[(42,7)],[0],1,2,result_words=0),
        Case("mark_capacity",var,[1],[(42,7)],[0],1,2,mark_words=0),
        Case("pair_capacity",two,[1,2,3,3],[(42,7),(42,8)],[0,0],4,5,pairs=1,
            expect=("UNSUPPORTED",7)),
        Case("scan_capacity",two,[1,2,3,3],[(42,7),(42,8)],[0,0],1,5,scans=1,
            expect=("UNSUPPORTED",8)),
        Case("invalid_receipt",var,[1],[(42,7)],[0],1,2,budget=0,used=1,
            expect=("INVALID",3)),
    ]
    for token in [0, 2**32-1, 2**32, 2**63, 2**64-1]:
        cases.append(Case("word_"+str(token),[(0,0,0,0),(0,token,0,0),(0,token,0,0)],[],[],[],1,2))
    shared = [(0,0,0,0),(1,0,42,7),(0,10,0,0)]
    edges: list[int] = []
    for _ in range(40):
        last = len(shared)-1
        shared.append((2,len(edges),2,0))
        edges += [last,last]
    cases.append(Case("shared_dag_occurs",shared,edges,[(42,7)],[0],1,len(shared)-1,budget=20000))
    paired = [(0,0,0,0),(1,0,42,7),(1,1,42,8)]
    pe: list[int] = []
    left,right = 1,2
    for _ in range(40):
        paired.append((2,len(pe),2,0));pe += [left,left];left=len(paired)-1
        paired.append((2,len(pe),2,0));pe += [right,right];right=len(paired)-1
    cases.append(Case("shared_dag_pairs",paired,pe,[(42,7),(42,8)],[0,0],left,right,budget=20000))
    cases.append(replace(cases[-1],name="memo_capacity",memo=1,expect=("UNSUPPORTED",10)))
    cases.append(replace(cases[3],name="memo_non_power",memo=3,expect=("UNSUPPORTED",1)))
    return cases


def random_case(rng: random.Random, number: int) -> Case:
    nv = rng.randrange(7)
    keys = sorted([(rng.randrange(8),i+1) for i in range(nv)], key=lambda k:(k[1],k[0]))
    nodes = [(0,0,0,0)] + [(1,i,*key) for i,key in enumerate(keys)]
    nodes += [(0,rng.randrange(8),0,0) for _ in range(rng.randrange(1,6))]
    edges: list[int] = []
    for _ in range(rng.randrange(2,18)):
        if nv and rng.randrange(5) == 0:
            i = rng.randrange(nv)
            nodes.append((1,i,*keys[i]))
        else:
            children = [rng.randrange(1,len(nodes)) for _ in range(rng.randrange(5))]
            nodes.append((2,len(edges),len(children),0))
            edges += children
    constants = [i for i,row in enumerate(nodes) if i and row[0] == 0]
    seed = [rng.choice([0,0,0,*constants,*range(i+2,nv+1)]) for i in range(nv)]
    left, right = rng.randrange(1,len(nodes)), rng.randrange(1,len(nodes))
    if number % 4 == 0:
        right = left
    if number % 4 == 1 and nv:
        left = rng.randrange(1,nv+1)
    return Case(f"random_{number}",nodes,edges,keys,seed,left,right)


def fixture(cases: list[Case], path: Path) -> dict[str,tuple[str,int,list[int]|None]]:
    lines: list[str] = []
    wanted = {}
    for case in cases:
        out,pay,result = oracle(case) if case.expect is None else (*case.expect,case.wanted)
        wanted[case.name] = out,pay,result
        sizes = [32*len(case.nodes),8*len(case.edges),8*len(case.seed),
            8*(len(case.seed) if case.result_words is None else case.result_words),16*case.pairs,
            8*case.scans,16*len(case.keys),320,
            8*(len(case.seed)+len(case.nodes)+case.memo if case.mark_words is None else case.mark_words)]
        lines.append("case "+case.name)
        lines += [f"region {r} {n}" for r,n in enumerate(sizes)]
        for r,words in [(0,[x for row in case.nodes for x in row]),(1,case.edges),
                (2,case.seed),(6,[x for row in case.keys for x in row])]:
            if words:
                lines.append(f"words {r} 0 "+" ".join(map(str,words)))
        lines += ["fill 3 165","fill 4 90","fill 5 195","fill 7 85","fill 8 170",
            f"arg0 {case.left}",f"arg1 {case.right}",f"budget {case.budget}",f"used {case.used}",
            f"expect {out} {pay}"]
        if out == "FIRED" and result is not None:
            lines += [f"want 3 {8*i} {x}" for i,x in enumerate(result)]
        lines += [f"want 2 {8*i} {x}" for i,x in enumerate(case.seed)]
        lines.append("run")
    path.write_text("\n".join(lines)+"\n")
    return wanted


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work",type=Path,required=True)
    parser.add_argument("--cases",type=int,default=1200)
    parser.add_argument("--seed",type=int,default=17)
    parser.add_argument("--cetta",default="cetta")
    parser.add_argument("--p1run",type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[1]
    work = args.work.resolve()
    work.mkdir(parents=True,exist_ok=False)
    runner = (args.p1run or repo/"native/p1/build/p1run").resolve()

    def run(label: str, command: list[str|Path], status: int = 0) -> str:
        result = subprocess.run(list(map(str,command)),cwd=repo,capture_output=True,text=True)
        output = result.stdout+result.stderr
        (work/(label+".log")).write_text(output)
        assert result.returncode == status,(label,result.returncode,output[-3000:])
        return output

    program,notes = work/"unify_prog.txt",work/"unify.notes"
    run("author",[args.cetta,"--no-results",here/"unify.metta",program,notes])
    assert program.read_bytes()==(here/"native_unify_prog.txt").read_bytes(), "stale packaged program"
    rng = random.Random(args.seed)
    cases = fixed_cases()+[random_case(rng,i) for i in range(args.cases)]
    fixture(cases,work/"cases.p1t")
    baseline = run("native",[runner,"--image",work/"native_unify.hol","native_unify",program,work/"cases.p1t"])
    assert f"{len(cases)} SAME, 0 DIFF" in baseline and "0 missed" in baseline.splitlines()[-1]
    run("hoisted",[runner,"--quiet","--hoist",program,work/"cases.p1t"])
    actual = {name:(out,int(pay),int(used)) for name,out,pay,used in re.findall(
        r"(?m)^([^/\n]+)/1 SAME expected (\w+) ret=(\d+) used=(\d+)$",baseline)}
    assert len(actual) == len(cases)
    for case in cases:
        if case.name.startswith("shared_dag_"):
            assert actual[case.name][2] <= 256+100*len(case.nodes), (case.name,actual[case.name])
    boundaries = []
    for case in cases[:150]:
        out,pay,cost = actual[case.name]
        if case.used or not cost or cost >= case.budget:
            continue
        _,_,result = oracle(case) if case.expect is None else (*case.expect,case.wanted)
        for delta in [-1,0,1]:
            budget = cost+delta
            expected = ("EXHAUSTED",4) if delta < 0 else (out,pay)
            boundaries.append(replace(case,name=case.name+"_fuel_"+str(delta),budget=budget,
                expect=expected,wanted=result))
        boundaries.append(replace(case,name=case.name+"_prepaid",budget=cost+7,used=7,
            expect=(out,pay),wanted=result))
    fixture(boundaries,work/"fuel.p1t")
    fuels = run("fuel",[runner,program,work/"fuel.p1t"])
    paid = {name:int(used) for name,used in re.findall(
        r"(?m)^([^/\n]+)/1 SAME expected \w+ ret=\d+ used=(\d+)$",fuels)}
    for case in boundaries:
        original = re.sub(r"_fuel_-?\d+$|_prepaid$","",case.name)
        expected = actual[original][2]+case.used
        if case.expect == ("EXHAUSTED",4): expected=case.budget
        assert paid[case.name] == expected,(case.name,paid[case.name],expected)
    source = (here/"unify.metta").read_text()
    mutations = {
        "scope_check":("(Cmp Eq v8 v2 v6)","(Imm TyBool v8 1)"),
        "occurs_check":("(Cmp Eq v7 v3 v1)","(Imm TyBool v7 0)"),
        "repeated_variable":("(Cmp Eq v8 v3 v4)","(Imm TyBool v8 0)"),
        "constant_equality":("(nm:get v1 left-a) (nm:get v2 right-a) (Cmp Eq v7 v1 v2)",
            "(nm:get v1 left-a) (nm:get v2 right-a) (Cmp Ne v7 v1 v2)"),
        "tuple_arity":("(nm:get v1 left-b) (nm:get v2 right-b) (Cmp Eq v7 v1 v2)",
            "(nm:get v1 left-b) (nm:get v2 right-b) (Cmp Ne v7 v1 v2)"),
        "child_offset":("(Addr edges v4 v4 v2 3 0) (Load edges v2 v4 0)",
            "(Addr edges v4 v4 v2 3 0) (Load edges v2 v4 8)"),
        "pair_capacity":("(Cmp Ult v5 v3 v4)","(Cmp Ule v5 v3 v4)"),
        "fuel_charge":("(TFund 1)","Skip"),
    }
    caught = {}
    fixed = fixed_cases()+[replace(fixed_cases()[3],name="no_fuel",budget=0,expect=("EXHAUSTED",4))]
    fixture(fixed,work/"controls.p1t")
    for name,(old,new) in mutations.items():
        assert source.count(old)==1,(name,source.count(old))
        text = source.replace(old,new).replace('"../p1/p1.metta"',json.dumps(str(repo/"native/p1/p1.metta")))
        text = text.replace('"layout.metta"',json.dumps(str(here/"layout.metta")))
        authored = work/(name+".metta")
        authored.write_text(text)
        mutant = work/(name+".txt")
        run(name+"-author",[args.cetta,"--no-results",authored,mutant,work/(name+".notes")])
        caught[name] = run(name,[runner,"--quiet",mutant,work/"controls.p1t"],1).splitlines()[-1]
    encoded = re.findall(r'"([0-9a-f]{64,})"',(work/"native_unify.hol").read_text())
    assert len(encoded)==1
    image = bytes.fromhex(encoded[0])
    packaged=re.findall(r'"([0-9a-f]{64,})"',(here/"native_unify.hol").read_text())
    assert len(packaged)==1 and bytes.fromhex(packaged[0])==image, "stale packaged image"
    result = {"passed":True,"cases":len(cases),"fuel_boundaries":len(boundaries),
        "native":baseline.splitlines()[-1],"fuel":fuels.splitlines()[-1],"mutations_caught":caught,
        "image_bytes":len(image),"image_sha256":hashlib.sha256(image).hexdigest(),
        "inputs":{p.name:hashlib.sha256(p.read_bytes()).hexdigest()
            for p in [here/"unify.metta",here/"layout.metta",Path(__file__).resolve()]},
        "scope":"first-order unification component; native/reference testing, not formal verification or complete tcalc/MeTTa"}
    (work/"result.json").write_text(json.dumps(result,indent=2)+"\n")
    print(result["native"])
    print(result["fuel"])
    print(f"native unifier: {len(image)} bytes; {len(caught)} mutations caught")


if __name__ == "__main__":
    main()
