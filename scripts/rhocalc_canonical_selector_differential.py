#!/usr/bin/env python3

from __future__ import annotations

import argparse
import random
import subprocess
import sys
from dataclasses import dataclass

from rhocalc_bounded_reachability import split_successor_set


@dataclass(frozen=True)
class Case:
    name: str
    process: str


def par(parts: list[str]) -> str:
    if not parts:
        return "rho:nil"
    if len(parts) == 1:
        return parts[0]
    return "(rho:par " + " ".join(parts) + ")"


def payload(case_index: int, item_index: int, shape: int) -> str:
    stem = f"p{case_index}x{item_index}"
    if shape == 0:
        return "rho:nil"
    if shape == 1:
        return f"(rho:send ${stem} rho:nil)"
    if shape == 2:
        return par([
            f"(rho:send ${stem}a rho:nil)",
            f"(rho:send ${stem}b rho:nil)",
        ])
    return f"(rho:recv $gate{case_index} ${stem}y (rho:drop ${stem}y))"


def continuation(case_index: int, item_index: int, binder: str,
                 shape: int) -> str:
    out = f"out{case_index}x{item_index}"
    if shape == 0:
        return "rho:nil"
    if shape == 1:
        return f"(rho:send ${out} (rho:drop ${binder}))"
    if shape == 2:
        return par([
            f"(rho:send ${out} (rho:drop ${binder}))",
            f"(rho:send $static{case_index}x{item_index} rho:nil)",
        ])
    inner = f"inner{case_index}x{item_index}"
    return (
        f"(rho:recv $gate{case_index} ${inner} "
        f"(rho:send ${out} (rho:drop ${binder})))"
    )


def named_cases() -> list[Case]:
    return [
        Case(
            "nil-contractum",
            par([
                "(rho:send $c rho:nil)",
                "(rho:recv $c $x rho:nil)",
            ]),
        ),
        Case(
            "two-senders-one-receiver",
            par([
                "(rho:send $c rho:nil)",
                "(rho:send $c (rho:send $p rho:nil))",
                "(rho:recv $c $x (rho:send $out (rho:drop $x)))",
            ]),
        ),
        Case(
            "two-receivers-one-sender",
            par([
                "(rho:send $c (rho:send $p rho:nil))",
                "(rho:recv $c $x (rho:send $outA (rho:drop $x)))",
                "(rho:recv $c $y (rho:send $outB (rho:drop $y)))",
            ]),
        ),
        Case(
            "multiple-channel-groups",
            par([
                "(rho:send $b rho:nil)",
                "(rho:recv $b $x (rho:send $outB (rho:drop $x)))",
                "(rho:send $a (rho:send $payload rho:nil))",
                "(rho:recv $a $y (rho:send $outA (rho:drop $y)))",
            ]),
        ),
        Case(
            "parallel-continuation",
            par([
                "(rho:send $c (rho:send $payload rho:nil))",
                "(rho:recv $c $x (rho:par (rho:send $z rho:nil) "
                "(rho:send $out (rho:drop $x))))",
                "(rho:send $z rho:nil)",
                "(rho:recv $z $y (rho:drop $y))",
            ]),
        ),
        Case(
            "alpha-sorted-continuations",
            par([
                "(rho:send $c rho:nil)",
                "(rho:send $c (rho:recv $g $u (rho:drop $u)))",
                "(rho:recv $c $x (rho:recv $g $v "
                "(rho:send $out (rho:drop $x))))",
                "(rho:recv $c $y (rho:send $out2 (rho:drop $y)))",
            ]),
        ),
        Case(
            "equal-key-residual-and-continuation",
            par([
                "(rho:send $c rho:nil)",
                "(rho:recv $c $x (rho:send $same rho:nil))",
                "(rho:send $same rho:nil)",
            ]),
        ),
        Case(
            "long-common-prefix-successors",
            par([
                "(rho:send $c (rho:send $payloadA rho:nil))",
                "(rho:send $c (rho:send $payloadB rho:nil))",
                "(rho:recv $c $x (rho:send $commonPrefixChannel0001 "
                "(rho:drop $x)))",
                "(rho:recv $c $y (rho:send $commonPrefixChannel0002 "
                "(rho:drop $y)))",
            ]),
        ),
    ]


