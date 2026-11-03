#!/usr/bin/env python3
"""Write-triggered native maintenance, reader retention and frozen-cut recovery."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time

from check_native_db_server import Client, E, S, capture, cursor, drain, publish, server_for
from check_native_kb_service import Server

ROOT = Path(__file__).resolve().parents[2]


def instrument(server, args):
    if args.valgrind_log_dir:
        server.command[:0] = ['valgrind', '--leak-check=full', '--errors-for-leak-kinds=definite,indirect',
                              '--error-exitcode=99', '--log-file='+str(args.valgrind_log_dir.resolve()/'server-%p.log')]
    return server


def seed(args, directory, image, legacy=False):
    directory.mkdir(mode=0o700)
    source = directory/'seed.metta'
    owner = '(let (StoreOpened $owner) (store:open '+json.dumps(str(directory/'store'))+' "create-directory") $owner)' if legacy else '(db:open '+json.dumps(str(directory/'store'))+')'
    source.write_text('!(import! &self (library db))\n!(import! &self (library lib_he))\n'
        + '(= (maintenance-seed:run) (let* (($db '+owner+') ($_ (db:ensure $db data ()))'
        + ' ($_ (assertEqual (db:import-act-result $db data seed copy '+json.dumps(str(image))+') (db:committed seed (True))))'
        + ' ($_ (db:close $db))) SEEDED))\n!(println! (maintenance-seed:run))\n')
    result = subprocess.run([str(args.binary.resolve()), '--lang', 'petta', str(source)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0 and 'SEEDED' in result.stdout.splitlines(), (result.stdout, result.stderr)


def status(client):
    value = client.route('ServiceMaintenanceInfo')
    assert value[0] == S('ServiceMaintenanceInfoV1'), value
    return value


def info(client):
    value = client.route('DbInfo')
    assert value[0] == S('db:database-info'), value
    return {str(pair[0].name): pair[1] for pair in value[1:]}


def finish(client, timeout=60):
    deadline = time.monotonic()+timeout
    observed = []
    while time.monotonic() < deadline:
        value = status(client)
        observed.append(value[1].name)
        assert value[1] != S('failed'), value
        if value[1] == S('idle'):
            details = info(client)
            assert details['has-base'] is True and details['compacting'] is False, details
            return value, observed
        time.sleep(.005)
    raise AssertionError(('maintenance did not complete', value))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('worker', type=Path)
    parser.add_argument('act_fixture', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--valgrind-log-dir', type=Path)
    args = parser.parse_args()
    if args.valgrind_log_dir:
        args.valgrind_log_dir.mkdir(parents=True, exist_ok=True)
    timeout = 300 if args.valgrind_log_dir else 60
    checks, latencies = [], []
    with tempfile.TemporaryDirectory(prefix='db-maintenance-', dir=ROOT/'runtime') as temporary:
        root = Path(temporary)
        for mode in ['normal', 'malformed']:
            directory = root/('generic-'+mode)
            directory.mkdir(mode=0o700)
            generic = instrument(Server(args.worker.resolve(), directory, module=ROOT/'tests/support/value_service_maintenance_fixture.metta',
                                        entry='maintenance-host:configuration'), args)
            generic.environment['CETTA_TEST_MAINTENANCE_MODE'] = mode
            client = None
            try:
                generic.start()
                client = Client(generic.path)
                deadline = time.monotonic()+5
                while status(client)[1] == S('scheduled'):
                    assert time.monotonic() < deadline
                before = status(client)
                assert before[1] == S('idle' if mode == 'normal' else 'failed'), before
                assert client.route('Echo', E('PayloadV1', 'preserved')) == E('PayloadV1', 'preserved')
                after = status(client)
                assert after[2] == before[2]+1, (before, after)
                time.sleep(.1)
                assert status(client) == after
                client.call(E('ReadOnlyV1'))
                for operation in ['ServiceMaintenanceCancel', 'ServiceMaintenanceResume']:
                    assert client.route(operation, ok=False) == E('ServiceRefusedV1', S('read-only'))
                assert status(client) == after
                checks.append('generic_'+mode+'_hook_and_acknowledgement')
            finally:
                if client:
                    client.close()
                generic.stop()
        image = root/'source.cact'
        subprocess.run([str(args.act_fixture.resolve()), 'generate', str(image), '4096', '512'], check=True, capture_output=True)
        active = root/'active'
        seed(args, active, image)
        server = instrument(server_for(args.worker.resolve(), active, active/'store'), args)
        client = None
        try:
            server.start()
            client = Client(server.path)
            initial = info(client)
            assert initial['compacting'] is True, initial
            old, _ = capture(client)
            for number in range(4):
                view, _ = capture(client)
                began = time.monotonic()
                answer = publish(client, view, S('during-'+str(number)), [E('SpaceInsertV1', 0, E('later', number))])
                latencies.append(time.monotonic()-began)
                assert answer[0] == S('KnowledgeCommittedV1'), answer
                client.drop(view)
            completed, phases = finish(client, timeout)
            old_scan = cursor(client, old)
            retained = drain(client, old_scan)
            assert len(retained) == 4096 and all(row[0][0] == S('row') for row in retained)
            client.drop(old_scan)
            client.drop(old)
            current, metadata = capture(client)
            assert metadata[5] == 4100, metadata
            client.drop(current)
            # Status reads and an idle timer cannot schedule another MeTTa turn.
            before = status(client)
            time.sleep(.15)
            after = status(client)
            assert before == after and before[1] == S('idle'), (before, after)
            source = active/'status.metta'
            source.write_text('!(import! &self (library db))\n'
                + '(= (maintenance-status:run) (let* (($db (db:connect '+json.dumps(str(server.path))+'))'
                + ' ($info (db:maintenance-info $db)) ($_ (db:close $db))) $info))\n!(println! (maintenance-status:run))\n')
            result = subprocess.run([str(args.binary.resolve()), '--lang', 'petta', str(source)], capture_output=True, text=True, timeout=30)
            assert result.returncode == 0 and '(db:maintenance (phase idle) ' in result.stdout, (result.stdout, result.stderr)
            checks.extend(['threshold_and_automatic_compaction', 'writers_continue', 'retained_reader', 'no_idle_evaluation'])
        finally:
            if client:
                client.close()
            server.stop()
        interrupted = root/'interrupted'
        seed(args, interrupted, image)
        server = instrument(server_for(args.worker.resolve(), interrupted, interrupted/'store'), args)
        server.environment['CETTA_DB_MAINTENANCE_WORK'] = '4096'
        client = None
        try:
            server.start()
            client = Client(server.path)
            before = info(client)
            assert before['compacting'] is True and status(client)[1] == S('scheduled'), before
            cancelled = client.route('ServiceMaintenanceCancel')
            assert cancelled[1] == S('cancelled'), cancelled
            time.sleep(.1)
            assert status(client) == cancelled
            control = interrupted/'control.metta'
            control.write_text('!(import! &self (library db))\n'
                + '(= (maintenance-control:run) (let* (($db (db:connect '+json.dumps(str(server.path))+'))'
                + ' ($_ (println! (db:maintenance-cancel $db)))'
                + ' ($_ (println! (db:maintenance-resume $db))) ($_ (db:close $db))) CONTROL_OK))\n!(println! (maintenance-control:run))\n')
            result = subprocess.run([str(args.binary.resolve()), '--lang', 'petta', str(control)], capture_output=True, text=True, timeout=30)
            assert result.returncode == 0 and '(db:maintenance (phase cancelled) ' in result.stdout and '(db:maintenance (phase scheduled) ' in result.stdout, (result.stdout, result.stderr)
            checks.append('cancel_and_resume_owning_job')
            client.close()
            client = None
            server.stop(crash=True)
            server.start()
            client = Client(server.path)
            completed, _ = finish(client, timeout)
            after = info(client)
            assert after['revision'] == before['revision'], (before, after)
            source, metadata = capture(client)
            assert metadata[5] == 4096
            client.drop(source)
            checks.append('restart_reuses_frozen_cut_and_preserves_revision')
        finally:
            if client:
                client.close()
            server.stop()
        legacy = root/'legacy'
        seed(args, legacy, image, legacy=True)
        server = instrument(server_for(args.worker.resolve(), legacy, legacy/'store'), args)
        client = None
        try:
            server.start()
            client = Client(server.path)
            before = info(client)
            assert before['format'] == 2 and before['delta-bytes'] >= 8388608, before
            time.sleep(.1)
            assert status(client)[1] == S('idle') and info(client) == before
            checks.append('legacy_open_never_upgrades')
        finally:
            if client:
                client.close()
            server.stop()
        failed = root/'failed'
        seed(args, failed, image)
        server = instrument(server_for(args.worker.resolve(), failed, failed/'store'), args)
        server.environment['CETTA_DB_MAINTENANCE_MEMORY'] = '1'
        client = None
        try:
            server.start()
            client = Client(server.path)
            observed = status(client)
            assert observed[1] == S('failed'), observed
            view, _ = capture(client)
            assert publish(client, view, S('ack-preserved'), [E('SpaceInsertV1', 0, E('later', S('safe')))]) [0] == S('KnowledgeCommittedV1')
            client.drop(view)
            view, metadata = capture(client)
            assert metadata[5] == 4097, metadata
            client.drop(view)
            before = status(client)
            time.sleep(.1)
            assert status(client) == before and before[1] == S('failed')
            checks.append('maintenance_refusal_preserves_acknowledged_writes')
        finally:
            if client:
                client.close()
            server.stop()
    receipt = dict(checks=checks, passed=len(checks), write_seconds=latencies)
    if args.output:
        args.output.write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
