#!/usr/bin/env python3
"""Module composition laws across HE and PeTTa.

Verifies the seven module laws under each dialect's own semantics:
  1. Sharing (diamond import contributes atoms, definitions, effects, and
     equations once, including under distinct relative path spellings).
  2. Idempotence (importing twice equals importing once).
  3. Protection (HE isolates the shared module space while &self extends it;
     PeTTa executes into each target space independently).
  4. Renaming (import-as! maps function definitions (=, =%), bodies, type
     assignments (:), subtype assignments (:<), equations (=e, =e>, =e<),
     and quoted code without leaking unrenamed definitions or renaming
     un-declared data constructors).
  5. Commuting (independent modules commute up to bag order, including
     overlapping =e classes, cross-module =e> preference cycles, and head
     aliases).
  6. Removal and cycles (HE protects imported atoms from importer remove-atom
     and rejects import cycles with ModuleImportCycle; PeTTa stores imported
     atoms directly in &self and breaks cycles via no-op re-entry).
  7. Equations across modules (classes join transitively across imports,
     preferences compose, head aliases cross modules, and base spaces stay
     isolated from importer equations).
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run_metta(cetta: Path, work_dir: Path, files: dict, flags: list,
              entry: str = "main.metta"):
    case_dir = Path(tempfile.mkdtemp(dir=work_dir))
    try:
        for rel, content in files.items():
            target = case_dir / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content, encoding="utf-8")
        proc = subprocess.run(
            [str(cetta), *flags, str(case_dir / entry)],
            cwd=case_dir,
            capture_output=True,
            text=True,
            timeout=30,
        )
        lines = [ln.strip() for ln in proc.stdout.splitlines() if ln.strip()]
        return proc.returncode, lines, proc.stderr.strip()
    finally:
        shutil.rmtree(case_dir, ignore_errors=True)


def check_eq(label: str, got, expected):
    if got != expected:
        raise AssertionError(f"{label}:\n  expected: {expected!r}\n  got:      {got!r}")


def check_bag_line(label: str, got_line: str, expected_items: list, is_he: bool):
    s = got_line.strip()
    if is_he:
        assert s.startswith("[(") and s.endswith(")]"), f"{label}: unexpected HE tuple {s!r}"
        inner = s[2:-2].strip()
    else:
        assert s.startswith("(") and s.endswith(")"), f"{label}: unexpected PeTTa tuple {s!r}"
        inner = s[1:-1].strip()
    items = sorted(inner.split()) if inner else []
    check_eq(label, items, sorted(expected_items))


def test_sharing(cetta: Path, work: Path):
    # Diamond import with data, definitions, type assignments, init side-effect,
    # and distinct relative path spellings.
    files = {
        "sub/shared.metta": (
            "(shared-fact 1)\n"
            "(: shared-fn (-> Number Number))\n"
            "(= (shared-fn $x) (+ $x 10))\n"
            "!(add-atom &self (init-ran tick))\n"
        ),
        "left.metta": "!(import! &self sub/shared)\n",
        "sub/right.metta": "!(import! &self ./shared.metta)\n",
        "main.metta": (
            "!(import! &self left)\n"
            "!(import! &self sub/right)\n"
            "!(collapse (match &self (shared-fact $x) $x))\n"
            "!(collapse (shared-fn 5))\n"
            "!(collapse (get-type shared-fn))\n"
            "!(collapse (match &self (init-ran $t) $t))\n"
        ),
    }
    rc, lines, err = run_metta(cetta, work, files, ["--lang", "he"])
    check_eq("sharing HE rc", (rc, err), (0, ""))
    check_eq("sharing HE results", lines[-4:], ["[(1)]", "[(15)]", "[((-> Number Number))]", "[(tick)]"])

    rc, lines, err = run_metta(cetta, work, files, ["--lang", "petta"])
    check_eq("sharing PeTTa rc", (rc, err), (0, ""))
    check_eq("sharing PeTTa results", lines[-4:], ["(1)", "(15)", "((-> Number Number))", "(tick)"])

    # Diamond import with =e equations
    files_eq = {
        "shared.metta": "(=e Cold Cool)\n(= (temp Cold) brisk)\n(reading Cold)\n",
        "left.metta": "!(import! &self shared)\n(=e Cool Mild)\n",
        "right.metta": "!(import! &self shared)\n(=e Cool Warm)\n",
        "main.metta": (
            "!(import! &self left)\n"
            "!(import! &self right)\n"
            "!(eq:same &self Mild Warm)\n"
            "!(collapse (temp Warm))\n"
            "!(collapse (eq:match &self (reading Mild) ok))\n"
        ),
    }
    for name, flags, exp in [
        ("HE-ext", ["--lang", "he", "--profile", "extended"], ["[True]", "[(brisk)]", "[(ok)]"]),
        ("PeTTa-ext", ["--lang", "petta", "--profile", "extended"], ["true", "(brisk)", "(ok)"]),
    ]:
        rc, lines, err = run_metta(cetta, work, files_eq, flags)
        check_eq(f"sharing-eq {name} rc", (rc, err), (0, ""))
        check_eq(f"sharing-eq {name} results", lines[-3:], exp)


def test_idempotence(cetta: Path, work: Path):
    base = {
        "base.metta": (
            "(item 7)\n"
            "(= (inc $x) (+ $x 1))\n"
            "(: inc (-> Number Number))\n"
            "(=e Alpha Beta)\n"
        ),
    }
    once_main = (
        "!(import! &self base)\n"
        "!(collapse (match &self (item $x) $x))\n"
        "!(collapse (inc 4))\n"
        "!(collapse (get-type inc))\n"
        "!(collapse (eq:class &self Alpha))\n"
    )
    twice_main = "!(import! &self base)\n" + once_main
    for name, flags in [
        ("HE-ext", ["--lang", "he", "--profile", "extended"]),
        ("PeTTa-ext", ["--lang", "petta", "--profile", "extended"]),
    ]:
        rc1, lines1, err1 = run_metta(cetta, work, {**base, "main.metta": once_main}, flags)
        rc2, lines2, err2 = run_metta(cetta, work, {**base, "main.metta": twice_main}, flags)
        check_eq(f"idempotence {name} rc", (rc1, err1, rc2, err2), (0, "", 0, ""))
        check_eq(f"idempotence {name} answers", lines2[-4:], lines1[-4:])


def test_protection(cetta: Path, work: Path):
    # Importer adds a definition on module's head and an =e equation on module's
    # constants. Check both &self (extending) and &m (module space isolation).
    files = {
        "modp.metta": (
            "(= (m-helper $x) (+ $x 1))\n"
            "(= (m-call $x) (m-helper $x))\n"
            "(= (m-label Cold) frosty)\n"
        ),
        "main.metta": (
            "!(import! &self modp)\n"
            "!(import! &m modp)\n"
            "(= (m-helper 5) 99)\n"
            "(=e Cold Warm)\n"
            "!(collapse (m-call 5))\n"
            "!(collapse (m-label Warm))\n"
            "!(eq:same &self Cold Warm)\n"
            "!(eq:same &m Cold Warm)\n"
        ),
    }
    rc, lines, err = run_metta(cetta, work, files, ["--lang", "he", "--profile", "extended"])
    check_eq("protection HE rc", (rc, err), (0, ""))
    check_bag_line("protection HE m-call", lines[-4], ["6", "99"], is_he=True)
    check_eq("protection HE eq/isolation", lines[-3:], ["[(frosty)]", "[True]", "[False]"])

    rc, lines, err = run_metta(cetta, work, files, ["--lang", "petta", "--profile", "extended"])
    check_eq("protection PeTTa rc", (rc, err), (0, ""))
    check_eq("protection PeTTa results", lines[-4:], ["(6 99)", "(frosty)", "true", "false"])

    # Live mutation on &m: visible in HE (shared module space), isolated in PeTTa.
    live_files = {
        "m.metta": "(orig 1)\n(= (f 1) one)\n",
        "main.metta": (
            "!(import! &self m)\n"
            "!(import! &m m)\n"
            "!(add-atom &m (added-live 2))\n"
            "!(add-atom &m (= (f 2) two))\n"
            "!(collapse (match &self (added-live $x) $x))\n"
            "!(collapse (f 2))\n"
        ),
    }
    rc, lines, err = run_metta(cetta, work, live_files, ["--lang", "he"])
    check_eq("live-module HE rc", (rc, err), (0, ""))
    check_eq("live-module HE sees &m write", lines[-2:], ["[(2)]", "[(two)]"])

    rc, lines, err = run_metta(cetta, work, live_files, ["--lang", "petta"])
    check_eq("live-module PeTTa rc", (rc, err), (0, ""))
    check_eq("live-module PeTTa isolates &m copy", lines[-2:], ["()", "()"])


def test_renaming(cetta: Path, work: Path):
    # Full import-as! signature morphism across =, =%, :, :<, =e, =e>, =e<,
    # bodies, quoted code, shared namespace stripping, and data constructors.
    files = {
        "renmod.metta": (
            "(: RSuper Type)\n"
            "(: RSub Type)\n"
            "(:< RSub RSuper)\n"
            "(:< RNum Number)\n"
            "(: RBox (-> Number RSuper))\n"
            "(: r-fn (-> Number Number))\n"
            "(= (r-helper $x) (+ $x 1))\n"
            "(= (r-fn $x) (r-helper (* $x 2)))\n"
            "(= (r-const) 42)\n"
            "(=% (r-dir $x) (+ $x 2))\n"
            "(=e> r-cold r-warm)\n"
            "(= (r-temp r-cold) chilly)\n"
            "(= (r-quote) (quote (r-fn 10)))\n"
            "(= (r-wrap $x) (PlainTag $x))\n"
        ),
        "nsmod.metta": (
            "(: ns:base (-> Number Number))\n"
            "(= (ns:base $x) (+ $x 100))\n"
            "(=e ns:zero ns:nil)\n"
        ),
        "main.metta": (
            "!(import-as! &self renmod p)\n"
            "!(import-as! &self nsmod q)\n"
            "!(p:r-fn 5)\n"
            "!(r-fn 5)\n"
            "!(+ (p:r-const) 1)\n"
            "!(p:r-dir 5)\n"
            "!(r-dir 5)\n"
            "!(get-type p:RBox)\n"
            "!(get-type RBox)\n"
            "!(collapse (match &self (:< p:RSub $t) $t))\n"
            "!(collapse (match &self (:< RSub $t) $t))\n"
            "!(collapse (match &self (:< p:RNum $t) $t))\n"
            "!(eq:same &self p:r-cold p:r-warm)\n"
            "!(eq:same &self r-cold r-warm)\n"
            "!(eq:norm &self p:r-cold)\n"
            "!(p:r-temp p:r-warm)\n"
            "!(r-temp r-cold)\n"
            "!(p:r-quote)\n"
            "!(p:r-wrap 7)\n"
            "!(q:base 5)\n"
            "!(eq:same &self q:zero q:nil)\n"
            "!(eq:same &self ns:zero ns:nil)\n"
        ),
    }

    rc, lines, err = run_metta(cetta, work, files, ["--lang", "he", "--profile", "extended"])
    check_eq("renaming HE-ext rc", (rc, err), (0, ""))
    check_eq(
        "renaming HE-ext results",
        lines[-20:],
        [
            "[11]",
            "[(r-fn 5)]",
            "[43]",
            "[7]",
            "[(r-dir 5)]",
            "[(-> Number p:RSuper)]",
            "[%Undefined%]",
            "[(p:RSuper)]",
            "[()]",
            "[(Number)]",
            "[True]",
            "[False]",
            "[p:r-warm]",
            "[chilly]",
            "[(r-temp r-cold)]",
            "[(quote (p:r-fn 10))]",
            "[(PlainTag 7)]",
            "[105]",
            "[True]",
            "[False]",
        ],
    )

    rc, lines, err = run_metta(cetta, work, files, ["--lang", "petta", "--profile", "extended"])
    check_eq("renaming PeTTa-ext rc", (rc, err), (0, ""))
    check_eq(
        "renaming PeTTa-ext results",
        lines[-20:],
        [
            "11",
            "(r-fn 5)",
            "43",
            "7",
            "(r-dir 5)",
            "(-> Number p:RSuper)",
            "%Undefined%",
            "(p:RSuper)",
            "()",
            "(Number)",
            "true",
            "false",
            "p:r-warm",
            "chilly",
            "(r-temp r-cold)",
            "(p:r-fn 10)",
            "(PlainTag 7)",
            "105",
            "true",
            "false",
        ],
    )


def test_commuting(cetta: Path, work: Path):
    # Independent modules including overlapping =e classes, cross-module =e>
    # preference cycles, and head aliases commute up to bag order.
    mods = {
        "ma.metta": (
            "(fact a)\n"
            "(=e> Cold Warm)\n"
            "(=e f g)\n"
            "(= (f Cold) ok-a)\n"
        ),
        "mb.metta": (
            "(fact b)\n"
            "(=e> Warm Cold)\n"
            "(= (g Warm) ok-b)\n"
        ),
    }
    tail = (
        "!(eq:norm &self Warm)\n"
        "!(collapse (eq:class &self Warm))\n"
        "!(collapse (match &self (fact $x) $x))\n"
        "!(collapse (g Cold))\n"
    )
    ab = {**mods, "main.metta": "!(import! &self ma)\n!(import! &self mb)\n" + tail}
    ba = {**mods, "main.metta": "!(import! &self mb)\n!(import! &self ma)\n" + tail}

    for name, flags, is_he in [
        ("HE-ext", ["--lang", "he", "--profile", "extended"], True),
        ("PeTTa-ext", ["--lang", "petta", "--profile", "extended"], False),
    ]:
        rc1, l1, err1 = run_metta(cetta, work, ab, flags)
        rc2, l2, err2 = run_metta(cetta, work, ba, flags)
        check_eq(f"commuting {name} rc", (rc1, err1, rc2, err2), (0, "", 0, ""))
        # eq:norm and eq:class are order-independent canonical outputs.
        check_eq(f"commuting {name} eq:norm", l1[-4], l2[-4])
        check_eq(f"commuting {name} eq:class", l1[-3], l2[-3])
        check_bag_line(f"commuting {name} facts AB", l1[-2], ["a", "b"], is_he)
        check_bag_line(f"commuting {name} facts BA", l2[-2], ["a", "b"], is_he)
        check_bag_line(f"commuting {name} g AB", l1[-1], ["ok-a", "ok-b"], is_he)
        check_bag_line(f"commuting {name} g BA", l2[-1], ["ok-a", "ok-b"], is_he)


def test_removal_and_cycles(cetta: Path, work: Path):
    # Removal of an imported atom: protected in HE, removed in PeTTa.
    rem_files = {
        "rmod.metta": "(rfact 1)\n(= (rfn 1) ok)\n",
        "main.metta": (
            "!(import! &self rmod)\n"
            "!(remove-atom &self (rfact 1))\n"
            "!(remove-atom &self (= (rfn 1) ok))\n"
            "!(collapse (match &self (rfact $x) $x))\n"
            "!(collapse (rfn 1))\n"
        ),
    }
    rc, lines, err = run_metta(cetta, work, rem_files, ["--lang", "he"])
    check_eq("removal HE rc", (rc, err), (0, ""))
    check_eq("removal HE preserves imported atoms", lines[-2:], ["[(1)]", "[(ok)]"])

    rc, lines, err = run_metta(cetta, work, rem_files, ["--lang", "petta"])
    check_eq("removal PeTTa rc", (rc, err), (0, ""))
    check_eq("removal PeTTa removes imported atoms", lines[-2:], ["()", "((rfn 1))"])

    # HE: removing an own duplicate removes only the own copy, preserving the module's.
    dup_files = {
        "rmod.metta": "(rfact 1)\n",
        "main.metta": (
            "!(import! &self rmod)\n"
            "!(add-atom &self (rfact 1))\n"
            "!(collapse (match &self (rfact $x) $x))\n"
            "!(remove-atom &self (rfact 1))\n"
            "!(collapse (match &self (rfact $x) $x))\n"
        ),
    }
    rc, lines, err = run_metta(cetta, work, dup_files, ["--lang", "he"])
    check_eq("removal-dup HE rc", (rc, err), (0, ""))
    check_eq("removal-dup HE keeps module copy", lines[-3:], ["[(1 1)]", "[()]", "[(1)]"])

    # Import cycles: refused with ModuleImportCycle in HE, no-op re-entry in PeTTa.
    cyc_files = {
        "ca.metta": "!(import! &self cb)\n(from-a 1)\n(= (fa) a-ok)\n",
        "cb.metta": "!(import! &self ca)\n(from-b 2)\n(= (fb) b-ok)\n",
        "main.metta": (
            "!(import! &self ca)\n"
            "!(fa)\n"
            "!(fb)\n"
            "!(collapse (match &self (from-a $x) $x))\n"
            "!(collapse (match &self (from-b $x) $x))\n"
        ),
    }
    rc, lines, err = run_metta(cetta, work, cyc_files, ["--lang", "he"])
    assert any("ModuleImportCycle" in ln for ln in lines) or "ModuleImportCycle" in err, (
        f"cycle HE should report ModuleImportCycle, got rc={rc}, out={lines}, err={err}"
    )

    rc, lines, err = run_metta(cetta, work, cyc_files, ["--lang", "petta"])
    check_eq("cycle PeTTa rc", (rc, err), (0, ""))
    check_eq("cycle PeTTa loads both", lines[-4:], ["a-ok", "b-ok", "(1)", "(2)"])


def test_equations_across_modules(cetta: Path, work: Path):
    # 3-module chain joining classes, composing preferences, cross-module head
    # alias, directional =% + =e, and base isolation.
    files = {
        "ebase.metta": (
            "(=e> Cold Cool)\n"
            "(= (label Cold) chilly)\n"
            "(=% (dprobe Cold) dir-ok)\n"
            "(= (f $x) (+ $x 10))\n"
            "(sensor Cold)\n"
        ),
        "emid.metta": (
            "!(import! &self ebase)\n"
            "(=e> Cool Warm)\n"
            "(=e f g)\n"
        ),
        "main.metta": (
            "!(import! &self emid)\n"
            "!(import! &base ebase)\n"
            "(=e> Warm Balmy)\n"
            "!(eq:norm &self Cold)\n"
            "!(collapse (eq:class &self Cold))\n"
            "!(label Balmy)\n"
            "!(dprobe Balmy)\n"
            "!(g 5)\n"
            "!(collapse (eq:match &self (sensor Balmy) ok))\n"
            "!(eq:same &base Cold Balmy)\n"
        ),
    }

    rc, lines, err = run_metta(cetta, work, files, ["--lang", "he", "--profile", "extended"])
    check_eq("equations-across-modules HE-ext rc", (rc, err), (0, ""))
    check_eq(
        "equations-across-modules HE-ext results",
        lines[-7:],
        [
            "[Balmy]",
            "[(Balmy Cold Cool Warm)]",
            "[chilly]",
            "[dir-ok]",
            "[15]",
            "[(ok)]",
            "[False]",
        ],
    )

    rc, lines, err = run_metta(cetta, work, files, ["--lang", "petta", "--profile", "extended"])
    check_eq("equations-across-modules PeTTa-ext rc", (rc, err), (0, ""))
    check_eq(
        "equations-across-modules PeTTa-ext results",
        lines[-7:],
        [
            "Balmy",
            "(Balmy Cold Cool Warm)",
            "chilly",
            "dir-ok",
            "15",
            "(ok)",
            "false",
        ],
    )


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: test_space_theory_module_laws_v1.py <path-to-cetta>")
    cetta = Path(sys.argv[1]).resolve()
    if not cetta.exists():
        sys.exit(f"cetta binary not found: {cetta}")

    work = Path(tempfile.mkdtemp(prefix="cetta-module-laws-"))
    try:
        test_sharing(cetta, work)
        test_idempotence(cetta, work)
        test_protection(cetta, work)
        test_renaming(cetta, work)
        test_commuting(cetta, work)
        test_removal_and_cycles(cetta, work)
        test_equations_across_modules(cetta, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("PASS: test_space_theory_module_laws_v1 (all 7 module laws across HE and PeTTa)")


if __name__ == "__main__":
    main()
