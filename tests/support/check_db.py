#!/usr/bin/env python3
"""Public db API: ordinary Spaces, scoped edits, durable answers and cleanup."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time
import os
import sys
import shlex


def process_counters(pid):
    process = Path('/proc') / str(pid)
    try:
        rss = next(line for line in (process / 'status').read_text().splitlines() if line.startswith('VmRSS:'))
        return dict(rss_kib=int(rss.split()[1]), descriptors=len(list((process / 'fd').iterdir())))
    except (FileNotFoundError, StopIteration):
        return dict(rss_kib=None, descriptors=None)


def check_case(binary, directory, case, expected):
    source = directory / (case + '.metta')
    source.write_text('!(import! &self ' + json.dumps(str(Path(__file__).with_name('db_fixture.metta').resolve())) + ')\n'
                      '!(println! (db-fixture:run (' + case + ' ' + json.dumps(str(directory / 'store')) + ')))\n')
    result = subprocess.run([str(binary), '--lang', 'petta', str(source)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, (case, result.stdout, result.stderr)
    assert expected in result.stdout.splitlines(), (case, result.stdout, result.stderr)
    assert 'AssertionFailed' not in result.stdout + result.stderr, (case, result.stdout, result.stderr)
    assert 'SHOULD_NOT_EXECUTE' not in result.stdout, (case, result.stdout)
    return dict(case=case, marker=expected, exit=result.returncode)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--worker', type=Path)
    parser.add_argument('--rounds', type=int, default=100)
    parser.add_argument('--read-rounds', type=int, default=1000)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='metta-db-') as temporary:
        directory = Path(temporary)
        cases = [('Basic', 'DB_BASIC_OK'), ('Read', 'DB_REOPEN_OK'), ('Rollback', 'DB_ROLLBACK_OK'),
                 ('Replay', 'DB_REPLAY_OK'), ('ReplayRead', 'DB_RESTART_REPLAY_OK'),
                 ('Named', 'DB_NAMED_OK'), ('Invalidate', 'DB_INVALIDATION_OK'),
                 ('Escape', 'DB_ESCAPE_CLOSED_OK'), ('SpaceRemove', 'DB_REMOVE_OK'), ('Page', 'DB_PAGE_OK'), ('QueryData', 'DB_QUERY_DATA_OK'), ('Resolve', 'DB_RESOLVE_OK'), ('Convenience', 'DB_CONVENIENCE_OK'), ('LocalRead', 'DB_LOCAL_READ_OK'), ('CloseBody', 'DB_CLOSED_BODY_REFUSED_OK'), ('Nested', 'DB_NESTED_REFUSED_OK'), ('RawNested', 'DB_RAW_NESTED_REFUSED_OK'), ('OtherDomain', 'DB_OTHER_DOMAIN_OK'), ('RaisedError', 'DB_RAISED_ERROR_OK'), ('Cycle', 'DB_CYCLE_OK')]
        results = [check_case(args.binary.resolve(), directory, case, expected) for case, expected in cases]
        named = directory / 'named-script.metta'
        named.write_text('!(import! &self (library db))\n'
            + '!(import! &self (library system))\n'
            + '!(db:open claims (system:arg 1))\n'
            + '!(db:transaction claims (|-> ($space) (let $_ (add-atom $space (fact persisted)) done)))\n'
            + '!(println! (collapse (match (db:snapshot claims) (fact $x) $x)))\n'
            + '!(db:close claims)\n')
        result = subprocess.run([str(args.binary.resolve()), '--lang', 'petta', str(named), '--', str(directory / 'named')],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0 and '(persisted)' in result.stdout.splitlines(), (result.stdout, result.stderr)
        results.append(dict(case='NamedConnectionScript', exit=result.returncode))
        # Block after staging a local edit. Another process must still write;
        # killing this callback must not publish its staged atom on reopen.
        entered, gate = directory / 'entered', directory / 'gate'
        os.mkfifo(gate)
        interrupt = directory / 'interrupt.metta'
        interrupt.write_text('!(import! &self ' + json.dumps(str(Path(__file__).with_name('db_fixture.metta').resolve())) + ')\n'
            + '!(db-fixture:run (Interrupt ' + ' '.join(json.dumps(str(p)) for p in (directory / 'store', entered, gate)) + '))\n')
        blocked = subprocess.Popen([str(args.binary.resolve()), '--lang', 'petta', str(interrupt)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 10
            while not entered.exists() and blocked.poll() is None and time.monotonic() < deadline:
                time.sleep(.005)
            assert entered.exists() and blocked.poll() is None, 'callback did not reach the blocking read'
            check_case(args.binary.resolve(), directory, 'Cycle', 'DB_CYCLE_OK')
            blocked.kill()
            blocked.communicate(timeout=10)
            results.append(dict(case='KillStagedCallback', returncode=blocked.returncode))
        finally:
            if blocked.poll() is None:
                blocked.kill(); blocked.communicate(timeout=10)
        results.append(check_case(args.binary.resolve(), directory, 'VerifyInterrupt', 'DB_INTERRUPTED_REOPEN_OK'))
        if sys.platform.startswith('linux'):
            fault = directory / 'entropy-fault.c'
            library = directory / 'entropy-fault.so'
            fault.write_text('#include <stddef.h>\n#include <errno.h>\n'
                             'int getentropy(void *p,size_t n){(void)p;(void)n;errno=EIO;return -1;}\n')
            compiled = subprocess.run(shlex.split(os.environ.get('CC', 'cc')) +
                ['-shared', '-fPIC', str(fault), '-o', str(library)], capture_output=True, text=True)
            assert compiled.returncode == 0, compiled.stderr
            marker = directory / 'entropy-callback'
            probe = directory / 'entropy.metta'
            probe.write_text('!(import! &self ' + json.dumps(str(Path(__file__).with_name('db_fixture.metta').resolve())) + ')\n'
                + '!(println! (db-fixture:run (EntropyFailure ' + json.dumps(str(directory / 'store')) + ' '
                + json.dumps(str(marker)) + ')))\n')
            failed_rng = subprocess.run([str(args.binary.resolve()), '--lang', 'petta', str(probe)],
                env=dict(os.environ, LD_PRELOAD=str(library)), capture_output=True, text=True, timeout=30)
            assert failed_rng.returncode == 0 and 'DB_ENTROPY_FAILURE_OK' in failed_rng.stdout.splitlines(), (failed_rng.stdout, failed_rng.stderr)
            assert not marker.exists(), 'callback ran without a request identity'
            results.append(dict(case='EntropyFailureBeforeCallback', exit=failed_rng.returncode))
        else:
            results.append(dict(case='EntropyFailureBeforeCallback', status='not-run: Linux loader injection'))
        samples = []
        if args.worker:
            sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
            from value_worker_client import Worker
            from space_service_client import E, S
            worker = Worker(args.worker.resolve(), Path(__file__).with_name('db_fixture.metta').resolve(),
                            'db-fixture:run', language='petta', profile=True)
            try:
                for index in range(args.rounds + 1):
                    assert worker.call(E('RaisedError', str(directory / 'store'))) == S('DB_RAISED_ERROR_OK')
                    assert worker.call(E('CloseBody', str(directory / 'store'))) == S('DB_CLOSED_BODY_REFUSED_OK')
                    assert worker.call(E('Cycle', str(directory / 'store'))) == S('DB_CYCLE_OK')
                    if index in {0, args.rounds // 4, args.rounds // 2, 3 * args.rounds // 4, args.rounds}:
                        samples.append(dict(round=index, **process_counters(worker.process.pid), metrics=worker.last_metrics))
                read_samples = []
                for index in range(args.read_rounds + 1):
                    assert worker.call(E('ReadCycle', str(directory / 'store'))) == S('DB_READ_CYCLE_OK')
                    if index in {0, args.read_rounds // 2, args.read_rounds}:
                        read_samples.append(dict(round=index, **process_counters(worker.process.pid), metrics=worker.last_metrics))
            finally:
                process, worker.process = worker.process, None
                if process is not None:
                    process.stdin.close()
                    code = process.wait(timeout=30)
                    process.stdout.close()
                    assert code == 0, ('worker exit', code)
                worker.close()
            assert len({s['descriptors'] for s in samples}) == 1, samples
            assert len({s['metrics']['permanent_bytes'] for s in samples[1:]}) == 1, samples
        report = dict(checks=results, cleanup_samples=samples, fixed_database_reads=read_samples if args.worker else [])
        if args.output:
            args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report))
