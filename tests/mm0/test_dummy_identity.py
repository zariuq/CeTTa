#!/usr/bin/env python3
"""Specification dummy identity: distinct UDummy commands must match
distinct specification dummies, unused dummies are vacuous.

The cases are those of the mm0-c dummy-injectivity repair supplied with the
CeTTa MM0 handoff (mm0-c-fix/.../tests/mmb/dummy-identity.py).  The MMB
fixtures in fixtures/dummy-identity/cases were compiled from the .mm1 files
beside them with `mm0-rs compile -s` at the pinned upstream revision; they
are byte-identical to the base64 fixtures of that script.

    python3 tests/mm0/test_dummy_identity.py runtime/native-mm0/mm0_check
"""
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
CASES_DIR = HERE / "fixtures" / "dummy-identity" / "cases"

COMMON = """strict provable sort wff;
sort set;
term wn (a: wff): wff;
prefix wn: $~$ prec 40;
term wal {x: set} (a: wff x): wff;
prefix wal: $A.$ prec 41;
term weq (a b: set): wff;
infixl weq: $=$ prec 50;
axiom two {x y: set}: $ ~ A. x A. y x = y $;
axiom refl {x: set}: $ A. x A. x x = x $;
"""
ONE = "def f {.d: set}: wff = $ A. d A. d d = d $;\n"
TWO = "def f {.d1 .d2: set}: wff = $ A. d1 A. d2 d1 = d2 $;\n"


def source(name):
    """The .mm0 interface of a case's .mm1 source (its declarations)."""
    text = (CASES_DIR / (name + ".mm1")).read_text()
    lines = [line for line in text.splitlines(keepends=True)
             if not line.startswith("pub theorem")]
    return "".join(lines)


def wrong_sort_proof(work):
    """Change the multiple-sorts definition's left-sort UDummy to the right
    sort; the sort check must still reject it."""
    data = bytearray((CASES_DIR / "multiple-sorts.mmb").read_bytes())
    terms = int.from_bytes(data[16:20], "little")
    nargs = int.from_bytes(data[terms + 8:terms + 10], "little")
    args = int.from_bytes(data[terms + 12:terms + 16], "little")
    offset = args + 8 * (nargs + 1)
    dummies = []
    while data[offset] & 63:
        opcode, size = data[offset] & 63, (0, 1, 2, 4)[data[offset] >> 6]
        if opcode == 0x33:
            dummies.append(offset)
        offset += 1 + size
    assert len(dummies) == 2 and data[dummies[0] + 1] == 1
    data[dummies[0] + 1] = 2
    path = work / "wrong-sort.mmb"
    path.write_bytes(bytes(data))
    return path


def main():
    checker = str(Path(sys.argv[1]).resolve())
    work = Path(tempfile.mkdtemp(prefix="mm0-dummy-"))
    proofs = {name: CASES_DIR / (name + ".mmb")
              for name in ("proof", "proof2", "multiple-sorts", "saved-terms", "formal-argument")}
    proofs["wrong-sort"] = wrong_sort_proof(work)
    foo, bar = "theorem foo: $ ~ f $;\n", "theorem bar: $ f $;\n"
    single, own = COMMON + ONE + bar, COMMON + TWO + foo
    many_unused = " ".join(".u" + str(i) for i in range(60))
    multiple, saved, formal = source("multiple-sorts"), source("saved-terms"), source("formal-argument")
    cases = [
        ("two-distinct", "proof", own, None),
        ("reject-collapsed-dummies", "proof", COMMON + ONE + foo, "dummy matched more than once"),
        ("alpha-renaming", "proof", own.replace(".d1 .d2", ".x .y")
         .replace("A. d1 A. d2 d1 = d2", "A. x A. y x = y"), None),
        ("permuted-declarations", "proof", own.replace(".d1 .d2", ".d2 .d1"), None),
        ("one-repeated", "proof2", single, None),
        ("reject-split-repeated-dummy", "proof2", COMMON + TWO + bar, "expression mismatch"),
        ("unused-spec-dummies", "proof2", single.replace("{.d: set}", "{.before .d .after: set}"), None),
        ("many-unused-spec-dummies", "proof2", single.replace("{.d: set}", "{" + many_unused + " .d: set}"), None),
        ("multiple-sorts", "multiple-sorts", multiple, None),
        ("permuted-multiple-sorts", "multiple-sorts",
         multiple.replace("{.x: left} {.y: right}", "{.y: right} {.x: left}"), None),
        ("reject-wrong-sort", "wrong-sort", multiple, "unify failure at dummy"),
        ("formal-and-dummy", "formal-argument", formal, None),
        ("reject-formal-as-dummy", "formal-argument", formal.replace("A. y x = y", "A. x x = x"),
         "expected a dummy"),
        ("saved-composite-terms", "saved-terms", saved, None),
        ("saved-composite-renamed", "saved-terms", saved.replace(".x .y", ".a .b")
         .replace("A. x x = x", "A. a a = a").replace("A. y y = y", "A. b b = b"), None),
        ("reject-collapse-after-saved-term", "saved-terms", saved.replace("A. y y = y", "A. x x = x"),
         "dummy matched more than once"),
    ]
    failed = 0
    for name, proof, spec, diagnostic in cases:
        result = subprocess.run([checker, str(proofs[proof])], input=spec, text=True,
                                capture_output=True, timeout=60)
        output = result.stdout + result.stderr
        if diagnostic is None:
            ok = result.returncode == 0
        else:
            ok = result.returncode == 2 and diagnostic in output   # REFUSED
        ok = ok and "AddressSanitizer" not in output and "runtime error:" not in output
        print(("PASS " if ok else "FAIL ") + name)
        if not ok:
            failed += 1
            print("   " + output.strip())
    print(f"Mm0DummyIdentitySummary {len(cases)} {failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
