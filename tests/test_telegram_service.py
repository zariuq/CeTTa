#!/usr/bin/env python3
"""Production entry, inert validation, private IPC and real mock-backed recovery."""
import json
import os
import pathlib
import signal
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time

from test_telegram_transport import TOKEN, Peer, certificate, peer

BIN = str(pathlib.Path(sys.argv[1]).resolve())
ROOT = pathlib.Path(__file__).resolve().parents[1]


class ServicePeer(Peer):
    def __init__(self, *args):
        self.updates = []
        super().__init__(*args)

    def add(self, update, chat=42):
        with self.lock:
            self.updates.append({'update_id': update, 'message': {'message_id': update,
                'chat': {'id': chat, 'type': 'private'}, 'from': {'id': 7}, 'text': 'synthetic request'}})

    def receipt(self, connection, method, path, body):
        assert method == 'POST'
        data = json.loads(body)
        if path.endswith('/getUpdates'):
            with self.lock:
                self.calls.append(('poll', data['offset']))
                updates = [u for u in self.updates if u['update_id'] >= data['offset']]
            return 200, [], json.dumps({'ok': True, 'result': updates}).encode()
        assert path == f'/bot{TOKEN}/sendMessage'
        assert data['chat_id'] == 42 and set(data) == {'chat_id', 'text'}
        with self.lock:
            self.calls.append(('send', data['text']))
        return 200, [], json.dumps({'ok': True, 'result': {'message_id': 10, 'chat': {'id': 42}}}).encode()


def rows(database, space):
    try:
        with sqlite3.connect(f'file:{database}?mode=ro', uri=True) as c:
            return dict(c.execute('SELECT key,value FROM records WHERE space=?', (space,)))
    except sqlite3.OperationalError:
        return {}


def wait(proc, predicate, label):
    until = time.monotonic() + 15
    while time.monotonic() < until:
        if predicate():
            return
        if proc.poll() is not None:
            out, err = proc.communicate()
            assert TOKEN.split(':')[1] not in out + err
            raise AssertionError((label, proc.returncode, out, err))
        time.sleep(.025)
    raise AssertionError(label)


def rpc(path, code=1, task='', body=''):
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as s:
        s.settimeout(5)
        s.connect(str(path))
        name = task.encode()
        s.sendall(b'CWP1' + bytes([code, len(name)]) + name + body.encode())
        r = s.recv(65606)
        assert r[:4] == b'CWP1'
        n = r[5]
        return r[4], r[6:6+n].decode(), r[6+n:].decode()


