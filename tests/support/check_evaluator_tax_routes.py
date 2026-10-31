#!/usr/bin/env python3
"""Where a loop over regex matches runs, and what one of its steps costs.

Each shape walks the matches of a balancing scan's steering characters over a
fixed text: an `if`/`let` destructuring loop, clause heads, `case`, `and`,
`not`, and the same loops called from callers the prepared machine does not
compile, whose open-tier call hands the loop to it.  Every shape must answer
alike on the default route, with the open equation machine off, and with the
prepared machine off; no step of a loop may leave the machine it runs in for
a host goal; and the loops this machine can compile must be compiled.  With
--instructions, perf counts one step's user instructions against each
shape's budget.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PRELUDE = r'''!(import! &self (library str))
!(import! &self (library re))
!(bind! &steer (new-state (re:compile "[\\\\\"()]")))
'''

DEFINITIONS = r'''
(= (base $text $list $n) $n)
(= (to-cons LNil $acc) $acc)
(= (to-cons (LCons $m $rest) $acc) (to-cons $rest (Cons $m $acc)))
(= (base-cons $text $list $n) (let $c (to-cons $list Nil) $n))
(= (if-let $text $list $n)
   (if (== $list LNil) $n
     (let (LCons (re:match $at $end $groups) $rest) $list
       (let $c (str:byte-slice $text $at $end) (if-let $text $rest (+ $n 1))))))
(= (heads $text LNil $n) $n)
(= (heads $text (LCons (re:match $at $end $groups) $rest) $n)
   (let $c (str:byte-slice $text $at $end) (heads $text $rest (+ $n 1))))
(= (user-heads $text Nil $n) $n)
(= (user-heads $text (Cons (re:match $at $end $groups) $rest) $n)
   (let $c (str:byte-slice $text $at $end) (user-heads $text $rest (+ $n 1))))
(= (by-case $text $list $n)
   (case $list
     ((LNil $n)
      ((LCons (re:match $at $end $groups) $rest)
       (let $c (str:byte-slice $text $at $end) (by-case $text $rest (+ $n 1)))))))
(= (with-and $text $list $n)
   (if (== $list LNil) $n
     (let (LCons (re:match $at $end $groups) $rest) $list
       (let $x (str:byte-slice $text $at $end)
         (if (and (== $x ")") (> $n -1)) (with-and $text $rest (+ $n 1))
           (with-and $text $rest $n))))))
(= (with-not $text $list $n)
   (if (== $list LNil) $n
     (let (LCons (re:match $at $end $groups) $rest) $list
       (let $x (str:byte-slice $text $at $end)
         (if (not (== $x "\"")) (with-not $text $rest (+ $n 1))
           (with-not $text $rest $n))))))
(= (scan $text $matches $depth $quoted $skip)
   (if (== $matches LNil) $depth
     (let (LCons (re:match $at $end $groups) $rest) $matches
       (if (== $at $skip) (scan $text $rest $depth $quoted -1)
         (let $c (str:byte-slice $text $at $end)
           (if $quoted
             (if (== $c "\\") (scan $text $rest $depth True $end)
               (scan $text $rest $depth (not (== $c "\"")) -1))
             (if (== $c "\"") (scan $text $rest $depth True -1)
               (if (== $c "(") (scan $text $rest (+ $depth 1) False -1)
                 (if (and (== $c ")") (> $depth 0))
                   (scan $text $rest (- $depth 1) False -1)
                   (scan $text $rest $depth False -1))))))))))
(= (via-case $text $list $n) (case 1 ((1 (if-let $text $list $n)))))
(= (via-let $text $list $n) (let $x 1 (if-let $text $list $n)))
(= (scan-via-case $text $list $n) (case 1 ((1 (scan $text $list 0 False -1)))))
'''

# name: (call, baseline, the loop runs compiled, instruction budget per step)
SHAPES = {
    "base": ("(base $text $found 0)", None, None, None),
    "base-cons": ("(base-cons $text $found 0)", None, None, None),
    "if-let": ("(if-let $text $found 0)", "base", True, 5000),
    "heads": ("(heads $text $found 0)", "base", True, 5000),
    "user-heads": ("(let $l (to-cons $found Nil) (user-heads $text $l 0))",
                   "base-cons", True, 5000),
    "by-case": ("(by-case $text $found 0)", "base", True, 5000),
    "with-and": ("(with-and $text $found 0)", "base", True, 7000),
    "with-not": ("(with-not $text $found 0)", "base", True, 7000),
    "scan": ("(scan $text $found 0 False -1)", "base", True, 7000),
    "via-case": ("(via-case $text $found 0)", "base", True, 5000),
    "via-let": ("(via-let $text $found 0)", "base", True, 5000),
    "scan-via-case": ("(scan-via-case $text $found 0)", "base", True, 7000),
}

# Text with every steering character, escapes and nesting, repeated.
UNIT = r'(say "a \"quoted\" (paren) \\ slash") (cmd (nested (deep "x")) tail) stray ) '


def program(call: str, repeats: int) -> str:
    text = UNIT * repeats
    literal = text.replace("\\", "\\\\").replace('"', '\\"')
    return (PRELUDE + DEFINITIONS +
            f'\n!(case (re:find-all (get-state &steer) "{literal}")\n'
            f'   (((re:matches $found)\n'
            f'     (let $text "{literal}" {call}))))\n')


def expected_results(repeats: int) -> dict[str, int]:
    """A character-level oracle, independent of regex/list execution."""
    text = UNIT * repeats
    steering = sum(char in '\\"()' for char in text)
    depth, quoted, escaped = 0, False, False
    for char in text:
        if escaped:
            escaped = False
        elif quoted:
            if char == '\\':
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif char == '(':
            depth += 1
        elif char == ')' and depth:
            depth -= 1
    return {
        'base': 0, 'base-cons': 0,
        'if-let': steering, 'heads': steering, 'user-heads': steering,
        'by-case': steering, 'via-case': steering, 'via-let': steering,
        'with-and': text.count(')'),
        'with-not': steering - text.count('"'),
        'scan': depth, 'scan-via-case': depth,
    }


def run(binary: Path, path: Path, extra: dict[str, str] | None = None,
        instructions: bool = False):
    env = {k: v for k, v in os.environ.items() if not k.startswith("CETTA_")}
    env.update(extra or {})
    command = [str(binary), "--lang", "petta", str(path)]
    if instructions:
        command = ["perf", "stat", "-x", ",", "-e", "instructions:u", "--"] + command
    result = subprocess.run(command, cwd=path.parent, env=env,
                            capture_output=True, text=True, timeout=120)
    count = None
    if instructions:
        for line in result.stderr.splitlines():
            fields = line.split(",")
            if len(fields) > 2 and fields[2].startswith("instructions") and fields[0].isdigit():
                count = int(fields[0])
    return result, count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--repeats", type=int, default=24)
    parser.add_argument("--instructions", action="store_true",
                        help="also count one step's instructions against each budget")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    if args.instructions and not shutil.which("perf"):
        parser.error("--instructions needs perf")
    with tempfile.TemporaryDirectory(prefix="evaluator-tax-") as temporary:
        directory = args.output or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        answers, failures = {}, []
        expected = expected_results(args.repeats)
        for name, (call, baseline, compiled, budget) in SHAPES.items():
            path = directory / f"{name}.metta"
            path.write_text(program(call, args.repeats))
            default, _ = run(binary, path, {"CETTA_OPEN_EQUATIONS_DEBUG": "host",
                                            "CETTA_PREPARED_PURE_DEBUG": "1"})
            routes = [default]
            for extra in ({"CETTA_OPEN_EQUATIONS_REFERENCE": "1"},
                          {"CETTA_PREPARED_PURE_REFERENCE": "1"},
                          {"CETTA_OPEN_EQUATIONS_REFERENCE": "1",
                           "CETTA_PREPARED_PURE_REFERENCE": "1"}):
                routes.append(run(binary, path, extra)[0])
            outputs = {r.stdout for r in routes}
            if any(r.returncode for r in routes) or len(outputs) != 1:
                failures.append(f"{name}: routes disagree or fail")
                continue
            printed = default.stdout.strip().splitlines()
            try:
                answer = int(printed[-1])
            except (ValueError, IndexError):
                failures.append(f"{name}: no numeric loop result; verify the runtime's source libraries")
                continue
            if answer != expected[name]:
                failures.append(f"{name}: result {answer}, expected {expected[name]}")
                continue
            answers[name] = str(answer)
            hosts = [line.removeprefix("open-equations host ")
                     for line in default.stderr.splitlines()
                     if line.startswith("open-equations host ")]
            per_step = [h for h in hosts if not h.startswith("__cetta_lib_re/")]
            if per_step:
                failures.append(f"{name}: a step left its machine for {sorted(set(per_step))}")
            lowered = "bodies lowered to register code" in default.stderr
            if compiled and not lowered:
                failures.append(f"{name}: the loop is not compiled")
        steps = int(answers.get("if-let", "0"))
        if steps <= 0:
            failures.append("the steering pattern found no matches")
        if args.instructions and steps > 0:
            # Two lengths of the same text: the difference is the added
            # steps, without imports or compilation; the baseline's
            # difference removes the longer text's matching and list.
            def growth(name):
                counted = []
                for repeats in (args.repeats, 2 * args.repeats):
                    path = directory / f"{name}-{repeats}.metta"
                    path.write_text(program(SHAPES[name][0], repeats))
                    result, count = run(binary, path, instructions=True)
                    printed = result.stdout.strip().splitlines()
                    if (result.returncode or not printed or count is None or
                            printed[-1] != str(expected_results(repeats)[name])):
                        failures.append(f"{name}: incomplete instruction observation at length {repeats}")
                        return None
                    counted.append(count)
                return counted[1] - counted[0]
            baselines = {name: growth(name) for name, shape in SHAPES.items()
                         if shape[1] is None}
            print(f"{'shape':16}{'instructions/step':>18}{'budget':>9}")
            for name, (call, baseline, compiled, budget) in SHAPES.items():
                if baseline is None:
                    continue
                measured = growth(name)
                if measured is None or baselines[baseline] is None:
                    continue
                per = (measured - baselines[baseline]) / steps
                over = per > budget
                print(f"{name:16}{per:18.0f}{budget:9}{'  OVER' if over else ''}")
                if over:
                    failures.append(f"{name}: {per:.0f} instructions per step, budget {budget}")
        if failures:
            print("\n".join(f"FAIL: {f}" for f in failures))
            return 1
    print(f"PASS: {len(SHAPES)} loop shapes over {steps} matches keep their machine, "
          "compile where they can, and answer alike on every route")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
