#!/usr/bin/env python3
"""Capability-based head sealing tests across HE and PeTTa.

Verifies that:
  1. Sealing a head symbol via `seal:seal-head` returns an opaque write facet
     from `lib/seal` and rejects ordinary writes (`add-atom`, `remove-atom`,
     top-level definitions, and `=e` equations naming the sealed head) with
     `(SealFault ...)`.
  2. An importer attempting to extend a sealed head inherited from a module
     gets `(SealFault ...)`, while unrelated unsealed heads remain writable.
  3. A holder of the write facet extends and removes definitions on the sealed
     head via `seal:head-add` / `seal:head-remove`.
  4. Attenuating the write facet (`seal:attenuate $w none`) drops write
     authority (`(SealFault missing-right)`), and a facet of another brand
     faults (`(SealFault wrong-facet)`).
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run_case(cetta: Path, work_dir: Path, files: dict, flags: list,
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


def strip_brackets(line: str, is_he: bool) -> str:
    if is_he and line.startswith("[") and line.endswith("]"):
        return line[1:-1]
    return line


def test_importer_refused_and_holder_allowed(cetta: Path, work: Path,
                                             label: str, flags: list, is_he: bool):
    sealed_mod = (
        "!(import! &self seal)\n"
        "(= (secret 1) initial)\n"
        "!(let $w (seal:seal-head &self secret secret-brand)\n"
        "   (bind! &secret-writer (new-state $w)))\n"
        "(= (secret-writer) (get-state &secret-writer))\n"
    )

    # Program 1: Importer attempts unauthorized extensions via add-atom,
    # equation aliasing, attenuated facet, and wrong-brand facet.
    # Unrelated unsealed head `open-fn` continues accepting ordinary writes.
    if is_he:
        unauth_main = (
            "!(import! &self sealed_api)\n"
            "!(import! &self seal)\n"
            "!(seal:head-sealed? &self secret)\n"
            "!(seal:head-sealed? &self open-fn)\n"
            "!(case (add-atom &self (= (secret 2) hijacked))\n"
            "   (((Error $c (SealFault $why)) (add-fault $why)) ($r (add-ok $r))))\n"
            "!(case (add-atom &self (=e secret evil-alias))\n"
            "   (((Error $c (SealFault $why)) (eq-fault $why)) ($r (eq-ok $r))))\n"
            "!(let $ro (seal:attenuate (secret-writer) none)\n"
            "   (case (seal:try-head-add $ro &self (= (secret 2) atten))\n"
            "     (((Error $c (SealFault $why)) (atten-fault $why)) ($r (atten-ok $r)))))\n"
            "!(let (seal:facets $other-s $other-u) (seal:new other-brand)\n"
            "   (case (seal:try-head-add $other-s &self (= (secret 2) wrong))\n"
            "     (((Error $c (SealFault $why)) (wrong-fault $why)) ($r (wrong-ok $r)))))\n"
            "!(let $_ (add-atom &self (= (open-fn 1) open-ok))\n"
            "   (open-fn 1))\n"
            "!(secret 1)\n"
            "!(secret 2)\n"
        )
    else:
        unauth_main = (
            "!(import! &self sealed_api)\n"
            "!(import! &self seal)\n"
            "!(seal:head-sealed? &self secret)\n"
            "!(seal:head-sealed? &self open-fn)\n"
            "!(case (catch (add-atom &self (= (secret 2) hijacked)))\n"
            "   (((Error $c (SealFault $why)) (add-fault $why)) ($r (add-ok $r))))\n"
            "!(case (catch (add-atom &self (=e secret evil-alias)))\n"
            "   (((Error $c (SealFault $why)) (eq-fault $why)) ($r (eq-ok $r))))\n"
            "!(let $ro (seal:attenuate (secret-writer) none)\n"
            "   (case (seal:try-head-add $ro &self (= (secret 2) atten))\n"
            "     (((Error $c (SealFault $why)) (atten-fault $why)) ($r (atten-ok $r)))))\n"
            "!(let (seal:facets $other-s $other-u) (seal:new other-brand)\n"
            "   (case (seal:try-head-add $other-s &self (= (secret 2) wrong))\n"
            "     (((Error $c (SealFault $why)) (wrong-fault $why)) ($r (wrong-ok $r)))))\n"
            "!(let $_ (add-atom &self (= (open-fn 1) open-ok))\n"
            "   (open-fn 1))\n"
            "!(secret 1)\n"
            "!(collapse (secret 2))\n"
        )

    rc, lines, err = run_case(
        cetta, work, {"sealed_api.metta": sealed_mod, "main.metta": unauth_main}, flags
    )
    check_eq(f"{label} unauth rc", (rc, err), (0, ""))
    tail = [strip_brackets(ln, is_he) for ln in lines[-9:]]
    check_eq(
        f"{label} unauth results",
        tail,
        [
            "True" if is_he else "true",
            "False" if is_he else "false",
            "(add-fault head-sealed)",
            "(eq-fault head-sealed)",
            "(atten-fault missing-right)",
            "(wrong-fault wrong-facet)",
            "open-ok",
            "initial",
            "(secret 2)" if is_he else "()",
        ],
    )

    # Program 2: Top-level static definition in importer attempting to extend
    # a sealed head is refused with SealFault.
    top_level_hijack = (
        "!(import! &self sealed_api)\n"
        "(= (secret 2) hijacked)\n"
        "!(secret 2)\n"
    )
    rc2, lines2, err2 = run_case(
        cetta, work, {"sealed_api.metta": sealed_mod, "main.metta": top_level_hijack}, flags
    )
    combined = "\n".join(lines2) + "\n" + err2
    assert rc2 != 0 or "SealFault" in combined, (
        f"{label} top-level hijack should refuse with SealFault, got rc={rc2}, out={lines2}, err={err2}"
    )
    assert "hijacked" not in lines2 and "[hijacked]" not in lines2, (
        f"{label} top-level hijack evaluated: {lines2}"
    )

    # Program 3: Holder of the write facet extends the sealed head and removes
    # its added definition through the facet.
    holder_main = (
        "!(import! &self sealed_api)\n"
        "!(import! &self seal)\n"
        "!(let $w (secret-writer)\n"
        "   (let $_ (seal:head-add $w &self (= (secret 2) authorized))\n"
        "     ((secret 1) (secret 2))))\n"
        "!(let $w (secret-writer)\n"
        "   (let $_ (seal:head-remove $w &self (= (secret 2) authorized))\n"
        "     (collapse (secret 2))))\n"
    )
    rc3, lines3, err3 = run_case(
        cetta, work, {"sealed_api.metta": sealed_mod, "main.metta": holder_main}, flags
    )
    check_eq(f"{label} holder rc", (rc3, err3), (0, ""))
    tail3 = [strip_brackets(ln, is_he) for ln in lines3[-2:]]
    check_eq(
        f"{label} holder results",
        tail3,
        [
            "(initial authorized)",
            "((secret 2))" if is_he else "()",
        ],
    )


def main() -> int:
    if len(sys.argv) < 2:
        sys.exit("usage: test_space_head_sealing_v1.py <path-to-cetta>")
    cetta = Path(sys.argv[1]).resolve()
    work = Path(tempfile.mkdtemp(prefix="cetta-head-sealing-"))
    try:
        test_importer_refused_and_holder_allowed(
            cetta, work, "HE", ["--lang", "he"], is_he=True
        )
        test_importer_refused_and_holder_allowed(
            cetta, work, "PeTTa-default", ["--lang", "petta"], is_he=False
        )
        test_importer_refused_and_holder_allowed(
            cetta, work, "PeTTa-linked", ["--lang", "petta", "--petta-linked-import"], is_he=False
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("PASS: test_space_head_sealing_v1 (HE + PeTTa default & linked: importer SealFault, holder write/remove, attenuation, brand isolation)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
