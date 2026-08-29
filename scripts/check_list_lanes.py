#!/usr/bin/env python3
"""Cross-lane algebra, domain, and mutation gates for list and clist."""

from __future__ import annotations

import argparse
from pathlib import Path
import random
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "runtime" / "list-lane-gate"
RUN_TIMEOUT_SECONDS = 60.0


def run(cetta: Path, lane: str, *arguments: str) -> str:
    command = [str(cetta)]
    if lane == "he":
        command.extend(("--lang", "he"))
    elif lane == "prime":
        command.extend(("--lang", "prime"))
    else:
        raise ValueError(f"unknown lane: {lane}")
    command.extend(arguments)
    completed = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=RUN_TIMEOUT_SECONDS,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"{' '.join(command)} exited {completed.returncode}:\n{completed.stdout}"
        )
    return completed.stdout.rstrip("\n")


def require_equal(label: str, actual: str, expected: str) -> None:
    if actual != expected:
        raise AssertionError(
            f"{label} mismatch\n--- expected ---\n{expected}\n"
            f"--- actual ---\n{actual}"
        )


def check_syntax(cetta: Path, fixture: str) -> int:
    source = ROOT / "tests" / f"{fixture}.metta"
    expected = (ROOT / "tests" / f"{fixture}.expected").read_text().rstrip("\n")
    outputs = {lane: run(cetta, lane, str(source)) for lane in ("he", "prime")}
    for lane, output in outputs.items():
        require_equal(f"{fixture}:{lane}", output, expected)
    require_equal(f"{fixture}:cross-lane", outputs["prime"], outputs["he"])
    return 3


DOMAIN_CASES = (
    (
        "list-first-empty",
        "(list:first ())",
        '[(Error (car-atom ()) "car-atom expects a non-empty expression as an argument")]',
    ),
    (
        "list-rest-empty",
        "(list:rest ())",
        '[(Error (cdr-atom ()) "cdr-atom expects a non-empty expression as an argument")]',
    ),
    (
        "list-at-negative",
        "(list:at (a b) -1)",
        '[(Error (index-atom (a b) -1) "Index is out of bounds")]',
    ),
    (
        "list-at-high",
        "(list:at (a b) 9)",
        '[(Error (index-atom (a b) 9) "Index is out of bounds")]',
    ),
    (
        "list-cons-non-expression",
        "(list:cons a not-an-expression)",
        "[(Error (cons-atom a not-an-expression) expected: "
        "(cons-atom <head> (: <tail> Expression)), found: "
        "(cons-atom a not-an-expression))]",
    ),
    (
        "list-len-non-expression",
        "(list:len not-an-expression)",
        "[(Error (size-atom not-an-expression) "
        "(BadArgType 1 Expression %Undefined%))]",
    ),
    (
        "clist-from-non-expression",
        "(clist:from-list not-an-expression)",
        "[(Error (decons-atom not-an-expression) expected: "
        "(decons-atom (: <expr> Expression)), found: "
        "(decons-atom not-an-expression))]",
    ),
)


def check_domains(cetta: Path) -> int:
    checks = 0
    for label, expression, expected_result in DOMAIN_CASES:
        outputs = {
            lane: run(
                cetta,
                lane,
                "-e",
                "!(import! &self clist)",
                "-e",
                f"!{expression}",
            )
            for lane in ("he", "prime")
        }
        expected = f"[()]\n{expected_result}"
        for lane, output in outputs.items():
            require_equal(f"{label}:{lane}", output, expected)
            checks += 1
        require_equal(f"{label}:cross-lane", outputs["prime"], outputs["he"])
        checks += 1
    return checks


def render_sequence(values: list[int]) -> str:
    return "(" + " ".join(str(value) for value in values) + ")"


