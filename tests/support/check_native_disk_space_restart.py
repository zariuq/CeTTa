"""Commit, kill without cleanup, reopen and query through the shared Space path."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile


binary = str(Path(sys.argv[1]).resolve())


def run(mode, directory, symbols=0):
    result = subprocess.run([binary, mode, str(directory), str(symbols)],
                            capture_output=True, text=True, timeout=30, check=True)
    assert "should-not-print" not in result.stdout
    return result.stdout


def killed_writer(directory):
    process = subprocess.Popen([binary, "write", str(directory)], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    try:
        import select
        assert select.select([process.stdout], [], [], 30)[0], "writer did not reach its checkpoint"
        marker = process.stdout.readline().strip()
        assert marker == "DURABLE_ACK", marker
    finally:
        process.kill()
        process.communicate(timeout=10)
    assert process.returncode == -signal.SIGKILL


with tempfile.TemporaryDirectory(prefix="cetta-disk-space-", dir=os.environ.get("TMPDIR")) as temporary:
    root = Path(temporary)
    committed = root / "committed"; committed.mkdir(mode=0o700)
    killed_writer(committed)
    first = run("read", committed, 256)
    second = run("read", committed, 512)
    identities = lambda text: [line for line in text.splitlines() if line.startswith("IDENTITY ")]
    assert len(identities(first)) == 4 and len(set(identities(first))) == 4
    assert identities(first) == identities(second)
    assert [line.split()[-1] for line in identities(first)] == ["facts/a", "facts/b", "facts/c", "facts/r"]
    assert "OWNED_SPACE_REOPEN_OK" in first and "OWNED_SPACE_REOPEN_OK" in second

    held = subprocess.Popen([binary, "hold", str(committed), "128"], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        import select
        assert select.select([held.stdout], [], [], 30)[0], "reader did not capture its view"
        assert held.stdout.readline().strip() == "VIEW_CAPTURED"
        assert "PUBLISHED" in run("append", committed)
        assert "COMPACTED" in run("compact", committed)
        output, error = held.communicate("!", timeout=30)
        assert held.returncode == 0, error
        assert identities(output) == identities(first)
    finally:
        if held.poll() is None:
            held.kill(); held.communicate(timeout=10)
    new_identities = identities(run("read", committed, 1024))
    assert len(new_identities) == 5 and set(identities(first)) < set(new_identities)

    damaged = root / "bad-atom"; damaged.mkdir(mode=0o700)
    killed_writer(damaged)
    run("corrupt", damaged)
    assert "CORRUPTION_IS_NOT_EMPTY_OR_COMPLETE" in run("check-corrupt", damaged)

print("PASS: owning disk Space reopens across killed processes, preserves identities and multiplicity, and survives publication/compaction")