with tempfile.TemporaryDirectory(prefix='cetta-service-entry-') as temp:
    root = pathlib.Path(temp)
    token = root / 'token'
    token.write_text(TOKEN)
    token.chmod(0o600)
    state = root / 'state'
    state.mkdir(mode=0o700)
    cert, key = certificate(root, 'local', '127.0.0.1')
    base = ['--root', str(ROOT), '--state-dir', str(state), '--worker', 'brain', '--chat', '42', '--credential-file', str(token)]

    def check(args, expected, fds=()):
        p = subprocess.run([BIN, *args], cwd=root, pass_fds=fds, capture_output=True, text=True, timeout=12)
        assert TOKEN.split(':')[1] not in p.stdout + p.stderr
        assert p.returncode == expected, (args, p.returncode, p.stdout, p.stderr)
        return p

    # Validation has no journal, listener or network side effects.
    check(['--check', *base], 0)
    assert not list(state.iterdir())
    for suffix in (['--run'], ['--durable-admin'], ['--chat', '42'], ['--chat', '0'],
                   ['--chat', '9223372036854775808'], ['--worker', 'another'],
                   ['--mock-origin', 'https://example.com'], ['--mock-origin', 'http://127.0.0.1:1/path'],
                   ['--ca-file', str(cert)], ['--credential-fd', '3']):
        # Bad numeric loopback syntax is rejected by the credential adapter.
        check(['--check', *base, *suffix], 78 if suffix[-1].endswith('/path') else 64)
    check(['--run', *base], 78)
    assert not list(state.iterdir())
    token.chmod(0o644)
    check(['--check', *base], 78)
    token.chmod(0o600)
    state.chmod(0o755)
    check(['--check', *base], 78)
    # The owner's group may be given access; others never.
    token.chmod(0o640); state.chmod(0o770)
    check(['--check', *base], 0)
    token.chmod(0o600); state.chmod(0o700)
    linked = root / 'token-link'; linked.symlink_to(token)
    check(['--check', *base[:-1], str(linked)], 78)
    with token.open('rb') as f:
        fdargs = base[:-2] + ['--credential-fd', str(f.fileno())]
        check(['--check', *fdargs], 0, (f.fileno(),))
    assert not list(state.iterdir())
    # A bogus activation environment never substitutes for a passed listener.
    activation = dict(os.environ, LISTEN_PID='1', LISTEN_FDS='1')
    p = subprocess.run([BIN, '--run', *base], cwd=root, env=activation,
                       capture_output=True, text=True, timeout=12)
    assert p.returncode == 78 and not list(state.iterdir())

    for i, protocol in enumerate(('http/1.1', 'http/1.1', 'h2')):
        state = root / f'state-{i}'; state.mkdir(mode=0o700)
        db = state / 'journal.db'
        tls = i > 0
        with peer(protocol, cert if tls else None, key if tls else None, ServicePeer) as (server, origin):
            path = root / f'worker-{i}.sock'
            listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            listener.bind(str(path)); listener.listen(16); path.chmod(0o600)
            args = ['--run', '--root', str(ROOT), '--state-dir', str(state), '--worker', 'brain',
                    '--chat', '42', '--credential-file', str(token), '--mock-origin', origin,
                    '--listener-fd', str(listener.fileno())]
            if tls:
                args += ['--ca-file', str(cert)]
            processes = []

            def start(override=None, activated=False):
                command = [BIN, *(override or args)]
                if activated:
                    # Exactly the systemd fd3/PID/count protocol, without
                    # installing units or contacting a service manager.
                    command = [sys.executable, '-c',
                        'import os,sys; fd=int(sys.argv[1]); os.dup2(fd,3); os.set_inheritable(3,True); '
                        'os.environ.update(LISTEN_PID=str(os.getpid()),LISTEN_FDS="1"); '
                        'os.execv(sys.argv[2],sys.argv[2:])', str(listener.fileno()),
                        BIN, *args[:args.index('--listener-fd')], *args[args.index('--listener-fd')+2:]]
                p = subprocess.Popen(command, cwd=root, pass_fds=(listener.fileno(),),
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                processes.append(p)
                return p

            def stop(p, kill=False):
                p.send_signal(signal.SIGKILL if kill else signal.SIGTERM)
                out, err = p.communicate(timeout=10)
                assert TOKEN.split(':')[1] not in out + err
                assert p.returncode == (-signal.SIGKILL if kill else 0), (p.returncode, out, err)

            try:
                # Insecure listener refused before journal creation or network.
                path.chmod(0o666)
                check(args, 78, (listener.fileno(),))
                assert not db.exists() and not server.calls
                path.chmod(0o600)
                check(['--check', *args[1:]], 0, (listener.fileno(),))
                assert not db.exists() and not server.calls
                p = start(activated=True)
                server.add(41)
                wait(p, lambda: len(rows(db, 'host.worker-tasks')) == 1, 'first task')
                task = rpc(path)
                assert task[0] == 65 and json.loads(task[2])['update_id'] == 41
                # No worker is connected during restart; the same task survives.
                stop(p, True)
                p = start()
                wait(p, lambda: any(k == 'poll' for k, _ in server.calls), 'polling')
                resumed = rpc(path)
                assert resumed == task
                result = json.dumps([['send', 'once', 'plain']])
                assert rpc(path, 2, task[1], result)[0] == 66
                wait(p, lambda: ('send', 'once') in server.calls and len(rows(db, 'host.outcomes')) == 1, 'send outcome')
                stop(p)
                before = list(server.calls)
                # Journal cannot be silently reassigned to a different worker/bot.
                other = args.copy(); other[other.index('--worker')+1] = 'another'
                check(other, 78, (listener.fileno(),))
                token.write_text('999999:' + TOKEN.split(':')[1])
                check(args, 78, (listener.fileno(),))
                assert server.calls == before
                token.write_text(TOKEN)
                p = start()
                wait(p, lambda: rpc(path, 3, task[1])[0] == 66, 'stored receipt after restart')
                assert rpc(path, 2, task[1], result)[0] == 66
                time.sleep(.2)
                assert server.calls.count(('send', 'once')) == 1
                stop(p)
            finally:
                for p in processes:
                    if p.poll() is None:
                        p.kill(); p.communicate(timeout=5)
                listener.close()
        print(f'{"HTTPS" if tls else "HTTP"}/{protocol}: production entry, private configuration, worker/service recovery and identity binding passed')
