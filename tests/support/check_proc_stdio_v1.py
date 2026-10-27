#!/usr/bin/env python3
"""Standard streams of child processes, the timed line read and the
executable's path. Each case is one MeTTa program run in PeTTa.

- (system:read-line MS) answers lines without their newline and one
  carriage return before it, then end_of_file; () when no line has begun
  arriving in time. A line that has begun is read to its end, and the
  blocking and the timed read share one buffer.
- (stdin inherit), (stdout inherit), (stdout null) and (stderr null) give a
  child the caller's streams or discard its output; an uncaptured stream
  reports proc:eof. A child that inherits standard input stays in the
  caller's process group; any other child leads a group of its own.
- On a terminal, a child that inherits standard input reads the line typed
  to the caller's terminal, while a child in a group of its own that opens
  the terminal to read it is stopped until its timeout: why inheriting
  standard input keeps the caller's group.
- system:executable names the running binary.
"""
import argparse
import fcntl
import os
import pty
import select
import subprocess
import sys
import tempfile
import termios
import time
from pathlib import Path

HEAD = "!(import! &self (library system))\n!(import! &self (library proc))\n"
DRAIN = "(= (drain) (case (system:read-line 300) ((() (idle)) (end_of_file (eof)) ($l (cons-atom $l (drain))))))\n"
# drain's list ends in idle or eof: cons-atom splices the marker expression.
GROUP = '"echo $(ps -o pgid= -p $$) $(ps -o pgid= -p $PPID)"'


def run(binary, directory, body, stdin_bytes=None, feed=None):
    """Run a program; FEED is a list of (delay, bytes) written in order."""
    program = Path(directory) / "case.metta"
    program.write_text(HEAD + body + "\n", encoding="utf-8")
    argv = [str(binary), "--lang", "petta", "--no-results", str(program)]
    if feed is None:
        result = subprocess.run(argv, input=stdin_bytes or b"", capture_output=True, timeout=60)
        return result.returncode, result.stdout.decode("utf-8", "replace")
    child = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        for delay, data in feed:
            time.sleep(delay)
            child.stdin.write(data)
            child.stdin.flush()
        child.stdin.close()
    except BrokenPipeError:
        pass  # the program finished before reading all of it
    out, _ = child.communicate(timeout=60)
    return child.returncode, out.decode("utf-8", "replace")


def run_on_terminal(binary, directory, body, typed):
    """Run a program as the foreground of a terminal of its own and type a
    line to it; answers what the terminal showed."""
    program = Path(directory) / "case.metta"
    program.write_text(HEAD + body + "\n", encoding="utf-8")
    master, slave = pty.openpty()

    def acquire():
        fcntl.ioctl(0, termios.TIOCSCTTY, 0)

    child = subprocess.Popen([str(binary), "--lang", "petta", "--no-results", str(program)],
                             stdin=slave, stdout=slave, stderr=slave, start_new_session=True,
                             preexec_fn=acquire, close_fds=True)
    os.close(slave)
    time.sleep(0.5)
    os.write(master, typed)
    shown = b""
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        ready, _, _ = select.select([master], [], [], 0.2)
        if ready:
            try:
                data = os.read(master, 4096)
            except OSError:
                break
            if not data:
                break
            shown += data
        elif child.poll() is not None:
            break
    child.wait(timeout=30)
    os.close(master)
    return child.returncode, shown.decode("utf-8", "replace").replace("\r", "")


def groups(text):
    """The child's and its parent's process groups from GROUP's output."""
    words = text.replace('"', " ").replace("\\n", " ").split()
    numbers = [w for w in words if w.isdigit()]
    return numbers[0], numbers[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binary", type=Path)
    binary = parser.parse_args().binary.resolve()
    failures, total = [], 0

    def check(name, got, want):
        nonlocal total
        total += 1
        if got != want:
            failures.append((name, got, want))

    with tempfile.TemporaryDirectory() as directory:
        code, out = run(binary, directory, DRAIN + "!(println! (drain))", b"one\r\ntwo\n\nthree\r")
        check("lines and the unterminated last line", (code, out), (0, '("one" "two" "" "three" eof)\n'))
        code, out = run(binary, directory, DRAIN + "!(println! (drain))", feed=[(1.0, b"late\n")])
        check("nothing within the deadline", (code, out), (0, "(idle)\n"))
        code, out = run(binary, directory, DRAIN + "!(println! (drain))",
                        feed=[(0.1, b"par"), (1.0, b"tial\n"), (1.0, b"")])
        check("a begun line is read to its end", (code, out), (0, '("partial" idle)\n'))
        code, out = run(binary, directory,
                        "!(println! ((system:read-line) (system:read-line 0) (system:read-line 0) (system:read-line)))",
                        b"a\nb\n")
        check("one buffer for both reads", (code, out), (0, '("a" "b" end_of_file end_of_file)\n'))
        code, out = run(binary, directory, "!(println! (system:read-line -1))")
        check("a negative wait is refused", (code, "expected (system:read-line) or" in out), (0, True))

        code, out = run(binary, directory, "!(println! (system:executable))")
        check("the executable", (code, out), (0, '"%s"\n' % binary))

        code, out = run(binary, directory,
                        '!(println! (proc:run ("sh" "-c" "read x; echo got $x") ((stdin inherit) (stdout inherit))))',
                        b"inherited\n")
        check("inherited input and output", (code, out.splitlines()[0] if out else ""), (0, "got inherited"))
        check("uncaptured streams have ended", "(proc:capture 0 0 0 0 proc:eof proc:eof" in out, True)
        code, out = run(binary, directory,
                        '!(println! (proc:run ("sh" "-c" "echo kept; echo gone >&2") ((stderr null))))')
        check("discarded error", (code, '"kept\n" ""' in out, "proc:eof proc:eof" in out), (0, True, True))
        code, out = run(binary, directory, '!(println! (proc:run ("sh") ((stdout bogus))))')
        check("an unknown stream mode", (code, out), (0, '(proc:failed proc:invalid-argument "stdout and stderr must be inherit or null")\n'))
        code, out = run(binary, directory, '!(println! (proc:run ("sh" "-c" %s) ((stdin inherit))))' % GROUP)
        child, parent = groups(out)
        check("an inheriting child stays in the caller's group", child == parent, True)
        code, out = run(binary, directory, '!(println! (proc:run ("sh" "-c" %s) ()))' % GROUP)
        child, parent = groups(out)
        check("another child leads a group of its own", child != parent, True)
        code, out = run(binary, directory,
                        '!(println! (proc:run ("sh" "-c" "read x; echo tree $x") ((stdin inherit) (stdout inherit) (lifetime owner))))',
                        b"owned\n")
        check("inherited input under lifetime owner", (code, out.splitlines()[0] if out else ""), (0, "tree owned"))

        code, shown = run_on_terminal(
            binary, directory,
            '!(println! (proc:run ("sh" "-c" "read x; echo terminal gave $x") ((stdin inherit) (stdout inherit) (timeout-ms 10000))))',
            b"typed\n")
        check("a terminal read by an inheriting child", (code, "terminal gave typed" in shown), (0, True))
        code, shown = run_on_terminal(
            binary, directory,
            '!(println! (proc:run ("sh" "-c" "read x < /dev/tty; echo read $x") ((timeout-ms 1500))))',
            b"typed\n")
        check("a terminal read from a group of its own stops until the timeout",
              (code, "proc:timed-out" in shown, "read typed" in shown), (0, True, False))

    for name, got, want in failures:
        print("FAIL %s:\n  got  %r\n  want %r" % (name, got, want))
    print("proc stdio: %d checks, %d failures" % (total, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
