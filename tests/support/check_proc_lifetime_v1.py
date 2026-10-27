#!/usr/bin/env python3
"""Lifetimes of child processes: run and detached, beside owner and group.

- Under (lifetime run) a background job the command leaves running survives
  the command's own exit; a timeout, or the owner's death by SIGKILL, ends
  the whole tree, the background job included. (lifetime owner) still ends
  leftovers when the command exits.
- Under (lifetime detached) the child leads a session of its own, outlives
  its handle and the CeTTa process that started it, and is still ended by
  proc:cancel and by a timeout. Captured output and inherited input are
  refused for it.

Every process a case starts is ended by the harness afterwards, whatever
the outcome.
"""
import argparse
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HEAD = "!(import! &self (library proc))\n"


def alive(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    # A zombie has exited; only its parent's reaping is pending.
    try:
        return Path("/proc/%d/stat" % pid).read_text().split(") ", 1)[1][0] != "Z"
    except (FileNotFoundError, IndexError):
        return False


def settle(predicate, seconds=5.0):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.05)
    return predicate()


def pid_from(path, seconds=10.0):
    settle(lambda: path.exists() and path.read_text().strip().isdigit(), seconds)
    text = path.read_text().strip() if path.exists() else ""
    return int(text) if text.isdigit() else None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binary", type=Path)
    binary = parser.parse_args().binary.resolve()
    failures, total, started = [], 0, []

    def check(name, got, want):
        nonlocal total
        total += 1
        if got != want:
            failures.append((name, got, want))

    def run(directory, body, wait=True):
        program = Path(directory) / ("case-%d.metta" % (total + len(started)))
        program.write_text(HEAD + body + "\n", encoding="utf-8")
        argv = [str(binary), "--lang", "petta", "--no-results", str(program)]
        if not wait:
            return subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE)
        result = subprocess.run(argv, stdin=subprocess.DEVNULL, capture_output=True, timeout=60)
        return result.returncode, result.stdout.decode("utf-8", "replace")

    def background(path):
        """A shell command that starts `sleep 300` in the background and
        writes its PID to PATH."""
        return "sleep 300 >/dev/null 2>&1 & echo $! > %s" % path

    try:
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)

            mark = work / "run-normal"
            code, out = run(directory, '!(println! (proc:run ("sh" "-c" "%s") ((lifetime run))))' % background(mark))
            pid = pid_from(mark)
            started.append(pid)
            check("run: the command exits normally", (code, "(proc:exited 0)" in out), (0, True))
            check("run: its background job survives", pid is not None and alive(pid), True)

            mark = work / "owner-normal"
            code, out = run(directory, '!(println! (proc:run ("sh" "-c" "%s") ((lifetime owner))))' % background(mark))
            pid = pid_from(mark)
            started.append(pid)
            check("owner: leftovers end with the command", pid is not None and settle(lambda: not alive(pid)), True)

            mark = work / "run-timeout"
            code, out = run(directory, '!(println! (proc:run ("sh" "-c" "%s; sleep 300") ((lifetime run) (timeout-ms 1500))))'
                            % background(mark))
            pid = pid_from(mark)
            started.append(pid)
            check("run: a timeout is reported", "proc:timed-out" in out, True)
            check("run: a timeout ends the background job too", pid is not None and settle(lambda: not alive(pid)), True)

            mark = work / "run-owner-death"
            owner = run(directory, '!(proc:run ("sh" "-c" "%s; sleep 300") ((lifetime run)))' % background(mark), wait=False)
            pid = pid_from(mark)
            started.append(pid)
            owner.kill()
            owner.wait(timeout=30)
            check("run: the owner's death ends the tree", pid is not None and settle(lambda: not alive(pid)), True)

            mark = work / "detached"
            code, out = run(directory,
                            '!(println! (proc:spawn ("sh" "-c" "echo $$ > %s; exec sleep 300") '
                            '((lifetime detached) (stdout null) (stderr null))))' % mark)
            pid = pid_from(mark)
            started.append(pid)
            check("detached: outlives its handle and its starter", (code, pid is not None and alive(pid)), (0, True))
            if pid is not None and alive(pid):
                check("detached: leads a session of its own", os.getsid(pid) == pid, True)
                check("detached: apart from the starter's session", os.getsid(pid) != os.getsid(0), True)

            mark = work / "detached-cancel"
            code, out = run(directory,
                            '!(let $owner (proc:spawn ("sh" "-c" "echo $$ > %s; exec sleep 300") '
                            '((lifetime detached) (stdout null) (stderr null))) '
                            '(let $waited (proc:wait $owner 1000) (let $cancel (proc:cancel $owner) (println! (proc:await $owner)))))'
                            % mark)
            pid = pid_from(mark)
            started.append(pid)
            check("detached: proc:cancel ends it", ("proc:cancelled" in out, pid is not None and settle(lambda: not alive(pid))),
                  (True, True))

            mark = work / "detached-timeout"
            code, out = run(directory,
                            '!(println! (proc:run ("sh" "-c" "echo $$ > %s; exec sleep 300") '
                            '((lifetime detached) (stdout null) (stderr null) (timeout-ms 1000))))' % mark)
            pid = pid_from(mark)
            started.append(pid)
            check("detached: a timeout ends it", ("proc:timed-out" in out, pid is not None and settle(lambda: not alive(pid))),
                  (True, True))

            for options, name in (("((lifetime detached))", "captured output"),
                                  ("((lifetime detached) (stdout null) (stderr null) (stdin inherit))", "inherited input")):
                code, out = run(directory, '!(println! (proc:spawn ("true") %s))' % options)
                check("detached refuses " + name, "(proc:failed proc:invalid-argument" in out, True)
            code, out = run(directory, '!(println! (proc:spawn ("true") ((lifetime forever))))')
            check("an unknown lifetime is refused",
                  '"lifetime must be group, owner, run or detached"' in out, True)
    finally:
        for pid in started:
            if pid is not None and alive(pid):
                os.kill(pid, signal.SIGKILL)

    for name, got, want in failures:
        print("FAIL %s:\n  got  %r\n  want %r" % (name, got, want))
    print("proc lifetime: %d checks, %d failures" % (total, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
