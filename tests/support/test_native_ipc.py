#!/usr/bin/env python3
"""Real native IPC, foreign notifications and non-consuming selection."""
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]

@unittest.skipUnless(os.environ.get('CETTA_BIN'), 'set CETTA_BIN')
class NativeIpc(unittest.TestCase):
    def guest(self, directory, program, lang='petta'):
        source = Path(directory) / 'probe.metta'
        source.write_text('!(import! &self '+ ('(library io)' if lang == 'petta' else 'io') +')\n'+program)
        result = subprocess.run([os.environ['CETTA_BIN'], '--lang', lang, str(source)],
                                cwd=ROOT, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn('Assertion failed', result.stderr)
        self.assertIn('IPC_OK', result.stdout, result.stdout + result.stderr)
        return result

    def peer(self, directory, framing, reply, delay=.04):
        path = str(Path(directory) / 'service.sock')
        server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET if framing == 'cwp' else socket.SOCK_STREAM)
        server.bind(path); server.listen(1)
        errors = []
        def work():
            try:
                server.settimeout(10)
                connection, _ = server.accept()
                with connection:
                    connection.recv(131072)
                    time.sleep(delay)
                    if reply:
                        if framing == 'cwp':
                            connection.sendall(reply)
                        else:
                            for byte in reply:
                                connection.sendall(bytes([byte]))
                                time.sleep(.0005)
            except (BrokenPipeError, ConnectionResetError):
                pass  # deadline clients deliberately close before the reply
            except Exception as error:
                errors.append(error)
            finally:
                server.close()
        thread = threading.Thread(target=work); thread.start()
        self.addCleanup(thread.join, 12)
        return json.dumps(path), thread, errors

    def test_cwp_wait_preserves_other_completion_and_single_value(self):
        for lang in ('petta', 'he'):
            with self.subTest(lang=lang), tempfile.TemporaryDirectory() as d:
                path, thread, errors = self.peer(d, 'cwp', b'CWP1'+bytes([64, 5])+b'token')
                result = self.guest(d, f'''!(let* (((io:pending $foreign) (io:submit (timer:after 0)))
                  ((io:pending $watch) (io:submit (cwp:request {path} 5 "" "1000" 2000)))
                  ($ready (collapse (io:wait-ready (io:ids $watch) 3000))))
                  (progn (assert (== $ready ((io:ready ($watch)))))
                    (assert (== (collapse (io:poll $watch)) ((io:event $watch (cwp:reply 64 "token" "")))))
                    (case (io:poll $foreign) (((io:event $foreign (timer:fired $_)) (println! IPC_OK))))))
''', lang)
                thread.join(2); self.assertFalse(errors, errors)
                self.assertEqual(result.stdout.count('IPC_OK'), 1)

    def test_csp_fragmented_malformed_reply_preserves_sent_uncertainty(self):
        with tempfile.TemporaryDirectory() as d:
            path, thread, errors = self.peer(d, 'csp', b'CSP1\x00\x00\x00\x04junk')
            self.guest(d, f'''!(let (io:pending $id) (io:submit (space:request {path} (request "rid" hello) 2000))
              (case (io:wait $id 3000)
                (((io:event $id (space:uncertain "rid" malformed-reply)) (println! IPC_OK)))))
''')
            thread.join(2); self.assertFalse(errors, errors)

    def test_deadline_disconnect_and_absence_are_explicit(self):
        for result, reply, delay, timeout in [('timeout', b'CWP1'+bytes([64,0]), .15, 25),
                                                ('disconnect', b'', .01, 2000)]:
            with self.subTest(result=result), tempfile.TemporaryDirectory() as d:
                path, thread, errors = self.peer(d, 'cwp', reply, delay)
                self.guest(d, f'''!(let (io:pending $id) (io:submit (cwp:request {path} 5 "" "1000" {timeout}))
                   (case (io:wait $id 3000) (((io:event $id (cwp:unavailable {result})) (println! IPC_OK)))))
''')
                thread.join(2); self.assertFalse(errors, errors)
        with tempfile.TemporaryDirectory() as d:
            path = json.dumps(str(Path(d) / 'absent'))
            self.guest(d, f'''!(let (io:pending $id) (io:submit (space:request {path} (request "rid" hello) 2000))
                   (case (io:wait $id 3000) (((io:event $id (space:unavailable "rid" absent)) (println! IPC_OK)))))
''')

    def test_relative_socket_paths_use_the_callers_working_directory(self):
        with tempfile.TemporaryDirectory() as d:
            path, thread, errors = self.peer(d, 'cwp', b'CWP1'+bytes([64, 0]))
            relative = json.dumps(os.path.relpath(json.loads(path), ROOT))
            self.guest(d, f'''!(let (io:pending $id) (io:submit (cwp:request {relative} 5 "" "1000" 2000))
               (case (io:wait $id 3000) (((io:event $id (cwp:reply 64 "" "")) (println! IPC_OK)))))
''')
            thread.join(2); self.assertFalse(errors, errors)

    def test_python_thread_can_notify_without_entering_evaluator(self):
        with tempfile.TemporaryDirectory() as d:
            module = Path(d) / 'notifier.py'
            module.write_text('import threading, time\nfrom cetta_bridge import notice\n'
                              'def start():\n'
                              '    def publish():\n        time.sleep(.03)\n        notice()\n'
                              '    threading.Thread(target=publish).start()\n    return 1\n')
            self.guest(d, f'''!(import! &self {json.dumps(str(module))})
!(let (io:pending $id) (io:submit (host:notice))
   (progn (py-call (notifier.start))
     (case (io:wait $id 2000) (((io:event $id (host:noticed $_)) (println! IPC_OK))))))
''')

if __name__ == '__main__': unittest.main()
