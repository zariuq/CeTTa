#!/usr/bin/env python3
"""Growth gates for the rho lane's structural order.

Each program runs a fixed number of reductions at a payload size n and at 2n,
and the work the order counts (pairs of nodes compared, nodes hashed) is
compared.  The payloads grow with n; the work per reduction must not, so the
ratio of the totals must stay flat.  A gate fails only when the ratio says the
growth class is wrong (at least 1.5 where it is flat), never on a count
itself, and the counts depend neither on time nor on load.

  relay     rhometta: messages whose values are lists of n cells pass back
            and forth between two persistent receivers; every step chooses
            among several enabled COMMs, so candidates and components are
            ordered each step.
  forward   strict core: messages whose processes are chains of n sends pass
            through a row of forwarders; every step chooses among several
            enabled COMMs.

Usage: rhocalc_order_growth.py CETTA_BINARY (a build with runtime stats).
"""

from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path


COUNTER_RE = re.compile(r"^runtime-counter\s+(\S+)\s+(\d+)\s*$")
FLAT = 1.5


def counters(stderr: str) -> dict[str, int]:
    result: dict[str, int] = {}
    for line in stderr.splitlines():
        match = COUNTER_RE.match(line.strip())
        if match:
            result[match.group(1)] = int(match.group(2))
    return result


def cells(n: int, tag: int) -> str:
    text = "nil"
    for i in range(n):
        text = f"(cons {tag} {i} {text})"
    return text


def relay(n: int) -> str:
    c = "(rho:quote (rho:val c))"
    d = "(rho:quote (rho:val d))"
    parts = [
        f"(rho:recv-persistent {c} $x (rho:send {d} (rho:drop $x)))",
        f"(rho:recv-persistent {d} $y (rho:send {c} (rho:drop $y)))",
    ]
    for j in range(4):
        parts.append(f"(rho:send {c} (rho:val (msg {j} {cells(n, j)})))")
    return "(rho:par\n  " + "\n  ".join(parts) + ")\n"


def chain(n: int, tag: int) -> str:
    """A process of n nested sends, beside a send on a name of its own."""
    text = "rho:nil"
    for _ in range(n):
        text = f"(rho:send (rho:quote rho:nil) {text})"
    name = "(rho:quote rho:nil)"
    for _ in range(tag):
        name = f"(rho:quote (rho:send {name} rho:nil))"
    return f"(rho:par (rho:send {name} rho:nil) {text})"


def forward(n: int) -> str:
    hops = 6
    # Free variables as channel names: c0 ... c6, each its own channel.
    names = [f"$c{i}" for i in range(hops + 1)]
    parts = []
    for i in range(hops):
        for k in range(3):
            parts.append(
                f"(rho:recv {names[i]} $x{i}x{k} "
                f"(rho:send {names[i + 1]} (rho:drop $x{i}x{k})))"
            )
    for j in range(3):
        parts.append(f"(rho:send {names[0]} {chain(n, j + 1)})")
    return "(rho:par\n  " + "\n  ".join(parts) + ")\n"


def run(binary: str, program: str, args: list[str], work: Path,
        name: str) -> tuple[int, int, int]:
    path = work / f"{name}.metta"
    path.write_text(program)
    command = [binary, "--emit-runtime-stats", "--quiet", *args, str(path)]
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, check=False)
    if completed.returncode not in (0, 3):
        diagnostics = "\n".join(
            line for line in completed.stderr.splitlines()
            if not COUNTER_RE.match(line.strip()))
        raise RuntimeError(f"{name}: exit {completed.returncode}\n"
                           f"{completed.stdout}\n{diagnostics}")
    found = counters(completed.stderr)
    if "rho-order-visit" not in found or "rho-hash-visit" not in found:
        raise RuntimeError(f"{name}: the order's counters are absent "
                           "(a build without runtime stats?)")
    return (found["rho-order-visit"], found["rho-hash-visit"],
            completed.returncode)


def gate(binary: str, work: Path, name: str, make, args: list[str],
         n: int) -> bool:
    small = run(binary, make(n), args, work, f"{name}-{n}")
    large = run(binary, make(2 * n), args, work, f"{name}-{2 * n}")
    total_small = small[0] + small[1]
    total_large = large[0] + large[1]
    ratio = total_large / max(total_small, 1)
    verdict = "PASS" if ratio < FLAT and total_small > 0 else "FAIL"
    print(f"{verdict}: rho order growth {name}: n={n} work={total_small} "
          f"(order {small[0]}, hash {small[1]}); n={2 * n} work={total_large} "
          f"(order {large[0]}, hash {large[1]}); ratio {ratio:.3f}")
    return verdict == "PASS"


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    binary = sys.argv[1]
    ok = True
    with tempfile.TemporaryDirectory(prefix="rho-order-growth.") as tmp:
        work = Path(tmp)
        ok &= gate(binary, work, "relay", relay,
                   ["--rho-reduction-limit", "60", "--lang", "rhocalc",
                    "--profile", "rhometta", "--syntax", "metta"], 400)
        ok &= gate(binary, work, "forward", forward,
                   ["--lang", "rhocalc", "--syntax", "metta"], 400)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
