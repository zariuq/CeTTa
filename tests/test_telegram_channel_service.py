#!/usr/bin/env python3
"""telegram-channel/1 through the real service entry and a local Bot API.

A client in the role of an agent's own loop reads deliveries in batches,
acknowledges them, submits keyed actions (also unprompted, to another
chat), and reads delivery receipts. A SIGKILL between submission and
delivery never repeats a send; a lost response does not block later messages.
"""
import json
import pathlib
import signal
import socket
from support.durable_journal_fixture import records as native_records
import subprocess
import sys
import tempfile
import time

from test_telegram_transport import TOKEN, Peer, certificate, peer

BIN = str(pathlib.Path(sys.argv[1]).resolve())
ROOT = pathlib.Path(__file__).resolve().parents[1]
NEXT, RESULT, RECEIPT, SUBMIT = 1, 2, 3, 4
IDLE, TASK, STORED, UNKNOWN, CONFLICT = 64, 65, 66, 68, 69


class ChannelPeer(Peer):
    def __init__(self, *args):
        self.updates, self.sent, self.message = [], [], 100
        super().__init__(*args)

    def add(self, update, chat, sender):
        with self.lock:
            self.updates.append({'update_id': update, 'message': {'message_id': update,
                'chat': {'id': chat, 'type': 'private'}, 'from': {'id': sender}, 'text': f'message {update}'}})

    def receipt(self, connection, method, path, body):
        assert method == 'POST'
        data = json.loads(body)
        if path.endswith('/getUpdates'):
            with self.lock:
                updates = [u for u in self.updates if u['update_id'] >= data['offset']]
            return 200, [], json.dumps({'ok': True, 'result': updates}).encode()
        assert path == f'/bot{TOKEN}/sendMessage', path
        with self.lock:
            self.sent.append((data['chat_id'], data['text']))
            self.message += 1
            message = self.message
        if data['text'] == 'lost reply':
            return None  # Accepted by the server; the response never arrives.
        return 200, [], json.dumps({'ok': True, 'result': {'message_id': message,
            'chat': {'id': data['chat_id']}}}).encode()


def rows(database, space):
    return native_records(database, space)


def wait(proc, predicate, label, seconds=15):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        if predicate():
            return
        if proc.poll() is not None:
            out, err = proc.communicate()
            assert TOKEN.split(':')[1] not in out + err
            raise AssertionError((label, proc.returncode, out, err))
        time.sleep(.025)
    raise AssertionError(label)


def rpc(path, code=NEXT, task='', body=''):
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as s:
        s.settimeout(5)
        s.connect(str(path))
        name = task.encode()
        s.sendall(b'CWP1' + bytes([code, len(name)]) + name + body.encode())
        r = s.recv(65606)
        assert r[:4] == b'CWP1'
        n = r[5]
        return r[4], r[6:6+n].decode(), r[6+n:].decode()


def next_task(proc, path, label):
    """Wait for the oldest pending task, return (id, observation)."""
    found = []
    def ready():
        r = rpc(path)
        if r[0] == TASK:
            found.append(r)
            return True
        assert r[0] == IDLE, r
        return False
    wait(proc, ready, label)
    return found[0][1], json.loads(found[0][2])


def ack(path, task):
    assert rpc(path, RESULT, task, '[]')[0] == STORED


def submit(path, key, commands):
    return rpc(path, SUBMIT, key, json.dumps(commands))[0]


