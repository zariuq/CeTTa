#!/usr/bin/env python3
"""Writer owners, locks and directory creation agree with Python's.

fs:open-write, fs:lock and fs:make-directories are compared with os.open,
fcntl.flock and Path.mkdir(parents=True, exist_ok=True): created modes,
errno values and the paths errors name. Locks are checked across processes in
both directions, and native and Python writers append to one file under the
same lock while every line stays whole and in each writer's order.
"""
import argparse
import errno
import fcntl
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

PYTHON_APPEND = r'''
import fcntl, json, os, sys
path, lines = sys.argv[1], json.loads(sys.stdin.read())
for line in lines:
    descriptor = os.open(path, os.O_APPEND | os.O_CREAT | os.O_WRONLY, 0o600)
    try:
        fcntl.flock(descriptor, fcntl.LOCK_EX)
        with os.fdopen(descriptor, "ab", closefd=False) as stream:
            stream.write(line.encode())
            stream.flush()
            os.fsync(stream.fileno())
        fcntl.flock(descriptor, fcntl.LOCK_UN)
    finally:
        os.close(descriptor)
'''

NATIVE_APPEND = '''!(import! &self (library fs))
(= (append-line $path $text)
   (let $owner (fs:open-write $path ("append" "create") 384)
     (progn (fs:lock $owner exclusive) (fs:write-all $owner $text) (fs:sync $owner)
            (fs:unlock $owner) (fs:close $owner))))
'''


def literal(text):
    return json.dumps(text, ensure_ascii=False)


class Engine:
    def __init__(self, binary, work):
        self.binary, self.work, self.count = binary, work, 0

    def program(self, source):
        self.count += 1
        path = self.work / ("program-%d.metta" % self.count)
        path.write_text(source, encoding="utf-8")
        return [str(self.binary), "--lang", "petta", str(path)]

    def results(self, source, cwd=None):
        result = subprocess.run(self.program(source), capture_output=True, text=True,
                                timeout=300, cwd=cwd)
        if result.returncode:
            raise SystemExit("engine exited %d: %s" % (result.returncode, result.stderr[-2000:]))
        return [line[len("(RESULT "):-1] for line in result.stdout.split("\n")
                if line.startswith("(RESULT ")]


def native_outcome(text):
    """() or (fs:failed kind operation "path" errno progress "message")."""
    if text == "()":
        return ("ok",)
    match = re.match(r'\(fs:failed \S+ \S+ ("(?:[^"\\]|\\.)*") (-?\d+) ', text)
    if not match:
        return ("unexpected", text)
    return ("error", int(match.group(2)), json.loads(match.group(1)))


def tree(root):
    entries = []
    for path in sorted(root.rglob("*")):
        info = path.lstat()
        entries.append((str(path.relative_to(root)), oct(info.st_mode)))
    return entries


def make_directory_cases(engine, failures):
    """Path.mkdir(parents=True, exist_ok=True, mode) against fs:make-directories."""
    cases = [("a/b/c", 0o700), ("a//b/./c/", 0o750), ("made/twice", 0o700), ("file", 0o700),
             ("file/x/y", 0o700), ("file//x/./y/", 0o700), (".", 0o700), ("", 0o700),
             ("link", 0o700), ("link/inside/deeper", 0o711), ("dangling", 0o700),
             ("dangling/x", 0o700), ("../%s/up/one", 0o755)]
    if os.geteuid():
        cases += [("closed/x/y", 0o700), ("closed", 0o700)]
    for umask in (0o022, 0o077):
        previous = os.umask(umask)
        try:
            roots = []
            for owner in ("python", "native"):
                root = engine.work / ("mkdir-%s-%o" % (owner, umask))
                shutil.rmtree(root, ignore_errors=True)
                root.mkdir()
                (root / "made/twice").mkdir(parents=True)
                (root / "file").write_text("x")
                (root / "target").mkdir()
                (root / "link").symlink_to("target")
                (root / "dangling").symlink_to("missing")
                (root / "closed").mkdir(mode=0o500)
                roots.append(root)
            python_root, native_root = roots
            outcomes = {}
            cwd = os.getcwd()
            try:
                os.chdir(python_root)
                for spec, mode in cases:
                    spec = spec.replace("%s", python_root.name)
                    try:
                        Path(spec).mkdir(parents=True, exist_ok=True, mode=mode)
                        outcomes[spec] = ("ok",)
                    except OSError as error:
                        outcomes[spec] = ("error", error.errno, str(error.filename))
            finally:
                os.chdir(cwd)
            source = "!(import! &self (library fs))\n" + "".join(
                "!(println! (RESULT (fs:make-directories %s %d)))\n"
                % (literal(spec.replace("%s", native_root.name)), mode) for spec, mode in cases)
            native = engine.results(source, cwd=native_root)
            for (spec, mode), text in zip(cases, native):
                want = outcomes[spec.replace("%s", python_root.name)]
                got = native_outcome(text)
                if want[0] == "error" and got[0] == "error":
                    got = ("error", got[1], got[2].replace(native_root.name, python_root.name))
                if got != want:
                    failures.append(("mkdir %r umask %o" % (spec, umask), got, want))
            for root in roots:
                (root / "closed").chmod(0o700)
            python_tree = [(name.replace(python_root.name, "ROOT"), mode) for name, mode in tree(python_root)]
            native_tree = [(name.replace(native_root.name, "ROOT"), mode) for name, mode in tree(native_root)]
            if python_tree != native_tree:
                failures.append(("mkdir trees umask %o" % umask, native_tree, python_tree))
        finally:
            os.umask(previous)
    return len(cases) * 2


