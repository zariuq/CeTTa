#!/usr/bin/env python3
import subprocess
import sys


def run_cetta(cetta_bin, flags, program):
    proc = subprocess.run(
        [cetta_bin] + flags + ["-e", program],
        capture_output=True,
        text=True,
        timeout=20,
    )
    if proc.returncode != 0:
        raise AssertionError(
            f"cetta {' '.join(flags)} exited {proc.returncode}\n"
            f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
        )
    return [line.strip() for line in proc.stdout.strip().splitlines() if line.strip()]


def expect_lines(name, got, expected):
    if got != expected:
        raise AssertionError(
            f"{name} mismatch:\n  got:      {got}\n  expected: {expected}"
        )


def main():
    cetta_bin = sys.argv[1] if len(sys.argv) > 1 else "./cetta"

    # 1. PeTTa --profile extended: Cold/Warm trap, raw match preservation,
    #    raw variable binding, empty_after_edit (install+remove), dynamic
    #    cache invalidation, head alias, and compound ground alias.
    got = run_cetta(
        cetta_bin,
        ["--lang", "petta", "--profile", "extended"],
        """
        (= (probe Cold) p1)
        (= (probe Warm) p2)
        (= (probe Cold) p3)
        (= (keep-raw $x) $x)
        (= (describe AI:UnitRange) unit-interval)
        (= (Pet Fido) good-dog)

        ; Warm caches before any =e exists (equational_revision == 0).
        !(collapse (probe Warm))

        ; Dynamically install aliases (invalidates compiled/program caches).
        !(let $_ (add-atom &self (=e Cold Warm))
          (let $_ (add-atom &self (=e AI:UnitRange (Range Float 0 1)))
            (let $_ (add-atom &self (=e Pet Companion))
              (let $_ (add-atom &self (=e Fido fido))
                installed))))

        ; Warm/Cold trap: both queries return all 3 raw occurrences in order.
        !(collapse (probe Warm))
        !(collapse (probe Cold))
        ; Raw match preserves stored syntax unchanged.
        !(collapse (match &self (= (probe Cold) $x) $x))
        ; Pattern variable binds caller's raw syntax (`Warm`), not canonical rep.
        !(keep-raw Warm)
        ; Compound ground alias & head alias.
        !(describe (Range Float 0 1))
        !(Companion fido)

        ; LiveTheoryProjections.empty_after_edit: remove (=e Cold Warm) -> rev advances,
        ; Cold/Warm no longer aliased while remaining aliases still work.
        !(let $_ (remove-atom &self (=e Cold Warm)) (collapse (probe Warm)))
        !(describe (Range Float 0 1))
        """,
    )
    expect_lines(
        "petta-extended-core",
        got,
        [
            "(p2)",
            "installed",
            "(p1 p2 p3)",
            "(p1 p2 p3)",
            "(p1 p3)",
            "Warm",
            "unit-interval",
            "good-dog",
            "(p2)",
            "unit-interval",
        ],
    )

    # 2. HE --profile extended: full parity on Cold/Warm, AI:UnitRange, head alias,
    #    eq:norm, eq:same, eq:match, raw match, and empty_after_edit.
    got = run_cetta(
        cetta_bin,
        ["--lang", "he", "--profile", "extended"],
        """
        (=e Cold Warm)
        (=e AI:UnitRange (Range Float 0 1))
        (=e Pet Companion)
        (=e Fido fido)
        (= (probe Cold) p1)
        (= (probe Warm) p2)
        (= (probe Cold) p3)
        (= (keep-raw $x) $x)
        (= (describe AI:UnitRange) unit-interval)
        (= (Pet Fido) good-dog)
        (Dog Fido)
        (Dog Fido)
        (HasTail fido)

        !(collapse (probe Warm))
        !(collapse (probe Cold))
        !(collapse (match &self (= (probe Cold) $x) $x))
        !(keep-raw Warm)
        !(describe (Range Float 0 1))
        !(Companion fido)
        !(eq:norm &self (Range Float 0 1))
        !(eq:same &self (Schema AI:UnitRange) (Schema (Range Float 0 1)))
        !(collapse (eq:match &self (Dog $x) (eq:match &self (HasTail $x) $x)))
        !(let $_ (remove-atom &self (=e Cold Warm)) (collapse (probe Warm)))
        """,
    )
    expect_lines(
        "he-extended-core",
        got,
        [
            "[(p1 p2 p3)]",
            "[(p1 p2 p3)]",
            "[(p1 p3)]",
            "[Warm]",
            "[unit-interval]",
            "[good-dog]",
            "[AI:UnitRange]",
            "[True]",
            "[(Fido Fido)]",
            "[(p2)]",
        ],
    )

    # 3. Backward-chainer goal-table sharing & pattern-miner join in PeTTa extended.
    got = run_cetta(
        cetta_bin,
        ["--lang", "petta", "--profile", "extended"],
        """
        !(bind! &kb (new-space))
        !(bind! &goals (new-space))
        !(bind! &counter (new-state 0))
        !(add-atom &kb (=e Fido fido))
        !(add-atom &kb (Dog Fido))
        !(add-atom &kb (Dog Fido))
        !(add-atom &kb (HasTail fido))

        (= (intern-goal $g)
           (let $key (eq:norm &kb $g)
             (let $existing (collapse (match &goals (goal-id $key $id) $id))
               (if (== $existing ())
                   (let $next (get-state &counter)
                     (let $_ (change-state! &counter (+ $next 1))
                       (let $_ (add-atom &goals (goal-id $key $next))
                         $next)))
                   (car-atom $existing)))))

        ; Both spellings share goal ID 0 instead of allocating two goals.
        !(intern-goal (Pet Fido))
        !(intern-goal (Pet fido))
        !(get-state &counter)

        ; Pattern-miner join via eq:match preserves duplicate evidence rows.
        !(collapse (eq:match &kb (Dog $x) (eq:match &kb (HasTail $x) $x)))
        ; Raw match remains strictly literal.
        !(collapse (match &kb (Dog fido) yes))
        !(collapse (eq:match &kb (Dog fido) yes))
        ; Compound-head alias indexed in eq:match head-arity bucket.
        !(let $_ (add-atom &kb (=e (Wrap A) Special))
          (let $_ (add-atom &kb ((Wrap A) 42))
            (collapse (eq:match &kb (Special $v) $v))))
        """,
    )
    # First 7 lines are bind!/add-atom setup outputs (`true` / `()`), check last 7 lines.
    expect_lines(
        "petta-extended-chainer-and-miner",
        got[-7:],
        [
            "0",
            "0",
            "1",
            "(Fido Fido)",
            "()",
            "(yes yes)",
            "(42)",
        ],
    )

    # 3b. Two-argument (eq:match pat tmpl) &self shorthand in both dialects,
    #     including rule-body binding analysis, outer let vars, repeated vars,
    #     and shorthand eq:norm / eq:same / eq:class.
    shorthand_prog = """
    (=e> Cold Cool Warm)
    (Row Cold First)
    (Row Warm Second)
    (Pair Cold Warm)
    (= (tagged-rows $tag)
       (collapse (eq:match (Row Cool $v) ($tag $v))))
    !(tagged-rows OK)
    !(collapse (eq:match (Pair $x $x) $x))
    !(let $k Hi (collapse (eq:match (Row Cold $v) ($k $v))))
    !(eq:norm Cold)
    !(eq:same Cold Warm)
    !(collapse (eq:class Cold))
    """
    expect_lines(
        "petta-extended-shorthand",
        run_cetta(cetta_bin, ["--lang", "petta", "--profile", "extended"], shorthand_prog),
        [
            "((OK First) (OK Second))",
            "(Cold)",
            "((Hi First) (Hi Second))",
            "Warm",
            "true",
            "(Warm Cold Cool)",
        ],
    )
    expect_lines(
        "he-extended-shorthand",
        run_cetta(cetta_bin, ["--lang", "he", "--profile", "extended"], shorthand_prog),
        [
            "[((OK First) (OK Second))]",
            "[(Cold)]",
            "[((Hi First) (Hi Second))]",
            "[Warm]",
            "[True]",
            "[(Warm Cold Cool)]",
        ],
    )

    # 4. Negative controls: default & non-extended profiles leave =e and eq:* inert.
    for flags, expected_probe, expected_same, expected_match2 in [
        (["--lang", "petta"], "(p2)", "(eq:same &self Cold Warm)", "(eq:match (Row Warm $V0) $V0)"),
        (["--lang", "petta", "--profile", "ai"], "(p2)", "(eq:same &self Cold Warm)", "(eq:match (Row Warm $V0) $V0)"),
        (["--lang", "he"], "[(p2)]", "[(eq:same ModuleSpace(GroundingSpace-top) Cold Warm)]", "[(eq:match (Row Warm $v) $v)]"),
        (["--lang", "he", "--profile", "he-compat"], "[(p2)]", "[(eq:same ModuleSpace(GroundingSpace-top) Cold Warm)]", "[(eq:match (Row Warm $v) $v)]"),
    ]:
        got = run_cetta(
            cetta_bin,
            flags,
            """
            (=e Cold Warm)
            (= (probe Cold) p1)
            (= (probe Warm) p2)
            (Row Cold First)
            !(collapse (probe Warm))
            !(eq:same &self Cold Warm)
            !(eq:match (Row Warm $v) $v)
            """,
        )
        expect_lines(
            f"negative-{'-'.join(flags)}",
            got,
            [expected_probe, expected_same, expected_match2],
        )

    print("PASS: scoped equational alias (=e / eq:norm / eq:same / eq:match) v1")


if __name__ == "__main__":
    main()
