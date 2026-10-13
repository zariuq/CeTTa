#!/usr/bin/env python3
"""Operator commands through the real service entry and a local Bot API.

The guarantee under test: every operator command is answered within its
deadline, whatever the agent does, because the service answers it and the
agent is only asked. The agent here is first absent (nothing reads its
socket), then present. Commands are also sent while the chat's own lane is
held by an uncertain send and while the agent floods the service with
submissions to another chat. A delegated command's late answer edits the
service's notice; stop and start take effect where sends are dispatched.
Taps on menu buttons race the same deadline: the tap is always answered in
time, by the agent's answer or the service's, and the answer redraws the menu.
"""
import json
import pathlib
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time

from test_telegram_transport import TOKEN, Peer, peer

BIN = str(pathlib.Path(sys.argv[1]).resolve())
ROOT = pathlib.Path(__file__).resolve().parents[1]
NEXT, RESULT, RECEIPT, SUBMIT = 1, 2, 3, 4
IDLE, TASK, STORED, LIMIT = 64, 65, 66, 72
DEADLINE_MS = 800
# Generous bounds for a loaded test machine; the measured latencies are
# printed. A reply must never wait for the agent, only for the service.
SERVICE_BOUND = 1.5
FALLBACK_BOUND = DEADLINE_MS / 1000 + 1.5
OPERATOR, CHAT, OTHER = 7, 42, 84

COMMANDS = '''
(tg-cmd:command "/help" help "list these commands")
(tg-cmd:command "/stop" stop "stop the agent's sends")
(tg-cmd:command "/start" start "resume the agent's sends")
(tg-cmd:command "/engine" delegated "show or switch the engine")
(tg-cmd:agent "Ada")
'''
FALLBACK = 'No answer from Ada yet. This message will be updated when it arrives.'
TAP_FALLBACK = 'No answer from Ada yet.'
MENU_MESSAGE = 900
MENU = ['menu', 'mode: iter', 'modes', [[['● iter', 'mode:iter'], ['agent', 'mode:agent']]]]
KEYBOARD = {'inline_keyboard': [[{'text': '● iter', 'callback_data': 'mode:iter'},
                                 {'text': 'agent', 'callback_data': 'mode:agent'}]]}


class BotAPI(Peer):
    def __init__(self, *args):
        self.updates, self.calls, self.message = [], [], 1000
        super().__init__(*args)

    def command(self, update, chat, text, sender=OPERATOR):
        with self.lock:
            assert not self.updates or update > self.updates[-1]['update_id'], update
            self.updates.append({'update_id': update, 'message': {'message_id': 500 + update,
                'chat': {'id': chat, 'type': 'private'}, 'from': {'id': sender}, 'text': text}})
        return time.monotonic()

    def tap(self, update, chat, data, sender=OPERATOR):
        with self.lock:
            assert not self.updates or update > self.updates[-1]['update_id'], update
            self.updates.append({'update_id': update, 'callback_query': {'id': 'q%d' % update,
                'from': {'id': sender}, 'data': data,
                'message': {'message_id': MENU_MESSAGE, 'chat': {'id': chat, 'type': 'private'}}}})
        return time.monotonic()

    def receipt(self, connection, method, path, body):
        data = json.loads(body)
        if path.endswith('/getUpdates'):
            with self.lock:
                updates = [u for u in self.updates if u['update_id'] >= data['offset']]
            return 200, [], json.dumps({'ok': True, 'result': updates}).encode()
        name = path.rsplit('/', 1)[1]
        with self.lock:
            self.message += 1
            message = self.message if name == 'sendMessage' else data.get('message_id')
            self.calls.append((time.monotonic(), name, data, message))
        if name == 'answerCallbackQuery':
            return 200, [], json.dumps({'ok': True, 'result': True}).encode()
        if name == 'getMe':
            return 200, [], json.dumps({'ok': True, 'result': {'id': int(TOKEN.split(':')[0]), 'is_bot': True,
                                                                'username': 'AdaTestBot'}}).encode()
        if data.get('text') == 'lost reply':
            return None
        if data.get('text') == 'slow':
            time.sleep(1.2)
        return 200, [], json.dumps({'ok': True, 'result': {'message_id': message,
            'chat': {'id': data['chat_id']}}}).encode()

    def tap_answers(self, update):
        with self.lock:
            return [c for c in self.calls if c[1] == 'answerCallbackQuery' and
                    c[2]['callback_query_id'] == 'q%d' % update]

    def redraws(self):
        with self.lock:
            return [c for c in self.calls if c[1] == 'editMessageText' and c[2]['message_id'] == MENU_MESSAGE]

    def replies_to(self, update):
        with self.lock:
            return [c for c in self.calls if c[1] == 'sendMessage' and
                    c[2].get('reply_parameters', {}).get('message_id') == 500 + update]