def generated_assertions() -> list[str]:
    """Generate operations whose expected values come from Python lists."""
    generator = random.Random(0x5EED)
    assertions: list[str] = []
    for case in range(14):
        left = [generator.randrange(0, 17) for _ in range(case % 9)]
        right = [generator.randrange(0, 17) for _ in range((case * 3) % 7)]
        left_form = render_sequence(left)
        right_form = render_sequence(right)
        appended = render_sequence(left + right)
        reversed_left = render_sequence(list(reversed(left)))
        mapped = render_sequence([value + 3 for value in left])
        filtered = render_sequence([value for value in left if value > 8])
        sorted_left = render_sequence(sorted(left))
        unique_left = render_sequence(list(dict.fromkeys(left)))
        removed_left = render_sequence(
            [value for value in left if not left or value != left[0]]
        )
        top_k_count = case % 5
        ranked_indices = sorted(
            range(len(left)), key=lambda index: (-left[index], index)
        )[:top_k_count]
        retained_top_k = render_sequence(
            [left[index] for index in sorted(ranked_indices)]
        )
        total = sum(left)
        assertions.extend(
            (
                f"!(assertEqual (clist:to-list (clist:from-list {left_form})) {left_form})",
                "!(assertEqual "
                f"(clist:from-list (clist:to-list (clist:from-list {left_form}))) "
                f"(clist:from-list {left_form}))",
                f"!(assertEqual (list:append {left_form} {right_form}) {appended})",
                "!(assertEqual "
                f"(clist:to-list (clist:append (clist:from-list {left_form}) "
                f"(clist:from-list {right_form}))) {appended})",
                f"!(assertEqual (list:reverse {left_form}) {reversed_left})",
                f"!(assertEqual (list:sort-numbers {left_form}) {sorted_left})",
                f"!(assertEqual (list:unique {left_form}) {unique_left})",
                "!(assertEqual "
                f"(list:retain-top-k-by-number test:identity-key "
                f"{left_form} {top_k_count}) {retained_top_k})",
                "!(assertEqual "
                f"(list:remove-all {left[0] if left else 0} {left_form}) "
                f"{removed_left})",
                "!(assertEqual "
                f"(clist:to-list (clist:reverse (clist:from-list {left_form}))) "
                f"{reversed_left})",
                f"!(assertEqual (list:map {left_form} $x (+ $x 3)) {mapped})",
                "!(assertEqual "
                f"(clist:to-list (clist:map (clist:from-list {left_form}) "
                f"$x (+ $x 3))) {mapped})",
                "!(assertEqual "
                f"(list:filter {left_form} $x (> $x 8)) {filtered})",
                "!(assertEqual "
                f"(clist:to-list (clist:filter (clist:from-list {left_form}) "
                f"$x (> $x 8))) {filtered})",
                "!(assertEqual "
                f"(list:foldl {left_form} 0 $acc $item (+ $acc $item)) {total})",
                "!(assertEqual "
                f"(clist:foldl (clist:from-list {left_form}) 0 "
                f"$acc $item (+ $acc $item)) {total})",
            )
        )

    for start, stop in ((0, 0), (0, 1), (0, 9), (2, 11), (8, 3)):
        expected = render_sequence(list(range(start, stop)))
        assertions.extend(
            (
                f"!(assertEqual (list:range {start} {stop}) {expected})",
                "!(assertEqual "
                f"(clist:to-list (clist:range {start} {stop})) {expected})",
            )
        )
    for count, item in ((0, 7), (1, -2), (8, 5)):
        expected = render_sequence([item] * count)
        assertions.extend(
            (
                f"!(assertEqual (list:repeat {count} {item}) {expected})",
                "!(assertEqual "
                f"(clist:to-list (clist:repeat {count} {item})) {expected})",
            )
        )
    return assertions


def check_generated_model(cetta: Path) -> int:
    RUNTIME.mkdir(parents=True, exist_ok=True)
    assertions = generated_assertions()
    driver = RUNTIME / "generated_direct_model.metta"
    driver.write_text(
        "!(import! &self clist)\n"
        "(= (test:identity-key $x) $x)\n"
        + "\n".join(assertions)
        + "\n"
    )
    expected = "\n".join("[()]" for _ in range(len(assertions) + 1))
    outputs = {lane: run(cetta, lane, str(driver)) for lane in ("he", "prime")}
    for lane, output in outputs.items():
        require_equal(f"generated direct model:{lane}", output, expected)
    require_equal(
        "generated direct model:cross-lane", outputs["prime"], outputs["he"]
    )
    return 3


