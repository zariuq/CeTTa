"""Actual evaluator/ledger controls; retained artifacts include crash receipts."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

p=argparse.ArgumentParser()
p.add_argument('binary',type=Path)
p.add_argument('--artifacts',type=Path,default=Path('runtime'))
p.add_argument('--skip-external-adapter',action='store_true')
a=p.parse_args()
a.artifacts.mkdir(parents=True,exist_ok=True)
root=Path(tempfile.mkdtemp(prefix='supervise-library.',dir=a.artifacts)).resolve()
binary=a.binary.resolve()
records=[]
def execute(lang,route,name,source,expected=0):
    program=root/f'{lang}-{route}-{name}.metta'
    program.write_text(source)
    env=dict(os.environ,CETTA_OPEN_EQUATIONS_REFERENCE=route)
    r=subprocess.run([str(binary),'--lang',lang,str(program)],env=env,
                     capture_output=True,text=True,timeout=40)
    (program.with_suffix('.stdout')).write_text(r.stdout)
    (program.with_suffix('.stderr')).write_text(r.stderr)
    records.append(dict(language=lang,route=route,name=name,status=r.returncode))
    assert r.returncode==expected,(program,r.returncode,r.stdout,r.stderr)
    return r.stdout

for lang in ('petta','he'):
    for route in ('0','1'):
        db=json.dumps(str(root/f'{lang}-{route}.db'))
        yes,no=('true','false') if lang=='petta' else ('True','False')
        out=execute(lang,route,'once',f'''
!(import! &self supervise)
(= (handler $payload $epoch $id $generation)
   (let $printed (println! HANDLER_ONCE) (+ $payload 1)))
!(let* (($s (supervise:open {db}))
        ($submitted (supervise:submit $s "task" 2 41))
        ($permit (supervise:next $s))
        ($first (supervise:dispatch-once $permit handler))
        ($second (supervise:dispatch-once $permit handler))
        ($epoch (supervise:epoch $s))
        ($early (supervise:acknowledge $s $epoch "task" 1))
        ($wrong (supervise:reply $s $epoch "task" 0 complete {yes} {no} 99))
        ($done (supervise:reply $s $epoch "task" 1 complete {yes} {no} $first))
        ($ack (supervise:acknowledge $s $epoch "task" 1)))
   ($submitted $first $second $early $wrong $done $ack (supervise:get $s "task")))
''')
        assert out.count('HANDLER_ONCE')==1,(lang,route,out)
        assert 'supervise:no-grant' in out and out.count('supervise:ignored')==2,out
        assert re.search(r'"task" 1 1 succeeded (?:true|True) 41 42',out),out
        out=execute(lang,route,'reopen',f'''
!(import! &self supervise)
!(let $s (supervise:open {db})
   (supervise:get $s "task"))
''')
        assert re.search(r'"task" 1 1 succeeded (?:true|True) 41 42',out),out
        # Foreign exception objects and variables are not durable data. Reading
        # a quoted expression must not execute it, on either dialect/route.
        inertdb=json.dumps(str(root/f'inert-{lang}-{route}.db'))
        out=execute(lang,route,'inert',f'''
!(import! &self supervise)
!(let* (($s (supervise:open {inertdb}))
        ($a (supervise:submit $s "inert" 1 (quote (println! INERT_SENTINEL))))
        ($invalid (supervise:submit $s "open" 1 $unbound)))
   ($a $invalid (supervise:get $s "inert")))
''')
        assert '\nINERT_SENTINEL\n' not in out,out
        assert '(println! INERT_SENTINEL)' in out and '(supervise:refused invalid)' in out,out

        retaineddb=json.dumps(str(root/f'retained-{lang}-{route}.db'))
        holder='(get-state supervised-ledger)' if lang=='petta' else '&supervised-ledger'
        permit_holder='(get-state dispatch-permit)' if lang=='petta' else '&dispatch-permit'
        bind_ledger=f'(change-state! supervised-ledger (supervise:open {retaineddb}))' if lang=='petta' else f'(bind! &supervised-ledger (supervise:open {retaineddb}))'
        bind_permit=f'(change-state! dispatch-permit (supervise:next {holder}))' if lang=='petta' else f'(bind! &dispatch-permit (supervise:next {holder}))'
        out=execute(lang,route,'retained-owner',f'''
!(import! &self supervise)
!{bind_ledger}
!(supervise:submit {holder} "retained" 1 11)
!{bind_permit}
!(supervise:claim {permit_holder})
!(supervise:claim {permit_holder})
!(supervise:get {holder} "retained")
!(supervise:release {permit_holder})
!(supervise:close {holder})
''')
        assert 'supervise:dispatch' in out and 'supervise:no-grant' in out,out
        assert '"retained" 1 0 active' in out,out

        dispatchdb=json.dumps(str(root/f'inert-dispatch-{lang}-{route}.db'))
        out=execute(lang,route,'inert-dispatch',f'''
!(import! &self supervise)
(= (data-handler $payload $epoch $id $generation) (quote $payload))
!(let* (($s (supervise:open {dispatchdb}))
        ($a (supervise:submit $s "data" 1 (quote (println! INERT_DISPATCH_SENTINEL))))
        ($permit (supervise:next $s)))
   (supervise:dispatch-once $permit data-handler))
''')
        assert '\nINERT_DISPATCH_SENTINEL\n' not in out,out
        assert '(println! INERT_DISPATCH_SENTINEL)' in out,out

        invaliddb=json.dumps(str(root/f'invalid-receipt-{lang}-{route}.db'))
        out=execute(lang,route,'invalid-receipt',f'''
!(import! &self supervise)
!(let* (($s (supervise:open {invaliddb}))
        ($a (supervise:submit $s "unserializable" 2 9))
        ($permit (supervise:next $s))
        ($epoch (supervise:epoch $s))
        ($reply (supervise:reply $s $epoch "unserializable" 1 complete {no} {yes} $permit)))
   ($reply (supervise:get $s "unserializable") (supervise:queue $s)))
''')
        assert '(supervise:refused invalid)' in out and '"unserializable" 1 1 uncertain' in out,out

        if a.skip_external_adapter:
            continue
        crashdb=json.dumps(str(root/f'crash-{lang}-{route}.db'))
        effect=root/f'effect-{lang}-{route}.txt'
        # One process exits after its physical effect and before recording a
        # result. Reopening must retain uncertainty and refuse automatic replay.
        module=root/f'crash_adapter_{lang}_{route}.py'
        module.write_text(f'''import os
def effect_then_exit():
    with open({str(effect)!r}, 'a') as f:
        f.write('effect\\n'); f.flush(); os.fsync(f.fileno())
    os._exit(86)
''')
        old=os.environ.get('PYTHONPATH')
        os.environ['PYTHONPATH']=str(root)+(os.pathsep+old if old else '')
        try:
            execute(lang,route,'crash',f'''
!(import! &self supervise)
(= (crash-handler $payload $epoch $id $generation)
   (py-call ({module.stem}.effect_then_exit)))
!(let* (($s (supervise:open {crashdb}))
        ($a (supervise:submit $s "crash" 2 7))
        ($permit (supervise:next $s)))
   (supervise:dispatch-once $permit crash-handler))
''',86)
            out=execute(lang,route,'recover',f'''
!(import! &self supervise)
!(let* (($s (supervise:open {crashdb}))
        ($view (supervise:get $s "crash"))
        ($next (supervise:next $s))
        ($queue (supervise:queue $s))
        ($b (supervise:submit $s "independent" 1 9))
        ($ready (supervise:next $s)))
   ($view $next $queue (supervise:claim $ready) (supervise:tasks $s)))
''')
            assert '"crash" 1 1 uncertain' in out and 'supervise:no-ready-task' in out,out
            assert re.search(r'supervise:dispatch "[0-9a-f]+" "independent" 1 9',out),out
            assert '(supervise:queue 0 0 1 0 0 0 0)' in out,out
            assert effect.read_text()=='effect\n',effect.read_text()
        finally:
            if old is None: os.environ.pop('PYTHONPATH',None)
            else: os.environ['PYTHONPATH']=old

(root/'receipt.json').write_text(json.dumps(records,indent=2)+'\n')
print('PASS supervised library: one-use dispatch, owned aliases, inert data and terminal accounting')
if a.skip_external_adapter:
    print('SKIP external-effect crash adapter: Python backend absent')
else:
    print('PASS external-effect crash/restart, uncertainty and independent work')
