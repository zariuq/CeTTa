#!/usr/bin/env python3
"""An effect attempt in any speculative branch must never reach the peer."""
import pathlib
import selectors
import subprocess
import sys
import tempfile
import urllib.request

fixture, test = (str(pathlib.Path(x).resolve()) for x in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="cetta-speculative-") as directory:
    marker = pathlib.Path(directory) / "forbidden"
    module = pathlib.Path(directory) / "speculative_foreign.py"
    module.write_text("from pathlib import Path\n"
                      "from hyperon.ext import register_atoms\n"
                      "from hyperon.atoms import OperationAtom, ValueAtom\n"
                      "def write():\n"
                      f"    Path({str(marker)!r}).write_text('foreign effect')\n"
                      "    return [ValueAtom(1)]\n"
                      "@register_atoms\n"
                      "def expose():\n"
                      "    return {'speculative-write': OperationAtom('speculative-write', write)}\n")
    server = subprocess.Popen([fixture], stdout=subprocess.PIPE, text=True)
    try:
        with selectors.DefaultSelector() as ready:
            ready.register(server.stdout, selectors.EVENT_READ)
            assert ready.select(timeout=5)
        base = f"http://127.0.0.1:{int(server.stdout.readline().strip())}"
        run = subprocess.run([test, base, str(marker), str(module)], capture_output=True, text=True, timeout=30)
        assert not marker.exists(), "speculation wrote an external file"
        with urllib.request.urlopen(base + "/counted-total", timeout=5) as response:
            assert response.read() == b"0", "speculation dispatched HTTP"
        assert run.returncode == 0, (run.stdout, run.stderr)
        assert "SHOULD-NOT-PRINT" not in run.stdout and "secret" not in run.stdout, run.stdout
        print(run.stdout, end="")
    finally:
        server.terminate()
        server.wait(timeout=5)
