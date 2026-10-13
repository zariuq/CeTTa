#!/usr/bin/env python3
"""Real library evaluation in both languages against a private loopback peer."""
import pathlib
import selectors
import subprocess
import sys
import tempfile

binary, fixture = (str(pathlib.Path(x).resolve()) for x in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="cetta-io-library-") as directory:
    server = subprocess.Popen([fixture], stdout=subprocess.PIPE, text=True)
    try:
        with selectors.DefaultSelector() as ready:
            ready.register(server.stdout, selectors.EVENT_READ)
            assert ready.select(timeout=5), "fixture did not start"
        port = int(server.stdout.readline().strip())
        program = pathlib.Path(directory) / "probe.metta"
        program.write_text(f'''!(import! &self io)
!(io:capabilities)
!(io:poll)
!(io:submit (http:request "GET" "http://127.0.0.1:{port}/one" () "" 2000 1024))
!(io:wait 5000)
!(io:poll)
!(io:wait 0)
!(io:submit (http:request "GET" "http://127.0.0.1:{port}/redirect/2" () "" 2000 1024))
!(io:wait 5000)
!(io:submit (http:request "GET" "http://127.0.0.1:{port}/redirect/2" () "" 2000 1024 True))
!(io:wait 5000)
!(io:submit (http:request "GET" "http://127.0.0.1:{port}/redirect/2" () "" 2000 1024 False))
!(io:wait 5000)
''')
        for language in ("he", "petta"):
            result = subprocess.run([binary, "--lang", language, str(program)],
                                    capture_output=True, text=True, timeout=10)
            assert result.returncode == 0 and not result.stderr, result
            lines = result.stdout.strip().splitlines()
            if language == "he":
                assert all(line.startswith("[") and line.endswith("]") for line in lines), lines
                lines = [line[1:-1] for line in lines]
            assert lines.pop(0) in ("true", "()"), lines
            assert lines == ['(http)', '(io:idle)', '(io:pending 1)',
                             '(io:event 1 (http:response 200 "one"))',
                             '(io:idle)', '(io:idle)',
                             '(io:pending 2)', '(io:event 2 (http:response 302 ""))',
                             '(io:pending 3)', '(io:event 3 (http:response 200 "one"))',
                             '(io:pending 4)', '(io:event 4 (http:response 302 ""))'], (language, lines)
        print("I/O library: HE and PeTTa submit/wait/poll, redirect default/opt-in, exact correlation and one-time consumption passed")
    finally:
        server.terminate()
        server.wait(timeout=5)