def open_cases(engine, failures):
    """os.open for writing against fs:open-write: modes, errors and offsets."""
    root = engine.work / "open"
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir()
    os.umask(0o022)
    cases = [(("append", "create"), 0o600, "new-600", None),
             (("append", "create"), 0o666, "new-666", None),
             (("create", "exclusive"), 0o600, "existing", "old"),
             (("append",), 0o600, "missing", None),
             (("append", "create"), 0o600, "directory", "dir"),
             (("truncate", "create"), 0o600, "truncated", "old contents"),
             ((), 0o600, "overwritten", "old contents"),
             (("append",), 0o600, "appended", "old contents")]
    flags = {"append": os.O_APPEND, "create": os.O_CREAT, "truncate": os.O_TRUNC,
             "exclusive": os.O_EXCL}
    results = {}
    for owner in ("python", "native"):
        base = root / owner
        base.mkdir()
        for _, _, name, setup in cases:
            if setup == "dir":
                (base / name).mkdir()
            elif setup is not None:
                (base / name).write_text(setup)
        if owner == "python":
            for names, mode, name, _ in cases:
                bits = os.O_WRONLY
                for flag in names:
                    bits |= flags[flag]
                try:
                    descriptor = os.open(base / name, bits, mode)
                    os.write(descriptor, b"new")
                    os.close(descriptor)
                    results[(owner, name)] = ("ok",)
                except OSError as error:
                    results[(owner, name)] = ("error", error.errno)
        else:
            source = "!(import! &self (library fs))\n" + "".join(
                "!(let $w (fs:open-write %s (%s) %d) (case $w (((NativeHandle $k $i) "
                "(progn (fs:write-all $w \"new\") (println! (RESULT (fs:close $w))))) "
                "($other (println! (RESULT $other))))))\n"
                % (literal(str(base / name)), " ".join('"%s"' % flag for flag in names), mode)
                for names, mode, name, _ in cases)
            for (_, _, name, _), text in zip(cases, engine.results(source)):
                outcome = native_outcome(text)
                results[(owner, name)] = ("ok",) if text == "fs:closed" else outcome[:2]
    for _, _, name, _ in cases:
        python_file, native_file = root / "python" / name, root / "native" / name
        want = results[("python", name)]
        got = results[("native", name)]
        if got != want:
            failures.append(("open %s" % name, got, want))
        if python_file.is_file() and native_file.is_file():
            want_state = (oct(python_file.stat().st_mode), python_file.read_bytes())
            got_state = (oct(native_file.stat().st_mode), native_file.read_bytes())
            if got_state != want_state:
                failures.append(("open %s file" % name, got_state, want_state))
    checked = len(cases)
    if Path("/dev/full").exists():
        text = engine.results('!(import! &self (library fs))\n'
                              '!(let $w (fs:open-write "/dev/full" ("append") 0) '
                              '(println! (RESULT (fs:write-all $w "x"))))\n')[0]
        if native_outcome(text)[:2] != ("error", errno.ENOSPC):
            failures.append(("write to /dev/full", text, "ENOSPC"))
        checked += 1
    return checked


