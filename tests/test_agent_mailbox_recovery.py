"""Crash/replay qualification; mailbox operations themselves run in MeTTa/C."""
import argparse
import json
import os
import re
import selectors
import subprocess
import tempfile
import time
from pathlib import Path


def qualify(binary, service_binary, language="petta"):
    with tempfile.TemporaryDirectory(prefix="cetta-mail-recovery-") as directory:
        root = Path(directory)
        socket = root / "mail.sock"
        service = None
        children = []

        def start():
            nonlocal service
            service = subprocess.Popen(
                [str(service_binary), "--socket", str(socket), "--data", str(root / "store")],
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            children.append(service)
            deadline = time.monotonic() + 20
            while not socket.exists():
                assert service.poll() is None, service.communicate()[1]
                assert time.monotonic() < deadline, "service startup timed out"
                time.sleep(.01)

        def crash_service():
            service.kill()
            service.communicate(timeout=10)
            socket.unlink(missing_ok=True)
            start()

        def run(source, crash=False):
            path = root / "client.metta"
            path.write_text('!(import! &self mailbox)\n!(import! &self io)\n' + source)
            process = subprocess.Popen([str(binary), "--lang", language, str(path)],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            children.append(process)
            if crash:
                with selectors.DefaultSelector() as selector:
                    selector.register(process.stdout, selectors.EVENT_READ)
                    deadline = time.monotonic() + 20
                    pending = b""
                    reached = False
                    while not reached:
                        remaining = deadline - time.monotonic()
                        assert remaining > 0 and selector.select(remaining), \
                            "client did not reach its committed transition"
                        chunk = os.read(process.stdout.fileno(), 4096)
                        assert chunk, "client exited before its committed transition"
                        pending += chunk
                        while b"\n" in pending:
                            line, pending = pending.split(b"\n", 1)
                            if line.strip() == b"CRASH_POINT":
                                reached = True
                                break
                            # Prime prints the empty result of an import.
                            assert line.strip() in (b"", b"[()]"), line
                process.kill()
            output, error = process.communicate(timeout=20)
            assert crash or process.returncode == 0, output + error
            return output

        def stop_after(expression):
            return ('!(let $_ ' + expression + '\n'
                    ' (let $_ (println! CRASH_POINT)\n'
                    '  (let (io:pending $id) (io:submit (timer:after 60000))\n'
                    '   (io:wait $id 61000))))\n')

        q = lambda value: json.dumps(str(value), ensure_ascii=False)
        prefix = q(socket) + ' "mail" '
        send = '(mailbox:send ' + prefix + '"one" "receiver" "sender" "native λ")'
        receive = '(mailbox:next ' + prefix + '"receiver")'
        ack = '(mailbox:ack ' + prefix + '"receiver" "one")'
        try:
            start()
            # The public zero-argument function must execute its native ID
            # producer, including in Prime where returned code is inert.
            first = re.findall(r'"([0-9a-f]{32})"', run('!(space:request-id)\n'))
            second = re.findall(r'"([0-9a-f]{32})"', run('!(space:request-id)\n'))
            assert len(first) == len(second) == 1 and first != second
            if language == "petta":
                assert re.search(r'"[0-9a-f]{32}"', run('!(__cetta_lib_space_id)\n'))
            # Publication has committed, but the sender's turn has not returned.
            run(stop_after(send), crash=True)
            crash_service()
            assert '(mailbox:sent "one" duplicate)' in run('!' + send + '\n')
            # Receive has moved the message to Ready, but no acknowledgement exists.
            run(stop_after(receive), crash=True)
            crash_service()
            assert '(mailbox:delivery "one" "sender" "native λ")' in run('!' + receive + '\n')
            assert '(mailbox:acked "one"' in run('!' + ack + '\n')
            crash_service()
            assert '(mailbox:idle)' in run('!' + receive + '\n')
            assert 'duplicate' in run('!' + ack + '\n')
            # A fresh receiving process replies; a fresh sender consumes and settles it.
            reply = '(mailbox:send ' + prefix + '"two" "sender" "receiver" "reply")'
            assert 'accepted' in run('!' + reply + '\n')
            assert '(mailbox:delivery "two" "receiver" "reply")' in run(
                '!(mailbox:next ' + prefix + '"sender")\n')
            assert '(mailbox:acked "two"' in run(
                '!(mailbox:ack ' + prefix + '"sender" "two")\n')
        finally:
            for process in children:
                if process.poll() is None:
                    process.terminate()
                process.communicate(timeout=10)
    print(f"PASS ({language}): native mailbox publication, receive, service crash, replay and round trip")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("service_binary", type=Path)
    parser.add_argument("--lang", choices=("petta", "prime"), default="petta")
    options = parser.parse_args()
    qualify(options.binary.resolve(), options.service_binary.resolve(), options.lang)
