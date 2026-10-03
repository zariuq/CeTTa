#!/usr/bin/env python3
"""Exercise contracted document outcomes through the actual process boundary."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('--artifacts', type=Path, default=Path('runtime'))
    args = parser.parse_args()
    binary = args.binary.resolve()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='run-cli.', dir=args.artifacts)).resolve()
    cases = [
        ('empty', '', 'petta', 0, 0, 0),
        ('error-shaped-data', '!(Error raw-data context)\n!(+ 40 2)\n', 'petta', 0, 2, 0),
        ('unknown-data', '!(unknown-call 1)\n', 'petta', 0, 1, 0),
        ('no-answers', '!(superpose ())\n', 'petta', 0, 1, 0),
        ('caught', '!(catch (callPredicate (Predicate (throw boom))))\n!(+ 40 2)\n', 'petta', 0, 2, 0),
        ('escaped', '!(callPredicate (Predicate (throw boom)))\n!(+ 40 2)\n', 'petta', 2, 2, 1),
        ('failed-test', '!(test 1 2)\n!(+ 40 2)\n', 'petta', 1, 2, 0),
        ('passed-test', '!(test 1 1)\n!(+ 40 2)\n', 'petta', 0, 2, 0),
        ('he-failed-test', '!(assertEqual 1 2)\n', 'he', 1, 1, 0),
        ('he-passed-test', '!(assertEqual 1 1)\n', 'he', 0, 1, 0),
    ]
    for route in ('0', '1'):
        env = dict(os.environ, CETTA_OPEN_EQUATIONS_REFERENCE=route)
        for name, text, language, status, declared, faults in cases:
            source = root / f'{name}-{route}.metta'
            source.write_text(text)
            report = source.with_suffix('.json')
            command = [str(binary), '--lang', language, '--run-contract', '--run-report', str(report), str(source)]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=30)
            (source.with_suffix('.stdout')).write_text(result.stdout)
            (source.with_suffix('.stderr')).write_text(result.stderr)
            document = json.loads(report.read_text())
            assert result.returncode == status, (name, route, result.returncode, result.stderr)
            assert document['contract_exit'] == status, (name, document)
            assert document['queries_declared'] == declared
            assert sum(q['faults'] for q in document['queries']) == faults
            if status == 0:
                assert all(q['observation_complete'] and q['finalization_ready'] and q['framing_ok'] for q in document['queries'])
            if name == 'failed-test':
                assert document['queries_unsettled'] == 1 and document['queries'][0]['tests'] == 1
            if name == 'no-answers':
                assert document['queries'][0]['answers'] == 0
            if name == 'escaped':
                assert document['queries_unsettled'] == 1
            assert document['report_publication_acknowledged'] is False
        for language in ('he','petta'):
            source=root/f'fuel-{language}-{route}.metta'
            source.write_text('(= (loop) (loop))\n!(loop)\n!(+ 40 2)\n')
            report=source.with_suffix('.json')
            r=subprocess.run([str(binary),'--lang',language,'--fuel','100','--run-contract','--run-report',str(report),str(source)],
                             env=env,capture_output=True,text=True,timeout=30)
            d=json.loads(report.read_text())
            assert r.returncode==1 and d['queries_declared']==2 and d['queries_unsettled']==1,(language,r.stderr,d)
            assert not d['queries'][0]['observation_complete'] and sum(q['faults'] for q in d['queries'])==0
        # Construct deeply nested data through tail calls so the type service,
        # rather than the parser or the construction, reaches its stack budget.
        for mode in ('inference', 'guard'):
            operation = '(get-type $v)' if mode == 'inference' else '(deep-same $v $v)'
            for depth, status in ((128, 0), (50000, 2)):
                source = root / f'type-stack-{mode}-{depth}-{route}.metta'
                source.write_text(
                    '(: deep-type (-> Number Number))\n'
                    '(: deep-same (-> $t $t Bool))\n'
                    '(= (deep-same $x $y) True)\n'
                    '(= (nest $n $v) (if (== $n 0) $v '
                    '(nest (- $n 1) (cons-atom deep-type ($v)))))\n'
                    f'(= (deep-use $v) (progn (println! before-type) {operation}))\n'
                    f'!(let $v (nest {depth} 1) (deep-use $v))\n'
                    '!(+ 40 2)\n')
                report = source.with_suffix('.json')
                r = subprocess.run(
                    [str(binary), '--lang', 'petta', '--run-contract',
                     '--run-report', str(report), str(source)],
                    env=env, capture_output=True, text=True, timeout=30)
                d = json.loads(report.read_text())
                assert r.returncode == status and d['contract_exit'] == status, (mode, depth, route, r.stderr, d)
                assert r.stdout.splitlines().count('before-type') == 1, (mode, depth, route, r.stdout)
                if status == 0:
                    answer = 'Number' if mode == 'inference' else 'true'
                    assert r.stdout.splitlines() == ['before-type', answer, '42'], (route, r.stdout)
                    assert d['queries_unsettled'] == 0 and d['diagnostics'] == []
                else:
                    assert r.stdout == 'before-type\n', (mode, route, r.stdout)
                    assert 'observation incomplete: stack-exhausted' in r.stderr
                    assert d['queries_declared'] == 2 and d['queries_unsettled'] == 1
                    assert not d['queries'][0]['observation_complete']
                    assert sum(q['faults'] for q in d['queries']) == 1
                    assert len(d['diagnostics']) == 1
                    assert d['diagnostics'][0]['classification'] == 'resource'
                    assert 'StackOverflow' in d['diagnostics'][0]['message']
        source=root/f'halt-{route}.metta';source.write_text('!(callPredicate (Predicate (halt 0)))\n!(+ 40 2)\n')
        report=source.with_suffix('.json')
        r=subprocess.run([str(binary),'--lang','petta','--run-contract','--run-report',str(report),str(source)],
                         env=env,capture_output=True,text=True,timeout=30)
        d=json.loads(report.read_text())
        assert r.returncode==1 and d['queries_declared']==2 and d['queries_unsettled']==2 and not d['document_framing_ok'],(r.stderr,d)
        source=root/f'workers-{route}.metta'
        source.write_text('!(collapse (hyperpose ((test 1 1) (test 2 2))))\n')
        report=source.with_suffix('.json')
        r=subprocess.run([str(binary),'--lang','petta','--num-threads','4','--run-contract','--run-report',str(report),str(source)],
                         env=env,capture_output=True,text=True,timeout=30)
        d=json.loads(report.read_text())
        assert r.returncode==0 and d['queries'][0]['tests']==2,(r.stdout,r.stderr,d)
        # The ordinary HE exit policy is preserved even when its JSON view
        # exposes an unsuccessful stricter contract.
        source = root / f'he-failed-test-{route}.metta'
        report = root / f'he-default-{route}.json'
        r = subprocess.run([str(binary), '--lang', 'he', '--run-report', str(report), str(source)],
                           env=env, capture_output=True, text=True, timeout=30)
        assert r.returncode == 0 and json.loads(report.read_text())['contract_exit'] == 1
        source = root / f'passed-test-{route}.metta'
        if Path('/dev/full').exists():
            with open('/dev/full', 'w') as sink:
                r = subprocess.run([str(binary), '--lang', 'petta', '--run-contract', str(source)],
                                   env=env, stdout=sink, stderr=subprocess.PIPE, timeout=30)
                assert r.returncode != 0
            r = subprocess.run([str(binary), '--lang', 'petta', '--run-report', '/dev/full', str(source)],
                               env=env, capture_output=True, timeout=30)
            assert r.returncode == 1 and b'publish run report' in r.stderr
        first = root / f'multi-first-{route}.metta'
        second = root / f'multi-second-{route}.metta'
        first.write_text('!(callPredicate (Predicate (throw boom)))\n')
        second.write_text('!(+ 40 2)\n!(+ 40 3)\n')
        report = root / f'multi-{route}.json'
        r = subprocess.run([str(binary), '--lang', 'petta', '--run-contract', '--run-report', str(report), str(first), str(second)],
                           env=env, capture_output=True, text=True, timeout=30)
        d = json.loads(report.read_text())
        assert r.returncode == 2 and d['queries_declared'] == 3 and d['queries_unsettled'] == 2
    print('PASS: CLI contracts account for queries, native verdicts, type-stack faults, handled/escaped faults and output/publication failure on both routes')


if __name__ == '__main__':
    main()
