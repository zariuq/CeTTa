#!/usr/bin/env python3
"""MM0 through CeTTa's public languages: import, checked MMB files, stored
proof DAGs, data declarations, owned handles and their authority."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from mmb_asm import (Mmb, bound, reg, ret, PROVABLE, STRICT,  # noqa: E402
                     UTERM, UREF, REF, TERM, TERM_SAVE, THM, END)

FIXTURES = ROOT / "tests" / "mm0" / "fixtures"
HOL = (str(FIXTURES / "hol.mmb"), str(FIXTURES / "hol.mm0"))


def definition_dependency_fixture(directory):
    """mm0-c accepts this file: ax5 applied with a := isz x, whose value
    depends on x (see tests/mm0/test_mmb.py)."""
    m = Mmb()
    wff = m.sort(STRICT | PROVABLE)
    nat = m.sort(0)
    wi = m.term([reg(wff), reg(wff)], ret(wff))
    al = m.term([bound(nat, 0), reg(wff, 1)], ret(wff))
    z = m.term([], ret(nat))
    eq = m.term([reg(nat), reg(nat)], ret(wff))
    isz = m.define([bound(nat, 0)], ret(wff, 1), [(UTERM, eq), (UREF, 0), (UTERM, z), END],
                   [(REF, 0), (TERM, z), (TERM, eq), END])
    ax5 = m.axiom([bound(nat, 0), reg(wff)],
                  [(UTERM, wi), (UREF, 1), (UTERM, al), (UREF, 0), (UREF, 1), END],
                  [(REF, 1), (REF, 0), (REF, 1), (TERM, al), (TERM, wi), END])
    m.theorem([bound(nat, 0)],
              [(UTERM, wi), (UTERM, isz), (UREF, 0), (UTERM, al), (UREF, 0), (UTERM, isz), (UREF, 0), END],
              [(REF, 0), (REF, 0), (TERM_SAVE, isz), (REF, 1), (REF, 0), (REF, 1), (TERM, al),
               (TERM, wi), (THM, ax5), END])
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "bad.mmb").write_bytes(m.build())
    (directory / "bad.mm0").write_text("""strict provable sort wff;