def rpc(path, code=NEXT, task='', body=''):
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as s:
        s.settimeout(5)
        s.connect(str(path))
        name = task.encode()
        s.sendall(b'CWP1' + bytes([code, len(name)]) + name + body.encode())
        r = s.recv(65606)
        n = r[5]
        return r[4], r[6:6+n].decode(), r[6+n:].decode()


def wait(proc, predicate, label, seconds=15):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        value = predicate()
        if value:
            return value
        if proc.poll() is not None:
            out, err = proc.communicate()
            raise AssertionError((label, proc.returncode, out, err))
        time.sleep(.01)
    raise AssertionError(label)


def tasks(path):
    """Every pending task, oldest first, answering none."""
    found, after = [], ''
    while True:
        code, task, body = rpc(path, NEXT, after)
        if code != TASK:
            return found
        found.append((task, json.loads(body)))
        after = task


def tap_answered(proc, api, update, sent, bound, label):
    """The one answer to a tap, and how long after the update it came."""
    calls = wait(proc, lambda: api.tap_answers(update), label, bound + 5)
    latency = calls[0][0] - sent
    assert latency <= bound, (label, latency)
    return calls[0], latency


def replied(proc, api, update, sent, bound, label):
    """The one reply to a command, and how long after the update it came."""
    calls = wait(proc, lambda: api.replies_to(update), label, bound + 5)
    latency = calls[0][0] - sent
    assert latency <= bound, (label, latency)
    return calls[0], latency


