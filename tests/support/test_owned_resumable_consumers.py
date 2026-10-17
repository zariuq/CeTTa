#!/usr/bin/env python3
"""Owned consumer observations: demand, duplicate order, effects and faults."""
from pathlib import Path
import argparse
import subprocess
import sys
import tempfile

# Expected observations were checked against the committed dialect runtime.
# Cases include refusals of fusion: they must retain ordinary evaluation.
CASES = [
    ('ordered-duplicates', 'he', '(= (choice) 3)\n(= (choice) 3)\n(= (choice) 5)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[3]\n[11]\n', 0),
    ('ordered-duplicates', 'petta', '(= (choice) 3)\n(= (choice) 3)\n(= (choice) 5)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '3\n11\n', 0),
    ('empty', 'he', '(= (choice) (empty))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[0]\n[0]\n', 0),
    ('empty', 'petta', '(= (choice) (empty))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '0\n0\n', 0),
    ('late-effect', 'he', '(= (choice) 3)\n(= (choice) (let $ignored (println! late-effect) 5))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', 'late-effect\nlate-effect\n[2]\n[8]\n', 0),
    ('late-effect', 'petta', '(= (choice) 3)\n(= (choice) (let $ignored (println! late-effect) 5))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', 'late-effect\nlate-effect\n2\n8\n', 0),
    ('late-fault', 'he', '(= (choice) 3)\n(= (choice) (/ 1 0))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[1]\n[3]\n', 0),
    ('late-fault', 'petta', '(= (choice) 3)\n(= (choice) (/ 1 0))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '', 2),
    ('unselected-effect', 'he', '(= (choice) (if True 3 (println! unselected-effect)))\n(= (choice) 5)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[2]\n[8]\n', 0),
    ('unselected-effect', 'petta', '(= (choice) (if True 3 (println! unselected-effect)))\n(= (choice) 5)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '2\n8\n', 0),
    ('conditional', 'he', '(= (choice) (if (< 1 2) (+ 1 2) 5))\n(= (choice) 5)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[2]\n[8]\n', 0),
    ('conditional', 'petta', '(= (choice) (if (< 1 2) (+ 1 2) 5))\n(= (choice) 5)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '2\n8\n', 0),
    ('non-number', 'he', '(= (choice) 3)\n(= (choice) held)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[2]\n[(+ 3 held)]\n', 0),
    ('non-number', 'petta', '(= (choice) 3)\n(= (choice) held)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '', 2),
    ('nested', 'he', '(= (coin $n) $n)\n(= (coin $n) (+ $n 1))\n(= (choice) (+ (coin 2) (coin 3)))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[4]\n[24]\n', 0),
    ('nested', 'petta', '(= (coin $n) $n)\n(= (coin $n) (+ $n 1))\n(= (choice) (+ (coin 2) (coin 3)))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '4\n24\n', 0),
    ('numeric-overflow', 'he', '(= (choice) 9223372036854775807)\n(= (choice) 1)\n(= (choice) 2)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (+ $a $x)))\n', '[3]\n[9223372036854775810]\n', 0),
    ('numeric-overflow', 'petta', '(= (choice) 9223372036854775807)\n(= (choice) 1)\n(= (choice) 2)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (+ $a $x))\n', '3\n9223372036854775810\n', 0),
    ('nondeterministic-step', 'he', '(= (choice) 3)\n(= (choice) 3)\n(= (both $a $x) (+ $a $x))\n(= (both $a $x) (- $a $x))\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (both $a $x)))\n', '[2]\n[6, 0, 0, -6]\n', 0),
    ('nondeterministic-step', 'petta', '(= (choice) 3)\n(= (choice) 3)\n(= (both $a $x) (+ $a $x))\n(= (both $a $x) (- $a $x))\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (both $a $x))\n', '2\n6\n0\n0\n-6\n', 0),
    ('step-effect', 'he', '(= (choice) 3)\n(= (choice) 5)\n!(size (collapse (choice)))\n!(let $xs (collapse (choice)) (foldl-atom $xs 0 $a $x (let $ignored (println! step-effect) (+ $a $x))))\n', 'step-effect\nstep-effect\n[2]\n[8]\n', 0),
    ('step-effect', 'petta', '(= (choice) 3)\n(= (choice) 5)\n!(length (collapse (choice)))\n!(foldl-atom (collapse (choice)) 0 $a $x (let $ignored (println! step-effect) (+ $a $x)))\n', 'step-effect\nstep-effect\n2\n8\n', 0),
    ('held-syntax', 'he', '(= (choice) 3)\n!(let $held (collapse (println! held-effect)) (quote $held))\n', 'held-effect\n[(quote (()))]\n', 0),
    ('held-syntax', 'petta', '(= (choice) 3)\n!(let $held (collapse (println! held-effect)) (quote $held))\n', 'held-effect\n(true)\n', 0),
    ('proof-count', 'he', '(= (proof-leaves 0) left)\n(= (proof-leaves 0) right)\n(= (proof-leaves $n) (if (> $n 0) (Node (proof-leaves (- $n 1)) (proof-leaves (- $n 1))) (empty)))\n!(size (collapse (proof-leaves 3)))\n', '[256]\n', 0),
    ('numeric-fold', 'he', '(= (samples $n) (* $n $n))\n(= (samples $n) (if (< $n 1024) (samples (+ $n 1)) (empty)))\n!(let $xs (collapse (samples 0)) (foldl-atom $xs 0 $acc $item (+ $acc $item)))\n', '[358438400]\n', 0),
    ('proof-count', 'petta', '(= (proof-leaves 0) left)\n(= (proof-leaves 0) right)\n(= (proof-leaves $n) (if (> $n 0) (Node (proof-leaves (- $n 1)) (proof-leaves (- $n 1))) (empty)))\n!(length (collapse (proof-leaves 3)))\n', '256\n', 0),
    ('numeric-fold', 'petta', '(= (samples $n) (* $n $n))\n(= (samples $n) (if (< $n 1024) (samples (+ $n 1)) (empty)))\n!(foldl-atom (collapse (samples 0)) 0 $acc $item (+ $acc $item))\n', '358438400\n', 0),
]

# Each case reaches the ordinary count consumer as well as the dialect's
# materialized collection. Expected values retain physical duplicate order.
CAPTURE_CASES = [
    ('captured-compound',
     '(= (pick $x $y) $x)\n(= (pick $x $y) $y)\n(= (pick $x $y) $x)\n',
     '(pick $capture (Pair 9 Right))', '(let $capture (Pair 7 Left) {query})',
     3, '((Pair 7 Left) (Pair 9 Right) (Pair 7 Left))',
     '((Pair 7 Left) (Pair 9 Right) (Pair 7 Left))'),
    ('nested-pattern',
     '(= (peel (Pair $x $tail)) (Answer $x $tail))\n'
     '(= (peel (Pair $x $tail)) (Answer $x $tail))\n',
     '(peel (Pair Box (Cons 7 Nil)))', '{query}',
     2, '((Answer Box (Cons 7 Nil)) (Answer Box (Cons 7 Nil)))',
     '((Answer Box (Cons 7 Nil)) (Answer Box (Cons 7 Nil)))'),
    ('recursive-data',
     '(= (members (Cons $x $tail)) $x)\n'
     '(= (members (Cons $x $tail)) (members $tail))\n'
     '(= (members Nil) (empty))\n',
     '(members (Cons (Pair 7 Left) (Cons (Pair 7 Left) (Cons (Pair 9 Right) Nil))))',
     '{query}', 3, '((Pair 7 Left) (Pair 7 Left) (Pair 9 Right))',
     '((Pair 7 Left) (Pair 7 Left) (Pair 9 Right))'),
    ('boolean-capture',
     '(= (flag $ok $value) (if $ok (Pair $value True) (empty)))\n'
     '(= (flagged) (flag (< 1 2) (Tag 7)))\n',
     '(flagged)', '{query}',
     1, '((Pair (Tag 7) True))', '((Pair (Tag 7) true))'),
    ('boolean-empty',
     '(= (flag $ok $value) (if $ok (Pair $value True) (empty)))\n'
     '(= (flagged) (flag (> 1 2) (Tag 7)))\n',
     '(flagged)', '{query}', 0, '()', '()'),
    ('mixed-call-shapes',
     '(= (pass $value) $value)\n'
     '(= (pack) (Pair (pass (Pair 7 True)) (pass 9)))\n',
     '(pack)', '{query}', 1, '((Pair (Pair 7 True) 9))', '((Pair (Pair 7 true) 9))'),
    ('empty-value-capture', '(= (echo $value) $value)\n',
     '(echo ())', '{query}', 1, '(())', '(())'),
]

OWNED_CAPTURE_ADMISSION = {}
for name, equations, producer, outer, count, he_values, petta_values in CAPTURE_CASES:
    for language, size, values in [('he', 'size', he_values),
                                    ('petta', 'length', petta_values)]:
        source = equations + '!' + outer.format(query=f'({size} (collapse {producer}))')
        source += '\n!' + outer.format(query=f'(collapse {producer})') + '\n'
        expected = f'[{count}]\n[{values}]\n' if language == 'he' else f'{count}\n{values}\n'
        CASES.append((name, language, source, expected, 0))
        OWNED_CAPTURE_ADMISSION[name, language] = True

for language, size, expected, status in [('he', 'size', '[1]\n', 0),
                                         ('petta', 'length', '', 2)]:
    # Reusing numeric admission for the same head with a data argument is
    # unsound. The ordinary boundary retains its inert result or fault.
    CASES.append(('data-in-number', language,
        '(= (pass $value) $value)\n'
        '(= (pack) (+ (pass 1) (pass (Pair 7 True))))\n'
        f'!({size} (collapse (pack)))\n', expected, status))
    OWNED_CAPTURE_ADMISSION['data-in-number', language] = False

for language, size, count, values in [
        ('he', 'size', 2, '((Pair 7 True) (same (Pair 7 True) (Pair 9 False)))'),
        ('petta', 'length', 1, '((Pair 7 true))')]:
    # A failed repeated-variable match has different no-rule readings.
    source = ('(= (same $x $x) $x)\n'
              '(= (pick) (same (Pair 7 True) (Pair 7 True)))\n'
              '(= (pick) (same (Pair 7 True) (Pair 9 False)))\n'
              f'!({size} (collapse (pick)))\n!(collapse (pick))\n')
    expected = f'[{count}]\n[{values}]\n' if language == 'he' else f'{count}\n{values}\n'
    CASES.append(('repeated-pattern', language, source, expected, 0))
    OWNED_CAPTURE_ADMISSION['repeated-pattern', language] = True

for name, argument, value in [
        ('literal-boolean-input', 'True', 'true'),
        ('literal-boolean-field', '(Pair 7 True)', '(Pair 7 true)')]:
    # Symbolic PeTTa truth values still require its ordinary normalization.
    # The owned producer only accepts already-completed entry arguments.
    CASES.append((name, 'petta', '(= (pass $value) $value)\n'
        f'!(length (collapse (pass {argument})))\n!(collapse (pass {argument}))\n',
        f'1\n({value})\n', 0))
    OWNED_CAPTURE_ADMISSION[name, 'petta'] = False

for name, body, value in [
        ('cache-rebind',
         '(let $a ({size} (collapse (echo 7))) '
         '(Pair $a ({size} (collapse (echo (Pair 9 Left))))))', '(Pair 1 1)'),
        ('cache-source-change',
         '(let $a ({size} (collapse (echo (Pair 7 Left)))) '
         '(let $i (add-atom &self (= (echo $x) $x)) '
         '(Pair $a ({size} (collapse (echo (Pair 7 Left)))))))', '(Pair 1 2)'),
        ('cache-loop',
         '(count-loop 8 (Pair 7 Left))', '8')]:
    for language, size in [('he', 'size'), ('petta', 'length')]:
        source = '(= (echo $x) $x)\n'
        if name == 'cache-loop':
            source += ('(= (count-loop $n $x) (if (< $n 1) 0 '
                '(+ ({size} (collapse (echo $x))) (count-loop (- $n 1) $x))))\n'
                .replace('{size}', size))
        source += '!' + body.replace('{size}', size) + '\n'
        expected = f'[{value}]\n' if language == 'he' else f'{value}\n'
        CASES.append((name, language, source, expected, 0))
        OWNED_CAPTURE_ADMISSION[name, language] = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--stats', action='store_true')
    args = parser.parse_args()
    binary = args.binary.resolve()
    failures = []
    with tempfile.TemporaryDirectory(prefix="owned-consumers-", dir="runtime") as directory:
        for name, language, text, expected, expected_exit in CASES:
            source = Path(directory) / (name + "-" + language + ".metta")
            source.write_text(text)
            command = [str(binary), "--lang", language, "--profile", "extended"]
            if args.stats:
                command.append('--emit-runtime-stats')
            result = subprocess.run(command + [str(source)],
                capture_output=True, text=True, timeout=30)
            if result.returncode != expected_exit or result.stdout != expected:
                failures.append((name, language, result.returncode, result.stdout, result.stderr))
            elif expected_exit and "uncaught PeTTa error:" not in result.stderr:
                failures.append((name, language, "missing fault", result.stderr))
            if args.stats and (name, language) in OWNED_CAPTURE_ADMISSION:
                counters = {fields[1]: int(fields[2]) for line in result.stderr.splitlines()
                            if len(fields := line.split()) == 3 and fields[0] == 'runtime-counter'}
                admitted = counters.get('owned-execution-admission', 0) > 0
                if admitted != OWNED_CAPTURE_ADMISSION[name, language]:
                    failures.append((name, language, 'owned admission', admitted))
                if name == 'cache-loop':
                    for counter, expected_count in [
                            ('prepared-pure-call-program-cache-store', 1),
                            ('prepared-pure-call-program-cache-hit', 7)]:
                        if counters.get(counter) != expected_count:
                            failures.append((name, language, counter, counters.get(counter)))
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print("owned resumable consumers: " + str(len(CASES)) + " dialect observations passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
