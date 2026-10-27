#!/usr/bin/env python3
"""system:getcwd against os.getcwd, and system:cwd, in every kind of directory.

Each case sets up the working directory in the child before it runs: a short
one, one with spaces and non-ASCII, two longer than PATH_MAX (about 5k and 10k
bytes, reached by relative steps), one that has been removed, a long one under
an unreadable ancestor, and one whose name is not UTF-8. Python's os.getcwd
runs in a child set up the same way. A success must give the same bytes; a
refusal the same errno and message, as (system:os-error errno message).
system:cwd gives the same directory, and keeps its Error on a refusal.

Importing lib_import starts the embedded SWI-Prolog where it is built in. Its
start-up cannot run without a readable working directory, so it must leave
the engine unstarted there, not end the process.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile

IMPORTS = {"petta": "!(import! &self (library system))\n!(import! &self (library str))\n",
           "he": "!(import! &self system)\n!(import! &self str)\n",
           "prime": "!(import! &self system)\n!(import! &self str)\n"}
# PeTTa catches system:cwd's Error; HE and Prime print the Error as a value.
CWD = {"petta": "(catch (system:cwd))", "he": "(system:cwd)", "prime": "(system:cwd)"}
PROGRAM = """!(println! (RESULT GETCWD (system:getcwd)))
!(println! (RESULT CWD {cwd}))
!(println! (RESULT LENGTH (let $d (system:getcwd) (if (== (get-type $d) String) (str:byte-length $d) none))))
!(println! (RESULT SAME (== (system:getcwd) {cwd})))
"""
PYTHON = """import json, os
try:
    print(json.dumps({"bytes": os.getcwdb().hex(), "text": os.getcwd()}))
except OSError as error:
    print(json.dumps({"errno": error.errno, "message": error.strerror}))
