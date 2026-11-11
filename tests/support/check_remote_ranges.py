#!/usr/bin/env python3
"""Stored scalar/range order parity using raw facts and the client matcher."""
import argparse
import json
import os
import shutil
from pathlib import Path
import subprocess
import tempfile
import time

from check_native_db_server import Client, E, S, Var, capture, publish, server_for, decode, DropReplyProxy
from space_service_client import BigInt, Rational

ROOT = Path(__file__).resolve().parents[2]


def run_client(args, source):
    command = [str(args.binary.resolve()), '--lang', 'petta', str(source)]
    if args.valgrind_log_dir:
        command[:0] = ['valgrind', '--leak-check=full', '--errors-for-leak-kinds=definite,indirect',
                       '--error-exitcode=99', '--log-file='+str(args.valgrind_log_dir.resolve()/'client-%p.log')]
    return subprocess.run(command, capture_output=True, text=True, timeout=120)


def instrument(server, args):
    if args.valgrind_log_dir:
        server.command[:0] = ['valgrind', '--leak-check=full', '--errors-for-leak-kinds=definite,indirect',
                             '--error-exitcode=99', '--log-file='+str(args.valgrind_log_dir.resolve()/'server-%p.log')]
    return server


def moved(client, token, operation, argument):
    value = client.call(E('UseValueV1', token, S(operation), argument))
    if value[0] == S('ServiceResourceV1'):
        assert value[2][0] == S('DbLinearV1'), value
        return value[1], value[2][1]
    return None, value