with tempfile.TemporaryDirectory(prefix='cetta-telegram-channel-') as temp:
    root = pathlib.Path(temp)
    token = root / 'token'
    token.write_text(TOKEN)
    token.chmod(0o600)
    cert, key = certificate(root, 'local', '127.0.0.1')
    state = root / 'state-check'; state.mkdir(mode=0o700)
    base = ['--root', str(ROOT), '--state-dir', str(state), '--worker', 'fixture', '--chat', '42',
            '--credential-file', str(token), '--program', 'channel']
    p = subprocess.run([BIN, '--check', *base], capture_output=True, text=True, timeout=12)
    assert p.returncode == 0 and 'telegram-channel/1' in p.stdout, (p.stdout, p.stderr)
    for bad in (['--program', 'channel'], ['--program', 'other'], ['--operator', '0']):
        p = subprocess.run([BIN, '--check', *base, *bad], capture_output=True, text=True, timeout=12)
        assert p.returncode == 64, (bad, p.returncode)

    # A refused connection never carried the request: reported not-sent, and
    # the chat keeps going, instead of holding for an operator.
    closed = socket.socket(); closed.bind(('127.0.0.1', 0)); port = closed.getsockname()[1]; closed.close()
    state = root / 'state-refused'; state.mkdir(mode=0o700)
    path = root / 'refused.sock'
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    listener.bind(str(path)); listener.listen(16); path.chmod(0o600)
    proc = subprocess.Popen([BIN, '--run', '--root', str(ROOT), '--state-dir', str(state), '--worker', 'fixture',
                             '--chat', '42', '--program', 'channel', '--credential-file', str(token),
                             '--mock-origin', f'http://127.0.0.1:{port}', '--listener-fd', str(listener.fileno())],
                            pass_fds=(listener.fileno(),), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        for n in (1, 2):
            assert submit(path, '42.0.%020d' % n, [['send', 'unreachable %d' % n, 'plain']]) == STORED
            task, receipt = next_task(proc, path, 'refused receipt %d' % n)
            assert receipt == ['delivery', '42.0.%020d' % n, 0, 1, ['not-sent']], receipt
            ack(path, task)
    finally:
        proc.send_signal(signal.SIGTERM); proc.communicate(timeout=10); listener.close()
    print('refused connection: reported not-sent and the chat continues')

    for i, protocol in enumerate(('http/1.1', 'http/1.1', 'h2')):
        state = root / f'state-{i}'; state.mkdir(mode=0o700)
        db = state / 'journal.db'
        tls = i > 0
        with peer(protocol, cert if tls else None, key if tls else None, ChannelPeer) as (server, origin):
            path = root / f'fixture-{i}.sock'
            listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            listener.bind(str(path)); listener.listen(16); path.chmod(0o600)
            args = ['--run', '--root', str(ROOT), '--state-dir', str(state), '--worker', 'fixture',
                    '--chat', '42', '--chat', '84', '--operator', '7', '--program', 'channel',
                    '--credential-file', str(token), '--mock-origin', origin,
                    '--listener-fd', str(listener.fileno())]
            if tls:
                args += ['--ca-file', str(cert)]
            processes = []

            def start():
                proc = subprocess.Popen([BIN, *args], pass_fds=(listener.fileno(),),
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                processes.append(proc)
                return proc

            def stop(proc, kill=False):
                proc.send_signal(signal.SIGKILL if kill else signal.SIGTERM)
                out, err = proc.communicate(timeout=10)
                assert TOKEN.split(':')[1] not in out + err
                assert proc.returncode == (-signal.SIGKILL if kill else 0), (proc.returncode, out, err)

            try:
                # Hand over from another poller that handled updates up to 40:
                # the journal's first cursor starts at 41, so 40 is never read.
                server.add(40, 42, 5)
                args += ['--initial-offset', '41']
                proc = start()
                # Deliveries carry lane, kind and role; the operator is recognized.
                server.add(41, 42, 7)
                first, observation = next_task(proc, path, 'first delivery')
                assert observation[:4] == ['input', '42.0', 'message', 'operator'], observation
                assert observation[4]['update_id'] == 41 and observation[4]['message']['text'] == 'message 41'
                # Several arrivals are read as one batch before any answer.
                server.add(42, 84, 5); server.add(43, 42, 5)
                wait(proc, lambda: len(rows(db, 'host.worker-tasks')) == 3, 'three deliveries')
                code, second, text = rpc(path, NEXT, first)
                assert code == TASK and json.loads(text)[:4] == ['input', '84.0', 'message', 'ordinary']
                code, third, text = rpc(path, NEXT, second)
                assert code == TASK and json.loads(text)[4]['update_id'] == 43
                assert rpc(path, NEXT, third)[0] == IDLE
                for task in (first, second, third):
                    ack(path, task)
                assert rpc(path, NEXT)[0] == IDLE
                # A keyed reply, then an unprompted message to another chat.
                assert submit(path, '42.0.00000000000000000001', [['send', 'hello', 'plain']]) == STORED
                task, receipt = next_task(proc, path, 'reply receipt')
                assert receipt[:4] == ['delivery', '42.0.00000000000000000001', 0, 1], receipt
                assert receipt[4][0] == 'delivered' and receipt[4][1] > 100, receipt
                ack(path, task)
                assert submit(path, '84.0.00000000000000000001', [['send', 'unprompted', 'plain']]) == STORED
                task, receipt = next_task(proc, path, 'unprompted receipt')
                assert receipt[:4] == ['delivery', '84.0.00000000000000000001', 0, 1] and receipt[4][0] == 'delivered'
                ack(path, task)
                assert server.sent == [(42, 'hello'), (84, 'unprompted')], server.sent
                # A seeded cursor is only a starting point: a later start never moves it.
                args[args.index('--initial-offset')+1] = '0'
                # SIGKILL right after a submission; the resubmission after restart
                # is the same input, so the message goes out exactly once.
                body = [['send', 'survives', 'plain']]
                assert submit(path, '42.0.00000000000000000002', body) == STORED
                stop(proc, True)
                proc = start()
                assert submit(path, '42.0.00000000000000000002', body) == STORED
                assert submit(path, '42.0.00000000000000000002', [['send', 'changed', 'plain']]) == CONFLICT
                task, receipt = next_task(proc, path, 'receipt after restart')
                assert receipt[:4] == ['delivery', '42.0.00000000000000000002', 0, 1] and receipt[4][0] == 'delivered'
                ack(path, task)
                assert server.sent.count((42, 'survives')) == 1
                # A chat outside the native policy never reaches the network.
                assert submit(path, '99.0.00000000000000000001', [['send', 'forbidden', 'plain']]) == STORED
                task, receipt = next_task(proc, path, 'rejection')
                assert receipt == ['rejected', '99.0.00000000000000000001', 'action-not-permitted'], receipt
                ack(path, task)
                # A lost response stays uncertain without holding any later message.
                assert submit(path, '42.0.00000000000000000003', [['send', 'lost reply', 'plain']]) == STORED
                task, receipt = next_task(proc, path, 'uncertain receipt')
                assert receipt[:4] == ['delivery', '42.0.00000000000000000003', 0, 1] and receipt[4][0] == 'uncertain', receipt
                ack(path, task)
                assert submit(path, '42.0.00000000000000000004', [['send', 'following message', 'plain']]) == STORED
                assert submit(path, '84.0.00000000000000000002', [['send', 'other chat', 'plain']]) == STORED
                received = set()
                for _ in range(2):
                    task, receipt = next_task(proc, path, 'following receipt')
                    assert receipt[4][0] == 'delivered', receipt
                    received.add(receipt[1])
                    ack(path, task)
                assert received == {'42.0.00000000000000000004','84.0.00000000000000000002'}
                assert (42, 'following message') in server.sent
                assert server.sent.count((42, 'lost reply')) == 1
                # Restart and resubmit the original occurrence: it must not send twice.
                stop(proc)
                proc = start()
                assert submit(path, '42.0.00000000000000000003', [['send', 'lost reply', 'plain']]) == STORED
                time.sleep(.2)
                assert server.sent.count((42, 'lost reply')) == 1
                assert all(chat in (42, 84) for chat, _ in server.sent)
                deliveries = [t for t in rows(db, 'host.worker-tasks').values() if b'"input"' in t]
                assert not any(b'"update_id":40' in t for t in deliveries) and len(deliveries) == 3, deliveries
                stop(proc)
                # The journal belongs to this program: the agent cannot adopt it.
                other = [a if a != 'channel' else 'agent' for a in args]
                p = subprocess.run([BIN, *other], pass_fds=(listener.fileno(),), capture_output=True,
                                   text=True, timeout=12)
                assert p.returncode == 78, (p.returncode, p.stdout, p.stderr)
            finally:
                for proc in processes:
                    if proc.poll() is None:
                        proc.kill(); proc.communicate(timeout=5)
                listener.close()
        print(f'{"HTTPS" if tls else "HTTP"}/{protocol}: channel deliveries, batched reads, keyed and unprompted sends, '
              f'receipts, restart without repetition, forbidden chat and uncertainty without blocking passed')
