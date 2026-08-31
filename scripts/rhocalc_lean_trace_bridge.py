#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
import tempfile
from collections import deque
from pathlib import Path

from rhocalc_bounded_reachability import normalize_expr, split_successor_set


RHOMETTA_HE_ARGS = (
    "--pretty-vars",
    "--profile",
    "extended",
    "--lang",
    "he",
)
RHOMETTA_MAX_VISITED = 256


def normalized_fixture_expr(bin_path: str, fixture: Path) -> tuple[str, str]:
    syntax = fixture.suffix.lstrip(".") or "mrho"
    proc = subprocess.run(
        [
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
            str(fixture),
        ],
        check=False,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    return syntax, proc.stdout.strip()


def public_one_step_successors(bin_path: str, fixture: Path) -> list[str]:
    syntax, input_expr = normalized_fixture_expr(bin_path, fixture)
    proc = subprocess.run(
        [
            bin_path,
            "--rho-reduction-limit",
            "1",
            "--lang",
            "rhocalc",
            "--syntax",
            syntax,
            str(fixture),
        ],
        check=False,
        text=True,
        capture_output=True,
    )
    if proc.returncode not in (0, 3):
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())

    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        if proc.returncode == 0:
            return []
        raise RuntimeError("one-step run produced no residual output")
    residual = normalize_expr(bin_path, syntax, lines[0])
    if residual == input_expr:
        return []
    return [residual]


def parse_he_collapse_tuple(text: str) -> list[str]:
    text = text.strip()
    if text == "[()]":
        return []
    if not (text.startswith("[") and text.endswith("]")):
        raise RuntimeError(f"unexpected HE collapse shape: {text}")
    atom = text[1:-1].strip()
    if atom == "()":
        return []
    if not atom:
        return []
    if not (atom.startswith("(") and atom.endswith(")")):
        return [atom]
    body = atom[1:-1].strip()
    if not body:
        return []

    items: list[str] = []
    start = 0
    paren_depth = 0
    bracket_depth = 0
    brace_depth = 0
    angle_depth = 0
    in_string = False
    escape = False

    def flush(end: int) -> None:
        item = body[start:end].strip()
        if item:
            items.append(item)

    for i, ch in enumerate(body):
        if in_string:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
            continue
        if ch == "(":
            paren_depth += 1
            continue
        if ch == ")":
            paren_depth -= 1
            continue
        if ch == "[":
            bracket_depth += 1
            continue
        if ch == "]":
            bracket_depth -= 1
            continue
        if ch == "{":
            brace_depth += 1
            continue
        if ch == "}":
            brace_depth -= 1
            continue
        if ch == "<":
            angle_depth += 1
            continue
        if ch == ">":
            angle_depth -= 1
            continue
        if (
            ch.isspace()
            and paren_depth == 0
            and bracket_depth == 0
            and brace_depth == 0
            and angle_depth == 0
        ):
            flush(i)
            start = i + 1
    flush(len(body))
    return items


def run_he_query(bin_path: str, support_fixture: Path, query: str) -> list[str]:
    try:
        support_text = support_fixture.read_text(encoding="utf-8")
    except OSError as exc:
        raise RuntimeError(str(exc)) from exc

    temp_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            "w",
            encoding="utf-8",
            suffix=".metta",
            prefix=".rhometta-bridge-",
            dir=support_fixture.parent,
            delete=False,
        ) as handle:
            temp_path = Path(handle.name)
            handle.write(support_text)
            if support_text and not support_text.endswith("\n"):
                handle.write("\n")
            handle.write(query)
            handle.write("\n")
        proc = subprocess.run(
            [bin_path, *RHOMETTA_HE_ARGS, str(temp_path)],
            check=False,
            text=True,
            capture_output=True,
        )
    finally:
        if temp_path is not None:
            temp_path.unlink(missing_ok=True)

    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    return [line.strip() for line in proc.stdout.splitlines() if line.strip()]


def rhometta_render_state(bin_path: str, support_fixture: Path, expr: str) -> str:
    lines = run_he_query(bin_path, support_fixture, f"!{expr}")
    if not lines:
        raise RuntimeError("rhometta render produced no output")
    line = lines[-1]
    if not (line.startswith("[") and line.endswith("]")):
        raise RuntimeError(f"unexpected rhometta render shape: {line}")
    rendered = line[1:-1].strip()
    if not rendered:
        raise RuntimeError("rhometta render produced an empty state")
    return rendered


def rhometta_transitions(bin_path: str, support_fixture: Path, state: str) -> list[str]:
    lines = run_he_query(
        bin_path,
        support_fixture,
        f"!(collapse (rhometta:transitions {state}))",
    )
    if not lines:
        return []
    return parse_he_collapse_tuple(lines[-1])


