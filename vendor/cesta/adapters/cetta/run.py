"""Qualify generated field audits in an owned, pinned CeTTa source capture."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import re
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
PIN = '9836290fdbbd4a0e4880ef30fdc8755fcb89ec6f'
PATHS = ['Makefile','VERSION','LICENSE','src','lib','tools','scripts','mk','make',
         'include','native','vendor','rust','python','specs','langdef','examples',
         'experiments/gslt2parse_foundation','tests/support','tests/prime',
         'benchmarks/chaining_crossengine/13_nars_tuffy']
NARS = 'benchmarks/chaining_crossengine/13_nars_tuffy/nars_tuffy_step10.metta'
ATP = 'tests/prime/practical/atp_superposition_replay.metta'
MARKER = '''static bool prime_eval_stack_audit_call_hub(
    PrimeNeedArenaAudit *eval_audit, PrimeNeedArenaAudit *survivor_audit,
    const PrimeNeedCallSources *hub) {'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run(command,cwd,log,*,environment=None,accepted=True,timeout=None,streamed=False):
    log.write_bytes(b'')
    if streamed:
        with log.open('wb') as output:
            result=subprocess.run(list(map(str,command)),cwd=cwd,env=environment,
                stdout=output,stderr=subprocess.STDOUT,timeout=timeout)
        result.stdout=b'';result.stderr=b''
    else:
        result = subprocess.run(list(map(str,command)),cwd=cwd,env=environment,
                                capture_output=True,timeout=timeout)
        log.write_bytes(result.stdout+result.stderr)
    if (result.returncode==0) != accepted:
        raise RuntimeError(f'{command!r}: exit {result.returncode}; {log}\n'+
                           log.read_bytes()[-3000:].decode(errors='replace'))
    return result


def capture(repo,work):
    tree=work/'cetta-capture';state=tree/'capture.json'
    if tree.exists():
        metadata=json.loads(state.read_text())
        if metadata['pin']!=PIN or metadata['kind']!='qualification source capture; no git worktree':
            raise RuntimeError('refusing an unrecognized source capture')
    else:
        tree.mkdir()
    missing=[path for path in PATHS if not (tree/path).exists()]
    if missing:
        process=subprocess.Popen(['git','-C',str(repo),'archive',PIN,*missing],stdout=subprocess.PIPE)
        with tarfile.open(fileobj=process.stdout,mode='r|') as archive:
            archive.extractall(tree,filter='data')
        if process.wait():raise RuntimeError('pinned source capture failed')
    state.write_text(json.dumps({'pin':PIN,'paths':PATHS,
        'kind':'qualification source capture; no git worktree'},indent=2)+'\n')
    return tree


def original(repo,path):
    return subprocess.run(['git','-C',str(repo),'show',PIN+':'+path],
                          capture_output=True,check=True).stdout


def patch_source(source):
    if source.count(MARKER)!=1:raise RuntimeError('native audit definition changed')
    start=source.index(MARKER)
    finish=source.index('\nstatic bool prime_eval_stack_audit_call_sources(',start)
    native=source[start:finish]
    begin=native.index('    for (const PrimeNeedCallSourceCell *cell = hub->cells;')
    end=native.rindex('    return true;')
    replacement='''    CestaCallSourceAudit audit = {eval_audit, survivor_audit};
    for (const PrimeNeedCallSourceCell *cell = hub->cells; cell; cell = cell->next)
        if (!cesta_call_source_audit(&audit, cell)) return false;
'''
    changed=native[:begin]+replacement+native[end:]
    adapter=(HERE/'call_source_adapter.inc').read_text()
    controls=(HERE/'call_source_controls.inc').read_text()
    source=source[:start]+adapter+'\n'+changed+source[finish:]+'\n'+controls
    marker='static bool prime_eval_stack_collect(\n    PrimeEvalStackDriver *driver) {'
    if source.count(marker)!=1:raise RuntimeError('stack collector changed')
    start=source.index(marker)
    effect=source.index('    Arena evacuated;',start)
    gate='''    CestaFrameGcAudit record_audit = {0};
    for (const PrimeEvalStackFrame *frame = driver->top; frame;
         frame = (const PrimeEvalStackFrame *)frame->continuation.next)
        if (!cesta_frame_gc_gate(&record_audit,frame)) return false;

'''
    source=source[:effect]+gate+source[effect:]
    source=source[:start]+(HERE/'frame_gate_adapter.inc').read_text()+'\n'+source[start:]
    start=source.index('static bool prime_native_control_collectable(')
    end=source.index('\nstatic bool prime_native_control_preserve_worlds(',start)
    native=source[start:end]
    needle='    for (const PrimeEvalStackFrame *frame = control->frames; frame;'
    if native.count(needle)!=1:raise RuntimeError('native frame admission loop changed')
    native=native.replace(needle,'    CestaFrameGcAudit record_audit = {0};\n'+needle,1)
    needle='            frame->control_key || frame->control_bound)\n            return false;'
    if native.count(needle)!=1:raise RuntimeError('native opaque frame gate changed')
    native=native.replace(needle,needle+'\n        if (!cesta_frame_gc_gate(&record_audit,frame)) return false;',1)
    return source[:start]+native+source[end:]+'\n'+(HERE/'frame_gate_controls.inc').read_text()


def fixture(tree,program,path,log,environment):
    result=run([program,'--emit-runtime-stats','--lang','prime',path],tree,log,
               environment=environment,timeout=300)
    expected=('[()]\n[((stv 0.6 0.486) (2 10))]' if path==NARS else
              (tree/path).with_suffix('.expected').read_text().strip())
    if result.stdout.decode().strip()!=expected:
        raise RuntimeError('Prime output changed: '+path)
    counters={}
    for line in (result.stdout+result.stderr).decode().splitlines():
        words=line.split()
        if len(words)==3 and words[0]=='runtime-counter':counters[words[1]]=int(words[2])
    keys=['eval-tail-collection-candidate','eval-tail-blocked-imprecise-root',
          'eval-tail-blocked-external-owner','eval-tail-blocked-live-outcome',
          'prime-eval-stack-gc-frame-safe-point','eval-tail-reclaimed-bytes','eval-tail-collected']
    selected={key:counters.get(key,0) for key in keys}
    if path==NARS:
        if not (selected[keys[4]]>0 and selected[keys[5]]>0 and selected[keys[6]]>0):
            raise RuntimeError('forced collection did not complete')
        if selected[keys[0]]!=sum(selected[key] for key in [*keys[1:4],keys[6]]):
            raise RuntimeError('collection accounting does not balance')
        if selected[keys[3]] or selected[keys[6]]>selected[keys[4]]:
            raise RuntimeError('live-root refusal or inconsistent safe-point accounting')
    return {'output_sha256':digest(result.stdout),'counters':selected}


def declaration_controls(cesta,compiler,flags,tree,evidence,source):
    mirror=evidence/'declaration-control.c';mirror.write_text(source)
    output=evidence/'preserved.h'
    schema=HERE/'frame_gate.records'
    command=[cesta,'record-visitor','--schema',schema,'-o',output,'--cc',compiler,
             '-std=c11',*flags,'-DCESTA_RECORD_DECLARE_ONLY=1',mirror]
    run(command,tree,evidence/'declaration-positive.log');saved=output.read_bytes()
    marker='} PrimeEvalStackFrame;'
    if source.count(marker)!=1:raise RuntimeError('frame declaration changed')
    mirror.write_text(source.replace(marker,'    Atom *cesta_unclassified;\n'+marker,1))
    result=run(command,tree,evidence/'declaration-new-pointer.log',accepted=False)
    if b'unclassified C field cesta_unclassified' not in result.stderr or output.read_bytes()!=saved:
        raise RuntimeError('a new continuation pointer escaped classification or replaced an output')
    mirror.write_text(source)
    bad=evidence/'wrong-owner.records'
    text=(HERE/'call_sources.records').read_text()
    old='(field common (borrow read) (owner episode)'
    if text.count(old)!=1:raise RuntimeError('cell owner contract changed')
    text=text.replace('(owner episode) (owner cell-links)','(owner episode) (owner evaluator) (owner cell-links)',1)
    bad.write_text(text.replace(old,'(field common (borrow read) (owner evaluator)',1))
    command[command.index('--schema')+1]=bad
    result=run(command,tree,evidence/'declaration-wrong-owner.log',accepted=False)
    if b"field owner differs from its visitor's owner contract" not in result.stderr or output.read_bytes()!=saved:
        raise RuntimeError('wrong-owner declaration escaped the native policy contract')
    return 2


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta-repo',type=Path,default=ROOT.parent/'hyperon/CeTTa')
    parser.add_argument('--cesta',type=Path,default=ROOT/'build/cesta')
    parser.add_argument('--compiler',choices=['gcc','clang'],default='gcc')
    parser.add_argument('--sanitizers',action='store_true')
    parser.add_argument('--mutations',action='store_true')
    parser.add_argument('--jobs',type=int,default=1)
    parser.add_argument('--work',type=Path,default=ROOT/'build/cetta-record-visitors')
    args=parser.parse_args();work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    repo=args.cetta_repo.resolve();cesta=args.cesta.resolve();tree=capture(repo,work)
    variant=args.compiler+('-asan' if args.sanitizers else '')
    evidence=work/('cetta-record-'+variant);evidence.mkdir(exist_ok=True)
    tool_bytes=cesta.read_bytes();tool=evidence/('cesta-'+digest(tool_bytes))
    if not tool.exists():tool.write_bytes(tool_bytes);tool.chmod(0o755)
    if tool.read_bytes()!=tool_bytes:raise RuntimeError('compiler snapshot identity changed')
    cesta=tool
    asp_path=tree/'native/asp/asp_cdnl_v1.c';asp=original(repo,'native/asp/asp_cdnl_v1.c')
    old=b'for (uint32_t round = 0u; ok; round++) {'
    if asp.count(old)!=1:raise RuntimeError('ASP enumeration loop changed')
    fixed=asp.replace(old,b'while (ok) {',1)
    if asp_path.read_bytes() not in (asp,fixed):raise RuntimeError('unrecognized ASP capture edit')
    tensor_path=tree/'src/tensor_dense_v1.c';tensor=original(repo,'src/tensor_dense_v1.c')
    tensor_fixed,count=re.subn(rb'if \(\((operation == DENSE_TENSOR_OP_[A-Za-z0-9_]+)\)\)',rb'if (\1)',tensor)
    if count!=13:raise RuntimeError('tensor comparison sites changed')
    if tensor_path.read_bytes() not in (tensor,tensor_fixed):raise RuntimeError('unrecognized tensor capture edit')
    interaction_path=tree/'src/interaction_core.c';interaction=original(repo,'src/interaction_core.c')
    checked_hooks=['ia_commit_is_current','ia_commit_is_formed','ia_commit_is_linear',
                   'ia_commit_is_permitted','ia_commit_was_taken','ia_commit_was_made',
                   'ia_commit_epoch_is_open']
    interaction_fixed=interaction
    for name in checked_hooks:
        pattern=rb'static inline bool '+name.encode()+rb'\(IaCommitRun \*c\) \{[^}]*\}\n'
        interaction_fixed,count=re.subn(pattern,
            lambda m:b'#if defined(ia_commit_run_CHECKED) && ia_commit_run_CHECKED\n'+m[0]+b'#endif\n',interaction_fixed)
        if count!=1:raise RuntimeError('checked protocol hook changed: '+name)
    if interaction_path.read_bytes() not in (interaction,interaction_fixed):raise RuntimeError('unrecognized interaction capture edit')
    emitter_path=tree/'native/gslt_native_direct_v1.c';emitter=original(repo,'native/gslt_native_direct_v1.c')
    start=emitter.index(b'static bool emit_equal_test(')
    end=emitter.index(b'/* \xe2\x94\x80\xe2\x94\x80 tail calls',start)
    condition=emitter[start:end]
    atom=b'nb_printf(e, "(%s->op == %ldu)", val, op_find(r->ops, s, 0u) + NATIVE_FIRST_OP_V1);'
    compound=b'nb_printf(e, "(%s->op == %ldu", val, id + NATIVE_FIRST_OP_V1);'
    close=b'    nb_puts(e, ")");\n    return true;\n}'
    if any(condition.count(x)!=1 for x in [atom,compound,close]):raise RuntimeError('native equality condition emitter changed')
    condition=condition.replace(atom,atom.replace(b'"(%s->op == %ldu)"',b'"%s->op == %ldu"'),1)
    condition=condition.replace(compound,b'nb_printf(e, arity ? "(%s->op == %ldu" : "%s->op == %ldu", val, id + NATIVE_FIRST_OP_V1);',1)
    condition=condition.replace(close,b'    if (arity) nb_puts(e, ")");\n    return true;\n}',1)
    emitter_fixed=emitter[:start]+condition+emitter[end:]
    wrapper=b'nb_printf(&o, "static %s", group_of[i] >= 0 ? "inline " : "");'
    if emitter_fixed.count(wrapper)!=1:raise RuntimeError('tail-group entry wrapper emission changed')
    emitter_fixed=emitter_fixed.replace(wrapper,wrapper.replace(b'? "inline "',b'? "GN_MAYBE_UNUSED inline "'),1)
    emitter_fixed=emitter_fixed.replace(b'are inline, so one never called is no warning',
        b'are inline and explicitly may be unused by direct callers',1)
    runtime_path=tree/'native/gslt_native_runtime_v1.h';runtime=original(repo,'native/gslt_native_runtime_v1.h')
    anchor=b'#include <stdio.h>\n'
    if runtime.count(anchor)!=1:raise RuntimeError('native runtime header prelude changed')
    runtime_fixed=runtime.replace(anchor,anchor+b'\n#if defined(__GNUC__) || defined(__clang__)\n'
        b'#define GN_MAYBE_UNUSED __attribute__((unused))\n#else\n#define GN_MAYBE_UNUSED\n#endif\n',1)
    if runtime_path.read_bytes() not in (runtime,runtime_fixed):raise RuntimeError('unrecognized native runtime header edit')
    make_path=tree/'Makefile';make=original(repo,'Makefile')
    prepare=b'\tsrc/parser.c $(SYMBOL_STANDALONE_SRC) src/atom.c src/binding/frame_identity.c src/name_key.c\n'
    if make.count(prepare)!=1:raise RuntimeError('equation preparation source set changed')
    make_fixed=make.replace(prepare,prepare[:-1]+b' \\\n\t$(MATCH_STANDALONE_SRC) src/term_canon.c src/variant_shape.c src/variant_instance.c src/term_universe.c\n',1)
    if make_path.read_bytes() not in (make,make_fixed):raise RuntimeError('unrecognized Makefile capture edit')
    if emitter_path.read_bytes() not in (emitter,emitter_fixed):raise RuntimeError('unrecognized emitter capture edit')
    for path,data in [(asp_path,fixed),(tensor_path,tensor_fixed),
                      (interaction_path,interaction_fixed),(emitter_path,emitter_fixed),
                      (runtime_path,runtime_fixed),(make_path,make_fixed)]:
        if path.read_bytes()!=data:path.write_bytes(data)
    state_path=tree/'cesta-record-client.json'
    prior=json.loads(state_path.read_text()) if state_path.exists() else {}
    native=original(repo,'src/eval.c');native_main=original(repo,'src/main.c')
    for path,expected in [('src/eval.c',native),('src/main.c',native_main)]:
        current=(tree/path).read_bytes()
        if current!=expected and digest(current)!=prior.get(path):
            raise RuntimeError('refusing to overwrite an unrecognized capture edit: '+path)
        if current!=expected:(tree/path).write_bytes(expected)
    baseline='runtime/cetta-record-'+variant+'-baseline'
    generated='runtime/cetta-record-'+variant+'-generated'
    options=['CESTA_BUILD=0','CESTA='+str(cesta),'CESTA_NATIVE_CC='+args.compiler,
             'BUILD=core','ENABLE_LIB_PROLOG=0','ENABLE_RUNTIME_STATS=1',
             'BUILD_OBJ_TAG=cesta-record-'+variant]
    if args.sanitizers:options+=['ENABLE_SANITIZERS=1']
    def build(target,log):
        run(['make','-s',*options,'-j',args.jobs,'BIN='+target,target],tree,evidence/log,streamed=True)
    environment=dict(os.environ,CETTA_GC='1',CETTA_GC_BUDGET_MB='1',
        CETTA_PRIME_NEED_HEAP_INDEX='1',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
        UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    print(variant+': building pinned native baseline',flush=True)
    build(baseline,'baseline-build.log')
    before={p:fixture(tree,tree/baseline,p,evidence/('baseline-'+str(i)+'.log'),environment)
            for i,p in enumerate([NARS,ATP])}
    source=patch_source(native.decode())
    patched_main=native_main.decode().replace('int main(int argc,char **argv) {',
        'extern int cesta_call_source_controls(void);\nextern int cesta_frame_gc_controls(void);\n'
        'int main(int argc,char **argv) {\n'
        '    if (argc==2 && !strcmp(argv[1],"--cesta-record-test")) return cesta_call_source_controls();\n'
        '    if (argc==2 && !strcmp(argv[1],"--cesta-frame-test")) return cesta_frame_gc_controls();',1)
    (tree/'src/eval.c').write_text(source);(tree/'src/main.c').write_text(patched_main)
    state_path.write_text(json.dumps({'src/eval.c':digest(source.encode()),
                                     'src/main.c':digest(patched_main.encode())},indent=2)+'\n')
    run(['make','-s',*options,'BIN='+generated,'--eval',
         'cesta-record-context: ; @$(file >cesta-record-cppflags.txt,$(CPPFLAGS) $(CFLAGS)) '
         '$(file >cesta-record-stage0-cppflags.txt,-Isrc -I. -Iexperiments/gslt2parse_foundation/native '
         '$(BRIDGE_CFLAGS) $(PY_CFLAGS) $(GMP_CFLAGS) $(LIB_PROLOG_CFLAGS) $(HTTP_CFLAGS) '
         '$(SIWC_CFLAGS) $(DURABLE_CFLAGS) -include $(STAGE0_BUILD_CONFIG_HEADER) $(CFLAGS) -DCETTA_NO_STDLIB) true',
         'cesta-record-context'],tree,evidence/'context.log')
    flags=shlex.split((tree/'cesta-record-cppflags.txt').read_text())
    stage0=shlex.split((tree/'cesta-record-stage0-cppflags.txt').read_text())
    headers={}
    for name,schema,output,configuration in [
        ('call_sources','call_sources','cesta_call_sources.h',flags),
        ('frame_gate','frame_gate','cesta_frame_gc_gate_heap.h',flags),
        ('frame_gate_base','frame_gate','cesta_frame_gc_gate_base.h',stage0)]:
        header=tree/'src/generated'/output;facts=evidence/(name+'.facts')
        command=[cesta,'record-visitor','--schema',HERE/(schema+'.records'),'-o',header,
                 '--facts',facts,'--cc',args.compiler,'-std=c11',*configuration,
                 '-DCESTA_RECORD_DECLARE_ONLY=1','src/eval.c']
        run(command,tree,evidence/(name+'-generation.log'))
        first=header.read_bytes();run(command,tree,evidence/(name+'-regeneration.log'))
        if header.read_bytes()!=first:raise RuntimeError('visitor generation is not deterministic')
        headers[name]=digest(first)
    print(variant+': building generated audit and native lifetime controls',flush=True)
    declaration_refusals=declaration_controls(cesta,args.compiler,flags,tree,evidence,source) if args.mutations else 0
    build(generated,'generated-build.log')
    program=tree/generated
    run([program,'--cesta-record-test'],tree,evidence/'lifetime-controls.log',environment=environment)
    run([program,'--cesta-frame-test'],tree,evidence/'frame-controls.log',environment=environment)
    after={p:fixture(tree,program,p,evidence/('generated-'+str(i)+'.log'),environment)
           for i,p in enumerate([NARS,ATP])}
    if before!=after:raise RuntimeError('generated audit changed answers or collection accounting')
    killed=0
    if args.mutations:
        old='return prime_eval_stack_audit_held_bindings(audit->eval_audit, audit->survivor_audit, bindings);'
        if source.count(old)!=1:raise RuntimeError('common-binding mutation site changed')
        mutant=source.replace(old,'(void)audit; (void)bindings; return true;',1)
        (tree/'src/eval.c').write_text(mutant)
        try:
            target='runtime/cetta-record-'+variant+'-mutant'
            build(target,'mutant-build.log')
            result=run([tree/target,'--cesta-record-test'],tree,evidence/'mutant-control.log',
                       environment=environment,accepted=False)
            if result.returncode!=5:raise RuntimeError('mutation failed for a different reason')
            killed+=1
        finally:(tree/'src/eval.c').write_text(source)
        build(generated,'restored-build.log')
        run([program,'--cesta-record-test'],tree,evidence/'restored-controls.log',environment=environment)
    summary={'source_pin':PIN,'compiler':variant,'cesta_sha256':digest(cesta.read_bytes()),
        'capture_fixes':{'native/asp/asp_cdnl_v1.c':{'original_sha256':digest(asp),
            'fixed_sha256':digest(fixed),'change':'Remove an unused unsigned loop counter; retain the same continuation condition.'},
            'src/tensor_dense_v1.c':{'original_sha256':digest(tensor),'fixed_sha256':digest(tensor_fixed),
            'change':'Remove redundant equality parentheses without changing the comparisons.'},
            'src/interaction_core.c':{'original_sha256':digest(interaction),'fixed_sha256':digest(interaction_fixed),
            'change':'Define checked-only native hooks under the same mode as their generated protocol uses.'},
            'native/gslt_native_direct_v1.c':{'original_sha256':digest(emitter),'fixed_sha256':digest(emitter_fixed),
            'change':'Print atomic equality conditions with one condition pair and mark tail-group entry wrappers that may have no direct caller.'},
            'native/gslt_native_runtime_v1.h':{'original_sha256':digest(runtime),'fixed_sha256':digest(runtime_fixed),
            'change':'Provide a compiler-portable maybe-unused annotation for generated private entry wrappers.'},
            'Makefile':{'original_sha256':digest(make),'fixed_sha256':digest(make_fixed),
            'change':'Link equation preparation with the parser term-universe and binding dependencies rather than relying on optimizer removal.'}},
        'schema_sha256':{name:digest((HERE/(name+'.records')).read_bytes()) for name in ['call_sources','frame_gate']},
        'generated_header_sha256':headers,'native_source_sha256':digest(native),
        'native_binary_sha256':digest((tree/baseline).read_bytes()),
        'generated_binary_sha256':digest(program.read_bytes()),'fixtures':after,
        'native_lifetime_controls':9,'live_frame_gate_controls':14,'semantic_mutants_killed':killed,
        'actual_client_declaration_refusals':declaration_refusals,
        'scope':'Actual field walk and native two-arena controls; native predicates and collector correctness remain explicit boundaries.'}
    (evidence/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(f'{variant}: actual generated call-source audit and live-frame gate; 23 native controls, '
          f'2 forced-GC fixtures and {killed} semantic mutants',flush=True)


if __name__=='__main__':main()
