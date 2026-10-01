"""Exercise bounded diagnostic views without changing caught Python objects."""
import argparse, json, os, subprocess, tempfile
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument('binary',type=Path); p.add_argument('--artifacts',type=Path,default=Path('runtime'))
a=p.parse_args(); a.artifacts.mkdir(exist_ok=True,parents=True)
root=Path(tempfile.mkdtemp(prefix='error-presentation.',dir=a.artifacts)).resolve()
(root/'diagnostic_probe.py').write_text('''from pathlib import Path
MARK = Path(__file__).with_suffix('.called')
class Hostile(ValueError):
    def __str__(self):
        MARK.write_text('str'); raise RuntimeError('formatter invoked')
    def __repr__(self):
        MARK.write_text('repr'); raise RuntimeError('formatter invoked')
    def __getattribute__(self, name):
        if name == 'args':
            MARK.write_text('attribute'); raise RuntimeError('getter invoked')
        return super().__getattribute__(name)
def plain(): raise ValueError('bad value Ω')
def private(): raise ValueError('https://example.invalid/?token=PRIVATE_SENTINEL')
def huge(): raise ValueError('Ω' * 1000000)
def controls(): raise ValueError('line1\\nline2\\x00tail')
def hostile(): raise Hostile('bounded detail survives')
def nonstring(): raise ValueError({'token': 'PRIVATE_SENTINEL'})
def surrogate(): raise ValueError('bad\\ud800text')
def identity(error): return isinstance(error, ValueError)
''')
for route in ('0','1'):
    env=dict(os.environ,CETTA_OPEN_EQUATIONS_REFERENCE=route,PYTHONPATH=str(root)+os.pathsep+os.environ.get('PYTHONPATH',''))
    for case in ('plain','private','huge','controls','hostile','nonstring','surrogate'):
        source=root/f'{case}-{route}.metta'; source.write_text(f'!(py-call (diagnostic_probe.{case}))\n!(+ 40 2)\n')
        for detail in (False,True):
            report=root/f'{case}-{route}-{detail}.json'
            cmd=[str(a.binary.resolve()),'--lang','petta','--run-contract','--run-report',str(report)]
            if detail: cmd+=['--diagnostic-details']
            r=subprocess.run(cmd+[str(source)],env=env,capture_output=True,text=True,timeout=30)
            report_text=report.read_text(); doc=json.loads(report_text)
            assert r.returncode==2 and doc['queries_unsettled']==1, (case,route,r.stderr,doc)
            assert sum(q['faults'] for q in doc['queries'])==1
            assert len(doc['diagnostics'])==1
            view=doc['diagnostics'][0]
            assert view['details_hidden']==(not detail)
            assert len(view['message'].encode('utf8'))<=255
            assert not any(ord(ch)<32 for ch in view['message'])
            if not detail:
                assert 'PRIVATE_SENTINEL' not in report_text+r.stderr
                assert not view['formatter_failed']
            elif case=='plain': assert view['message']=='bad value Ω'
            elif case=='private': assert 'PRIVATE_SENTINEL' in view['message']
            elif case=='huge': assert view['truncated'] and view['message'].startswith('Ω')
            elif case=='controls': assert view['message']=='line1 line2 tail'
            elif case=='hostile': assert view['message']=='bounded detail survives'
            elif case=='nonstring': assert view['formatter_failed'] and 'PRIVATE_SENTINEL' not in report_text+r.stderr
            elif case=='surrogate': assert view['message']=='bad�text'
            assert not (root/'diagnostic_probe.called').exists()
    source=root/f'caught-{route}.metta'
    source.write_text('!(let (Error (python_error $type $object) $context) (catch (py-call (diagnostic_probe.plain))) (py-call (diagnostic_probe.identity $object)))\n!(+ 40 2)\n')
    report=source.with_suffix('.json')
    r=subprocess.run([str(a.binary.resolve()),'--lang','petta','--run-contract','--diagnostic-details','--run-report',str(report),str(source)],env=env,capture_output=True,text=True,timeout=30)
    doc=json.loads(report.read_text())
    assert r.returncode==0 and doc['diagnostics']==[] and sum(q['faults'] for q in doc['queries'])==0,(r.stdout,r.stderr,doc)
    assert r.stdout.splitlines()==['true','42'],(r.stdout,r.stderr)
print('PASS: bounded/private diagnostics, Unicode/control text, hostile formatters, fallback and caught exception identity on both routes')