with tempfile.TemporaryDirectory(prefix='cetta-telegram-commands-') as temp:
    root = pathlib.Path(temp)
    token = root / 'token'; token.write_text(TOKEN); token.chmod(0o600)
    commands = root / 'commands.metta'; commands.write_text(COMMANDS)
    state = root / 'state-check'; state.mkdir(mode=0o700)
    base = ['--root', str(ROOT), '--state-dir', str(state), '--worker', 'ada', '--chat', str(CHAT),
            '--credential-file', str(token), '--program', 'channel']

    # Declarations only: anything else refuses to start.
    p = subprocess.run([BIN, '--check', *base, '--commands', str(commands)], capture_output=True, text=True, timeout=12)
    assert p.returncode == 0, (p.stdout, p.stderr)
    for bad in ('!(import! &self fs)\n', '(= (tg-cmd:kind $x) help)\n', '(tg-cmd:command "/Help" help "x")\n',
                '(tg-cmd:command "/a" delegated "x")\n(tg-cmd:command "/a" delegated "y")\n',
                '(tg-cmd:command "/stop" stop "x")\n', '(tg-cmd:command "/a" other "x")\n',
                '(tg-cmd:command "/a" delegated "say \\"hi\\"")\n'):
        wrong = root / 'wrong.metta'; wrong.write_text(bad)
        p = subprocess.run([BIN, '--check', *base, '--commands', str(wrong)], capture_output=True, text=True, timeout=12)
        assert p.returncode == 78, (bad, p.returncode, p.stderr)
    agent = [a if a != 'channel' else 'agent' for a in base]
    p = subprocess.run([BIN, '--check', *agent, '--commands', str(commands)], capture_output=True, text=True, timeout=12)
    assert p.returncode == 64, p.returncode
    print('command declarations: only declarations are accepted')

    state = root / 'state'; state.mkdir(mode=0o700)
    with peer('http/1.1', None, None, BotAPI) as (api, origin):
        path = root / 'ada.sock'
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        listener.bind(str(path)); listener.listen(16); path.chmod(0o600)
        proc = subprocess.Popen([BIN, '--run', '--root', str(ROOT), '--state-dir', str(state), '--worker', 'ada',
                                 '--chat', str(CHAT), '--chat', str(OTHER), '--operator', str(OPERATOR),
                                 '--program', 'channel', '--credential-file', str(token), '--mock-origin', origin,
                                 '--listener-fd', str(listener.fileno()), '--commands', str(commands),
                                 '--command-deadline-ms', str(DEADLINE_MS)],
                                pass_fds=(listener.fileno(),), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        latencies = {}
        try:
            wait(proc, lambda: rpc(path)[0] == IDLE, 'service ready')

            # The agent is absent: nothing reads its tasks. A service command is
            # answered at once, a delegated one with the notice at its deadline.
            sent = api.command(1, CHAT, '/help')
            call, latencies['help, agent absent'] = replied(proc, api, 1, sent, SERVICE_BOUND, 'help')
            assert call[2]['text'].startswith('/help — list these commands\n/stop'), call
            sent = api.command(2, CHAT, '/engine')
            call, latencies['delegated, agent absent'] = replied(proc, api, 2, sent, FALLBACK_BOUND, 'fallback')
            assert call[2]['text'] == FALLBACK and latencies['delegated, agent absent'] >= DEADLINE_MS / 1000 - .05
            fallback_2 = call[3]

            # The chat's own lane is held by an uncertain send; commands to that
            # chat do not wait behind it.
            assert rpc(path, SUBMIT, '%d.0.%020d' % (CHAT, 1), json.dumps([['send', 'lost reply', 'plain']]))[0] == STORED
            wait(proc, lambda: any(c[2].get('text') == 'lost reply' for c in api.calls), 'lost reply sent')
            assert rpc(path, SUBMIT, '%d.0.%020d' % (CHAT, 2), json.dumps([['send', 'held', 'plain']]))[0] == STORED
            sent = api.command(3, CHAT, '/help')
            _, latencies['help, lane held'] = replied(proc, api, 3, sent, SERVICE_BOUND, 'help behind held lane')
            sent = api.command(4, CHAT, '/engine cetta')
            call, latencies['delegated, lane held'] = replied(proc, api, 4, sent, FALLBACK_BOUND, 'fallback behind held lane')
            assert call[2]['text'] == FALLBACK
            fallback_4 = call[3]

            # The agent floods the service with sends to another chat.
            flood_stop = threading.Event()
            def flood():
                n = 10
                while not flood_stop.is_set():
                    n += 1
                    rpc(path, SUBMIT, '%d.0.%020d' % (OTHER, n), json.dumps([['send', 'flood %d' % n, 'plain']]))
            flooder = threading.Thread(target=flood, daemon=True); flooder.start()
            time.sleep(.3)
            sent = api.command(5, CHAT, '/help')
            _, latencies['help, flood'] = replied(proc, api, 5, sent, SERVICE_BOUND, 'help during flood')
            sent = api.command(6, CHAT, '/engine')
            _, latencies['delegated, flood'] = replied(proc, api, 6, sent, FALLBACK_BOUND, 'fallback during flood')
            # A tap while the agent is absent: the service answers it at the
            # deadline, and nothing redraws the menu yet.
            sent = api.tap(7, CHAT, 'mode:iter')
            call, latencies['tap, agent absent'] = tap_answered(proc, api, 7, sent, FALLBACK_BOUND, 'tap fallback')
            assert call[2]['text'] == TAP_FALLBACK and latencies['tap, agent absent'] >= DEADLINE_MS / 1000 - .05
            assert not api.redraws()
            # The absent agent's receipts outgrow its task queue: the service
            # keeps them waiting and keeps answering.
            def flood_sent():
                with api.lock:
                    return sum(1 for c in api.calls if c[2].get('chat_id') == OTHER)
            wait(proc, lambda: flood_sent() >= 400, 'flood past the task queue and the effect queue', 90)
            sent = api.command(8, CHAT, '/help')
            _, latencies['help, task queue full'] = replied(proc, api, 8, sent, SERVICE_BOUND, 'help with full task queue')
            flood_stop.set(); flooder.join(timeout=10)
            assert (CHAT, 'held') not in [(c[2].get('chat_id'), c[2].get('text')) for c in api.calls]

            # The agent arrives and answers the commands it was asked: each
            # late answer edits that command's own notice.
            # It reads its receipts; the questions that waited behind them
            # follow.
            questions, receipts = {}, set()
            def arrive():
                for task, value in tasks(path):
                    if value[0] in ('command', 'callback'):
                        questions[task] = value
                    else:
                        if value[0] == 'delivery':
                            receipts.add(value[1])
                        rpc(path, RESULT, task, '[]')
                return sum(v[0] == 'command' for v in questions.values()) == 3 and \
                    any(v[0] == 'callback' for v in questions.values())
            wait(proc, arrive, 'questions after receipts', 60)
            taps = [t for t in sorted(questions.items()) if t[1][0] == 'callback']
            assert [t[1][1:] for t in taps] == [['7', 'mode:iter', '42.0', MENU_MESSAGE]], taps
            assert rpc(path, RESULT, taps[0][0], json.dumps(MENU))[0] == STORED
            redrawn = wait(proc, api.redraws, 'late tap redraws the menu')
            assert redrawn[0][2]['text'] == 'modes' and redrawn[0][2]['reply_markup'] == KEYBOARD, redrawn
            assert len(api.tap_answers(7)) == 1
            asked = sorted(((t, v) for t, v in questions.items() if v[0] == 'command'), key=lambda t: int(t[1][1]))
            assert [t[1][1:] for t in asked] == [['2', '/engine', '', '42.0'], ['4', '/engine', 'cetta', '42.0'],
                                                 ['6', '/engine', '', '42.0']], asked
            for task, (_, key, name, args, lane) in asked:
                assert rpc(path, RESULT, task, json.dumps(['answer', 'engine for %s: cetta' % key]))[0] == STORED
            def edits():
                with api.lock:
                    return [c for c in api.calls if c[1] == 'editMessageText' and c[2]['message_id'] != MENU_MESSAGE]
            done = wait(proc, lambda: len(edits()) == 3 and edits(), 'three edits')
            assert {(c[2]['message_id'], c[2]['text']) for c in done} >= {(fallback_2, 'engine for 2: cetta'),
                                                                          (fallback_4, 'engine for 4: cetta')}, done

            # The agent catches up on everything that waited for it: a
            # receipt for every send of the flood, none lost while its task
            # queue was full.
            def caught_up():
                pending = tasks(path)
                for task, value in pending:
                    if value[0] == 'delivery':
                        receipts.add(value[1])
                    rpc(path, RESULT, task, '[]')
                return not pending and not tasks(path)
            wait(proc, caught_up, 'agent caught up', 60)
            flood_receipts = {key for key in receipts if key.startswith('%d.0.' % OTHER)}
            assert len(flood_receipts) == flood_sent(), (len(flood_receipts), flood_sent())

            # A present agent answers before the deadline: its answer is the
            # reply, and no notice is sent.
            responder_stop = threading.Event()
            tapped = []
            def respond():
                while not responder_stop.is_set():
                    for task, value in tasks(path):
                        if value[0] == 'callback':
                            tapped.append(value[1])
                        body = (json.dumps(['answer', 'engine: cetta']) if value[0] == 'command' else
                                json.dumps(MENU) if value[0] == 'callback' else '[]')
                        rpc(path, RESULT, task, body)
                    time.sleep(.01)
            responder = threading.Thread(target=respond, daemon=True); responder.start()
            sent = api.command(9, CHAT, '/engine')
            call, latencies['delegated, agent present'] = replied(proc, api, 9, sent, FALLBACK_BOUND, 'answer')
            assert call[2]['text'] == 'engine: cetta', call
            time.sleep(DEADLINE_MS / 1000 + .3)
            assert len(api.replies_to(9)) == 1 and not any(
                c[2].get('text') == FALLBACK for c in api.calls if c[2].get('reply_parameters', {}).get('message_id') == 509)

            # A present agent answers a tap before the deadline: its toast
            # answers the tap, its menu redraws the message, and the service
            # sends no fallback.
            before = len(api.redraws())
            sent = api.tap(10, CHAT, 'mode:iter')
            call, latencies['tap, agent present'] = tap_answered(proc, api, 10, sent, FALLBACK_BOUND, 'tap answer')
            assert call[2]['text'] == 'mode: iter', call
            wait(proc, lambda: len(api.redraws()) > before, 'menu redrawn')
            assert api.redraws()[-1][2]['reply_markup'] == KEYBOARD
            time.sleep(DEADLINE_MS / 1000 + .3)
            assert len(api.tap_answers(10)) == 1

            # Anyone else's tap is answered by the service at once and never
            # reaches the agent.
            sent = api.tap(11, CHAT, 'mode:agent', sender=5)
            call, latencies['tap, not an operator'] = tap_answered(proc, api, 11, sent, SERVICE_BOUND, 'tap refusal')
            assert call[2]['text'] == 'These buttons are for operators.'
            time.sleep(.3)
            assert tapped == ['10'], tapped

            # Stop and start take effect where sends are dispatched: an action
            # already accepted waits, a new submission is refused, and after
            # start the waiting action goes out. Their own lanes, not flooded.
            def texts():
                with api.lock:
                    return [c[2].get('text') for c in api.calls]
            batch = '%d.2.%020d' % (OTHER, 1)
            assert rpc(path, SUBMIT, batch, json.dumps([['send', 'slow', 'plain'], ['send', 'after slow', 'plain']]))[0] == STORED
            wait(proc, lambda: 'slow' in texts(), 'slow send started')
            sent = api.command(12, CHAT, '/stop')
            call, latencies['stop'] = replied(proc, api, 12, sent, SERVICE_BOUND, 'stop')
            assert call[2]['text'] == 'Stopped. Nothing Ada submits is sent until /start.'
            refused = '%d.1.%020d' % (OTHER, 1)
            assert rpc(path, SUBMIT, refused, json.dumps([['send', 'while stopped', 'plain']]))[0] == STORED
            time.sleep(1.8)
            assert 'after slow' not in texts() and 'while stopped' not in texts(), texts()[-5:]
            sent = api.command(13, CHAT, '/start')
            replied(proc, api, 13, sent, SERVICE_BOUND, 'start')
            wait(proc, lambda: 'after slow' in texts(), 'accepted action after start')
            assert rpc(path, SUBMIT, '%d.1.%020d' % (OTHER, 2), json.dumps([['send', 'after start', 'plain']]))[0] == STORED
            wait(proc, lambda: 'after start' in texts(), 'send after start')
            assert 'while stopped' not in texts()

            # The service learned its own username: a command addressed to it
            # is a command, one addressed to another bot is ordinary input.
            wait(proc, lambda: any(c[1] == 'getMe' for c in api.calls), 'getMe asked')
            time.sleep(.3)
            sent = api.command(15, CHAT, '/help@AdaTestBot')
            call, latencies['help addressed to this bot'] = replied(proc, api, 15, sent, SERVICE_BOUND, 'addressed help')
            assert call[2]['text'].startswith('/help — list these commands'), call
            api.command(16, CHAT, '/help@OtherBot')
            time.sleep(.5)
            assert not api.replies_to(16)
            # Someone who is not an operator: the same text is ordinary input.
            api.command(17, CHAT, '/help', sender=5)
            time.sleep(.5)
            assert not api.replies_to(17)

            responder_stop.set(); responder.join(timeout=10)
        finally:
            proc.send_signal(signal.SIGTERM)
            out, err = proc.communicate(timeout=15)
            assert TOKEN.split(':')[1] not in out + err
            listener.close()
    print('commands: ' + ', '.join('%s %.0f ms' % (k, v * 1000) for k, v in latencies.items()))
    print('operator commands: answered by the service within the deadline with the agent absent, behind a held '
          'lane and during a flood; late answers edit the notice; taps are answered in time and redraw their menus; '
          'a full task queue of an absent agent stops nothing; stop and start at dispatch; commands addressed to this bot by '
          'the username it looked up passed')
