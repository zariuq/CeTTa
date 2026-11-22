#!/usr/bin/env python3
"""The C target of the direct GSLT compiler (petta-direct --target c, through
rulec) on its fixtures (tests/langdef/gslt_c/).

Each FILE.metta is a presentation with its entry modes in FILE.entries.  When
FILE.expected starts with "refused:", compiling it to C must fail with that
text in the message.  Otherwise it compiles, rulec_validate checks the decision
DAGs and the generated C against the rules, the C builds with strict warnings
into the answers driver (tests/support/gslt_c_answers_v1.c), and its calls
(FILE.calls, MODE INPUT... per line) give, as sets of answers, FILE.expected and
the answers of the PeTTa target evaluated by CeTTa.  An input "..." is a source
text, the list (cons (cp N) ...) of its scalars (bytes that are not UTF-8 read
as U+DC80..U+DCFF).

usage: run_gslt_c_fixtures.py LANGDEF_COMPILER RULEC_VALIDATE CETTA [--work DIR]
"""
import argparse
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "support"))
from check_transform_swi_differential import read_forms, write_form

GROUND = ["experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta",
          "experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_relations_v1.metta"]


def compile_args(compiler, source, entries, target, out):
    args = [compiler, "petta-direct", "--target", target]
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


def answer_sets(text):
    """Each line's answers ((gslt:result:MODE OUT ...) ...) as a set of their spellings."""
    sets = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        forms = read_forms(line)
        if len(forms) != 1 or not isinstance(forms[0], list):
            raise ValueError("not a line of answers: " + line)
        sets.append(frozenset(write_form(answer) for answer in forms[0]))
    return sets


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("compiler")
    ap.add_argument("validate")
    ap.add_argument("cetta")
    ap.add_argument("--work", default="runtime/langdef-gslt-c")
    args = ap.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    here = root / "tests/langdef/gslt_c"
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
        else:
            checked = subprocess.run([args.validate, str(c_out) + ".wire", str(c_out) + ".dag", str(c_out)],
                                     capture_output=True, text=True)
            binary = work / name
            build = subprocess.run(["cc", "-O1", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I.",
                                    '-DGSLT_C_PROGRAM="%s"' % c_out, "tests/support/gslt_c_answers_v1.c",
                                    "-o", str(binary)], cwd=root, capture_output=True, text=True)
            calls = [line.strip() for line in source.with_suffix(".calls").read_text().splitlines()
                     if line.strip() and not line.strip().startswith(";")]
            spelled = []
            for call in calls:
                entry, _, inputs = call.partition(" ")
                spelled.append((entry, TEXT.sub(text_list, inputs)))
            calls_file = work / (name + ".calls")
            calls_file.write_text("".join("%s %s\n" % call for call in spelled))
            if checked.returncode != 0:
                problem = "rulec_validate: %s" % (checked.stdout + checked.stderr)[-600:]
            elif build.returncode != 0:
                problem = "C build failed: %s" % build.stderr[-600:]
            else:
                native_run = subprocess.run([str(binary), "--calls", str(calls_file)], capture_output=True,
                                            text=True, timeout=300)
                native = native_run.stdout
                if native_run.returncode != 0:
                    problem = "native execution exited %d: %s" % (native_run.returncode, native_run.stderr[-400:])
                elif answer_sets(native) != answer_sets(expected):
                    problem = "native answers %r, expected %r" % (native, expected)
                else:
                    program = work / (name + ".petta.metta")
                    done = subprocess.run(compile_args(args.compiler, source, entries, "petta", program),
                                          cwd=root, capture_output=True, text=True)
                    if done.returncode != 0:
                        failed += 1
                        print("FAIL %s: PeTTa compilation exited %d: %s" % (name, done.returncode, done.stderr))
                        continue
                    if name == "nondet_relations":
                        definitions = [f for f in read_forms(program.read_text())
                                       if isinstance(f, list) and len(f) == 3
                                       and f[0] == "=" and isinstance(f[1], list)]
                        # Member forwards its recursive result; Append must
                        # still construct the left result's new prefix.
                        for relation, forwarded in (("Member:01", True), ("Append:001", False)):
                            head = "gslt:mode:" + relation
                            bodies = [f[2] for f in definitions if f[1][:1] == [head]]
                            actual = any(isinstance(b, list) and b[:1] == [head] for b in bodies)
                            if len(bodies) != 2 or actual != forwarded:
                                raise AssertionError(f"unexpected self-result forwarding for {relation}")
                    lines = [program.read_text()]
                    for entry, inputs in spelled:
                        values = read_forms(inputs)
                        if len(values) != entry.rsplit(":", 1)[1].count("1"):
                            raise ValueError("wrong input count in fixture call: %s %s" % (entry, inputs))
                        held = " ".join("(quote " + write_form(value) + ")" for value in values)
                        lines.append("!(let $_printed (println! (collapse (gslt:entry:%s %s))) (empty))" % (entry, held))
                    driver = work / (name + ".driver.metta")
                    driver.write_text("\n".join(lines) + "\n")
                    ran = subprocess.run([sys.executable, str(root / "tests/support/run_gslt_program.py"),
                                          args.cetta, "--lang", "petta", str(driver)], cwd=root,
                                         capture_output=True, text=True, timeout=300)
                    if ran.returncode != 0:
                        problem = "PeTTa execution exited %d: %s" % (ran.returncode, ran.stderr[-400:])
                    elif answer_sets(ran.stdout) != answer_sets(native):
                        problem = "PeTTa target answers %r, native %r" % (ran.stdout, native)
        if problem:
            failed += 1
            print("FAIL %s: %s" % (name, problem))
        else:
            passed += 1
    print("(GsltCFixtures %d passed %d failed)" % (passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
