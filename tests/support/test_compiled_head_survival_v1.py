#!/usr/bin/env python3
"""Permanent test: compiled definitions survive unrelated edits and invalidate on
dependent edits across HE and PeTTa, including module-shared reuse and the
live-reading rule.
"""

import os
import subprocess
import sys
import tempfile
from pathlib import Path


def run_cetta(cetta: str, lang: str, code: str, extra_env=None, profile=None) -> str:
    env = os.environ.copy()
    if extra_env:
        env.update(extra_env)
    with tempfile.NamedTemporaryFile(
        "w", suffix=".metta", delete=False, encoding="utf-8"
    ) as tf:
        tf.write(code)
        path = tf.name
    try:
        cmd = [cetta, "--lang", lang]
        if profile:
            cmd += ["--profile", profile]
        cmd += ["--quiet", path]
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=30,
            env=env,
        )
        if proc.returncode != 0:
            raise AssertionError(
                f"cetta --lang {lang} failed (rc={proc.returncode}):\n"
                f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
            )
        lines = []
        for raw in proc.stdout.splitlines():
            s = raw.strip()
            if not s:
                continue
            if lang == "he" and s.startswith("[") and s.endswith("]"):
                s = s[1:-1].strip()
            lines.append(s)
        return "\n".join(lines)
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def test_unrelated_edits_and_dependent_invalidation(cetta: str) -> None:
    """Unrelated function definitions and data mutations preserve results;
    edits on dependency heads, wildcard rules, and =e equations invalidate."""
    for lang in ("he", "petta"):
        code = """
(= (helper $x) (+ $x 10))
(= (work $k) (if (<= $k 0) 0 (+ (helper $k) (work (- $k 1)))))

!(work 5)
; Unrelated function definitions and data writes/removals
!(let $_ (add-atom &self (= (memo 1) 100))
   (let $_ (add-atom &self (tag 1))
     (let $_ (remove-atom &self (tag 1))
       (work 5))))

; Edit a dependency head (helper) -> must see new definition
!(let $_ (remove-atom &self (= (helper $x) (+ $x 10)))
   (let $_ (add-atom &self (= (helper $x) (+ $x 100)))
     (work 5)))
"""
        out = [l.strip() for l in run_cetta(cetta, lang, code).splitlines() if l.strip()]
        assert out == ["65", "65", "515"], f"{lang} dep-invalidation mismatch: {out}"

        # Wildcard definition invalidation in HE; new clause invalidation in PeTTa
        if lang == "he":
            wild_code = """
(= (pure-inc $x) (+ $x 1))
!(pure-inc 10)
!(let $_ (add-atom &self (= (other-fn $y) $y)) (pure-inc 10))
!(let $_ (add-atom &self (= ($any 10) 999))
   (collapse (pure-inc 10)))
"""
        else:
            wild_code = """
(= (pure-inc $x) (+ $x 1))
!(pure-inc 10)
!(let $_ (add-atom &self (= (other-fn $y) $y)) (pure-inc 10))
!(let $_ (add-atom &self (= (pure-inc 10) 999))
   (collapse (pure-inc 10)))
"""
        out = [l.strip() for l in run_cetta(cetta, lang, wild_code).splitlines() if l.strip()]
        assert out[0] == "11" and out[1] == "11", f"{lang} pre-invalidation mismatch: {out}"
        assert "999" in out[2] and "11" in out[2], f"{lang} invalidation mismatch: {out}"

        # Equational alias (=e) invalidation (under extended profiles where =e enters dispatch)
        ext_profile = "extended" if lang == "he" else "petta-extended"
        eq_code = """
(= (f-base $x) (+ $x 2))
!(f-base 5)
!(let $_ (add-atom &self (= (unrelated-g $y) $y)) (f-base 5))
!(let $_ (add-atom &self (= (f-alt $x) (+ $x 20)))
   (let $_ (add-atom &self (=e f-base f-alt))
     (collapse (f-base 5))))
"""
        out = [l.strip() for l in run_cetta(cetta, lang, eq_code, profile=ext_profile).splitlines() if l.strip()]
        assert out[0] == "7" and out[1] == "7", f"{lang} pre-=e mismatch: {out}"
        assert "25" in out[2] and "7" in out[2], f"{lang} =e invalidation mismatch: {out}"


def test_live_reading_ruling(cetta: str) -> None:
    """An already-entered call finishes under its entered snapshot while
    subsequent calls observe newly added definitions."""
    for lang in ("he", "petta"):
        code = """
(= (target $x) (+ $x 1))
(= (caller-then-mutate $x)
   (let $before (target $x)
     (let $_ (add-atom &self (= (target $y) (+ $y 100)))
       (pair $before (collapse (target $x))))))
!(caller-then-mutate 5)
"""
        out = [l.strip() for l in run_cetta(cetta, lang, code).splitlines() if l.strip()]
        assert len(out) == 1, f"{lang} live-reading unexpected lines: {out}"
        assert out[0].startswith("(pair 6 ("), f"{lang} before-call wrong: {out}"
        assert "105" in out[0] and "6" in out[0], f"{lang} after-call missed new def: {out}"