def original_packet(client, collection):
    source, _ = capture(client, collection)
    opened = client.route('DbRangeOpen', E('DbRangeOpenV1', collection, 4, 3, E('Inclusive', 5), E('Inclusive', 5), False), source)
    assert opened[0] == S('ServiceResourceV1'), opened
    token, sequence = opened[1], 0
    client.drop(source)
    for _ in range(100):
        token, value = moved(client, token, 'DbRangeNext', E('DbRangeNextV1', sequence))
        if value[0] == S('DbRangeRowV1'):
            assert value[1] == 0, value
            token, chunk = moved(client, token, 'DbRangeRead', E('DbRangeReadV1', sequence, S('body'), 0, 65536))
            assert chunk[0] == S('DbRangeChunkV1')
            raw = chunk[1][4]
            raw = raw if isinstance(raw, bytes) else raw.encode()
            assert decode(raw) == E('open', Var(0), Var(0), 5), decode(raw)
            client.drop(token)
            return
        assert value[0] == S('DbRangeSkipV1'), value
    raise AssertionError('raw open fact not delivered')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('worker', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--valgrind-log-dir', type=Path)
    args = parser.parse_args()
    if args.valgrind_log_dir:
        args.valgrind_log_dir.mkdir(parents=True, exist_ok=True)
    checks = []
    with tempfile.TemporaryDirectory(prefix='remote-ranges-', dir=ROOT/'runtime') as temporary:
        root = Path(temporary)
        for indexed in [False, True]:
            directory = root/('indexed' if indexed else 'scan')
            directory.mkdir(mode=0o700)
            server = instrument(server_for(args.worker.resolve(), directory, directory/'store'), args)
            client = None
            try:
                server.start()
                client = Client(server.path)
                collection = S('values')
                indexes = (E('SpaceRangeIndexV1', 3, 2), E('SpaceRangeIndexV1', 4, 3)) if indexed else ()
                ensured = client.route('DbEnsure', E('DbEnsureV1', collection, indexes))
                assert ensured == S('true'), ensured
                source, _ = capture(client, collection)
                facts = [E('value', S('negative'), -2), E('value', S('one'), 1),
                         E('value', S('float-one'), 1.0), E('value', S('fraction'), Rational('3/2')),
                         E('value', S('five-a'), 5), E('value', S('five-b'), 5),
                         E('value', E('range-test:side-effect'), 5),
                         E('value', S('six'), BigInt('6')), E('value', S('outside'), 20),
                         E('value', S('minus-zero'), -0.0), E('value', S('negative-fraction'), Rational('-1/2')),
                         E('value', S('minimum'), -9223372036854775808), E('value', S('maximum'), 9223372036854775807),
                         E('value', S('large'), BigInt('9223372036854775808')),
                         E('value', S('string'), 'seven'), E('value', S('open-field'), Var(0)),
                         E('open', Var(0), Var(0), 5), E('other', S('shape')), Var(0), E('range-test:side-effect'),
                         E('println!', S('ACCIDENTAL_EXECUTION')),
                         E('value', 'x'*300000, 5)]
                result = publish(client, source, S('seed'), [E('SpaceInsertV1', i, row) for i, row in enumerate(facts)], collection=collection)
                assert result[0] == S('KnowledgeCommittedV1'), result
                client.drop(source)
                original_packet(client, collection)
                checks.append(('indexed' if indexed else 'scan')+'_raw_repeated_variables_and_parent_close')
                source, _ = capture(client, collection)
                assert client.route('DbRangeOpen', E('DbRangeOpenV1', S('data'), 3, 2, S('NoBound'), S('NoBound'), False), source) == E('DbRefusedV1', S('range-source-scope'))
                assert client.route('DbRangeOpen', E('DbRangeOpenV1', collection, 3, 2, S('NoBound'), S('NoBound'), S('invalid')), source) == E('DbRefusedV1', S('range-order-flag'))
                client.drop(source)
                source = directory/'client.metta'
                source.write_text('!(import! &self '+json.dumps(str(ROOT/'tests/support/remote_range_fixture.metta'))+')\n'
                    + '!(println! (range-test:run (Compare '+json.dumps(str(directory/'store'))+' '
                    + json.dumps(str(server.path))+' values '+('True' if indexed else 'False')+')))\n')
                result = run_client(args, source)
                assert result.returncode == 0 and 'REMOTE_RANGE_COMPARE_OK' in result.stdout.splitlines(), (result.returncode, result.stdout, result.stderr)
                assert 'ACCIDENTAL_EXECUTION' not in result.stdout+result.stderr
                checks.append('indexed' if indexed else 'complete_scan')
                source.write_text('!(import! &self '+json.dumps(str(ROOT/'tests/support/remote_range_fixture.metta'))+')\n'
                    + '!(println! (range-test:run (Retain '+json.dumps(str(directory/'store'))+' '
                    + json.dumps(str(server.path))+' values)))\n')
                result = run_client(args, source)
                assert result.returncode == 0 and 'REMOTE_RANGE_RETAINED_OK' in result.stdout.splitlines(), (result.returncode, result.stdout, result.stderr)
                assert 'ACCIDENTAL_EXECUTION' not in result.stdout+result.stderr
                checks.append(('indexed' if indexed else 'scan')+'_retained_revision_and_connection_owner_close')
                example = subprocess.run([str(args.binary.resolve()), '--lang', 'petta', str(ROOT/'examples/db_server_client.metta')],
                                         env=dict(os.environ, CETTA_DB_SOCKET=str(server.path)), capture_output=True, text=True, timeout=30)
                assert example.returncode == 0 and 'bob' in example.stdout.splitlines(), (example.stdout, example.stderr)
                checks.append(('indexed' if indexed else 'scan')+'_named_server_example')
                compact = client.route('DbCompactOpen', E('DbCompactOpenV1', 1048576))
                assert compact[0] == S('ServiceResourceV1'), compact
                token = compact[1]
                for _ in range(10000):
                    progress = client.route('DbCompactStep', E('DbCompactStepV1', 1048576), token)
                    if progress[0] == S('DbCompactionDoneV1'):
                        break
                    assert progress[0] == S('StoreCompactionMore'), progress
                else:
                    raise AssertionError('compaction failed to complete')
                client.drop(token)
                source.write_text('!(import! &self '+json.dumps(str(ROOT/'tests/support/remote_range_fixture.metta'))+')\n'
                    + '!(println! (range-test:run (Compare '+json.dumps(str(directory/'store'))+' '
                    + json.dumps(str(server.path))+' values '+('True' if indexed else 'False')+')))\n')
                result = run_client(args, source)
                assert result.returncode == 0 and 'REMOTE_RANGE_COMPARE_OK' in result.stdout.splitlines(), (result.returncode, result.stdout, result.stderr)
                assert 'ACCIDENTAL_EXECUTION' not in result.stdout+result.stderr
                checks.append(('indexed' if indexed else 'scan')+'_act_base_order_and_identity_parity')
                for case, marker in [('Cancel', 'REMOTE_RANGE_CANCELLED_OK'), ('Interrupted', 'REMOTE_RANGE_INTERRUPTED_OK')]:
                    proxy = DropReplyProxy(directory/'lost.sock', server.path, S('DbRangeRead')) if case == 'Interrupted' else None
                    socket_path = proxy.path if proxy else server.path
                    source.write_text('!(import! &self '+json.dumps(str(ROOT/'tests/support/remote_range_fixture.metta'))+')\n'
                        + '!(println! (range-test:run ('+case+' '+json.dumps(str(socket_path))+')))\n')
                    result = run_client(args, source)
                    if proxy:
                        proxy.join()
                        assert proxy.observed.is_set()
                    assert result.returncode == 0 and marker in result.stdout.splitlines(), (case, result.returncode, result.stdout, result.stderr)
                    deadline = time.monotonic()+5
                    while client.call(E('StatsV1'))[2] != 0:
                        assert time.monotonic() < deadline
                        time.sleep(.005)
                    checks.append(('indexed' if indexed else 'scan')+'_'+case.lower())
                if not indexed:
                    image = directory/'values.cact'
                    source.write_text('!(import! &self (library db))\n'
                        + '(= (range-export:run) (let* (($db (db:connect '+json.dumps(str(server.path))+'))'
                        + ' ($answer (db:export-act $db values '+json.dumps(str(image))+')) ($_ (db:close $db))) $answer))\n'
                        + '!(println! (range-export:run))\n')
                    exported = run_client(args, source)
                    assert exported.returncode == 0 and image.is_file(), (exported.stdout, exported.stderr)
                    act_directory = root/'act'
                    act_directory.mkdir(mode=0o700)
                    act_server = instrument(server_for(args.worker.resolve(), act_directory, image), args)
                    observer = None
                    try:
                        act_server.start()
                        observer = Client(act_server.path)
                        original_packet(observer, S('data'))
                        source.write_text('!(import! &self '+json.dumps(str(ROOT/'tests/support/remote_range_fixture.metta'))+')\n'
                            + '!(println! (range-test:run (Compare '+json.dumps(str(image))+' '
                            + json.dumps(str(act_server.path))+' data False)))\n')
                        compared = run_client(args, source)
                        assert compared.returncode == 0 and 'REMOTE_RANGE_COMPARE_OK' in compared.stdout.splitlines(), (compared.returncode, compared.stdout, compared.stderr)
                        assert 'ACCIDENTAL_EXECUTION' not in compared.stdout+compared.stderr
                        checks.append('standalone_act_local_remote_range_parity_and_raw_variables')
                    finally:
                        if observer:
                            observer.close()
                        act_server.stop()
            finally:
                if client:
                    client.close()
                server.stop()
        original_directory, copied_directory = root/'owner-original', root/'owner-copy'
        original_directory.mkdir(mode=0o700)
        copied_directory.mkdir(mode=0o700)
        first = instrument(server_for(args.worker.resolve(), original_directory, original_directory/'store'), args)
        second = None
        try:
            first.start()
            shutil.copytree(original_directory/'store', copied_directory/'store')
            second = instrument(server_for(args.worker.resolve(), copied_directory, copied_directory/'store'), args)
            second.start()
            left, right = Client(first.path), Client(second.path)
            try:
                first_owner, second_owner = left.route('DbCapabilities')[1], right.route('DbCapabilities')[1]
                assert first_owner[0] == second_owner[0] == S('DbOwnerV1')
                assert first_owner[1] == second_owner[1] and first_owner[2] != second_owner[2]
            finally:
                left.close()
                right.close()
            source = root/'owner-client.metta'
            source.write_text('!(import! &self '+json.dumps(str(ROOT/'tests/support/remote_db_fixture.metta'))+')\n'
                + '!(println! (remote-fixture:run (OtherOwner '+json.dumps(str(first.path))+' '+json.dumps(str(second.path))+')))\n')
            result = run_client(args, source)
            assert result.returncode == 0 and 'REMOTE_OTHER_OWNER_OK' in result.stdout.splitlines(), (result.stdout, result.stderr)
            checks.append('backup_epoch_does_not_merge_independent_commit_domains')
        finally:
            if second:
                second.stop()
            first.stop()
        original = (ROOT/'examples/native_db_server.metta').read_text()
        for name, modified in [('version', original.replace('(DbProtocolV1 1 (', '(DbProtocolV1 2 (')),
                               ('capability', original.replace('ImmutablePositionsV1', 'MutablePositionsV0'))]:
            directory = root/('protocol-'+name)
            directory.mkdir(mode=0o700)
            module = directory/'server.metta'
            module.write_text(modified)
            server = server_for(args.worker.resolve(), directory, directory/'store')
            server.command[-2] = str(module)
            instrument(server, args)
            try:
                server.start()
                source = directory/'client.metta'
                source.write_text('!(import! &self '+json.dumps(str(ROOT/'tests/support/remote_range_fixture.metta'))+')\n'
                    + '!(println! (range-test:run (Protocol '+json.dumps(str(server.path))+')))\n')
                refused = run_client(args, source)
                assert refused.returncode == 0 and 'REMOTE_PROTOCOL_REFUSED_OK' in refused.stdout.splitlines(), (refused.stdout, refused.stderr)
                checks.append('incompatible_'+name+'_refused_at_connection')
            finally:
                server.stop()
    result = dict(checks=checks, passed=len(checks))
    if args.output:
        args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