def check_scale(cetta: Path) -> int:
    """Correctness precedes timing: one broad and one recursive-depth rung."""
    RUNTIME.mkdir(parents=True, exist_ok=True)
    assertions = (
        "!(assertEqual (list:len (list:range 10000)) 10000)",
        "!(assertEqual (list:foldl (list:range 10000) 0 $a $x (+ $a $x)) 49995000)",
        # list:map/filter deliberately retain the established map-atom and
        # filter-atom semantics; the deeper rungs below exercise the recursive
        # clist presentation and its bridge to expression-backed sequences.
        "!(assertEqual (list:at (list:map (list:range 100) $x (+ $x 1)) 99) 100)",
        "!(assertEqual (list:len (list:filter (list:range 50) $x (> $x 24))) 25)",
        "!(assertEqual (clist:len (clist:range 200)) 200)",
        "!(assertEqual (clist:first (clist:reverse (clist:range 200))) 199)",
        "!(assertEqual (clist:to-list (clist:from-list (list:range 200))) (list:range 200))",
        # A produced constructor value is invocation data, not syntax to
        # recompile at every consumer step.  This composition crossed the
        # native-stack boundary when the eager value proof was ignored.
        "!(assertEqual "
        "(clist:foldl "
        "(clist:append (clist:range 600) (clist:range 600)) "
        "0 $a $x (+ $a $x)) 359400)",
    )
    driver = RUNTIME / "scale_correctness.metta"
    driver.write_text(
        "!(import! &self clist)\n" + "\n".join(assertions) + "\n"
    )
    expected = "\n".join("[()]" for _ in range(len(assertions) + 1))
    outputs = {lane: run(cetta, lane, str(driver)) for lane in ("he", "prime")}
    for lane, output in outputs.items():
        require_equal(f"scale correctness:{lane}", output, expected)
    require_equal(
        "scale correctness:cross-lane", outputs["prime"], outputs["he"]
    )
    return 3


def replace_once(source: str, marker: str, replacement: str) -> str:
    if source.count(marker) != 1:
        raise AssertionError(f"mutation marker count is not one: {marker!r}")
    return source.replace(marker, replacement)


def require_killed_mutation(cetta: Path, lane: str, driver: Path) -> None:
    output = run(cetta, lane, str(driver))
    if "Error (assertEqual" not in output:
        raise AssertionError(
            f"{driver.name}:{lane} mutation survived\n--- output ---\n{output}"
        )