def rhometta_run_outcomes(bin_path: str, support_fixture: Path, expr: str) -> tuple[str, list[str]]:
    lines = run_he_query(
        bin_path,
        support_fixture,
        f"!(collapse (rhometta:run {expr}))",
    )
    if not lines:
        raise RuntimeError("rhometta:run produced no output")
    line = lines[-1]
    return line, parse_he_collapse_tuple(line)


def rhometta_canonical_quiescent_state(
    bin_path: str, support_fixture: Path, state: str
) -> str:
    _, outcomes = rhometta_run_outcomes(bin_path, support_fixture, state)
    if len(outcomes) != 1:
        raise RuntimeError("quiescent-state canonicalization did not produce exactly one outcome")
    return outcomes[0]


def rhometta_bfs_quiescent_outcomes(
    bin_path: str, support_fixture: Path, expr: str
) -> list[str]:
    initial = rhometta_render_state(bin_path, support_fixture, expr)
    queue: deque[str] = deque([initial])
    seen: set[str] = {initial}
    outcomes: list[str] = []

    while queue:
        state = queue.popleft()
        if len(seen) > RHOMETTA_MAX_VISITED:
            raise RuntimeError(
                f"rhometta outcome exploration exceeded {RHOMETTA_MAX_VISITED} visited states"
            )
        successors = rhometta_transitions(bin_path, support_fixture, state)
        if not successors:
            outcomes.append(rhometta_canonical_quiescent_state(bin_path, support_fixture, state))
            continue
        for succ in successors:
            if succ not in seen:
                seen.add(succ)
                queue.append(succ)
    return outcomes


def rhometta_assert_alpha_equal(
    bin_path: str, support_fixture: Path, expr: str, expected_items: list[str]
) -> None:
    expected_expr = "()"
    if expected_items:
        expected_expr = f"({' '.join(expected_items)})"
    lines = run_he_query(
        bin_path,
        support_fixture,
        f"!(assertAlphaEqual (collapse (rhometta:run {expr})) {expected_expr})",
    )
    if not lines:
        raise RuntimeError("alpha-equality oracle produced no output")
    line = lines[-1]
    if line != "[()]":
        raise RuntimeError(f"unexpected alpha-equality oracle output: {line}")


def he_expect_true(bin_path: str, support_fixture: Path, expr: str) -> None:
    lines = run_he_query(bin_path, support_fixture, f"!{expr}")
    if not lines:
        raise RuntimeError("HE bridge query produced no output")
    line = lines[-1]
    if line != "[True]":
        raise RuntimeError(f"expected [True], saw {line}")


