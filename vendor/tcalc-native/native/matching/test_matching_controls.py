#!/usr/bin/env python3
"""Check semantic separating controls for contextual import and native queries."""
import argparse
import json
from pathlib import Path
import shlex
import signal
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build",type=Path,required=True)
parser.add_argument("--work",type=Path,required=True)
parser.add_argument("--cc",default="cc")
args=parser.parse_args()
here=Path(__file__).resolve().parent
build=args.build.resolve();work=args.work.resolve();work.mkdir(parents=True,exist_ok=False)
compiler=shlex.split(args.cc)
options=["-O2","-std=c11","-Wall","-Wextra","-Werror","-UNDEBUG",
         "-I",str(here),"-I",str(here/"../p1"),"-I",str(here/"../../tisa")]
mutations=[
    ("scope","native_match_view.c","test_view.c",
        "if (!variable_slot(b,node.variable,b->frames[top].value,&native.a))",
        "node.variable.scope=0; if (!variable_slot(b,node.variable,b->frames[top].value,&native.a))"),
    ("context","native_match_view.c","test_view.c",
        "g->origins[g->node_count]=origin;","g->origins[g->node_count]=(NmHostValue){origin.syntax,0};"),
    ("binding","native_match_view.c","test_view.c","if (bound) {","if (false) {"),
    ("open-as-entailed","native_match.c","test_view.c",
        "return result->payload ? NM_TRUTH_OPEN : NM_TRUTH_ENTAILED;",
        "return result->payload ? NM_TRUTH_ENTAILED : NM_TRUTH_OPEN;"),
    ("snapshot-parent","native_match_view.c","test_solution.c",
        "for (const NmSolution *at=s;at;at=at->parent) {",
        "for (const NmSolution *at=s;at;at=NULL) {"),
    ("snapshot-scope","native_match_view.c","test_solution.c",
        "if (a.scope!=b.scope) return a.scope<b.scope ? -1 : 1;",
        "if (false) return 0;"),
    ("lost-zero-value","native_match_view.c","test_solution.c",
        "if (at->entries[i].changed &&",
        "if (at->entries[i].changed && at->entries[i].value.syntax &&"),
    ("binding-count","native_match_view.c","test_solution.c",
        "if (s->changes!=result->payload) { free(s);return NM_VIEW_FAULT; }",
        "if (false) { free(s);return NM_VIEW_FAULT; }"),
    ("fuel-as-end","native_query.c","test_query.c",
        "if (result.outcome==NM_EXHAUSTED) return terminal(q,NM_QUERY_EXHAUSTED);",
        "if (result.outcome==NM_EXHAUSTED) return terminal(q,NM_QUERY_END);"),
    ("ask-order","native_admission.c","test_admission.c",
        "if (a->ask==NM_ASK_BEFORE_TELLS)","if (a->ask==NM_ASK_AFTER_TELLS)"),
    ("assumed-open-guard","native_admission.c","test_admission.c",
        "if (a->receipt.truth==NM_TRUTH_ENTAILED)",
        "if (a->receipt.truth==NM_TRUTH_ENTAILED || a->receipt.truth==NM_TRUTH_OPEN)"),
    ("activation-fuel","native_admission.c","test_admission.c",
        "++a->receipt.activations;++a->receipt.fuel_used;","++a->receipt.activations;"),
    ("early-publication","native_admission.c","test_admission.c",
        "a && a->status==NM_ADMITTED &&", "a && (a->status==NM_ADMITTED || a->status==NM_ADMISSION_YIELD) &&"),
    ("unfinished-region","native_census.c","test_census.c",
        "if (!o) return NM_REGION_UNAVAILABLE;","if (!o) return NM_REGION_ENABLED;"),
    ("possible-as-certain","native_census.c","test_census.c",
        "if (truth==NM_TRUTH_ENTAILED) c->certain",
        "if (truth==NM_TRUTH_ENTAILED || truth==NM_TRUTH_OPEN) c->certain"),
    ("empty-without-validation","native_census.c","test_census.c",
        "if (!valid(c)) return terminal(c,NM_CENSUS_INVALIDATED);",
        "if (!valid(c)) return terminal(c,NM_CENSUS_INVALIDATED);\n    if (!c->count) return terminal(c,NM_CENSUS_DONE);"),
    ("take-read-branch","native_selection.S","test_selection.c","    je 6f","    jne 6f"),
    ("carry-branch","native_selection.S","test_selection.c","    jb 3f","    ja 3f"),
]
results={}
for name,source,test,old,new in mutations:
    text=(here/source).read_text();assert text.count(old)==1,(name,"mutation anchor")
    directory=work/name;directory.mkdir()
    mutated=directory/source;mutated.write_text(text.replace(old,new))
    obj=directory/"mutated.o";exe=directory/"control"
    for label,command in [
        ("compile",compiler+options+["-c",str(mutated),"-o",str(obj)]),
        ("link",compiler+options+[str(here/test),str(obj),str(build/"libnative_match.a"),"-o",str(exe)])]:
        p=subprocess.run(command,cwd=here,text=True,capture_output=True)
        (directory/(label+".log")).write_text(p.stdout+p.stderr)
        assert p.returncode==0,(name,label,p.stdout,p.stderr)
    p=subprocess.run([str(exe)],cwd=here,text=True,capture_output=True)
    (directory/"result.log").write_text(p.stdout+p.stderr)
    assert p.returncode==-signal.SIGABRT and "Assertion" in p.stderr,(name,p.returncode,p.stdout,p.stderr)
    results[name]={"caught":True,"returncode":p.returncode}
(work/"result.json").write_text(json.dumps({"passed":True,"controls":results},indent=2)+"\n")
print(f"native matching controls: {len(results)} semantic mutations caught by separating specimens")