def random_case(rng: random.Random, case_index: int) -> Case:
    parts: list[str] = []
    channel_count = rng.randint(1, 4)
    item_index = 0
    for channel_index in range(channel_count):
        channel = f"c{case_index}x{channel_index}"
        send_count = rng.randint(1, 5)
        recv_count = rng.randint(1, 5)
        for _ in range(send_count):
            parts.append(
                f"(rho:send ${channel} "
                f"{payload(case_index, item_index, rng.randrange(4))})"
            )
            item_index += 1
        for _ in range(recv_count):
            binder = f"x{case_index}x{item_index}"
            body = continuation(
                case_index, item_index, binder, rng.randrange(4)
            )
            parts.append(f"(rho:recv ${channel} ${binder} {body})")
            item_index += 1
    for static_index in range(rng.randint(0, 3)):
        parts.append(
            f"(rho:send $stuck{case_index}x{static_index} rho:nil)"
        )
    rng.shuffle(parts)
    return Case(f"random-{case_index:04d}", par(parts))


def run(command: list[str], timeout: float) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
    )


def last_nonempty_line(text: str) -> str:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return lines[-1] if lines else ""


def exhaustive_first(bin_path: str, process: str,
                     timeout: float) -> tuple[str, str]:
    program = (
        "!(import! &self lts:rho) "
        f"!(lts:rho:transitions {process})"
    )
    result = run(
        [bin_path, "--lang", "he", "--profile", "he-extended", "-e", program],
        timeout,
    )
    if result.returncode != 0:
        return "", result.stderr.strip() or result.stdout.strip()
    frontier = split_successor_set(last_nonempty_line(result.stdout))
    if not frontier:
        return "", "exhaustive frontier was unexpectedly empty"
    return frontier[0], ""


def selected_first(bin_path: str, process: str,
                   timeout: float) -> tuple[str, str]:
    result = run(
        [
            bin_path,
            "--rho-reduction-limit",
            "1",
            "--lang",
            "rhocalc",
            "--syntax",
            "mrho",
            "-e",
            process,
        ],
        timeout,
    )
    if result.returncode not in (0, 3):
        return "", result.stderr.strip() or result.stdout.strip()
    selected = last_nonempty_line(result.stdout)
    if not selected:
        return "", "canonical selector produced no residual"
    return selected, ""


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Compare the optimized pure-rho canonical selector with the "
            "independent exhaustive one-step LTS frontier."
        )
    )
    parser.add_argument("bin_path")
    parser.add_argument("--cases", type=int, default=96)
    parser.add_argument("--seed", type=lambda value: int(value, 0),
                        default=0xC377A)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args(argv[1:])
    if args.cases < 0:
        parser.error("--cases must be nonnegative")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    rng = random.Random(args.seed)
    cases = named_cases()
    cases.extend(random_case(rng, index) for index in range(args.cases))

    for case in cases:
        try:
            expected, expected_error = exhaustive_first(
                args.bin_path, case.process, args.timeout
            )
            actual, actual_error = selected_first(
                args.bin_path, case.process, args.timeout
            )
        except subprocess.TimeoutExpired as exc:
            print(f"FAIL {case.name}: timeout: {exc}", file=sys.stderr)
            print(f"PROCESS {case.process}", file=sys.stderr)
            return 1
        if expected_error or actual_error or actual != expected:
            print(f"FAIL {case.name}", file=sys.stderr)
            if expected_error:
                print(f"EXHAUSTIVE_ERROR {expected_error}", file=sys.stderr)
            if actual_error:
                print(f"SELECTOR_ERROR {actual_error}", file=sys.stderr)
            print(f"EXPECTED {expected}", file=sys.stderr)
            print(f"ACTUAL {actual}", file=sys.stderr)
            print(f"PROCESS {case.process}", file=sys.stderr)
            return 1

    print(
        "PASS: pure-rho canonical selector agrees with exhaustive frontier "
        f"({len(named_cases())} named + {args.cases} generated; seed={args.seed})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
