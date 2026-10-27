#!/usr/bin/env python3
"""system:expanduser against os.path.expanduser, under every kind of HOME.

HOME is absent (the account database answers for getuid()), empty, the root,
absolute with and without trailing slashes, relative, non-ASCII, and "~". Each
is crossed with paths that start with "~" alone, "~/", "~name" for this
account, root and a missing account, and paths that do not start with "~".
The engine runs in PeTTa with the HOME under test; Python computes the same
in this process. One environment also runs in HE and Prime.
"""
import argparse
import os
from pathlib import Path
import pwd
import subprocess
import sys
import tempfile

ME = pwd.getpwuid(os.getuid()).pw_name
HOMES = [None, "", "/", "//", "/home/test", "/home/test/", "/home/test//", "relative/dir", "relative/",
         "/home/tëst ü", "~", "/a b/c"]
PATHS = ["~", "~/", "~/a", "~/a/b/", "~//x", "~~", "~/~", "~nosuchuser_q7/x", "~nosuchuser_q7",
         "~root", "~root/", "~root/x", "~" + ME, "~" + ME + "/p", "~" + ME + "/", "a~", "/~", "", "x/~/y", "~ /x"]
IMPORTS = {"petta": "!(import! &self (library system))\n", "he": "!(import! &self system)\n",
           "prime": "!(import! &self system)\n"}


def literal(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def python_expand(path, home):
    saved = os.environ.get("HOME")
    try:
        if home is None:
            os.environ.pop("HOME", None)
        else:
            os.environ["HOME"] = home
        return os.path.expanduser(path)
    finally:
        if saved is None:
            os.environ.pop("HOME", None)
        else:
            os.environ["HOME"] = saved


def run(binary, dialect, home):
    environment = dict(os.environ)
    environment.pop("HOME", None)
    if home is not None:
        environment["HOME"] = home
    with tempfile.TemporaryDirectory() as directory:
        program = Path(directory) / "expanduser.metta"
        program.write_text(IMPORTS[dialect] + "".join("!(println! (RESULT (system:expanduser %s)))\n" % literal(p)
                                                      for p in PATHS), encoding="utf-8")
        result = subprocess.run([str(binary.resolve()), "--lang", dialect, str(program)], env=environment,
                                capture_output=True, encoding="utf-8", errors="replace", timeout=120)
    if result.returncode:
        raise SystemExit("%s: engine exited %d: %s" % (dialect, result.returncode, result.stderr[-1500:]))
    lines = [line[len("(RESULT "):-1] for line in result.stdout.split("\n") if line.startswith("(RESULT ")]
    if len(lines) != len(PATHS):
        raise SystemExit("%s: expected %d results, read %d" % (dialect, len(PATHS), len(lines)))
    return lines


def printed(dialect, text):
    """How println! shows a string result: quoted in PeTTa, quoted inside an
    expression in HE and Prime too."""
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binary", type=Path)
    arguments = parser.parse_args()
    failures, total = [], 0
    runs = [("petta", home) for home in HOMES] + [("he", None), ("prime", None), ("he", "/home/test/")]
    for dialect, home in runs:
        for path, got in zip(PATHS, run(arguments.binary, dialect, home)):
            total += 1
            want = printed(dialect, python_expand(path, home))
            if got != want:
                failures.append((dialect, home, path, got, want))
    for dialect, home, path, got, want in failures[:30]:
        print("FAIL %s HOME=%r path=%r:\n  got  %s\n  want %s" % (dialect, home, path, got, want))
    print("expanduser: %d cases, %d failures" % (total, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