"""


def steps(names):
    def enter():
        for name in names:
            os.chdir(name)
    return enter


class Cases:
    def __init__(self, root):
        self.root = root

    def directory(self, label, components):
        """Create the components below ROOT/label one level at a time, relative
        to the previous level, since the whole path may exceed PATH_MAX."""
        base = self.root / label
        base.mkdir()
        parent = os.open(base, os.O_RDONLY | os.O_DIRECTORY)
        try:
            for name in components:
                os.mkdir(name, dir_fd=parent)
                child = os.open(name, os.O_RDONLY | os.O_DIRECTORY, dir_fd=parent)
                os.close(parent)
                parent = child
        finally:
            os.close(parent)
        return [os.fsencode(base)] + list(components)

    def all(self):
        cases = {}
        cases["short"] = steps(self.directory("short", [b"plain"]))
        cases["spaces-non-ascii"] = steps(self.directory("spaces", ["tëst ü".encode(), b"a b"]))
        cases["longer-than-path-max"] = steps(self.directory("long", [b"d%02d" % i + b"x" * 247 for i in range(21)]))
        cases["much-longer"] = steps(self.directory("longer", [b"e%02d" % i + b"y" * 247 for i in range(40)]))
        cases["non-utf8-name"] = steps(self.directory("bytes", [b"name-\xff\xfe-end"]))
        removed = self.directory("removed", [])[0]

        def enter_removed():
            # Each child removes its own fresh directory after entering it.
            os.chdir(removed)
            os.mkdir(b"gone")
            os.chdir(b"gone")
            os.rmdir(os.path.join(removed, b"gone"))
        cases["removed"] = enter_removed
        hidden = self.directory("hidden", [b"f%02d" % i + b"z" * 247 for i in range(20)])
        self.locked = os.path.join(hidden[0], hidden[1], hidden[2])

        def enter_hidden():
            steps(hidden)()
        cases["long-under-unreadable-ancestor"] = enter_hidden
        return cases


def run_engine(binary, dialect, enter, directory):
    program = directory / ("getcwd-%s.metta" % dialect)
    program.write_text(IMPORTS[dialect] + PROGRAM.format(cwd=CWD[dialect]), encoding="utf-8")
    result = subprocess.run([str(binary), "--lang", dialect, str(program)], preexec_fn=enter,
                            capture_output=True, timeout=120)
    if result.returncode:
        raise SystemExit("%s: engine exited %d: %s" % (dialect, result.returncode, result.stderr[-1500:]))
    lines = {}
    for line in result.stdout.split(b"\n"):
        if line.startswith(b"(RESULT "):
            key, _, value = line[len(b"(RESULT "):-1].partition(b" ")
            lines[key.decode()] = value
    lines["STDOUT"] = result.stdout
    return lines


def run_python(enter):
    result = subprocess.run([sys.executable, "-I", "-c", PYTHON], preexec_fn=enter, capture_output=True,
                            text=True, timeout=60)
    return json.loads(result.stdout)


def printed_string(raw):
    return b'"' + raw.replace(b"\\", b"\\\\").replace(b'"', b'\\"') + b'"'


def compare(case, dialect, got, want):
    problems = []
    if "errno" in want:
        message = printed_string(want["message"].encode())
        expected = b"(system:os-error %d %s)" % (want["errno"], message)
        if got.get("GETCWD") != expected:
            problems.append("getcwd %r, want %r" % (got.get("GETCWD"), expected))
        # HE and Prime let the Error escape println!, as the evaluation's result.
        refusal = got.get("CWD", b"") if "CWD" in got else got["STDOUT"]
        if b"(Error" not in refusal or b"cannot determine current directory" not in refusal:
            problems.append("cwd %r, want its Error" % got.get("CWD"))
    else:
        raw = bytes.fromhex(want["bytes"])
        if got.get("LENGTH") != str(len(raw)).encode():
            problems.append("byte length %r, want %d" % (got.get("LENGTH"), len(raw)))
        if got.get("GETCWD") != printed_string(raw):
            problems.append("getcwd %r, want %r" % (got.get("GETCWD", b"")[:120], printed_string(raw)[:120]))
        if got.get("SAME") != b"True" and got.get("SAME") != b"true":
            problems.append("system:cwd differs from system:getcwd: %r" % got.get("SAME"))
    return ["%s %s: %s" % (dialect, case, p) for p in problems]


def prolog_start(binary, enter, directory, readable):
    """In a readable directory lib_import loads. Where SWI-Prolog cannot start,
    the import fails with a stated reason instead of ending the process."""
    program = directory / "prolog-start.metta"
    program.write_text("!(import! &self (library lib_import))\n!(println! (Started))\n", encoding="utf-8")
    result = subprocess.run([str(binary), "--lang", "petta", str(program)], preexec_fn=enter,
                            capture_output=True, timeout=120)
    if readable:
        return result.returncode == 0 and b"(Started)" in result.stdout, result
    return (result.returncode == 1 and b"SWI-Prolog not started" in result.stderr
            and b"memory fault" not in result.stderr), result


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binary", type=Path)
    parser.add_argument("--without-prolog", action="store_true",
                        help="omit bootstrap checks for a build without embedded Prolog")
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()
    failures, total = [], 0
    root = Path(tempfile.mkdtemp(prefix="getcwd-v1-"))
    try:
        cases = Cases(root)
        enters = cases.all()
        os.chmod(cases.locked, stat.S_IXUSR | stat.S_IWUSR)   # search, no read
        for case, enter in enters.items():
            want = run_python(enter)
            for dialect in (["petta", "he", "prime"] if case in ("short", "removed") else ["petta"]):
                total += 1
                failures += compare(case, dialect, run_engine(binary, dialect, enter, root), want)
            if not arguments.without_prolog and case in ("removed", "longer-than-path-max", "much-longer", "short"):
                total += 1
                good, result = prolog_start(binary, enter, root, case == "short")
                if not good:
                    failures.append("lib_import in %s: exit %d, %r" % (case, result.returncode, result.stderr[-300:]))
    finally:
        try:
            os.chmod(cases.locked, stat.S_IRWXU)
        except Exception:
            pass
        shutil.rmtree(root, ignore_errors=True)
    for failure in failures[:30]:
        print("FAIL " + failure)
    print("getcwd: %d cases, %d failures" % (total, len(failures)))
    if arguments.without_prolog:
        print("embedded Prolog bootstrap: disabled in this build (4 checks omitted)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