def main() -> int:
    if len(sys.argv) != 8:
        print(
            "usage: rhocalc_lean_trace_bridge.py "
            "<bin> <fixture.mrho> <expected-count> <mode> <expected-file-or-dash> "
            "<lean-file> <theorem>",
            file=sys.stderr,
        )
        return 2

    bin_path = sys.argv[1]
    fixture = Path(sys.argv[2])
    expected_count = int(sys.argv[3])
    mode = sys.argv[4]
    expected_file_arg = sys.argv[5]
    lean_file = Path(sys.argv[6])
    theorem = sys.argv[7]

    if mode not in (
        "exact-one-step-nonempty",
        "exact-one-step-empty",
        "exact-one-step-match-file",
        "cetta-empty-lean-can-step",
        "rhometta-run-exact-set",
        "he-expect-true",
    ):
        print(
            "mode must be exact-one-step-nonempty, exact-one-step-empty, exact-one-step-match-file, cetta-empty-lean-can-step, rhometta-run-exact-set, or he-expect-true",
            file=sys.stderr,
        )
        return 2

    if mode == "he-expect-true":
        expr = expected_file_arg
        if expr == "-" or not expr.strip():
            print("he-expect-true requires the entry expression in the expected-file column", file=sys.stderr)
            return 2
        if expected_count != 1:
            print("he-expect-true requires expected-count = 1", file=sys.stderr)
            return 2
        try:
            he_expect_true(bin_path, fixture, expr)
        except RuntimeError as exc:
            print(str(exc), file=sys.stderr)
            return 1

        if lean_file == Path("-") or theorem == "-":
            return 0

        try:
            lean_text = lean_file.read_text(encoding="utf-8")
        except OSError as exc:
            print(str(exc), file=sys.stderr)
            return 2

        if theorem not in lean_text:
            print("lean theorem name not found", file=sys.stderr)
            print(f"file: {lean_file}", file=sys.stderr)
            print(f"theorem: {theorem}", file=sys.stderr)
            return 1

        return 0

    if mode == "rhometta-run-exact-set":
        expr = expected_file_arg
        if expr == "-" or not expr.strip():
            print("rhometta-run-exact-set requires the entry expression in the expected-file column", file=sys.stderr)
            return 2
        try:
            expected_outcomes = rhometta_bfs_quiescent_outcomes(bin_path, fixture, expr)
            actual_run_line, actual_outcomes = rhometta_run_outcomes(bin_path, fixture, expr)
            rhometta_assert_alpha_equal(bin_path, fixture, expr, expected_outcomes)
        except RuntimeError as exc:
            print(str(exc), file=sys.stderr)
            return 2

        if len(expected_outcomes) != expected_count:
            print("expected outcome count mismatch", file=sys.stderr)
            print(f"fixture: {fixture}", file=sys.stderr)
            print(f"expr: {expr}", file=sys.stderr)
            print(f"expected-count: {expected_count}", file=sys.stderr)
            print(f"derived-count: {len(expected_outcomes)}", file=sys.stderr)
            for item in expected_outcomes:
                print(item, file=sys.stderr)
            return 1

        if len(actual_outcomes) != expected_count:
            print("rhometta:run outcome count mismatch", file=sys.stderr)
            print(f"fixture: {fixture}", file=sys.stderr)
            print(f"expr: {expr}", file=sys.stderr)
            print(f"expected-count: {expected_count}", file=sys.stderr)
            print(f"actual-count: {len(actual_outcomes)}", file=sys.stderr)
            print(f"actual-run: {actual_run_line}", file=sys.stderr)
            return 1

        if lean_file == Path("-") or theorem == "-":
            return 0

        try:
            lean_text = lean_file.read_text(encoding="utf-8")
        except OSError as exc:
            print(str(exc), file=sys.stderr)
            return 2

        if theorem not in lean_text:
            print("lean theorem name not found", file=sys.stderr)
            print(f"file: {lean_file}", file=sys.stderr)
            print(f"theorem: {theorem}", file=sys.stderr)
            return 1

        return 0

    try:
        successors = public_one_step_successors(bin_path, fixture)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if len(successors) != expected_count:
        print("successor count mismatch", file=sys.stderr)
        print(f"fixture: {fixture}", file=sys.stderr)
        print(f"expected: {expected_count}", file=sys.stderr)
        print(f"actual: {len(successors)}", file=sys.stderr)
        return 1

    if mode == "exact-one-step-nonempty" and len(successors) == 0:
        print("expected a one-step witness but successor set is empty", file=sys.stderr)
        print(f"fixture: {fixture}", file=sys.stderr)
        return 1

    if mode in ("exact-one-step-empty", "cetta-empty-lean-can-step") and len(successors) != 0:
        print("expected empty one-step successor set but found successors", file=sys.stderr)
        print(f"fixture: {fixture}", file=sys.stderr)
        print(f"successor-count: {len(successors)}", file=sys.stderr)
        return 1

    if mode == "exact-one-step-match-file":
        if expected_file_arg == "-":
            print("expected file must be provided for exact-one-step-match-file", file=sys.stderr)
            return 2
        expected_file = Path(expected_file_arg)
        try:
            expected_text = expected_file.read_text(encoding="utf-8").strip()
        except OSError as exc:
            print(str(exc), file=sys.stderr)
            return 2
        expected_items = split_successor_set(expected_text)
        if expected_items:
            if len(expected_items) != 1:
                print("expected file must contain exactly one residual for exact-one-step-match-file", file=sys.stderr)
                print(f"file: {expected_file}", file=sys.stderr)
                print(f"items: {len(expected_items)}", file=sys.stderr)
                return 1
            expected_residual = expected_items[0]
        else:
            expected_residual = expected_text
        if len(successors) != 1:
            print("expected exactly one one-step successor", file=sys.stderr)
            print(f"fixture: {fixture}", file=sys.stderr)
            print(f"actual-count: {len(successors)}", file=sys.stderr)
            return 1
        if successors[0] != expected_residual:
            print("one-step residual mismatch", file=sys.stderr)
            print(f"fixture: {fixture}", file=sys.stderr)
            print(f"expected: {expected_residual}", file=sys.stderr)
            print(f"actual: {successors[0]}", file=sys.stderr)
            return 1

    try:
        lean_text = lean_file.read_text(encoding="utf-8")
    except OSError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if theorem not in lean_text:
        print("lean theorem name not found", file=sys.stderr)
        print(f"file: {lean_file}", file=sys.stderr)
        print(f"theorem: {theorem}", file=sys.stderr)
        return 1

    if mode == "cetta-empty-lean-can-step" and "CanStep" not in lean_text:
        print("Lean CanStep witness not found in theorem file", file=sys.stderr)
        print(f"file: {lean_file}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
