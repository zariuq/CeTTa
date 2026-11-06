#!/usr/bin/env python3
"""The HE canonical machine's last-call rule.

A frame whose last child is a call in tail position, and which has nothing
left to do but publish that child's answers, leaves the stack when a producer
stands in for the child (eval_he_canonical.inc, he_last_call).

--memory (a runtime-stats build): a recursion whose levels end in such calls
holds the same number of frames at two depths, and its peak RSS at 10,000 and
100,000 levels differs by a small constant; a recursion whose levels keep
work after their child (a second equation, a superposed alternative, a typed
argument checked after its value, an error a later alternative drops) holds
more frames the deeper it runs.  With CETTA_HE_LAST_CALL=0 every frame stays.

--answers N (any build): the rule answers as the machine without it.  Named
fixtures and N programs drawn from a seeded generator run with the rule on
and off; outputs must be equal after normalizing addresses and fresh
variable numbers.  A differing program is shrunk to a minimal one."""

import argparse
import os
import random
import re
import resource
import subprocess
import sys
import tempfile

TIMEOUT = 60


def limit_memory():
    limit = 24 * 1024 ** 3
    resource.setrlimit(resource.RLIMIT_AS, (limit, limit))


def run(binary, path, env=None, stats=False):
    """Output, counters and peak RSS (KiB) of one run."""
    command = [binary] + (["--emit-runtime-stats"] if stats else []) + \
        ["--lang", "he", path]
    proc = subprocess.Popen(command, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True,
                            env=dict(os.environ, **(env or {})),
                            preexec_fn=limit_memory)
    try:
        out, err = proc.communicate(timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        return "timeout", "", {}, ""
    counters = {}
    kept = []
    for line in err.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "runtime-counter":
            counters[parts[1]] = int(parts[2])
        else:
            kept.append(line)
    return proc.returncode, out, counters, "\n".join(kept)


def peak_rss_kib(binary, path, env):
    """Peak resident set of one run, from the child's own rusage."""
    pid = os.fork()
    if pid == 0:
        try:
            limit_memory()
            devnull = os.open(os.devnull, os.O_WRONLY)
            os.dup2(devnull, 1)
            os.dup2(devnull, 2)
            os.execve(binary, [binary, "--lang", "he", path],
                      dict(os.environ, **env))
        finally:
            os._exit(127)
    _, status, usage = os.wait4(pid, 0)
    if not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
        sys.exit(f"FAIL: {path} exited with status {status}")
    return usage.ru_maxrss


# ---------------------------------------------------------------- memory

LOOP_PY = """(= (loop $n) (if (== $n 0) done (let $o ((py-atom math.sqrt) 4.0) (loop (- $n 1)))))
!(loop {n})
"""
LOOP_CAR = """(= (loop $n) (if (== $n 0) done (let $o (car-atom (a b)) (loop (- $n 1)))))
!(loop {n})
"""
LOOP_TYPED = """(: loop (-> Number Symbol))
(= (loop $n) (if (== $n 0) done (let $o (car-atom (a b)) (loop (- $n 1)))))
!(loop {n})
"""
# The same recursion inside a let's source and as an argument, where
# bindings are kept, and through case and a wrapper.
LOOP_KEPT = """(= (loop $n) (if (== $n 0) done (case (car-atom (a b)) ((a (loop (- $n 1))) ($_ never)))))
(= (wrap $x) $x)
!(let $r (loop {n}) $r)
!(wrap (loop {n}))
"""
# An open call: its variable is passed down and bound at the bottom.
LOOP_OPEN = """(= (loop $n $x) (if (== $n 0) (unify $x k $x no) (let $o (car-atom (a b)) (loop (- $n 1) $x))))
!(loop {n} $q)
"""
POSITIVE = (("python", LOOP_PY, "[done]\n"),
             ("car-atom", LOOP_CAR, "[done]\n"),
             ("typed", LOOP_TYPED, "[done]\n"),
             ("kept bindings", LOOP_KEPT, "[done]\n[done]\n"),
             ("open call", LOOP_OPEN, "[k]\n"))

# Each level keeps work after its child, on the canonical route.
NEGATIVE = (
    ("two equations",
     """(= (loop $n) (if (== $n 0) done (let $o (car-atom (a b)) (loop (- $n 1)))))
(= (loop $n) other)
!(collapse (loop {n}))
"""),
    ("superpose",
     """(= (loop $n) (if (== $n 0) done (let $o (car-atom (a b)) (superpose ((loop (- $n 1)) z)))))
!(collapse (loop {n}))
"""),
    ("typed argument",
     """(: idn (-> Number Number))
(= (idn $x) $x)
(= (loop $n) (if (== $n 0) 0 (let $o (car-atom (a b)) (idn (loop (- $n 1))))))
!(loop {n})
"""),
    ("error a later alternative drops",
     """(= (loop $n) (Error (loop $n) nope))
(= (loop $n) (if (== $n 0) done (let $o (car-atom (a b)) (loop (- $n 1)))))
!(let $r (loop {n}) $r)
"""),
)

FRAME_DEPTHS = (200, 400)
RSS_DEPTHS = (10000, 100000)
# What a recursion's frames and their roots may add between the two depths
# under a small collection budget: the arena's churn, not its depth.
RSS_SLACK_KIB = 12 * 1024


def python_available(binary, scratch):
    path = os.path.join(scratch, "probe.metta")
    with open(path, "w") as out:
        out.write("!((py-atom math.sqrt) 4.0)\n")
    status, out, _, _ = run(binary, path)
    return status == 0 and out == "[2.0]\n"


def program_path(scratch, kind, name, n):
    return os.path.join(scratch, f"{kind}_{re.sub(r'[^a-z]+', '_', name)}_{n}.metta")


def frame_peak(binary, scratch, name, template, n, env=None):
    path = program_path(scratch, "frames", name, n)
    with open(path, "w") as out:
        out.write(template.format(n=n))
    status, out, counters, err = run(binary, path, env, stats=True)
    if status != 0:
        sys.exit(f"FAIL: {name} at depth {n} exited {status}: {err[-300:]}")
    if "he-canonical-frame-peak" not in counters:
        sys.exit("FAIL: the binary reports no he-canonical-frame-peak "
                 "(a runtime-stats build is required)")
    return out, counters["he-canonical-frame-peak"]


def check_memory(binary):
    with tempfile.TemporaryDirectory() as scratch:
        python = python_available(binary, scratch)
        notes = []
        for name, template, expected in POSITIVE:
            if name == "python" and not python:
                notes.append("python skipped: this build has no py-atom")
                continue
            peaks = []
            for n in FRAME_DEPTHS:
                out, peak = frame_peak(binary, scratch, name, template, n)
                if out != expected:
                    sys.exit(f"FAIL: {name} at depth {n} answered {out!r}")
                peaks.append(peak)
            if peaks[0] != peaks[1]:
                sys.exit(f"FAIL: {name} holds {peaks} frames at depths "
                         f"{FRAME_DEPTHS}")
            _, kept = frame_peak(binary, scratch, name, template,
                                 FRAME_DEPTHS[0], {"CETTA_HE_LAST_CALL": "0"})
            if kept <= peaks[0]:
                sys.exit(f"FAIL: {name} without the rule holds {kept} frames, "
                         f"with it {peaks[0]}")
            rss = []
            for n in RSS_DEPTHS:
                path = program_path(scratch, "rss", name, n)
                with open(path, "w") as out:
                    out.write(template.format(n=n))
                rss.append(peak_rss_kib(binary, path,
                                        {"CETTA_GC_BUDGET_MB": "8"}))
            if rss[1] - rss[0] > RSS_SLACK_KIB:
                sys.exit(f"FAIL: {name} peaks at {rss} KiB at depths "
                         f"{RSS_DEPTHS}")
        for name, template in NEGATIVE:
            peaks = []
            for n in FRAME_DEPTHS:
                out, peak = frame_peak(binary, scratch, name, template, n)
                reference, _ = frame_peak(binary, scratch, name, template, n,
                                          {"CETTA_HE_LAST_CALL": "0"})
                if out != reference:
                    sys.exit(f"FAIL: {name} at depth {n} answers {out!r}, "
                             f"without the rule {reference!r}")
                peaks.append(peak)
            if peaks[1] <= peaks[0]:
                sys.exit(f"FAIL: {name} holds {peaks} frames at depths "
                         f"{FRAME_DEPTHS}; its levels keep work")
    print("PASS: HE recursions whose levels end in a tail call hold the same "
          "frames at every depth and flat memory (Python, car-atom, typed, "
          "kept bindings, an open call); a second equation, a superposed "
          "alternative, a typed argument and a droppable error keep theirs"
          + ("" if not notes else " (" + "; ".join(notes) + ")"))


# ---------------------------------------------------------------- answers

HEX = re.compile(r"0x[0-9a-fA-F]+")
FRESH = re.compile(r"\$([A-Za-z_][\w\-]*)#[0-9]+")


def normalize(text):
    text = HEX.sub("0x?", text)
    lines = []
    for line in text.splitlines():
        names = {}

        def rename(match):
            key = match.group(0)
            if key not in names:
                names[key] = f"${match.group(1)}#{len(names)}"
            return names[key]
        lines.append(FRESH.sub(rename, line))
    return "\n".join(lines)


def differs(binary, scratch, program, tag, env=None):
    """None when both ways answer alike, "timeout" when either is unfinished,
    else the two normalized outcomes."""
    path = os.path.join(scratch, f"p_{tag}.metta")
    with open(path, "w") as out:
        out.write(program)
    on = run(binary, path, env)
    off = run(binary, path, dict(env or {}, CETTA_HE_LAST_CALL="0"))
    if on[0] == "timeout" or off[0] == "timeout":
        return "timeout"
    a = (on[0], normalize(on[1]), normalize(on[3]))
    b = (off[0], normalize(off[1]), normalize(off[3]))
    return (a, b) if a != b else None


def show(outcome):
    status, out, err = outcome
    return f"exit {status}\n{out}" + (f"\n[stderr]\n{err}" if err else "")


def shrink(binary, scratch, program, env=None):
    lines = [line for line in program.splitlines() if line.strip()]
    changed = True
    while changed:
        changed = False
        for i in range(len(lines)):
            trial = lines[:i] + lines[i + 1:]
            if not any(line.startswith("!") for line in trial):
                continue
            if isinstance(differs(binary, scratch, "\n".join(trial) + "\n",
                                  "shrink", env), tuple):
                lines = trial
                changed = True
                break
    return "\n".join(lines) + "\n"


FIXTURES = (
    ("closed loop at every binding context", """
(= (loop $n) (if (== $n 0) done (let $o (car-atom (a b)) (loop (- $n 1)))))
(= (wrap $x) $x)
!(loop 4)
!(let $r (loop 4) $r)
!(wrap (loop 4))
!(collapse (loop 4))
!(case (loop 4) (($x (got $x))))
!(collapse-bind (loop 4))
"""),
    ("open call whose answer binds its argument", """
(= (g $n $x) (if (== $n 0) (unify $x (k $n) $x no) (let $o (car-atom (a b)) (g (- $n 1) $x))))
!(g 3 $q)
!(let $r (g 3 $q) ($r $q))
!(collapse-bind (g 3 $q))
!(let ($r $s) ((g 2 $q) $q) (pair $r $s))
"""),
    ("let-bound variables reaching the answer", """
(= (h $n $x) (if (== $n 0) (pair $x done) (let $v (car-atom ($n b)) (h (- $n 1) (s $v)))))
!(h 3 a)
!(h 3 $q)
!(let $r (h 3 $q) ($r $q))
!(collapse-bind (h 2 $q))
"""),
    ("NotReducible, Empty, errors and stuck calls at the bottom", """
(= (nr $n) (if (== $n 0) NotReducible (let $o (car-atom (a b)) (nr (- $n 1)))))
(= (em $n) (if (== $n 0) (empty) (let $o (car-atom (a b)) (em (- $n 1)))))
(= (er $n) (if (== $n 0) (Error er bad) (let $o (car-atom (a b)) (er (- $n 1)))))
(= (st $n) (if (== $n 0) (nobody $n) (let $o (car-atom (a b)) (st (- $n 1)))))
!(nr 3)
!(let $r (nr 3) $r)
!(em 3)
!(collapse (em 3))
!(er 3)
!(superpose ((er 3) ok))
!(let $r (superpose ((er 2) ok)) $r)
!(st 3)
!(let $r (st 3) $r)
"""),
    ("NotReducible reached through tail calls", """
(= (ln $n) (if (== $n 0) (let $o (car-atom (a b)) NotReducible) (let $o (car-atom (a b)) (ln (- $n 1)))))
(= (cn $n) (if (== $n 0) (case (car-atom (a b)) (($_ NotReducible))) (case (car-atom (a b)) ((a (cn (- $n 1))) ($_ never)))))
(= (sn $n $x) (if (== $n 0) (switch (car-atom (a b)) (($_ NotReducible))) (switch (car-atom (a b)) (($_ (sn (- $n 1) $x))))))
(= (wn $n) (if (== $n 0) (wrap (let $o (car-atom (a b)) NotReducible)) (let $o (car-atom (a b)) (wrap (wn (- $n 1))))))
(= (wrap $z) $z)
!(ln 3)
!(let $r (ln 3) $r)
!(cn 3)
!(sn 3 a)
!(let $r (sn 3 $q) ($r $q))
!(wn 3)
!(collapse (wn 3))
"""),
    ("typed and polymorphic tails", """
(: tl (-> Number Symbol))
(= (tl $n) (if (== $n 0) done (let $o (car-atom (a b)) (tl (- $n 1)))))
(: bad (-> Number Number))
(= (bad $n) (if (== $n 0) done (let $o (car-atom (a b)) (bad (- $n 1)))))
(: poly (-> Number $t $t))
(= (poly $n $x) (if (== $n 0) $x (let $o (car-atom (a b)) (poly (- $n 1) $x))))
!(tl 3)
!(let $r (tl 3) $r)
!(bad 3)
!(let $r (bad 3) $r)
!(poly 3 7)
!(poly 3 $q)
!(let $r (poly 2 $q) ($r $q))
"""),
    ("case, switch, nop and effects in tail position", """
(= (cs $n) (if (== $n 0) done (case (car-atom (a b)) ((a (cs (- $n 1))) ($_ never)))))
(= (sw $n) (if (== $n 0) done (switch (car-atom (a b)) ((a (sw (- $n 1))) ($_ never)))))
(= (pr $n) (if (== $n 0) done (let $o (println! (level $n)) (pr (- $n 1)))))
!(cs 3)
!(sw 3)
!(let $r (sw 3) $r)
!(pr 3)
!(nop (pr 2))
!(let $r (nop (cs 2)) $r)
"""),
    ("mutual recursion through a wrapper and a second equation", """
(= (ping $n) (if (== $n 0) ping-done (relay (pong (- $n 1)))))
(= (pong $n) (if (== $n 0) pong-done (let $o (cdr-atom (a b)) (ping (- $n 1)))))
(= (relay $x) $x)
(= (alt $n) (if (== $n 0) done (let $o (car-atom (a b)) (alt (- $n 1)))))
(= (alt $n) (alt-side $n))
!(ping 5)
!(let $r (ping 4) $r)
!(alt 2)
!(let $r (alt 2) $r)
"""),
)

# Deep recursions under a small collection budget: collections run while
# producers that took a released frame's place deliver.
COLLECTED = {"CETTA_GC_BUDGET_MB": "1"}
DEEP = (
    ("deep recursions while collecting", """
(= (loop $n) (if (== $n 0) done (let $o (car-atom (a b)) (loop (- $n 1)))))
(= (open $n $x) (if (== $n 0) (unify $x (k $n) $x no) (let $o (car-atom (a b)) (open (- $n 1) $x))))
(= (keep $n $x) (if (== $n 0) (pair $x done) (case (car-atom (a b)) ((a (keep (- $n 1) (s $x))) ($_ never)))))
(: typed (-> Number Atom))
(= (typed $n) (if (== $n 0) done (let $o (cdr-atom (a b)) (typed (- $n 1)))))
(= (wrap $x) $x)
!(loop 2000)
!(let $r (loop 2000) $r)
!(wrap (loop 2000))
!(collapse (loop 2000))
!(open 2000 $q)
!(let $r (open 2000 $q) ($r $q))
!(collapse-bind (open 1000 $q))
!(let $r (keep 300 a) $r)
!(typed 2000)
!(let $r (typed 2000) $r)
"""),
)

SOURCES = ("(car-atom (a b))", "(cdr-atom (a b))", "(+ 1 2)", "$n", "$x",
           "(superpose (a b))", "(println! (at $n))", "(unify $x (k $n) $x no)",
           "(nobody $n)", "(Error src bad)", "(empty)", "(collapse (superpose (1 2)))")
LEAVES = ("done", "$n", "$x", "(pair $x $n)", "(Error leaf bad)", "NotReducible",
          "(empty)", "Empty", "(superpose (x y))", "$free", "(nobody $n)",
          "(unify $x (k $n) $x no)", "(car-atom ())", "(let $w (k $n) (pair $w $x))")
XARGS = ("$x", "(s $x)", "a", "$v", "$fresh", "(k $n)")
TYPES = ("(-> Number Atom Atom)", "(-> Number Atom Symbol)", "(-> Number $t $t)",
         "(-> Number Number Number)", "(-> Number %Undefined% %Undefined%)")
CONTEXTS = ("!{call}", "!(let $r {call} $r)", "!(let $r {call} ($r $q))",
            "!(wrap {call})", "!(collapse {call})", "!(collapse-bind {call})",
            "!(case {call} (($y (got $y))))", "!(superpose ({call} other))",
            "!(nop {call})")


def leaf_tail(rng):
    """A level that ends in a value its last control evaluates as a call."""
    leaf = rng.choice(LEAVES)
    shape = rng.randrange(5)
    if shape == 0:
        return f"(let $v {rng.choice(SOURCES)} {leaf})"
    if shape == 1:
        return f"(case {rng.choice(SOURCES)} ((a {leaf}) ($_ {rng.choice(LEAVES)})))"
    if shape == 2:
        return f"(switch {rng.choice(SOURCES)} (($_ {leaf})))"
    if shape == 3:
        return f"(if True {leaf} never)"
    return f"(nop {leaf})"


def tail(rng, funcs, depth):
    target = rng.choice(funcs)
    call = f"({target} (- $n 1) {rng.choice(XARGS)})"
    if rng.random() < 0.2:
        call = f"(wrap {call})"
    if rng.random() < 0.15:
        return f"(if (== $n 1) {leaf_tail(rng)} {call})"
    if depth <= 0 or rng.random() < 0.25:
        return call
    inner = tail(rng, funcs, depth - 1)
    shape = rng.randrange(6)
    if shape == 0:
        return f"(let $v {rng.choice(SOURCES)} {inner})"
    if shape == 1:
        return f"(if {rng.choice(('True', '(== 1 1)', '(== $n 1)'))} {inner} {rng.choice(LEAVES)})"
    if shape == 2:
        return f"(case {rng.choice(SOURCES)} ((a {inner}) ($_ {rng.choice(LEAVES)})))"
    if shape == 3:
        return f"(switch {rng.choice(SOURCES)} (($_ {inner})))"
    if shape == 4:
        return f"(let ($v $u) ({rng.choice(SOURCES)} b) {inner})"
    return f"(let $v {rng.choice(SOURCES)} (if True {inner} never))"


def generate(rng):
    funcs = [f"f{i}" for i in range(rng.randint(1, 3))]
    lines = ["(= (wrap $z) $z)"]
    for name in funcs:
        if rng.random() < 0.35:
            lines.append(f"(: {name} {rng.choice(TYPES)})")
        lines.append(f"(= ({name} $n $x) (if (== $n 0) {rng.choice(LEAVES)} "
                     f"{tail(rng, funcs, rng.randint(0, 3))}))")
        if rng.random() < 0.15:
            lines.append(f"(= ({name} $n $x) {rng.choice(LEAVES)})")
    for _ in range(rng.randint(2, 4)):
        call = f"({rng.choice(funcs)} {rng.randint(0, 4)} {rng.choice(('a', '$q', '(k b)'))})"
        lines.append(rng.choice(CONTEXTS).format(call=call))
    return "\n".join(lines) + "\n"


def check_answers(binary, count, seed):
    rng = random.Random(seed)
    with tempfile.TemporaryDirectory() as scratch:
        programs = [(name, text) for name, text in FIXTURES]
        programs += [(f"generated {seed}:{i}", generate(rng))
                     for i in range(count)]
        programs = [(name, text, COLLECTED) for name, text in DEEP] + \
            [(name, text, None) for name, text in programs]
        timeouts = 0
        for tag, (name, program, env) in enumerate(programs):
            result = differs(binary, scratch, program, tag, env)
            if result is None:
                continue
            if result == "timeout":
                timeouts += 1
                continue
            minimal = shrink(binary, scratch, program, env)
            again = differs(binary, scratch, minimal, "minimal", env)
            on, off = again if isinstance(again, tuple) else result
            sys.exit(f"FAIL: {name} answers differently with the last-call "
                     f"rule\n--- minimal program\n{minimal}--- with the rule\n"
                     f"{show(on)}\n--- without\n{show(off)}")
    print(f"PASS: {len(FIXTURES) + len(DEEP)} named programs and {count} "
          f"generated ones (seed {seed}) answer the same with the last-call "
          f"rule and without, deep ones while collecting"
          + (f" ({timeouts} unfinished within {TIMEOUT}s both ways)"
             if timeouts else ""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("--memory", action="store_true")
    parser.add_argument("--answers", type=int, default=0)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    if args.memory:
        check_memory(binary)
    if args.answers:
        check_answers(binary, args.answers, args.seed)


if __name__ == "__main__":
    main()
