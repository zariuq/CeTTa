#!/usr/bin/env python3
"""Native original-candidate service, owner-bound proposals and ACT sources."""
import argparse
from collections import Counter
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading

from check_native_kb_service import Client, Server, E, S, Var, encode, decode

ROOT = Path(__file__).resolve().parents[2]


def server_for(worker, root, source):
    server = Server(worker, root, module=ROOT / 'examples/native_db_server.metta',
                    entry='db-server:configuration')
    server.environment.update(CETTA_DB_SOURCE=str(source), CETTA_DB_SOCKET=str(server.path))
    return server


def capture(client, collection=S('data')):
    answer = client.route('DbCapture', E('DbCaptureV1', collection))
    assert answer[0] == S('ServiceResourceV1') and answer[2][0] == S('DbCapturedV1'), answer
    return answer[1], answer[2]


def cursor(client, source, pattern=None):
    answer = client.route('DbScan', (), source) if pattern is None else client.route(
        'DbCandidates', E('DbCandidatesV1', pattern), source)
    assert answer[0] == S('ServiceResourceV1'), answer
    return answer[1]


def packet(client, scan, sequence, part, length):
    data = b''
    while len(data) < length:
        reply = client.route('DbCandidateRead', E('DbCandidateReadV1', sequence, S(part), len(data), 65536), scan)
        assert reply[0:4] == (S('StoreCandidateChunk'), sequence, S(part), len(data)), reply
        chunk = reply[4].encode('utf-8') if isinstance(reply[4], str) else reply[4]
        assert chunk and len(chunk) <= 65536
        data += chunk
    assert len(data) == length
    return decode(data)


def drain(client, scan, sequence=0):
    rows = []
    while True:
        reply = client.route('DbCandidateNext', E('DbCandidateNextV1', sequence, 1), scan)
        if reply == S('StoreCandidatePaused'):
            continue
        if reply == S('StoreCandidateEnd'):
            return rows
        assert reply[0:2] == (S('StoreCandidateRow'), sequence), reply
        assert client.route('DbCandidateNext', E('DbCandidateNextV1', sequence, 1), scan) == reply
        rows.append((packet(client, scan, sequence, 'body', reply[2]),
                     packet(client, scan, sequence, 'identity', reply[3])))
        sequence += 1


def publish(client, source, request, changes, operation=S('operation'), values=(), dependencies=(), collection=S('data')):
    return client.route('DbPublish', E('DbPublishV1', collection, request, operation,
                                      tuple(changes), values, dependencies), source)


