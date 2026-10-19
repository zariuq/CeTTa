#!/usr/bin/env python3
"""Compare the native textual frontend with its staged composition.

re:compile-with runs lexical preparation, the baked reader, selection,
lowering and compilation as one native composition. The same stages remain
separately observable: this gate composes them explicitly, exactly as the
library composed them before, and requires byte-identical stored programs
and identical failure values for generated valid and invalid descriptions,
under both look-behind policies, on every host.
"""
import argparse
import json
import random
from pathlib import Path

from gslt2parse_schema_v1 import Symbol
from test_lbnf_values_v1 import headed
from test_parser_artifacts_v1 import run

ROOT = Path(__file__).resolve().parents[1]
SELECTION = ('(ParserSelectionV1 (LCons (ParserRejectFollowingClassV1 '
             '"re-src:backref" "RxDigitClass") LNil))')
STAGED = f"""(= (staged-compile $text $policy)
   (case (__cetta_lib_re prepare-source ($text))
     (((re:source-text $cleaned $mapping)
       (let* (($reader (langdef:load-baked-parser "regex-pattern-reader"))
              ($syntax (langdef:parser-parse-text-selected $reader $cleaned {SELECTION}))
              ($lowered (__cetta_lib_re lower-source ($syntax $mapping $policy $cleaned)))
              ($observed
                (case $lowered
                  (((re:source-value $pattern $names) $lowered)
                   ($not-lowered
                    (let $evidence
                      (langdef:parser-parse-text-forest-selected $reader $cleaned {SELECTION} 0)
                      (__cetta_lib_re lower-source ($evidence $mapping $policy $cleaned))))))))
         (case $observed
           (($ready
             (let $closed (langdef:close-parser $reader)
               (case $ready
                 (((re:source-value $pattern $names) (__cetta_lib_re compile-source ($pattern $names)))
                  ($failure $failure)))))))))
      ($preparation $preparation))))
(= (composition-observation $value)
   (case $value
     (((NativeHandle $kind $id) (let* (($stored (re:store $value)) ($closed (re:close $value))) (stored $stored)))
      ($other $other))))
"""

ATOMS = ["a", "b", "λ", "0", "9", "_", "\\.", "\\\\", "\\n", "\\t", "\\d", "\\D", "\\w", "\\W",
         "\\s", "\\S", "\\b", "\\B", "\\A", "\\z", "\\x41", "\\x{3bb}", ".", "^", "$",
         "[abc]", "[^a-z]", "[a-z--b]", "[\\d_]", "[λ-μ]", "[a-]", "[]a]",
         "\\p{L}", "\\P{Nd}", "\\1", "\\2", "\\12", "\\k<w>", "(?P=w)", "\\q", "[z-a]"]
FLAGS = ["(?i)", "(?m)", "(?s)", "(?x)", "(?-i)", "(?u)", "(?U)", "(?i:", "(?x: "]
BROKEN = ["(", ")", "[", "a{", "a{2,1}", "*", "a**", "\\", "(?<", "{3}", "(?z)"]


def generate(rng, depth):
    if depth <= 0 or rng.random() < 0.25:
        return rng.choice(ATOMS)
    kind = rng.randrange(13)
    sub = generate(rng, depth - 1)
    if kind == 0:
        return sub + generate(rng, depth - 1)
    if kind == 1:
        return sub + "|" + generate(rng, depth - 1)
    if kind == 2:
        return "(" + sub + ")"
    if kind == 3:
        return "(?:" + sub + ")"
    if kind == 4:
        return "(?<w>" + sub + ")"
    if kind == 5:
        return "(?P<w>" + sub + ")"
    if kind == 6:
        return sub + rng.choice(["*", "+", "?", "*?", "+?", "??", "{2}", "{1,}", "{0,3}", "{2,3}?", "{0}"])
    if kind == 7:
        return rng.choice(["(?=", "(?!", "(?<=", "(?<!"]) + sub + ")"
    if kind == 8:
        flag = rng.choice(FLAGS)
        return flag + sub + (")" if flag.rstrip().endswith(":") else "")
    if kind == 9:
        return sub + " # note\n" if rng.random() < 0.5 else "(?x) " + sub + " "
    if kind == 10:
        return rng.choice(BROKEN) + sub if rng.random() < 0.5 else sub + rng.choice(BROKEN)
    if kind == 11:
        return "|".join(generate(rng, 0) for _ in range(rng.randrange(2, 6)))
    return "".join(generate(rng, 0) for _ in range(rng.randrange(2, 8)))


def patterns(count, seed):
    rng = random.Random(seed)
    fixed = ["", "a", "a|ab", "(a)(b)\\12", "(?<=([ab]+)([bc]+))$", "(?x) a  b # c\n c",
             "|".join(f"keyword{i:03d}" for i in range(300)), "(" * 64 + "λ" + ")" * 64, "a{40}"]
    return fixed + [generate(rng, rng.randrange(1, 5)) for _ in range(count)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/regex-frontend-composition-v1")
    parser.add_argument("--count", type=int, default=120)
    parser.add_argument("--seed", type=int, default=20261017)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    corpus = patterns(args.count, args.seed)
    checks = programs = failures = 0
    for host in ("he", "petta", "petta-1.1", "prime"):
        program = "!(import! &self re)\n" + STAGED
        for index, pattern in enumerate(corpus):
            literal = json.dumps(pattern, ensure_ascii=False)
            for policy in ("Forward", "Reverse"):
                rule = f"(re:lookbehind-policy {policy})"
                program += (f"!(let* (($fused (composition-observation (re:compile-with {literal} {rule})))"
                            f" ($staged (composition-observation (staged-compile {literal} {rule})))"
                            f" ($kind (case $fused (((stored $bytes) program) ($failure $failure)))))"
                            f" (RegexCompositionV1 {index} {policy} (== $fused $staged) $kind))\n")
        # Negative controls: the comparison distinguishes different programs
        # and different failures.
        program += ('!(let* (($a (composition-observation (re:compile "a")))'
                    ' ($b (composition-observation (staged-compile "b" (re:lookbehind-policy Forward))))'
                    ' ($c (composition-observation (re:compile "(")))'
                    ' ($d (composition-observation (staged-compile "a{" (re:lookbehind-policy Forward)))))'
                    ' (RegexCompositionControlV1 (== $a $b) (== $c $d)))\n')
        values = run(args.cetta.resolve(), host, args.output, "composition", program)
        controls = [row for row in values if headed(row, "RegexCompositionControlV1", 2)]
        assert len(controls) == 1 and all(value in (Symbol("False"), Symbol("false"))
                                          for value in controls[0][1:]), (host, controls)
        checks += 2
        rows = [row for row in values if headed(row, "RegexCompositionV1", 4)]
        assert len(rows) == 2 * len(corpus), (host, "missing composition observations", len(rows))
        for row in rows:
            equal, kind = row[3], row[4]
            assert equal in (Symbol("True"), Symbol("true")), (host, corpus[row[1]], row[2], kind)
            if kind == Symbol("program"):
                programs += 1
            else:
                failures += 1
            checks += 1
    assert programs and failures, ("the corpus must exercise both programs and failures", programs, failures)
    print(f"(RegexFrontendCompositionV1Summary {checks} 0 {programs} {failures})")


if __name__ == "__main__":
    main()
