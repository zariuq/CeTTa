#!/usr/bin/env python3
"""Stack and queue spaces against a model of them: their reads follow their
order.

A stack's order is its atoms newest first, a queue's oldest first.  Random
programs add atoms, take the first atom of a shape (`once` of a take),
remove atoms, and read: a collapse of a match, `once` and `select` of one,
a conjunction, which answers in the order of its patterns as written, each
read in the kind's order, and the same reads from a function's body.  The
model is the list of atoms in the order they arrived, written independently
of the engine; every read is tagged and its answer compared exactly.  Each program runs in
PeTTa (the compiled tier and the machine) and in HE, in the extended
profile.  `get-atoms` promises no order and is not asked.

Usage: check_ordered_space_reads.py CETTA [PROGRAMS]
"""
import os
import random
import subprocess
import sys
import tempfile

KINDS = ("stack", "queue")
KEYS = (1, 2, 3)


def ordered(kind, atoms):
    """The model's atoms in the kind's order."""
    return list(reversed(atoms)) if kind == "stack" else list(atoms)


def show(atom):
    return "(" + " ".join(str(part) for part in atom) + ")"


def program(rng, kind, steps):
    """A program, and the model's answer to each read, by the read's tag."""
    lines = ["!(bind! &s (new-space %s))" % kind,
             "(= (body-read $k) (collapse (match &s (j $k $v) $v)))"]
    answers = {}
    atoms = []

    def ask(text, answer):
        tag = len(answers) + 1
        lines.append("!(r %d %s)" % (tag, text))
        answers[tag] = "(r %d %s)" % (tag, answer)

    for _ in range(steps):
        roll = rng.random()
        key = rng.choice(KEYS)
        if roll < 0.40 or not atoms:
            value = rng.randrange(10)
            atom = ("j", key, value) if rng.random() < 0.7 else ("k", value)
            atoms.append(atom)
            lines.append("!(add-atom &s %s)" % show(atom))
            continue
        view = ordered(kind, atoms)
        hits = [a for a in view if a[0] == "j" and a[1] == key]
        if roll < 0.50:
            # Take the first (j KEY $v) in the kind's order.
            if hits:
                atoms.remove(hits[0])
                ask("(once (transform &s (take (j %d $v)) $v))" % key, str(hits[0][2]))
        elif roll < 0.56:
            # Remove one occurrence through the shared public contract.
            atom = rng.choice(atoms)
            if atoms.count(atom) == 1:
                atoms.remove(atom)
                lines.append("!(remove-atom &s %s)" % show(atom))
        elif roll < 0.70:
            ask("(collapse (match &s (j %d $v) $v))" % key,
                "(" + " ".join(str(a[2]) for a in hits) + ")")
        elif roll < 0.78:
            if hits:
                ask("(once (match &s (j %d $v) $v))" % key, str(hits[0][2]))
        elif roll < 0.84:
            if len(hits) >= 2:
                ask("(select 2 (match &s (j %d $v) $v))" % key,
                    "(" + " ".join(str(a[2]) for a in hits[:2]) + ")")
        elif roll < 0.92:
            # (j K $v) then (k $v): for each first atom in order, the second
            # pattern's atoms in order.
            pairs = [a[2] for a in hits for b in view
                     if b[0] == "k" and b[1] == a[2]]
            ask("(collapse (match &s (, (j %d $v) (k $v)) (p $v)))" % key,
                "(" + " ".join("(p %d)" % v for v in pairs) + ")")
        else:
            ask("(body-read %d)" % key, "(" + " ".join(str(a[2]) for a in hits) + ")")
    return lines, answers


def run(binary, lang, lines, environment):
    with tempfile.TemporaryDirectory(prefix="ordered-reads-") as directory:
        path = os.path.join(directory, "program.metta")
        with open(path, "w") as handle:
            handle.write("\n".join(lines) + "\n")
        env = dict(os.environ)
        env.update(environment)
        result = subprocess.run([binary, "--lang", lang, "--profile", "extended", path],
                                capture_output=True, text=True, timeout=60, env=env)
    return result.returncode, result.stdout.splitlines(), result.stderr


def compare(lang, answers, output):
    """Each tagged read's answer against the model's; other lines ignored."""
    got = {}
    for line in output:
        text = line[1:-1] if lang == "he" and line.startswith("[") else line
        if text.startswith("(r "):
            try:
                got[int(text.split()[1])] = text
            except ValueError:
                pass
    for tag, expected in answers.items():
        if got.get(tag) != expected:
            return tag, expected, got.get(tag)
    return None


def main():
    binary = sys.argv[1]
    programs = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    checked = 0
    for seed in range(programs):
        rng = random.Random(seed)
        kind = KINDS[seed % len(KINDS)]
        lines, answers = program(rng, kind, 30 + seed % 20)
        for lang, environment in (("petta", {}),
                                  ("petta", {"CETTA_OPEN_EQUATIONS_REFERENCE": "1"}),
                                  ("he", {})):
            code, output, stderr = run(binary, lang, lines, environment)
            if code != 0:
                print("FAIL: seed %d, %s %s %s: exit %d: %s"
                      % (seed, kind, lang, environment, code, stderr.strip()[-300:]))
                return 1
            mismatch = compare(lang, answers, output)
            if mismatch:
                tag, expected, actual = mismatch
                asked = next(l for l in lines if l.startswith("!(r %d " % tag))
                print("FAIL: seed %d, %s space, %s %s\n  asked : %s\n  model : %s\n  engine: %s"
                      % (seed, kind, lang, environment, asked, expected, actual))
                return 1
            checked += len(answers)
    print("PASS: stack and queue spaces answer their reads in their order: %d answers "
          "of collapses, once, select, conjunctions, takes and function bodies, "
          "PeTTa on the tier and the machine and HE" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