class DropReplyProxy:
    """Acknowledge observation of a full upstream reply, then drop it."""
    def __init__(self, path, upstream, drop_operation=None):
        self.path = path
        self.upstream = upstream
        self.drop_operation = drop_operation
        self.drop = threading.Event()
        self.observed = threading.Event()
        self.error = None
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.listener.bind(str(path)); self.listener.listen(1)
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    @staticmethod
    def frame(connection):
        header = b''
        while len(header) < 8:
            part = connection.recv(8-len(header))
            if not part: return None
            header += part
        assert header[:4] == b'CSP1'
        size, = struct.unpack('>I', header[4:]); body = b''
        while len(body) < size:
            part = connection.recv(size-len(body)); assert part
            body += part
        return header + body

    def run(self):
        try:
            client, _ = self.listener.accept()
            with client, socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
                server.connect(str(self.upstream)); client.settimeout(10); server.settimeout(10)
                while True:
                    request = self.frame(client)
                    if request is None: break
                    server.sendall(request); reply = self.frame(server); assert reply
                    body = decode(request[8:])[2]
                    operation = body[1] if body[0] == S('CallV1') and len(body) == 3 else (
                        body[2] if body[0] in [S('UseV1'), S('UseValueV1')] and len(body) == 4 else None)
                    if self.drop.is_set() or operation == self.drop_operation:
                        self.observed.set(); break
                    client.sendall(reply)
        except Exception as error:
            self.error = error
        finally:
            self.listener.close()

    def join(self):
        self.thread.join(10); assert not self.thread.is_alive() and self.error is None, self.error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('worker', type=Path)
    parser.add_argument('--act-fixture', type=Path)
    args = parser.parse_args(); worker = args.worker.resolve()
    results = {}
    with tempfile.TemporaryDirectory(prefix='native-db-', dir=ROOT/'runtime') as temporary:
        root = Path(temporary); server = server_for(worker, root, root/'store')
        first = second = None
        try:
            server.start(); first, second = Client(server.path), Client(server.path)
            caps = first.route('DbCapabilities')
            assert caps[0] == S('DbCapabilitiesV1') and caps[3:5] == (S('directory'), S('ReadWrite')), caps
            long_request = 'request-' + 'x' * 4096
            assert first.call(E('StatsV1'), identifier=long_request)[0] == S('ServiceStatsV1')
            empty, context = capture(first)
            facts = (E('edge', S('a'), S('b')), E('edge', S('a'), S('b')),
                     E('rel', Var(0), Var(0)), E('blob', 'z'*300000),
                     E('=', E('inert'), E('println!', S('ACCIDENTAL_EXECUTION'))))
            changes = tuple(E('SpaceInsertV1', i, fact) for i, fact in enumerate(facts))
            own = (E('DbFreshV1', caps[1], caps[2], context[3], S('DbWholeV1')),)
            committed = publish(first, empty, 'seed', changes, values=(S('done'),), dependencies=own)
            assert committed[0:2] == (S('KnowledgeCommittedV1'), E('DbTransactionValuesV1', (S('done'),))), committed
            assert first.route('DbResolve', E('DbResolveV1', S('data'), 'seed', S('operation'))) == E('db:resolved', 'seed', (S('done'),))
            assert drain(first, cursor(first, empty)) == []
            assert publish(first, empty, 'stale', ()) == E('StoreRefused', S('conflict'))
            source, current = capture(first)
            foreign = (E('DbFreshV1', 'foreign-owner', caps[2], current[3], S('DbWholeV1')),)
            assert publish(first, source, 'foreign', (), dependencies=foreign) == E('DbRefusedV1', S('dependency-owner-or-scope'))
            inert_path = root / 'must-stay-inert'
            callable_owner = (E('DbFreshV1', E('fs:write', str(inert_path), 'not-data'),
                                caps[2], current[3], S('DbWholeV1')),)
            assert publish(first, source, 'callable-owner', (), dependencies=callable_owner) == E('DbRefusedV1', S('dependency-owner-or-scope'))
            assert not inert_path.exists(), 'dependency data executed as a server call'
            other_read = (E('DbFreshV1', caps[1], caps[2], current[3], E('DbCollectionV1', S('other'))),)
            accepted = publish(first, source, 'other-collection-freshness', (), dependencies=other_read)
            assert accepted[0] == S('KnowledgeCommittedV1'), accepted
            assert publish(first, source, 'wrong-scope', (), collection=S('other')) == E('DbRefusedV1', S('dependency-owner-or-scope'))
            scan = cursor(first, source)
            clone = first.route('DbCandidateClone', E('DbCandidateCloneV1', S('current')), scan)[1]
            assert second.route('DbCandidateNext', E('DbCandidateNextV1', 0, 1), scan, ok=False) == E('ServiceRefusedV1', S('unknown-resource'))
            first.drop(source); first.drop(scan)
            rows = drain(first, clone)
            assert Counter(encode(row[0]) for row in rows) == Counter(encode(fact) for fact in facts)
            assert len({encode(row[1]) for row in rows}) == len(facts)
            rewound = first.route('DbCandidateClone', E('DbCandidateCloneV1', S('rewind')), clone)[1]
            assert drain(first, rewound) == rows
            assert first.route('DbCandidateCancel', (), rewound) == S('StoreCandidateCancelled')
            assert first.route('DbCandidateNext', E('DbCandidateNextV1', 0, 1), rewound) == S('StoreCandidateCancelled')
            first.drop(rewound); first.drop(clone)
            source, _ = capture(first)
            selected = cursor(first, source, E('rel', S('one'), Var(0)))
            selected_rows = drain(first, selected)
            rels = [row[0] for row in selected_rows if isinstance(row[0], tuple) and row[0][0] == S('rel')]
            assert rels == [E('rel', Var(0), Var(0))], rels
            first.drop(selected); first.drop(source); first.drop(empty)
            results['original_facts_duplicates_variables_chunks_and_retained_clones'] = True

            # A read-only peer is attenuated before any write handler runs.
            assert second.call(E('ReadOnlyV1')) == S('ServiceReadOnlyV1')
            assert second.route('DbCapabilities')[4] == S('ReadOnly')
            source, _ = capture(second)
            assert second.route('DbPublish', E('DbPublishV1', S('data'), 'ro', S('operation'), (), (), ()), source, ok=False) == E('ServiceRefusedV1', S('read-only'))
            second.drop(source)
            results['peer_rights_and_dependency_owner_scope'] = True

            proxy = DropReplyProxy(root/'drop.sock', server.path); lost = Client(proxy.path)
            token, _ = capture(lost); proxy.drop.set()
            lost_change = (E('SpaceInsertV1', 0, E('lost', S('committed'))),)
            try:
                publish(lost, token, 'lost-reply', lost_change, values=(S('once'),))
                raise AssertionError('proxy delivered the deliberately dropped reply')
            except (AssertionError, ConnectionError, OSError) as error:
                assert 'deliberately dropped' not in str(error)
            finally:
                lost.close(); proxy.join()
            assert proxy.observed.is_set()
            assert first.route('DbResolve', E('DbResolveV1', S('data'), 'lost-reply', S('operation'))) == E('db:resolved', 'lost-reply', (S('once'),))
            results['publication_before_lost_reply_resolved'] = True
            incarnation = caps[2]
            first.close(); first = None; second.close(); second = None
            server.stop(crash=True); server.start(); first = Client(server.path)
            assert first.route('DbCapabilities')[2] != incarnation
            assert first.route('DbResolve', E('DbResolveV1', S('data'), 'lost-reply', S('operation'))) == E('db:resolved', 'lost-reply', (S('once'),))
            token, _ = capture(first); scan = cursor(first, token); rows = drain(first, scan)
            assert len(rows) == len(facts)+1
            first.drop(scan); first.drop(token)
            results['restart_preserves_facts_and_receipts'] = True
        finally:
            if first: first.close()
            if second: second.close()
            server.stop()
        if args.act_fixture:
            image = root/'facts.cact'
            subprocess.run([str(args.act_fixture.resolve()), 'write', str(image)], cwd=ROOT, check=True, capture_output=True)
            act_root = root/'act'; act_root.mkdir(mode=0o700)
            act = server_for(worker, act_root, image); client = None
            try:
                act.start(); client = Client(act.path)
                assert client.route('DbCapabilities')[3:5] == (S('act'), S('ReadOnly'))
                token, meta = capture(client); scan = cursor(client, token); client.drop(token)
                assert len(drain(client, scan)) == meta[5] == 17
                client.drop(scan); token, _ = capture(client)
                assert publish(client, token, 'file-write', ()) == E('DbRefusedV1', S('read-only'))
                client.drop(token); results['standalone_act_read_only_source'] = True
            finally:
                if client: client.close()
                act.stop()
    print(json.dumps(results, indent=2))

if __name__ == '__main__':
    main()