def check_mutations(cetta: Path) -> int:
    RUNTIME.mkdir(parents=True, exist_ok=True)

    list_source = (ROOT / "lib" / "list.metta").read_text()
    clist_source = (ROOT / "lib" / "clist.metta").read_text()

    mutation_specs = (
        (
            "conversion-order",
            clist_source,
            "(= (clist:from-list $xs)\n"
            "   (let ($head $tail) (decons-atom $xs)\n"
            "     (let $rest (eval (clist:from-list $tail))\n"
            "       (clist:cons $head $rest))))",
            "(= (clist:from-list $xs)\n"
            "   (let ($head $tail) (decons-atom $xs)\n"
            "     (let $rest (eval (clist:from-list $tail))\n"
            "       (clist:append $rest (clist:cons $head (clist:nil))))))",
            "!(assertEqual (clist:to-list (clist:from-list (a b c))) (a b c))\n",
        ),
        (
            "clist-length",
            clist_source,
            "(= (clist:len-acc (ClistCons $head $tail) $acc)\n"
            "   (let $next (eval (+ $acc 1))\n"
            "     (eval (clist:len-acc $tail $next))))",
            "(= (clist:len-acc (ClistCons $head $tail) $acc)\n"
            "   (eval (clist:len-acc $tail $acc)))",
            "!(assertEqual (clist:len (clist:from-list (a b c))) 3)\n",
        ),
        (
            "clist-reverse-accumulator",
            clist_source,
            "(= (clist:reverse-acc (ClistCons $head $tail) $acc)\n"
            "   (let $next (eval (clist:cons $head $acc))\n"
            "     (eval (clist:reverse-acc $tail $next))))",
            "(= (clist:reverse-acc (ClistCons $head $tail) $acc)\n"
            "   (eval (clist:reverse-acc $tail $acc)))",
            "!(assertEqual (clist:reverse (clist:from-list (a b c))) "
            "(clist:from-list (c b a)))\n",
        ),
        (
            "default-sort",
            list_source,
            "(= (list:sort $xs)\n   (sort-atom $xs))",
            "(= (list:sort $xs)\n   $xs)",
            "!(assertEqual (list:sort (ccc a bb)) (a bb ccc))\n",
        ),
        (
            "numeric-sort",
            list_source,
            "(= (list:sort-numbers $xs)\n"
            "   (sort-numbers-atom $xs))",
            "(= (list:sort-numbers $xs)\n"
            "   $xs)",
            "!(assertEqual (list:sort-numbers (3 1 2)) (1 2 3))\n",
        ),
        (
            "remove-all",
            list_source,
            "(= (list:remove-all $item $xs)\n"
            "   (remove-all-atom $item $xs))",
            "(= (list:remove-all $item $xs)\n"
            "   $xs)",
            "!(assertEqual (list:remove-all a (b a a c)) (b c))\n",
        ),
        (
            "unique",
            list_source,
            "(= (list:unique $xs)\n   (unique-atom $xs))",
            "(= (list:unique $xs)\n   $xs)",
            "!(assertEqual (list:unique (a b a c b)) (a b c))\n",
        ),
        (
            "stable-merge",
            list_source,
            "if $take-left\n",
            "if (if (eval ($precedes $right-head $left-head))\n"
            "                        False\n"
            "                        $take-left)\n",
            "(= (test:key<= (item $a $x) (item $b $y)) (<= $a $b))\n"
            "!(assertEqual (list:sort-by test:key<= "
            "((item 1 a) (item 0 z) (item 1 b))) "
            "((item 0 z) (item 1 a) (item 1 b)))\n",
        ),
        (
            "append",
            list_source,
            "(= (list:append $lhs $rhs)\n"
            "   (union-atom $lhs $rhs))",
            "(= (list:append $lhs $rhs)\n"
            "   $lhs)",
            "!(assertEqual (list:append (a b) (c d)) (a b c d))\n",
        ),
        (
            "reverse",
            list_source,
            "(= (list:reverse $xs)\n   (list:reverse-acc $xs ()))",
            "(= (list:reverse $xs)\n   $xs)",
            "!(assertEqual (list:reverse (a b c)) (c b a))\n",
        ),
        (
            "map",
            list_source,
            "(= (list:map $xs $var $body)\n   (map-atom $xs $var $body))",
            "(= (list:map $xs $var $body)\n   $xs)",
            "!(assertEqual (list:map (1 2 3) $x (+ $x 1)) (2 3 4))\n",
        ),
        (
            "filter",
            list_source,
            "(= (list:filter $xs $var $body)\n   (filter-atom $xs $var $body))",
            "(= (list:filter $xs $var $body)\n   $xs)",
            "!(assertEqual (list:filter (1 2 3 4) $x (> $x 2)) (3 4))\n",
        ),
        (
            "retain-top-k-by-number",
            list_source,
            "(= (list:retain-top-k-by-number $key-function $xs $count)\n"
            "   (function\n"
            "     (chain (eval $xs) $__list_top_k_items\n"
            "       (chain (eval $count) $__list_top_k_count\n"
            "         (eval\n"
            "           (_minimal-retain-top-k-by-number\n"
            "             $__list_top_k_items $__list_top_k_count $__list_top_k_item\n"
            "             (eval ($key-function $__list_top_k_item))))))))",
            "(= (list:retain-top-k-by-number $key-function $xs $count)\n"
            "   $xs)",
            "(= (test:key (item $key $value)) $key)\n"
            "!(assertEqual (list:retain-top-k-by-number test:key "
            "((item 1 a) (item 3 b) (item 2 c)) 2) "
            "((item 3 b) (item 2 c)))\n",
        ),
        (
            "retain-top-k-relational-fallback",
            list_source,
            "(chain (unkey-atom $retained) $result\n"
            "         (return $result))",
            "(chain (unkey-atom $retained) $result\n"
            "         (return ()))",
            "(= (test:key-choice (item $key $value)) $key)\n"
            "(= (test:key-choice (item $key $value)) (+ $key 10))\n"
            "!(assertEqualToResult "
            "(list:retain-top-k-by-number test:key-choice "
            "((item 2 a) (item 3 b)) 1) "
            "(((item 3 b)) ((item 2 a)) ((item 3 b)) ((item 3 b))))\n",
        ),
    )

    drivers: list[Path] = []
    for label, source, marker, replacement, assertion in mutation_specs:
        mutant = replace_once(source, marker, replacement)
        mutant_path = RUNTIME / f"{label}_mutant.metta"
        mutant_path.write_text(mutant)
        driver = RUNTIME / f"{label}_mutant_driver.metta"
        driver.write_text(
            f"!(import! &self ./{mutant_path.name})\n" + assertion
        )
        drivers.append(driver)

    checks = 0
    for lane in ("he", "prime"):
        for driver in drivers:
            require_killed_mutation(cetta, lane, driver)
            checks += 1
    return checks


def main() -> int:
    global RUN_TIMEOUT_SECONDS
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, default=ROOT / "cetta")
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()
    RUN_TIMEOUT_SECONDS = args.timeout
    cetta = args.cetta.resolve()
    if not cetta.is_file():
        print(f"missing CeTTa executable: {cetta}", file=sys.stderr)
        return 2

    try:
        checks = 0
        checks += check_syntax(cetta, "test_list_syntax")
        checks += check_syntax(cetta, "test_clist_syntax")
        checks += check_domains(cetta)
        checks += check_generated_model(cetta)
        checks += check_scale(cetta)
        checks += check_mutations(cetta)
    except (AssertionError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print(f"(ListLaneGateSummary {checks} {checks} 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
