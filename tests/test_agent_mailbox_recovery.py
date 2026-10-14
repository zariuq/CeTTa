"""Crash/replay qualification; mailbox operations themselves run in MeTTa/C."""
import json
import selectors
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def qualify(binary, service_binary):
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
            path.write_text('!(import! &self mailbox)\n!(import! &self (library io))\n' + source)
            process = subprocess.Popen([str(binary), "--lang", "petta", str(path)],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            children.append(process)
            if crash:
                with selectors.DefaultSelector() as selector:
                    selector.register(process.stdout, selectors.EVENT_READ)
                    assert selector.select(20), "client did not reach its committed transition"
                    assert process.stdout.readline().strip() == "CRASH_POINT"
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
    print("PASS: native mailbox publication, receive, service crash, replay and round trip")


if __name__ == "__main__":
    qualify(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