def lock_cases(engine, failures):
    """A Python holder blocks the native writer, and a native holder blocks Python."""
    path = engine.work / "locked.jsonl"
    path.unlink(missing_ok=True)
    descriptor = os.open(path, os.O_APPEND | os.O_CREAT | os.O_WRONLY, 0o600)
    fcntl.flock(descriptor, fcntl.LOCK_EX)
    native = subprocess.Popen(engine.program(
        NATIVE_APPEND + "!(append-line %s \"native\\n\")\n" % literal(str(path))),
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    time.sleep(0.5)
    if native.poll() is not None or path.read_bytes():
        failures.append(("native waits for a Python lock", native.poll(), path.read_bytes()))
    os.write(descriptor, b"python\n")
    fcntl.flock(descriptor, fcntl.LOCK_UN)
    os.close(descriptor)
    native.wait(timeout=60)
    if path.read_bytes() != b"python\nnative\n":
        failures.append(("Python, then native", path.read_bytes(), b"python\nnative\n"))

    marker = engine.work / "native-holds"
    marker.unlink(missing_ok=True)
    holder = subprocess.Popen(engine.program(
        "!(import! &self (library fs))\n!(import! &self (library system))\n"
        "!(let $w (fs:open-write %s (\"append\") 0) (progn (fs:lock $w exclusive) "
        "(fs:write %s \"held\") (system:read-line) (fs:write-all $w \"native again\\n\") "
        "(fs:close $w)))\n" % (literal(str(path)), literal(str(marker)))),
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    deadline = time.monotonic() + 60
    while not marker.exists() and holder.poll() is None and time.monotonic() < deadline:
        time.sleep(0.02)
    descriptor = os.open(path, os.O_APPEND | os.O_WRONLY)
    try:
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        failures.append(("Python is refused a native lock", "acquired", "EWOULDBLOCK"))
    except BlockingIOError:
        pass
    holder.communicate(b"\n", timeout=60)
    fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
    os.close(descriptor)
    if path.read_bytes() != b"python\nnative\nnative again\n":
        failures.append(("native holder's write", path.read_bytes(), b"python\nnative\nnative again\n"))
    return 2


def interleaving(engine, failures, writers, count, seed):
    """Native and Python writers append concurrently under one lock."""
    rng = random.Random(seed)
    path = engine.work / "interleaved.jsonl"
    path.unlink(missing_ok=True)
    processes = []
    for index in range(writers):
        for owner in ("native", "python"):
            name = "%s-%d" % (owner, index)
            lines = [json.dumps({"writer": name, "index": k,
                                 "pad": "é" * rng.choice((1, 40, 3000))}, ensure_ascii=False) + "\n"
                     for k in range(count)]
            if owner == "python":
                process = subprocess.Popen([sys.executable, "-c", PYTHON_APPEND, str(path)],
                                           stdin=subprocess.PIPE, stderr=subprocess.PIPE)
                process.stdin.write(json.dumps(lines).encode())
                process.stdin.close()
            else:
                process = subprocess.Popen(engine.program(NATIVE_APPEND + "".join(
                    "!(append-line %s %s)\n" % (literal(str(path)), literal(line)) for line in lines)),
                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            processes.append((name, process))
    for name, process in processes:
        if process.wait(timeout=600):
            failures.append(("writer %s" % name, process.returncode, process.stderr.read()[-500:]))
    seen = {}
    lines = path.read_bytes().split(b"\n")
    if lines[-1] != b"":
        failures.append(("file ends with a newline", lines[-1][-80:], b""))
    for line in lines[:-1]:
        try:
            entry = json.loads(line)
        except ValueError:
            failures.append(("whole line", line[:120], "JSON"))
            continue
        expected = seen.get(entry["writer"], -1) + 1
        if entry["index"] != expected:
            failures.append(("order of %s" % entry["writer"], entry["index"], expected))
        seen[entry["writer"]] = entry["index"]
    if len(lines) - 1 != 2 * writers * count:
        failures.append(("line count", len(lines) - 1, 2 * writers * count))
    return len(lines) - 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--writers", type=int, default=4)
    parser.add_argument("--lines", type=int, default=150)
    parser.add_argument("--seed", type=int, default=20261026)
    args = parser.parse_args()
    failures = []
    saved = os.umask(0o022)
    try:
        with tempfile.TemporaryDirectory() as directory:
            engine = Engine(args.binary.resolve(), Path(directory))
            checked = make_directory_cases(engine, failures)
            checked += open_cases(engine, failures)
            checked += lock_cases(engine, failures)
            lines = interleaving(engine, failures, args.writers, args.lines, args.seed)
    finally:
        os.umask(saved)
    for what, got, want in failures[:12]:
        print("MISMATCH %s\n  native %r\n  python %r" % (what, got, want))
    print("filesystem-writers: %d cases and %d interleaved lines, %d mismatches"
          % (checked, lines, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
