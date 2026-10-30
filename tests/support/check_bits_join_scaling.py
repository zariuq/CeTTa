#!/usr/bin/env python3
"""Binary relations kept as bits answer probes of either column in time
proportional to their answers, and joins over them read the smaller leg at
each visit.

Two programs, each at N and 8N, must answer exactly and cost at most sixteen
times as much at 8N (linear work gives about eight, a scan or a kept leg
paying a hub's degree about sixty-four):
- reverse lookups: N pairs, N/8 sources with 8 targets each; a lookup per
  target binding only the second column;
- star triangles: a hub with an edge to and from each of N leaves and a
  ring through them; the triangles counted by one conjunctive match.

Usage: check_bits_join_scaling.py CETTA
"""
import os
import resource
import subprocess
import sys
import tempfile


def reverse_program(n):
    nodes = n // 8
    facts = []
    for i in range(nodes):
        for j in range(8):
            facts.append(f"(e {i} {(i * 7919 + j * 104729 + 13) % nodes})")
    loop = ("(= (go $k $n $acc) (if (>= $k $n) $acc "
            "(go (+ $k 1) $n (+ $acc (size-atom (collapse (match &self (e $x $k) $x)))))))\n")
    return "\n".join(facts) + "\n" + loop + f"!(go 0 {nodes} 0)\n", str(n)


def star_program(n):
    hub = n
    facts = []
    for i in range(n):
        facts.append(f"(e {hub} {i})")
        facts.append(f"(e {i} {hub})")
        facts.append(f"(e {i} {(i + 1) % n})")
    query = "!(size-atom (collapse (match &self (, (e $a $b) (e $b $c) (e $c $a)) 1)))\n"
    # Triangles: the hub with each ring edge, hub -> i -> i+1 -> hub, counted
    # once per rotation: three per ring edge.
    return "\n".join(facts) + "\n" + query, str(3 * n)


def run(cetta, program, expected):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(program)
        path = f.name
    try:
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        result = subprocess.run([cetta, "--lang", "petta", path],
                                capture_output=True, text=True, timeout=300)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
    finally:
        os.unlink(path)
    answer = result.stdout.split()
    if result.returncode != 0 or answer != [expected]:
        print(f"FAIL: exit {result.returncode}, answer {answer[:4]} against "
              f"[{expected}]; {result.stderr[-200:]}")
        sys.exit(1)
    return (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)


def ladder(cetta, make, small_n, large_n, what):
    small = min(run(cetta, *make(small_n)) for _ in range(2))
    large = min(run(cetta, *make(large_n)) for _ in range(2))
    ratio = large / max(small, 0.01)
    if ratio > 16.0:
        print(f"FAIL: {what}: {large:.2f} s at {large_n} against {small:.2f} s "
              f"at {small_n} ({ratio:.1f}x)")
        sys.exit(1)
    return f"{what} {large:.2f} s at {large_n}, {small:.2f} s at {small_n} ({ratio:.1f}x)"


def main():
    cetta = sys.argv[1]
    first = ladder(cetta, reverse_program, 32000, 256000, "reverse lookups")
    second = ladder(cetta, star_program, 2000, 16000, "star triangles")
    print(f"PASS: bits answer either column by their answers: {first}; {second}")


if __name__ == "__main__":
    main()
