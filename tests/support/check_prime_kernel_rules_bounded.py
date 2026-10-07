#!/usr/bin/env python3
"""The admitted rules a covered call reads are remembered per thread for one
state of the space and of admission, and the list handed to the call is built
in the call's own arena.  A program that changes the space between covered
calls must therefore not keep a list per change: memory may grow with the
facts it adds, but not with the number of changes times the number of rules.

Two programs add K facts, one at a time, over a fixed set of admitted rules
(R definitions of three equations each).  In one, a covered call follows
each addition; in the other, the same call quoted, so nothing is computed.
The extra resident memory of the first over the second is measured at two
values of K.  Kept lists would make it grow by K times R lists between them;
it must stay within a fixed slack instead, 4 MB.  The type judgments the
calls remember lapse at every change of a space; before they were released
at the next change, they grew by about 2 KB per call, 6 MB between the two
values of K.  The growth now measures under 2 MB.

It also checks that what is remembered follows the space: a rule removed
stops computing, a rule added as a raw atom confers nothing, the admitted
rule added back computes again, and a definition published after the calls
computes at once."""

import os
import signal
import subprocess
import sys
import tempfile
import time

RULES = 100
SMALL, LARGE = 1000, 4000
SLACK_KB = 4 * 1024
TIMEOUT_SECONDS = 300

PREAMBLE = (
    "(set:profile hol)\n"
    "!(set:inductive &self pos (u 0) (: one pos) (: bit0 (-> pos pos)) "
    "(: bit1 (-> pos pos)))\n"
    "!(set:define &self psucc (-> pos pos)\n"
    "   (= (psucc one) (bit0 one))\n"
    "   (= (psucc (bit0 $p)) (bit1 $p))\n"
    "   (= (psucc (bit1 $p)) (bit0 (psucc $p))))\n")


def definitions():
    return "".join(
        f"!(set:define &self keep{i} (-> pos pos)\n"
        f"   (= (keep{i} one) one)\n"
        f"   (= (keep{i} (bit0 $p)) (bit0 $p))\n"
        f"   (= (keep{i} (bit1 $p)) (bit1 $p)))\n"
        for i in range(RULES))


def loop(count, quoted):
    call = "(psucc (bit1 (bit1 one)))"
    step = f"!(quote {call})\n" if quoted else f"!{call}\n"
    return "".join(f"!(add-atom &self (tick {k}))\n" + step for k in range(count))


def measure(binary, source):
    with tempfile.NamedTemporaryFile(
            "w", suffix=".metta", dir="runtime", delete=False) as handle:
        handle.write(source)
        path = handle.name
    try:
        pid = os.fork()
        if pid == 0:
            devnull = os.open(os.devnull, os.O_WRONLY)
            out = os.open(path + ".out", os.O_WRONLY | os.O_CREAT, 0o600)
            os.dup2(out, 1)
            os.dup2(devnull, 2)
            os.execv(binary, [binary, "--lang", "prime", path])
        deadline = time.monotonic() + TIMEOUT_SECONDS
        while True:
            waited, status, usage = os.wait4(pid, os.WNOHANG)
            if waited == pid:
                break
            if time.monotonic() > deadline:
                os.kill(pid, signal.SIGKILL)
                os.wait4(pid, 0)
                os.unlink(path + ".out")
                sys.exit(f"FAIL: a program ran past {TIMEOUT_SECONDS} s")
            time.sleep(0.05)
        with open(path + ".out") as result:
            lines = result.read().splitlines()
        os.unlink(path + ".out")
        if status != 0:
            sys.exit(f"FAIL: a program exited with status {status}")
        return lines, usage.ru_maxrss
    finally:
        os.unlink(path)


def bounded(binary):
    extra = {}
    for count in (SMALL, LARGE):
        calls, called = measure(binary, PREAMBLE + definitions() + loop(count, False))
        if calls.count("[(bit0 (bit0 (bit0 one)))]") != count:
            sys.exit(f"FAIL: the covered call did not compute {count} times")
        _, quoted = measure(binary, PREAMBLE + definitions() + loop(count, True))
        extra[count] = called - quoted
    print(f"covered calls after each of K additions, {3 * RULES + 3} admitted rules: "
          f"extra resident memory {extra[SMALL]} KB at K={SMALL}, "
          f"{extra[LARGE]} KB at K={LARGE}")
    if extra[LARGE] > extra[SMALL] + SLACK_KB:
        sys.exit("FAIL: the memory of covered calls grows with the number of "
                 "changes of the space")


PROBE = PREAMBLE + """\
!(psucc (bit1 one))
!(match &self (type:rule psucc $n ((App (DeclConst bit1) $x)) $r)
        (remove-atom &self (type:rule psucc $n ((App (DeclConst bit1) $x)) $r)))
!(psucc (bit1 one))
!(psucc (bit0 one))
!(add-atom &self (type:rule psucc 1 ((App (DeclConst bit1) (PVar 0))) (DeclConst one)))
!(psucc (bit1 one))
!(remove-atom &self (type:rule psucc 1 ((App (DeclConst bit1) (PVar 0))) (DeclConst one)))
!(add-atom &self (type:rule psucc 1 ((App (DeclConst bit1) (PVar 0)))
                            (App (DeclConst bit0) (App (DeclConst psucc) (PVar 0)))))
!(psucc (bit1 one))
!(set:define &self pdouble (-> pos pos) (= (pdouble $p) (bit0 $p)))
!(pdouble (bit1 one))
"""
PROBE_EXPECTED = [
    "[pos]", "[psucc]",
    "[(bit0 (bit0 one))]",          # psucc 3 = 4
    "[()]",                         # its bit1 rule removed
    "[(psucc (bit1 one))]",         # no rule computes it now
    "[(bit1 one)]",                 # the other rules still do
    "[()]",                         # a raw rule, never admitted
    "[(psucc (bit1 one))]",         # confers nothing
    "[()]", "[()]",                 # the admitted rule's content back
    "[(bit0 (bit0 one))]",          # computes again
    "[pdouble]",
    "[(bit0 (bit1 one))]",          # published after the calls, computes
]


def invalidation(binary):
    lines, _ = measure(binary, PROBE)
    if lines != PROBE_EXPECTED:
        sys.exit("FAIL: the remembered rules did not follow the space:\n  "
                 + "\n  ".join(lines))


def main():
    binary = os.path.abspath(sys.argv[1])
    os.makedirs("runtime", exist_ok=True)
    invalidation(binary)
    bounded(binary)
    print("PASS: covered calls read the space's current admitted rules, and "
          "remembering them does not grow with the changes of the space")


if __name__ == "__main__":
    main()
