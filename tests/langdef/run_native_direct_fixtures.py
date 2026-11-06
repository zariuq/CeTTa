#!/usr/bin/env python3
"""The C target of the direct GSLT compiler on its fixtures (native_direct/).

Each FILE.metta is a presentation with its entry modes in FILE.entries.
When FILE.expected starts with "refused:", compiling it to C must fail with
that text in the message.  Otherwise it compiles, its calls (FILE.calls,
ENTRY INPUT... per line) run natively (native_direct/run_entry.c) and must
print FILE.expected, and the same calls on the PeTTa target, evaluated by
CeTTa, must give the same answers.  An input "..." is a source text: a
cursor over its bytes natively, the list (cons (cp N) ...) its scalars make
on the PeTTa target.  The modes compiled as class runs (scans) are those in
FILE.scans, and those compiled as token automata those in FILE.automata,
none when the file is absent.

usage: run_native_direct_fixtures.py LANGDEF_COMPILER CETTA [--work DIR]
"""
import argparse
import pathlib
import re
import subprocess
import sys

GROUND = ["experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta",
          "experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_relations_v1.metta"]


def compile_args(compiler, source, entries, target, out):
    args = [compiler, "petta-direct", "--target", target]
    if target == "c":
        args += ["--c-prefix", "fixture"]
    for g in GROUND + [str(source)]:
        args += ["--source", g]
    for e in entries:
        args += ["--closed-entry-mode", e]
    return args + ["--out", str(out)]


TEXT = re.compile(r'"((?:[^"\\]|\\x[0-9a-fA-F]{2}|\\["\\])*)"')


def text_list(match):
    """A source text as the list langdef reads it: its bytes decoded as
    UTF-8, a byte that is not UTF-8 as U+DC80..U+DCFF."""
    raw = bytearray()
    body = match.group(1)
    i = 0
    while i < len(body):
        if body.startswith('\\x', i):
            raw.append(int(body[i + 2:i + 4], 16))
            i += 4
        elif body[i] == '\\':
            raw.append(ord(body[i + 1]))
            i += 2
        else:
            raw.extend(body[i].encode())
            i += 1
    out = 'nil'
    for ch in reversed(bytes(raw).decode('utf-8', 'surrogateescape')):
        out = '(cons (cp %d) %s)' % (ord(ch), out)
    return out


def scanned(c_source):
    """The modes the C target compiled as class runs."""
    return sorted(re.findall(r'^/\* (\S+) \(class run\) \*/$', c_source, re.M))


def automata(c_source):
    """The modes the C target compiled as token automata."""
    return sorted(re.findall(r'^/\* (\S+) as a token automaton', c_source, re.M))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("compiler")
    ap.add_argument("cetta")
    ap.add_argument("--work", default="runtime/langdef-native-direct")
    args = ap.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    here = root / "tests/langdef/native_direct"
    work = (root / args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    passed = failed = 0
    for source in sorted(here.glob("*.metta")):
        name = source.stem
        entries = source.with_suffix(".entries").read_text().split()
        expected = source.with_suffix(".expected").read_text()
        c_out = work / (name + ".c")
        done = subprocess.run(compile_args(args.compiler, source, entries, "c", c_out), cwd=root,
                              capture_output=True, text=True)
        message = done.stdout + done.stderr
        problem = None
        if expected.startswith("refused:"):
            want = expected[len("refused:"):].strip()
            if done.returncode == 0:
                problem = "compiled, but should be refused (%s)" % want
            elif want not in message:
                problem = "refused with %r, not %r" % (message.strip()[-200:], want)
        elif done.returncode != 0:
            problem = "refused: %s" % message.strip()[-300:]
        elif scanned(c_out.read_text()) != sorted(source.with_suffix(".scans").read_text().split()
                                                  if source.with_suffix(".scans").exists() else []):
            problem = "class runs %s" % scanned(c_out.read_text())
        elif automata(c_out.read_text()) != sorted(source.with_suffix(".automata").read_text().split()
                                                   if source.with_suffix(".automata").exists() else []):
            problem = "token automata %s" % automata(c_out.read_text())
        else:
            binary = work / name
            build = subprocess.run(["cc", "-O1", "-std=c11", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
                                    "-I.", str(c_out), "native/gslt_native_runtime_v1.c",
                                    str(here / "run_entry.c"), "-o", str(binary)],
                                   cwd=root, capture_output=True, text=True)
            calls = source.with_suffix(".calls").read_text()
            if build.returncode != 0:
                problem = "C build failed: %s" % build.stderr[-400:]
            else:
                native = subprocess.run([str(binary)], input=calls, capture_output=True, text=True).stdout
                if native != expected:
                    problem = "native answers %r, expected %r" % (native, expected)
                else:
                    program = work / (name + ".petta.metta")
                    done = subprocess.run(compile_args(args.compiler, source, entries, "petta", program),
                                          cwd=root, capture_output=True, text=True)
                    lines = [program.read_text()]
                    for call in calls.splitlines():
                        if call.strip():
                            entry, _, inputs = call.partition(" ")
                            lines.append("!(println! (collapse (gslt:entry:%s %s)))" % (entry, TEXT.sub(text_list, inputs)))
                    driver = work / (name + ".driver.metta")
                    driver.write_text("\n".join(lines) + "\n")
                    ran = subprocess.run([args.cetta, "--lang", "petta", str(driver)], cwd=root,
                                         capture_output=True, text=True, timeout=300)
                    petta = "".join(l + "\n" for l in ran.stdout.splitlines() if l.startswith("("))
                    if petta != native:
                        problem = "PeTTa target answers %r, native %r" % (petta, native)
        if problem:
            failed += 1
            print("FAIL %s: %s" % (name, problem))
        else:
            passed += 1
    print("(GsltNativeDirectFixtures %d passed %d failed)" % (passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