def test_module_shared_reuse_across_importers(cetta: str) -> None:
    """A module's pure definitions are compiled once and reused by every
    importer that adds nothing on their heads, while an importer that extends
    a module head sees its own extension without affecting other importers."""
    with tempfile.TemporaryDirectory() as td:
        mod_path = Path(td) / "pure_math_mod.metta"
        mod_path.write_text(
            "(= (m-step $k) (if (<= $k 0) 0 (+ $k (m-step (- $k 1)))))\n"
            "(= (m-entry $n) (m-step $n))\n",
            encoding="utf-8",
        )
        imp1_path = Path(td) / "imp1.metta"
        imp1_path.write_text(
            f'!(import! &self "{mod_path}")\n'
            "(= (imp1-only $x) (+ $x 1))\n"
            "(= (imp1-run $n) (m-entry $n))\n",
            encoding="utf-8",
        )
        imp2_path = Path(td) / "imp2.metta"
        imp2_path.write_text(
            f'!(import! &self "{mod_path}")\n'
            "(= (imp2-only $x) (+ $x 2))\n"
            "(= (imp2-run $n) (m-entry $n))\n",
            encoding="utf-8",
        )
        # HE: two independent importing modules share the compiled module program;
        # extending m-entry in &self takes effect without corrupting imp1/imp2.
        he_code = f"""
!(import! &self "{mod_path}")
!(import! &imp1 "{imp1_path}")
!(import! &imp2 "{imp2_path}")
!(add-atom &self (= (local-a $x) $x))
!(m-entry 15)
!(match &imp1 (= (imp1-only $x) $y) $y)
!(let $_ (add-atom &self (= (local-b $x) $x)) (m-entry 15))
!(let $_ (add-atom &self (= (m-entry 0) 777))
   (collapse (m-entry 0)))
"""
        out = [l.strip() for l in run_cetta(cetta, "he", he_code).splitlines() if l.strip()]
        assert out[0] == "120" and out[2] == "120", f"he module reuse mismatch: {out}"
        assert "777" in out[3] and "0" in out[3], f"he importer override mismatch: {out}"

        # PeTTa: importing a module, adding unrelated definitions, then extending m-entry.
        petta_code = f"""
!(import! &self "{mod_path}")
!(add-atom &self (= (local-a $x) $x))
!(m-entry 15)
!(add-atom &self (= (local-b $x) $x))
!(m-entry 15)
!(let $_ (add-atom &self (= (m-entry 0) 777))
   (collapse (m-entry 0)))
"""
        out = [l.strip() for l in run_cetta(cetta, "petta", petta_code).splitlines() if l.strip()]
        vals = [x for x in out if x not in ("true", "()")]
        assert vals[0] == "120" and vals[1] == "120", f"petta module reuse mismatch: {out}"
        assert "777" in vals[2] and "0" in vals[2], f"petta importer override mismatch: {out}"


def test_tcalc_modules_defs_survive_unrelated_edits(cetta: str) -> None:
    """Definitions loaded from lib/tcalc_modules.metta keep working identically
    across unrelated function definitions added to &self."""
    root = Path(__file__).resolve().parents[2]
    mod_lib = root / "lib" / "tcalc_modules.metta"
    reach_mod = root / "tests" / "tcalc" / "modules" / "reach.module"
    code = f"""
!(import! &self "{mod_lib}")
!(let $t1 (tcalc:module-text "{reach_mod}" "Calc")
   (let $_ (add-atom &self (= (unrelated-memo 1) 42))
     (let $_ (add-atom &self (= (unrelated-memo 2) 43))
       (let $t2 (tcalc:module-text "{reach_mod}" "Calc")
         (== $t1 $t2)))))
"""
    for lang in ("he", "petta"):
        out = [l.strip() for l in run_cetta(cetta, lang, code).splitlines() if l.strip()]
        assert out[-1] in ("True", "true"), f"{lang} tcalc_modules survival mismatch: {out}"


def main() -> int:
    cetta = sys.argv[1] if len(sys.argv) > 1 else "./cetta"
    test_unrelated_edits_and_dependent_invalidation(cetta)
    test_live_reading_ruling(cetta)
    test_module_shared_reuse_across_importers(cetta)
    test_tcalc_modules_defs_survive_unrelated_edits(cetta)
    print("PASS: test_compiled_head_survival_v1 (per-head survival, invalidation, live-reading, module reuse, tcalc_modules)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