sort nat;
term wi (a b: wff): wff; infixr wi: $->$ prec 25;
term al {x: nat} (a: wff x): wff; prefix al: $A.$ prec 41;
term z: nat;
term eq (a b: nat): wff; infixl eq: $=$ prec 50;
def isz {x: nat}: wff x = $ x = z $;
axiom ax5 {x: nat} (a: wff): $ a -> A. x a $;
theorem bad {x: nat}: $ isz x -> A. x isz x $;
""")
    return str(directory / "bad.mmb"), str(directory / "bad.mm0")


class Checks:
    def __init__(self, binary):
        self.binary = str(binary)
        self.count = 0

    def run(self, args):
        return subprocess.run([self.binary, *args], cwd=ROOT, capture_output=True, text=True, timeout=120)

    def check(self, condition, message, actual=None):
        self.count += 1
        if not condition:
            raise AssertionError(f"{message}: {actual!r}")

    def query(self, language, expression, prefix=""):
        source = "!(import! &self mm0kernel)\n" + prefix + "\n!(" + expression + ")\n"
        result = self.run(["--lang", language, "--quiet", "-e", source])
        self.check(result.returncode == 0, f"{language} exits successfully", result)
        lines = result.stdout.splitlines()
        expected_lines = 2 if language == "petta" else 1
        self.check(len(lines) == expected_lines, f"{language} one query result for {expression}", result.stdout)
        value = lines[-1]
        if language != "petta" and value.startswith("[") and value.endswith("]"):
            value = value[1:-1]
        return {"True": "true", "False": "false"}.get(value, value)

    def equals(self, language, expression, expected, prefix=""):
        value = self.query(language, expression, prefix)
        self.check(value == expected, f"{language} {expression[:160]}", value)

    def refuses(self, language, expression, status, fragment=None, prefix=""):
        value = self.query(language, expression, prefix)
        self.check(value.startswith(("(mm0:refused ", "(mm0:error ")) and f'"{status}"' in value and
                   (fragment is None or fragment in value), f"{language} refuses {expression[:160]}", value)


def q(text, language, captures=()):
    """Literal data: Prime passes arguments as computations, so its literals
    are quoted; HE and PeTTa take them as they are."""
    if language != "prime":
        return text
    capture = "{" + " ".join(captures) + "}" if captures else ""
    return f"(quote {text}){capture}"


COUNTS = "(let (Mm0Stats $s $t $n $bytes) (mm0:stats $th) ($s $t $n))"


def hol_let(body, extra=""):
    return (f'let* (($th (mm0:theory)) ($_ (mm0:load-mmb $th "{HOL[0]}" "{HOL[1]}")){extra}) {body}')


SWAP_STMT = "(theorem swap ((var P wff) (var Q wff)) () (im (an P Q) (an Q P)))"
SWAP_ROWS = ("((Mm0Row 1 (thm anr (P Q)) ()) (Mm0Row 2 (thm anl (P Q)) ()) "
             "(Mm0Row 3 (thm ian ((an P Q) Q P)) (1 2) (im (an P Q) (an Q P))))")
SWAP_OUT = "(theorem swap ((var P wff ()) (var Q wff ())) () (im (an P Q) (an Q P)))"


def prove(language, stmt, rows, root, dummies="()", captures=()):
    return f"mm0:prove $th {q(stmt, language)} {q(dummies, language)} {q(rows, language, captures)} {root}"


def mmb_checks(checks, language, dummy, bad):
    checks.equals(language, hol_let(COUNTS), "(3 34 127)")
    checks.equals(language, f'mm0:check-mmb "{HOL[0]}" "{HOL[1]}"', "(mm0:verified 3 34 127)")
    checks.equals(language, hol_let("(mm0:statement (mm0:theorem $th syl))"),
                  "(axiom syl ((var P wff ()) (var Q wff ()) (var R wff ())) ((im P Q) (im Q R)) (im P R))")
    checks.equals(language, hol_let("(mm0:statement (mm0:theorem $th lamT))"),
                  "(axiom lamT ((var G wff ()) (var A type ()) (var B type ()) (bound x term) "
                  "(var t term (x))) ((im (an G (ty x A)) (ty t B))) (im G (ty (lam x A t) (fun A B))))")
    checks.refuses(language, hol_let("(mm0:theorem $th no_such_theorem)"), "MALFORMED")
    # The unsound dummy match the mm0-c repair addresses, and the definition
    # dependency that patched mm0-c still accepts.
    checks.refuses(language, f'mm0:check-mmb "{dummy[0]}" "{dummy[1]}"', "REFUSED",
                   "dummy matched more than once")
    checks.equals(language, f'mm0:check-mmb "{dummy[0]}" "{dummy[2]}"', "(mm0:verified 2 4 3)")
    checks.refuses(language, f'mm0:check-mmb "{bad[0]}" "{bad[1]}"', "REFUSED", "disjoint variable violation")
    checks.refuses(language, 'mm0:check-mmb "tests/mm0/fixtures/missing.mmb" "x.mm0"', "MALFORMED")
    # An MMB file loads into an empty theory, all or nothing.
    checks.refuses(language, hol_let(f'(mm0:load-mmb $th "{HOL[0]}" "{HOL[1]}")'), "REFUSED")
    checks.equals(language, f'let* (($th (mm0:theory)) ($r (mm0:load-mmb $th "{dummy[0]}" "{dummy[1]}"))) '
                  + COUNTS, "(0 0 0)")


def dag_checks(checks, language):
    p = lambda rows, root=3, stmt=SWAP_STMT: prove(language, stmt, rows, root)  # noqa: E731
    checks.equals(language, hol_let(f"(mm0:statement ({p(SWAP_ROWS)}))"), SWAP_OUT)
    # Stored rows are ordinary data in a space; the checked theorem is stored
    # and queried again.
    checks.equals(language, hol_let(
        "(match $kb (Checked swap $proof) (mm0:statement $proof))",
        f" ($kb (new-space)) ($_2 (add-atom $kb (Mm0Proof swap {SWAP_STMT} () {SWAP_ROWS} 3)))"
        " ($h (match $kb (Mm0Proof swap $s $d $rows $root) (mm0:prove $th $s $d $rows $root)))"
        " ($_3 (add-atom $kb (Checked swap $h)))"), SWAP_OUT)
    # Premises are ordered, rows are addresses, claims are checked.
    bad = [
        ("((Mm0Row 1 (thm anr (P Q)) ()) (Mm0Row 2 (thm anl (P Q)) ()) "
         "(Mm0Row 3 (thm ian ((an P Q) Q P)) (2 1)))", "REFUSED", "parent row does not prove"),
        ("((Mm0Row 1 (thm anr (P Q)) ()) (Mm0Row 3 (thm ian ((an P Q) Q P)) (1 2)))",
         "REFUSED", "not checked earlier"),
        ("((Mm0Row 1 (thm anr (P Q)) ()) (Mm0Row 1 (thm anl (P Q)) ()) "
         "(Mm0Row 3 (thm ian ((an P Q) Q P)) (1 1)))", "REFUSED", "duplicate proof row ID"),
        ("((Mm0Row 1 (thm anr (P Q)) () (an P Q)))", "REFUSED", "claim"),
        ("((Mm0Row 1 (thm anr (P Q)) ()) (Mm0Row 2 (thm anl (P Q)) ()) "
         "(Mm0Row 3 (thm ian ((an P Q) Q P)) (1)))", "REFUSED", "wrong number of hypotheses"),
        ("((Mm0Row 1 (thm anr (P)) ()))", "REFUSED", "wrong number of arguments"),
        ("((Mm0Row 1 (thm nope (P Q)) ()))", "MALFORMED", "unknown MM0 theorem"),
        ("((Mm0Row 1 (axiom anr (P Q)) ()))", "MALFORMED", "proof rule"),
        ("((Mm0Row 1 (thm anr (P R)) ()))", "MALFORMED", "unknown MM0 variable"),
        ("((Mm0Row 1 (thm anr (P $x)) ()))", "MALFORMED", "MeTTa variable"),
        ("((Mm0Row 1 (thm anr (P (nope))) ()))", "MALFORMED", "unknown MM0 term"),
        ("((Mm0Row 1 (thm anr (P (im Q))) ()))", "REFUSED", "wrong number of arguments"),
        ("((Mm0Row -1 (thm anr (P Q)) ()))", "MALFORMED", "proof row"),
        ("((Mm0Row 1 (hyp 0) ()))", "REFUSED", "no such hypothesis"),
    ]
    for rows, status, fragment in bad:
        root = 3 if "Mm0Row 3" in rows else 1
        checks.refuses(language, hol_let(f"({p(rows, root)})"), status, fragment)
    checks.refuses(language, hol_let(f"({p(SWAP_ROWS, 2)})"), "REFUSED", "does not conclude")
    checks.refuses(language, hol_let(f"({p(SWAP_ROWS, 9)})"), "REFUSED", "root not checked")
    # A failed proof admits nothing; a name is admitted once.
    checks.equals(language, hol_let(COUNTS, f" ($bad ({p(bad[0][0])}))"), "(3 34 127)")
    checks.refuses(language, hol_let(f"({p(SWAP_ROWS)})", f" ($h ({p(SWAP_ROWS)}))"), "REFUSED",
                   "duplicate name")
    # A hypothesis row and a theorem cited by its handle.
    stmt = "(theorem hyp_id ((var P wff)) ((im P P)) (im P P))"
    checks.equals(language, hol_let(f"(mm0:statement ({prove(language, stmt, '((Mm0Row 0 (hyp 0) ()))', 0)}))"),
                  "(theorem hyp_id ((var P wff ())) ((im P P)) (im P P))")
    stmt = "(theorem syl2 ((var P wff)) () (im P P))"
    checks.equals(language, hol_let(
        f"(mm0:statement ({prove(language, stmt, '((Mm0Row 0 (thm $id (P)) ()))', 0, captures=('$id',))}))",
        " ($id (mm0:theorem $th id))"), "(theorem syl2 ((var P wff ())) () (im P P))")
    # Conversion: unfold bi, under thm, under im.
    stmt = ("(theorem bi_eq ((var G wff) (var a term) (var b term)) ((im G (thm (bi a b)))) "
            "(im G (thm (eqc (bool) a b))))")
    conv = "(conv (cong im ((refl G) (cong thm ((unfold bi (a b) ()))))))"
    rows = f"((Mm0Row 1 (hyp 0) ()) (Mm0Row 2 {conv} (1)))"
    checks.equals(language, hol_let(f"(mm0:statement ({prove(language, stmt, rows, 2)}))"),
                  "(theorem bi_eq ((var G wff ()) (var a term ()) (var b term ())) "
                  "((im G (thm (bi a b)))) (im G (thm (eqc (bool) a b))))")
    rows = f"((Mm0Row 1 (hyp 0) ()) (Mm0Row 2 (conv (symm (cong im ((refl G) (cong thm ((unfold bi (a b) ()))))))) (1)))"
    checks.refuses(language, hol_let(f"({prove(language, stmt, rows, 2)})"), "REFUSED", "left side")
    rows = "((Mm0Row 1 (hyp 0) ()) (Mm0Row 2 (conv (unfold im (G G) ())) (1)))"
    checks.refuses(language, hol_let(f"({prove(language, stmt, rows, 2)})"), "REFUSED", "not a definition")
    # Hostile authored functions that share constructor spellings do not run
    # on data handed to the checker.
    hostile = "(= (Mm0Row $i $r $p) corrupt)\n(= (an $a $b) corrupt)\n(= (thm $n $a) corrupt)\n"
    checks.equals(language, hol_let(f"(mm0:statement ({p(SWAP_ROWS)}))"), SWAP_OUT, hostile)


def declaration_checks(checks, language):
    decls = [
        "(sort wff (strict provable))", "(sort set ())",
        "(term wi ((var a wff) (var b wff)) (wff ()))",
        "(term wal ((bound x set) (var a wff (x))) (wff ()))",
        "(term weq ((var a set) (var b set)) (wff ()))",
        "(term z () (set ()))",
        "(def isz ((bound x set)) (wff (x)) () (weq x (z)))",
        "(def f2 () (wff ()) ((dummy d1 set) (dummy d2 set)) (wal d1 (wal d2 (weq d1 d2))))",
        "(def f1 () (wff ()) ((dummy d set) (dummy unused set)) (wal d (wal d (weq d d))))",
    ]
    setup = " ".join(f"($d{i} (mm0:declare $th {q(d, language)}))" for i, d in enumerate(decls))
    ax5 = q("(axiom ax5 ((bound x set) (var a wff)) () (wi a (wal x a)))", language)
    axid = q("(axiom ax_id ((var a wff)) () (wi a a))", language)
    let = lambda body, extra="": (f"let* (($th (mm0:theory)) {setup} ($ax5 (mm0:declare $th {ax5})) "  # noqa: E731
                                  f"($axid (mm0:declare $th {axid})){extra}) {body}")
    checks.equals(language, let(COUNTS), "(2 7 2)")
    checks.equals(language, let("(mm0:statement $ax5)"),
                  "(axiom ax5 ((bound x set) (var a wff ())) () (wi a (wal x a)))")
    checks.equals(language, let("(mm0:declare $th (sort set ()))"), '(mm0:refused "REFUSED" "duplicate sort name")')
    checks.refuses(language, let(f"(mm0:declare $th {q('(def bad ((bound x set)) (wff ()) () (weq x x))', language)})"),
                   "REFUSED", "unaccounted dependencies")
    checks.refuses(language, let(f"(mm0:declare $th {q('(def bad () (wff ()) ((dummy y set)) (weq y y))', language)})"),
                   "REFUSED", "unaccounted dependencies")
    checks.refuses(language, let(f"(mm0:declare $th {q('(def bad () (wff ()) ((dummy y wff)) (wi y y))', language)})"),
                   "REFUSED", "strict")
    checks.refuses(language, let(f"(mm0:declare $th {q('(term bad ((bound x wff)) (wff ()))', language)})"),
                   "REFUSED", "strict")
    checks.refuses(language, let(f"(mm0:declare $th {q('(theorem t () () (z))', language)})"), "MALFORMED")
    stmt5 = lambda arg: f"(theorem t5 ((bound y set) (bound w set) (var p wff)) () (wi {arg} (wal y {arg})))"  # noqa: E731
    rows5 = lambda arg: f"((Mm0Row 1 (thm ax5 (y {arg})) ()))"  # noqa: E731
    checks.equals(language, let(f"(mm0:statement ({prove(language, stmt5('(weq w w)'), rows5('(weq w w)'), 1)}))"),
                  "(theorem t5 ((bound y set) (bound w set) (var p wff ())) () (wi (weq w w) (wal y (weq w w))))")
    for arg, fragment in (("(weq y y)", "disjoint"), ("(isz y)", "disjoint"),
                          ("(wal y (weq w w))", "disjoint")):
        checks.refuses(language, let(f"({prove(language, stmt5(arg), rows5(arg), 1)})"), "REFUSED", fragment)
    # Unfolding with dummy images: distinct and fresh, one per declared dummy.
    stmt = "(theorem u ((bound a set) (bound c set)) ((f2)) (wal a (wal c (weq a c))))"
    rows = lambda images: f"((Mm0Row 1 (hyp 0) ()) (Mm0Row 2 (conv (unfold f2 () {images})) (1)))"  # noqa: E731
    checks.equals(language, let(f"(mm0:statement ({prove(language, stmt, rows('(a c)'), 2)}))"),
                  "(theorem u ((bound a set) (bound c set)) ((f2)) (wal a (wal c (weq a c))))")
    stmt_same = "(theorem u ((bound a set) (bound c set)) ((f2)) (wal a (wal a (weq a a))))"
    checks.refuses(language, let(f"({prove(language, stmt_same, rows('(a a)'), 2)})"), "REFUSED", "not fresh")
    stmt1 = "(theorem v ((bound a set) (bound c set)) ((f1)) (wal a (wal a (weq a a))))"
    rows1 = lambda images: f"((Mm0Row 1 (hyp 0) ()) (Mm0Row 2 (conv (unfold f1 () {images})) (1)))"  # noqa: E731
    checks.equals(language, let(f"(mm0:statement ({prove(language, stmt1, rows1('(a c)'), 2)}))"),
                  "(theorem v ((bound a set) (bound c set)) ((f1)) (wal a (wal a (weq a a))))")
    checks.refuses(language, let(f"({prove(language, stmt1, rows1('(a)'), 2)})"), "REFUSED", "dummy images")
    # Proof dummies may appear in rows but not in the statement.
    stmt = "(theorem w ((var p wff)) () (wi p (wal y p)))"
    checks.refuses(language, let(f"({prove(language, stmt, '((Mm0Row 1 (thm ax5 (y p)) ()))', 1, '((dummy y set))')})"),
                   "REFUSED", "proof dummies")
    stmt = "(theorem w ((var p wff)) () (wi p p))"
    rows = "((Mm0Row 1 (thm ax_id (p)) ()))"
    checks.equals(language, let(f"(mm0:statement ({prove(language, stmt, rows, 1, '((dummy y set))')}))"),
                  "(theorem w ((var p wff ())) () (wi p p))")


def authority_checks(checks, language):
    other = (f' ($other (mm0:theory)) ($_o (mm0:load-mmb $other "{HOL[0]}" "{HOL[1]}"))'
             ' ($foreign (mm0:theorem $other id))')
    checks.refuses(language, 'mm0:statement (NativeHandle "mm0-thm" 2)', "MALFORMED", "live MM0 theorem")
    checks.refuses(language, hol_let('(mm0:statement (NativeHandle "mm0-thm" 2))', ' ($h (mm0:theorem $th id))'),
                   "MALFORMED", "live MM0 theorem")
    checks.refuses(language, 'mm0:stats (NativeHandle "mm0-theory" 1)', "MALFORMED", "live MM0 theory")
    checks.refuses(language, hol_let('(mm0:stats (NativeHandle "mm0-theory" 1))'), "MALFORMED", "live MM0 theory")
    stmt = "(theorem t ((var P wff)) () (im P P))"
    checks.refuses(language, hol_let(f"({prove(language, stmt, '((Mm0Row 0 (thm $foreign (P)) ()))', 0, captures=('$foreign',))})", other),
                   "FOREIGN", "another MM0 theory")
    # One theorem looked up twice: two handles, one declaration.
    checks.equals(language, hol_let("(mm0:same-declaration $a $b)", " ($a (mm0:theorem $th id)) ($b (mm0:theorem $th id))"),
                  "true")
    checks.equals(language, hol_let("(mm0:same-declaration $a $foreign)", " ($a (mm0:theorem $th id))" + other),
                  "false")
    checks.equals(language, hol_let("(== $a $a)", " ($a (mm0:theorem $th id))"), "true")
    # A public expression carrying an old registry number has no seal identity.
    checks.equals(language, hol_let('(== $a (NativeHandle "mm0-thm" 2))', " ($a (mm0:theorem $th id))"), "false")
    checks.equals(language, hol_let('(case ($a (NativeHandle "mm0-thm" 2)) ((($x $x) leaked) ($other opaque)))',
                                    " ($a (mm0:theorem $th id))"), "opaque")
    checks.refuses(language, 'mm0:statement (Sealed mm0)', "MALFORMED", "live MM0 theorem")
    # Budgets stop work without changing the theory; a later call proceeds.
    checks.refuses(language, hol_let(f"({prove(language, SWAP_STMT, SWAP_ROWS, 3)})", " ($b (mm0:budget $th 3))"),
                   "LIMIT", "budget")
    checks.equals(language, hol_let(f"(mm0:statement ({prove(language, SWAP_STMT, SWAP_ROWS, 3)}))",
                                    f" ($b (mm0:budget $th 3)) ($f ({prove(language, SWAP_STMT, SWAP_ROWS, 3)}))"
                                    " ($r (mm0:budget $th -1))"), SWAP_OUT)
    checks.refuses(language, f'let* (($th (mm0:theory)) ($b (mm0:budget $th 100))) '
                   f'(mm0:load-mmb $th "{HOL[0]}" "{HOL[1]}")', "LIMIT")
    # Many retained theorem capsules, each still genuine.
    choices = " ".join(map(str, range(300)))
    checks.equals(language, hol_let("(let $hs (collapse (let $i (superpose (" + choices + ")) (mm0:theorem $th syl))) "
                                    "((size-atom $hs) (collapse (let $h (superpose $hs) (mm0:same-declaration $h $h)))))"),
                  "(300 (" + " ".join(["true"] * 300) + "))")


def lifetime_checks(checks, language):
    """Handles are released with their last holder; stored ones persist."""
    source = ("!(mm0:live)\n"
              f'!({hol_let("(mm0:live)", " ($a (mm0:theorem $th syl))")})\n'
              "!(mm0:live)\n"
              "!(bind! &keep (new-space))\n"
              f'!({hol_let("(add-atom &keep (Kept $h))", " ($h (mm0:theorem $th syl))")})\n'
              "!(mm0:live)\n"
              "!(match &keep (Kept $t) (mm0:statement $t))\n")
    result = checks.run(["--lang", language, "--quiet", "-e", "!(import! &self mm0kernel)\n" + source])
    checks.check(result.returncode == 0, f"{language} lifetimes run", result)
    lines = [line[1:-1] if line.startswith("[") and line.endswith("]") else line
             for line in result.stdout.splitlines() if line not in ("true", "[()]", "()")]
    expected = ["(Mm0Live 0 0)", "(Mm0Live 1 1)", "(Mm0Live 0 0)", "(Mm0Live 1 1)",
                "(axiom syl ((var P wff ()) (var Q wff ()) (var R wff ())) ((im P Q) (im Q R)) (im P R))"]
    checks.check(lines == expected, f"{language} handle lifetimes", lines)
    # A state cell holding a theorem (an HE state cell): replacing the cell's
    # value leaves the handle that was read out of it intact.
    if language == "he":
        checks.equals(language, hol_let("(mm0:statement $old)",
                                        " ($a (mm0:theorem $th syl)) ($cell (new-state $a))"
                                        " ($old (get-state $cell)) ($b (mm0:theorem $th id))"
                                        " ($_c (change-state! $cell $b))"),
                      "(axiom syl ((var P wff ()) (var Q wff ()) (var R wff ())) ((im P Q) (im Q R)) (im P R))")
    # Mutating stored proof data after the check leaves the theorem as checked.
    checks.equals(language, hol_let(
        "(mm0:statement $h)",
        f" ($kb (new-space)) ($_2 (add-atom $kb (Mm0Proof {SWAP_STMT} {SWAP_ROWS})))"
        " ($h (match $kb (Mm0Proof $s $rows) (mm0:prove $th $s () $rows 3)))"
        f" ($_3 (remove-atom $kb (Mm0Proof {SWAP_STMT} {SWAP_ROWS})))"
        " ($_4 (add-atom $kb (Mm0Proof (theorem swap () () (im)) ())))"), SWAP_OUT)


def main():
    binary = Path(sys.argv[1]).resolve()
    checks = Checks(binary)
    work = ROOT / "runtime" / "native-mm0-host"
    bad = definition_dependency_fixture(work)
    d = FIXTURES / "dummy-identity"
    dummy = (str(d / "proof.mmb"), str(d / "spec.mm0"), str(d / "spec-own.mm0"))
    for language in ("he", "petta", "prime"):
        mmb_checks(checks, language, dummy, bad)
        dag_checks(checks, language)
        declaration_checks(checks, language)
        authority_checks(checks, language)
        if language != "prime":
            lifetime_checks(checks, language)
    print(f"Mm0HostSummary {checks.count} 0")


if __name__ == "__main__":
    main()
