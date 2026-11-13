#!/usr/bin/env python3
"""Readers left on a space's rows answer as readers given their own copy.

A removal that only marks rows leaves the readers open on the space where
they are (space.c, "Frozen views"); before, every removal copied the rows for
them, and CETTA_SPACE_READERS_REFERENCE=1 still does.  The two must be the
same store to every reader.  Each program here opens readers and removes,
adds and takes under them: a reader removing what it reads, removing rows it
has yet to read, removing rows it has read; readers inside readers; a
transform taking under its own reader; pops under a reader of a priority
space; at sizes on both sides of where a space reads its indexes and where a
frozen view counts its removed rows by bits.  Every program runs both ways,
in PeTTa and in HE, and each reader's answers must be the same bag.  (What
a reader that removes as it reads meets depends on the order it visits rows
in; both ways visit them alike, the same cursor over the same rows, which is
what makes the mechanisms comparable.  No order of answers is promised.)
"""
import os
import subprocess
import sys
import tempfile

from space_run_modes import run_both

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

SPACES = (
    ("atom", "(new-space)"),
    ("atom strict", "(new-space atom strict)"),
    ("set", "(new-space set)"),
    ("priority", "(new-space priority)"),
    ("priority full", "(new-space priority full)"),
)

# Each reader is one answer line; {n} is the number of jobs.
READERS = (
    ("removes what it reads",
     "(collapse (match &s (job $j) (let $_ (remove-atom &s (job $j)) $j)))"),
    ("removes the job after the one it reads",
     "(collapse (match &s (job $j) (let* (($k (+ $j 1)) ($_ (remove-atom &s (job $k)))) $j)))"),
    ("removes the job before the one it reads",
     "(collapse (match &s (job $j) (let* (($k (- $j 1)) ($_ (remove-atom &s (job $k)))) $j)))"),
    ("removes every other job at its first answer",
     "(collapse (match &s (job $j) (let $_ (if (== $j 0) (collapse (match &s (job $k) "
     "(if (== (% $k 2) 1) (remove-atom &s (job $k)) skip))) skip) $j)))"),
    ("adds as it reads",
     "(collapse (match &s (job $j) (let* (($k (+ $j {n})) ($_ (add-atom &s (job $k)))) $j)))"),
    ("a reader inside a reader, removing",
     "(collapse (match &s (job $i) (match &s (job $j) "
     "(if (== $j (+ $i 1)) (let $_ (remove-atom &s (job $j)) (pair $i $j)) (empty)))))"),
    ("a transform taking under its own reader",
     "(collapse (transform &s (take (job $j)) (took $j)))"),
    ("a transform reading one job and taking another",
     "(collapse (transform &s (, (job $i) (take (job $j))) (pair $i $j)))"),
    ("first answers that take, one after another",
     "(collapse (let $i (superpose (1 2 3 4 5 6)) (once (transform &s (take (job $j)) (took $i $j)))))"),
)

PRIORITY_READERS = (
    ("pops under its reader",
     "(collapse (match &s (job $j) (let $p (space:priority:pop &s) (read $j popped $p))))"),
    ("truncates under its reader",
     "(collapse (match &s (job $j) (let $_ (if (== $j 2) (space:priority:truncate &s 3) skip) $j)))"),
)


def program(space, reader, n):
    lines = ["!(bind! &s %s)" % space]
    # Out of order, with a tag row among the jobs so no index covers the space.
    for i in range(n):
        j = (i * 7 + 3) % n
        lines.append("!(add-atom &s (job %d))" % j)
        if i % 5 == 0:
            lines.append("!(add-atom &s (tag %d))" % j)
    lines.append("!(answer %s)" % reader.format(n=n))
    lines.append("!(jobs-left (size-atom (collapse (match &s (job $j) x))))")
    lines.append("!(tags-left (size-atom (collapse (match &s (tag $j) x))))")
    lines.append("!(again %s)" % reader.format(n=n))
    lines.append("!(jobs-then (size-atom (collapse (match &s (job $j) x))))")
    return "\n".join(lines) + "\n"


def run_once(binary, lang, text, reference, mode_env):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(text)
        path = f.name
    try:
        env = dict(os.environ)
        env.pop("CETTA_SPACE_READERS_REFERENCE", None)
        if reference:
            env["CETTA_SPACE_READERS_REFERENCE"] = "1"
        env.update(mode_env)
        p = subprocess.run([binary, "--lang", lang, "--profile", "extended", path],
                           cwd=ROOT, env=env, capture_output=True, text=True, timeout=600)
    finally:
        os.unlink(path)
    kept = [l for l in p.stdout.splitlines()
            if "answer" in l or "left" in l or "again" in l or "jobs-then" in l]
    return p.returncode, kept, p.stderr.strip().splitlines()[-1:] if p.stderr.strip() else []


def elements(text):
    """The top-level elements of the expression TEXT."""
    inner = text.strip()[1:-1]
    out, depth, cur, quoted = [], 0, "", False
    for ch in inner:
        if ch == '"':
            quoted = not quoted
        if not quoted and ch == "(":
            depth += 1
        if not quoted and ch == ")":
            depth -= 1
        if not quoted and depth == 0 and ch.isspace():
            if cur:
                out.append(cur)
            cur = ""
            continue
        cur += ch
    if cur:
        out.append(cur)
    return out


def as_bag(line):
    """A printed answer line, (label (x ...)) or [(label (x ...))], with the
    elements of its collapse sorted."""
    text = line.strip()
    if text.startswith("[") and text.endswith("]"):
        text = text[1:-1]
    if not text.startswith("("):
        return text
    fields = elements(text)
    if len(fields) == 2 and fields[1].startswith("("):
        return "(%s (%s))" % (fields[0], " ".join(sorted(elements(fields[1]))))
    return text


def run(binary, lang, text, reference):
    code, lines, stderr = run_both(
        lambda mode_env: run_once(binary, lang, text, reference, mode_env),
        same=lambda a, b: as_bag(a) == as_bag(b))
    return code, [as_bag(line) for line in lines], stderr


def main():
    binary = os.path.abspath(sys.argv[1])
    compared = 0
    for n in (6, 18, 40, 150):
        for space_name, space in SPACES:
            readers = READERS + (PRIORITY_READERS if "priority" in space_name else ())
            for reader_name, reader in readers:
                text = program(space, reader, n)
                for lang in ("petta", "he"):
                    kept = run(binary, lang, text, False)
                    copied = run(binary, lang, text, True)
                    if kept != copied:
                        print("FAIL: %s space of %d, %s, %s" % (space_name, n, reader_name, lang))
                        print("  reader left on the rows:", kept[0], kept[1][:2], kept[2])
                        print("  reader given a copy    :", copied[0], copied[1][:2], copied[2])
                        for a, b in zip(kept[1], copied[1]):
                            if a != b:
                                print("  first difference:\n    %s\n    %s" % (a[:300], b[:300]))
                                break
                        return 1
                    compared += 1
    print("PASS: readers left on a space's rows answer as readers given a copy: %d "
          "programs (%d readers, %d kinds of space, four sizes, PeTTa and HE)"
          % (compared, len(READERS) + len(PRIORITY_READERS), len(SPACES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
