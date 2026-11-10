#!/usr/bin/env python3
"""transform without an import under --profile extended, in both lanes.

Usage: check_transform_profiles.py CETTA_BINARY

Under `--profile extended`, HE and PeTTa provide transform without an import
through the native module: a program that never
imports lib_transform transforms, natively over a named space.  Under the
plain profiles, as in stable PeTTa and HE, transform is a symbol like any
other until the program imports its library.  A program that imports the
library under extended answers as it does under the plain profile: the second
import changes nothing.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

BODY = ("!(bind! &s (new-space))\n"
        "!(add-atom &s (job a))\n!(add-atom &s (job b))\n"
        "!(sort-atom (collapse (transform &s (take (job $j)) (done $j))))\n"
        "!(collapse (match &s (job $j) $j))\n")
# the answers under each lane's printing
EXTENDED = {"he": "[((done a) (done b))]\n[()]", "petta": "((done a) (done b))\n()"}
IMPORTED = {"he": "!(import! &self (library lib_transform))\n",
            "petta": "!(import! &self (library lib_transform))\n"}


def run(binary, lang, profile, path, trace=False):
    command = [binary, "--lang", lang] + (["--profile", profile] if profile else []) + [path]
    env = dict(os.environ)
    if trace:
        env["CETTA_TRANSFORM_TRACE"] = "1"
    p = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout, p.stderr


def last_lines(stdout, n):
    return "\n".join(stdout.splitlines()[-n:])


def main():
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="cetta-transform-profiles-", dir=os.path.join(ROOT, "runtime")) as work:
        for lang in ("he", "petta"):
            plain = os.path.join(work, f"plain_{lang}.metta")
            with open(plain, "w") as f:
                f.write(BODY)
            code, out, err = run(binary, lang, "extended", plain, trace=True)
            fired = [l for l in err.splitlines() if l.startswith("[transform] fired")]
            if code != 0 or last_lines(out, 2) != EXTENDED[lang] or not fired:
                print(f"FAIL: {lang} --profile extended: transform without an import "
                      f"answered {last_lines(out, 2)!r} (native firings: {len(fired)})")
                return 1
            code, out, _ = run(binary, lang, None, plain)
            if code != 0 or "(transform " not in out:
                print(f"FAIL: {lang}: without --profile extended and without an import, "
                      f"transform was not a plain symbol: {last_lines(out, 2)!r}")
                return 1
            imported = os.path.join(ROOT, "runtime", f"transform_profiles_imported_{lang}.metta")
            with open(imported, "w") as f:
                f.write(IMPORTED[lang] + BODY)
            try:
                with_profile = run(binary, lang, "extended", imported)
                without = run(binary, lang, None, imported)
            finally:
                os.unlink(imported)
            if with_profile[0] != 0 or with_profile[1] != without[1] or \
                    last_lines(without[1], 2) != EXTENDED[lang]:
                print(f"FAIL: {lang}: importing lib_transform under --profile extended "
                      f"answered {with_profile[1]!r}, without it {without[1]!r}")
                return 1
    print("PASS: --profile extended provides transform without an import in HE and PeTTa, "
          "natively; the plain profiles keep it a symbol until the library is imported; "
          "an import under extended changes nothing")
    return 0


if __name__ == "__main__":
    sys.exit(main())
