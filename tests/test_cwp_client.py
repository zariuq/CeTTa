#!/usr/bin/env python3
"""lib/cwp from MeTTa against the real channel service and a local Bot API.

A MeTTa program reads a delivery, acknowledges it, submits a send and reads
its receipt, all through cwp:call. An absent socket is reported as such, and
a socket that never answers is abandoned at the timeout.
"""
import json
import pathlib
import signal
import socket
import subprocess
import sys
import tempfile
import time

from test_telegram_transport import TOKEN, Peer, peer

SERVICE = str(pathlib.Path(sys.argv[1]).resolve())
CETTA = str(pathlib.Path(sys.argv[2]).resolve())
ROOT = pathlib.Path(__file__).resolve().parents[1]


class BotAPI(Peer):
    def __init__(self, *args):
        self.updates, self.sent = [], []
        super().__init__(*args)

    def receipt(self, connection, method, path, body):
        data = json.loads(body)
        if path.endswith('/getUpdates'):
            with self.lock:
                updates = [u for u in self.updates if u['update_id'] >= data['offset']]
            return 200, [], json.dumps({'ok': True, 'result': updates}).encode()
        with self.lock:
            self.sent.append((data['chat_id'], data['text']))
        return 200, [], json.dumps({'ok': True, 'result': {'message_id': 77, 'chat': {'id': data['chat_id']}}}).encode()


def metta(program, *modes):
    for mode in modes:
        with tempfile.NamedTemporaryFile('w', suffix='.metta', delete=False) as f:
            f.write(program)
        args = [CETTA, *(['--lang', mode] if mode != 'he' else []), f.name]
        p = subprocess.run(args, capture_output=True, text=True, timeout=60, cwd=ROOT)
        pathlib.Path(f.name).unlink()
        # HE prints each result set in brackets; PeTTa prints the result.
        lines = [line[1:-1] if line.startswith('[') and line.endswith(']') else line
                 for line in p.stdout.strip().splitlines()]
        yield mode, lines, p.stderr


with tempfile.TemporaryDirectory(prefix='cetta-cwp-') as temp:
    root = pathlib.Path(temp)
    token = root / 'token'; token.write_text(TOKEN); token.chmod(0o600)
    state = root / 'state'; state.mkdir(mode=0o700)
    path = root / 'fixture.sock'
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    listener.bind(str(path)); listener.listen(16); path.chmod(0o600)
    silent = root / 'silent.sock'
    mute = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    mute.bind(str(silent)); mute.listen(16)
    with peer('http/1.1', None, None, BotAPI) as (api, origin):
        proc = subprocess.Popen([SERVICE, '--run', '--root', str(ROOT), '--state-dir', str(state), '--worker', 'fixture',
                                 '--chat', '42', '--program', 'channel', '--credential-file', str(token),
                                 '--mock-origin', origin, '--listener-fd', str(listener.fileno())],
                                pass_fds=(listener.fileno(),), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            with api.lock:
                api.updates.append({'update_id': 5, 'message': {'message_id': 5, 'chat': {'id': 42, 'type': 'private'},
                                    'from': {'id': 9}, 'text': 'hello'}})
            until = time.monotonic() + 15
            probe = f'!(import! &self cwp)\n!(cwp:next "{path}" "")\n'
            while True:
                _, out, err = next(metta(probe, 'he'))
                if out and '(cwp:reply 65 ' in out[-1]:
                    break
                assert time.monotonic() < until, (out, err)
                time.sleep(.1)
            for mode, out, err in metta(f'''!(import! &self cwp)
!(import! &self json)
!(let (cwp:reply 65 $task $body) (cwp:next "{path}" "") (cwp:result "{path}" $task "[]"))
!(cwp:next "{path}" "")
!(cwp:submit "{path}" "42.0.00000000000000000001" "[[\\"send\\",\\"from metta\\",\\"plain\\"]]")
''', 'he'):
                assert (out[-3].startswith('(cwp:reply 66 ') and out[-2] == '(cwp:reply 64 "" "")' and
                        out[-1] == '(cwp:reply 66 "42.0.00000000000000000001" "")'), (mode, out, err)
            until = time.monotonic() + 15
            while not api.sent:
                assert time.monotonic() < until
                time.sleep(.05)
            assert api.sent == [(42, 'from metta')], api.sent
            receipt = []
            until = time.monotonic() + 15
            while not receipt:
                _, out, err = next(metta(probe, 'petta'))
                if out and '(cwp:reply 65 ' in out[-1] and 'delivery' in out[-1]:
                    receipt = out[-1]
                assert time.monotonic() < until, (out, err)
                time.sleep(.1)
            assert '[\\"delivered\\",77]' in receipt, receipt
            # Unreachable and silent services, in both languages.
            for mode, out, err in metta(f'!(import! &self cwp)\n!(cwp:next "{root}/absent.sock" "")\n', 'he', 'petta'):
                assert out[-1] == '(cwp:unavailable absent)', (mode, out, err)
            started = time.monotonic()
            for mode, out, err in metta(f'!(import! &self cwp)\n!(cwp:call "{silent}" 1 "" "" 300)\n', 'he'):
                assert out[-1] == '(cwp:unavailable timeout)', (mode, out, err)
            assert time.monotonic() - started < 5
        finally:
            proc.send_signal(signal.SIGTERM); proc.communicate(timeout=15)
            listener.close(); mute.close()
    print('cwp client: delivery read, acknowledged, send submitted and receipted from MeTTa (HE and PeTTa); '
          'absent and silent services reported within the timeout')
