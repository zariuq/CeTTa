#!/usr/bin/env python3

import subprocess
import sys

from rhocalc_tiny_semantics import immediate_successors_from_mrho, strip_comments


def split_successor_set(text: str) -> list[str]:
    text = text.strip()
    if not (text.startswith("[") and text.endswith("]")):
        return []

    body = text[1:-1].strip()
    if not body:
        return []

    items: list[str] = []
    depth = 0
    start = 0
    for i, ch in enumerate(body):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "," and depth == 0:
            items.append(body[start:i].strip())
            start = i + 1
    items.append(body[start:].strip())
    return items


def run_cmd(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, check=False, text=True, capture_output=True)


def normalize_expr(bin_path: str, syntax: str, expr: str) -> str:
    expr = expr.strip()
    if syntax == "mrho":
        return expr
    proc = run_cmd([
        bin_path,
        "--translate",
        "--syntax",
        syntax,
        "--lang",
        "rhocalc",
        "--lang",
        "rhocalc",
        "--syntax",
        "mrho",
        "-e",
        expr,
    ])
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    return proc.stdout.strip()


def successor_set_from_file(bin_path: str, syntax: str, path: str) -> list[str]:
    if syntax == "mrho":
        with open(path, "r", encoding="utf-8") as handle:
            return immediate_successors_from_mrho(strip_comments(handle.read()))
    proc = run_cmd([
        bin_path,
        "--translate",
        "--lang",
        "rhocalc",
        "--syntax",
        syntax,
        "--lang",
        "rhocalc",
        "--syntax",
        "mrho",
        path,
    ])
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    return immediate_successors_from_mrho(proc.stdout.strip())


def successor_set_from_expr(bin_path: str, syntax: str, expr: str) -> list[str]:
    return immediate_successors_from_mrho(normalize_expr(bin_path, syntax, expr))


def scheduler_residual(
    bin_path: str, syntax: str, depth: int, policy: str, path: str
) -> tuple[str, int]:
    args = [
        bin_path,
        "--rho-reduction-limit",
        str(depth),
        "--lang",
        "rhocalc",
        "--syntax",
        syntax,
    ]
    if policy != "canonical":
        args.extend(["--rho-scheduler", policy])
    args.append(path)
    proc = run_cmd(args)
    if proc.returncode not in (0, 3):
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("scheduler run produced no residual output")
    return normalize_expr(bin_path, syntax, lines[0]), proc.returncode


def exact_depth_reachable(
    bin_path: str, syntax: str, depth: int, path: str
) -> set[str]:
    if depth < 1:
        raise ValueError("depth must be at least 1")

    current = set(successor_set_from_file(bin_path, syntax, path))
    for _ in range(1, depth):
        next_states: set[str] = set()
        for state in sorted(current):
            next_states.update(successor_set_from_expr(bin_path, "mrho", state))
        current = next_states
    return current


def main() -> int:
    if len(sys.argv) != 6:
        print(
            "usage: rhocalc_bounded_reachability.py <bin> <syntax> <depth> <policy> <file>",
            file=sys.stderr,
        )
        return 2

    bin_path = sys.argv[1]
    syntax = sys.argv[2]
    depth = int(sys.argv[3])
    policy = sys.argv[4]
    path = sys.argv[5]

    try:
        target, _ = scheduler_residual(bin_path, syntax, depth, policy, path)
        reachable = exact_depth_reachable(bin_path, syntax, depth, path)
    except (RuntimeError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if target in reachable:
        return 0

    print("target residual not found in exact-depth reachable set", file=sys.stderr)
    print(f"target: {target}", file=sys.stderr)
    print(f"reachable-count: {len(reachable)}", file=sys.stderr)
    for item in sorted(reachable):
        print(item, file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
