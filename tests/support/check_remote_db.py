#!/usr/bin/env python3
"""Ordinary MeTTa operations over a captured native database service."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import socket
import threading
import time
import struct

from check_native_db_server import Client, E, S, Var, capture, publish, server_for, DropReplyProxy, decode, encode

ROOT = Path(__file__).resolve().parents[2]


def invoke(binary, directory, arguments, marker):
    source = directory / 'client.metta'
    source.write_text('!(import! &self ' + json.dumps(str(ROOT / 'tests/support/remote_db_fixture.metta'))
                      + ')\n!(println! (remote-fixture:run (' + arguments + ')))\n')
    run = subprocess.run([str(binary), '--lang', 'petta', str(source)],
                         capture_output=True, text=True, timeout=40)
    assert run.returncode == 0 and marker in run.stdout.splitlines(), (
        arguments, run.returncode, run.stdout, run.stderr)
    assert 'Assertion failed' not in run.stdout + run.stderr
    assert 'ACCIDENTAL_EXECUTION' not in run.stdout
    return dict(case=arguments.split()[0], marker=marker, exit=run.returncode)


class ProposalProxy:
    """Cut only at a decoded operation boundary; retain the upstream evidence."""
    def __init__(self, path, upstream, mode):
        self.path, self.upstream, self.mode = path, upstream, mode
        self.observed = False
        self.error = None
        self.scans = 0
        self.steps = 0
        self.listener = socket.socket(socket.AF_UNIX)
        self.listener.bind(str(path))
        self.listener.listen(1)
        self.listener.settimeout(10)
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        try:
            client, _ = self.listener.accept()
            with client, socket.socket(socket.AF_UNIX) as server:
                server.connect(str(self.upstream))
                client.settimeout(10)
                server.settimeout(10)
                while True:
                    packet = DropReplyProxy.frame(client)
                    if packet is None:
                        break
                    body = decode(packet[8:])[2]
                    operation = body[2] if body[0] == S('UseV1') else body[1]
                    if self.mode == 'cancel' and operation == S('DbScan'):
                        self.scans += 1
                        self.observed = True
                        time.sleep(.2)
                    if self.mode == 'count' and operation == S('DbCandidateStep'):
                        self.steps += 1
                        self.observed = True
                    if self.mode == 'read-disconnect' and operation == S('DbCandidates'):
                        self.observed = True
                        break
                    if operation == S('DbPublish') and self.mode == 'stale':
                        writer = Client(self.upstream)
                        try:
                            source, _ = capture(writer)
                            result = publish(writer, source, 'intervening-write',
                                             (E('SpaceInsertV1', 0, E('intervening', S('committed'))),))
                            assert result[0] == S('KnowledgeCommittedV1'), result
                        finally:
                            writer.close()
                        self.observed = True
                    server.sendall(packet)
                    reply = DropReplyProxy.frame(server)
                    assert reply
                    if operation == S('DbPublish') and self.mode in ('lost', 'post-commit-error'):
                        result = decode(reply[8:])[3]
                        assert result[0] == S('KnowledgeCommittedV1'), result
                        self.observed = True
                        if self.mode == 'lost':
                            break
                        identifier = decode(reply[8:])[1]
                        failed = encode(E('reply', identifier, S('error'), E('ServiceFailedV1', S('handler-fuel'))))
                        reply = b'CSP1' + struct.pack('>I', len(failed)) + failed
                    client.sendall(reply)
        except Exception as error:
            self.error = error
        finally:
            self.listener.close()

    def join(self):
        self.thread.join(10)
        assert not self.thread.is_alive() and self.error is None and self.observed, self.error


def seed(path):
    client = Client(path)
    try:
        source, _ = capture(client)
        facts = (E('edge', S('a'), S('b')), E('edge', S('a'), S('b')),
                 E('rel', Var(0), Var(0)), E('blob', 'z' * 300000),
                 E('=', E('inert'), E('println!', S('ACCIDENTAL_EXECUTION'))))
        result = publish(client, source, 'seed', tuple(
            E('SpaceInsertV1', i, fact) for i, fact in enumerate(facts)))
        assert result[0] == S('KnowledgeCommittedV1'), result
        client.drop(source)
    finally:
        client.close()


def check(binary, worker, directory, case, marker):
    server = server_for(worker, directory, directory / 'store')
    try:
        server.start()
        seed(server.path)
        source = directory / 'client.metta'
        fixture = ROOT / 'tests/support/remote_db_fixture.metta'
        source.write_text('!(import! &self ' + json.dumps(str(fixture)) + ')\n'
                          '!(println! (remote-fixture:run (' + case + ' '
                          + json.dumps(str(server.path)) + ')))\n')
        run = subprocess.run([str(binary), '--lang', 'petta', str(source)],
                             capture_output=True, text=True, timeout=40)
        assert run.returncode == 0 and marker in run.stdout.splitlines(), (
            case, run.returncode, run.stdout, run.stderr)
        assert 'AssertionFailed' not in run.stdout + run.stderr
        assert 'ACCIDENTAL_EXECUTION' not in run.stdout
        return dict(case=case, marker=marker, exit=run.returncode)
    finally:
        server.stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('worker', type=Path)
    parser.add_argument('--native-fixture', type=Path)
    parser.add_argument('--act-fixture', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    results = []
    with tempfile.TemporaryDirectory(prefix='remote-db-', dir=ROOT / 'runtime') as temporary:
        root = Path(temporary)
        for case, marker in [('Reads', 'REMOTE_READS_OK'), ('Named', 'REMOTE_NAMED_OK'),
                             ('Transactions', 'REMOTE_TRANSACTIONS_OK'),
                             ('CloseBody', 'REMOTE_CLOSED_BODY_OK'),
                             ('Nested', 'REMOTE_NESTED_OK'),
                             ('LocalReads', 'REMOTE_LOCAL_READS_OK'),
                             ('Collections', 'REMOTE_COLLECTIONS_OK'),
                             ('Maintenance', 'REMOTE_MAINTENANCE_OK'),
                             ('Numbers', 'REMOTE_NUMBERS_OK'),
                             ('Removals', 'REMOTE_REMOVALS_OK'),
                             ('LargeTransaction', 'REMOTE_LARGE_TRANSACTION_OK')]:
            directory = root / case
            directory.mkdir(mode=0o700)
            results.append(check(args.binary.resolve(), args.worker.resolve(), directory, case, marker))
        if args.native_fixture:
            directory = root / 'native'
            directory.mkdir(mode=0o700)
            server = server_for(args.worker.resolve(), directory, directory / 'store')
            try:
                server.start()
                seed(server.path)
                run = subprocess.run([str(args.native_fixture.resolve()), str(server.path)],
                                     capture_output=True, text=True, timeout=40)
                assert run.returncode == 0 and 'REMOTE_SPACE_OK' in run.stdout.splitlines(), (
                    run.returncode, run.stdout, run.stderr)
                results.append(dict(case='NativeRetainedCursors', marker='REMOTE_SPACE_OK', exit=0))
                proxy = ProposalProxy(directory / 'cancel.sock', server.path, 'cancel')
                run = subprocess.run([str(args.native_fixture.resolve()), str(proxy.path), '--cancel'],
                                     capture_output=True, text=True, timeout=40)
                assert run.returncode == 0 and 'REMOTE_CANCELLED_READER_OK' in run.stdout.splitlines(), (
                    run.returncode, run.stdout, run.stderr)
                proxy.join()
                assert proxy.scans == 1, 'queued cancelled scan was transmitted'
                results.append(dict(case='CancelKeepsSiblingReader', marker='REMOTE_CANCELLED_READER_OK', exit=0))
            finally:
                server.stop()
        for mode, case, marker in [('lost', 'LostWrite', 'REMOTE_LOST_WRITE_OK'),
                                   ('post-commit-error', 'LostWrite', 'REMOTE_LOST_WRITE_OK'),
                                   ('stale', 'StaleWrite', 'REMOTE_STALE_WRITE_OK'),
                                   ('read-disconnect', 'Interrupted', 'REMOTE_INTERRUPTED_OK')]:
            directory = root / mode
            directory.mkdir(mode=0o700)
            server = server_for(args.worker.resolve(), directory, directory / 'store')
            try:
                server.start()
                seed(server.path)
                proxy = ProposalProxy(directory / 'proxy.sock', server.path, mode)
                results.append(invoke(args.binary.resolve(), directory,
                                      case + ' ' + json.dumps(str(proxy.path)), marker))
                proxy.join()
                if mode in ('lost', 'post-commit-error'):
                    effect = str(proxy.path) + '-client-effect'
                    results.append(invoke(args.binary.resolve(), directory,
                                          'ResolveLost ' + json.dumps(str(server.path)) + ' ' + json.dumps(effect),
                                          'REMOTE_RESOLVE_LOST_OK'))
                    server.stop(crash=True)
                    server.start()
                    results.append(invoke(args.binary.resolve(), directory,
                                          'ResolveLost ' + json.dumps(str(server.path)) + ' ' + json.dumps(effect),
                                          'REMOTE_RESOLVE_LOST_OK'))
            finally:
                server.stop()
        # Removing an indexed key reads its rows, not the collection's 401.
        directory = root / 'remove-cost'
        directory.mkdir(mode=0o700)
        server = server_for(args.worker.resolve(), directory, directory / 'store')
        try:
            server.start()
            proxy = ProposalProxy(directory / 'count.sock', server.path, 'count')
            results.append(invoke(args.binary.resolve(), directory,
                                  'RemoveCost ' + json.dumps(str(proxy.path)), 'REMOTE_REMOVE_COST_OK'))
            proxy.join()
            assert proxy.steps < 40, ('indexed removal read the collection', proxy.steps)
            results.append(dict(case='RemoveCostSteps', steps=proxy.steps))
        finally:
            server.stop()
        for same_owner, case, marker in [(True, 'NestedActivation', 'REMOTE_ACTIVATION_BARRIER_OK'),
                                         (False, 'OtherOwner', 'REMOTE_OTHER_OWNER_OK')]:
            directory = root / case
            directory.mkdir(mode=0o700)
            second_directory = directory / 'second'
            second_directory.mkdir(mode=0o700)
            first = server_for(args.worker.resolve(), directory, directory / 'store')
            second = server_for(args.worker.resolve(), second_directory,
                                directory / 'store' if same_owner else second_directory / 'store')
            try:
                first.start()
                second.start()
                results.append(invoke(args.binary.resolve(), directory,
                                      case + ' ' + json.dumps(str(first.path)) + ' ' + json.dumps(str(second.path)), marker))
            finally:
                second.stop()
                first.stop()
        if args.act_fixture:
            image = root / 'snapshot.cact'
            subprocess.run([str(args.act_fixture.resolve()), 'write', str(image)],
                           capture_output=True, text=True, check=True)
            directory = root / 'read-only'
            directory.mkdir(mode=0o700)
            server = server_for(args.worker.resolve(), directory, image)
            try:
                server.start()
                source = directory / 'client.metta'
                source.write_text('!(import! &self ' + json.dumps(str(ROOT / 'tests/support/remote_db_fixture.metta'))
                                  + ')\n!(println! (remote-fixture:run (ReadOnly '
                                  + json.dumps(str(server.path)) + ')))\n')
                run = subprocess.run([str(args.binary.resolve()), '--lang', 'petta', str(source)],
                                     capture_output=True, text=True, timeout=40)
                assert run.returncode == 0 and 'REMOTE_READ_ONLY_OK' in run.stdout.splitlines(), (
                    run.returncode, run.stdout, run.stderr)
                results.append(dict(case='ReadOnlyACT', marker='REMOTE_READ_ONLY_OK', exit=0))
            finally:
                server.stop()
    report = dict(results=results, status='pass')
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
