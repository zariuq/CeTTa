#!/usr/bin/env python3
"""HOL through CeTTa's public languages, including authority and replay controls."""
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
FUSION_SHA = "29544be92d9cc1e6b3b59ba5b210604b9a748366a68b3bcdc2619717d68fd98a"


def uv(value):
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)


def stream(records, flags=0):
    """Independent encoder: the test does not call the kernel's stream writer."""
    data = b"HOLRPLY1" + bytes.fromhex(FUSION_SHA) + bytes([1, 0, 0, flags])
    data += b"".join(records)
    checksum = 14695981039346656037
    for byte in data:
        checksum = ((checksum ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return data + b"\x7f" + uv(len(records)) + checksum.to_bytes(8, "little")


def record(opcode, *args):
    return bytes([opcode]) + b"".join(uv(arg) for arg in args)


def name(value):
    data = value.encode()
    return b"\x01" + uv(len(data)) + data


def fixtures(directory):
    directory.mkdir(parents=True, exist_ok=True)
    identity = [name("A"), name("x"), name("identity"), record(2, 0),
                record(4, 1, 0), record(0x10, 0), record(0x31, 0, 2),
                record(0x32, 0), record(0x32, 0)]
    axiom = [name("bool"), name("p"), name("axiom"), record(3, 0, 0),
             record(4, 1, 0), record(0x22, 0), record(0x31, 0, 2), record(0x32, 0)]
    # ASSUME on a type variable is not a Boolean proposition. Probe streams
    # can record this refusal, but cannot be accepted as complete proofs.
    probe = identity[:5] + [record(0x15, 0)]
    paths = {}
    for label, data in (("identity", stream(identity)), ("axiom", stream(axiom)),
                        ("probe", stream(probe, flags=1)),
                        ("truncated", stream(identity)[:-1]),
                        ("trailing", stream(identity) + b"extra")):
        paths[label] = directory / (label + ".holr")
        paths[label].write_bytes(data)
    return paths


class Checks:
    def __init__(self, binary):
        self.binary = str(binary)
        self.count = 0

    def run(self, args):
        return subprocess.run([self.binary, *args], cwd=ROOT, capture_output=True,
                              text=True, timeout=40)

    def check(self, condition, message, actual=None):
        self.count += 1
        if not condition:
            raise AssertionError(f"{message}: {actual!r}")

    def query(self, language, expression, prefix=""):
        source = "!(import! &self holkernel)\n" + prefix + "\n!(" + expression + ")\n"
        result = self.run(["--lang", language, "--quiet", "-e", source])
        self.check(result.returncode == 0, f"{language} exits successfully", result)
        lines = result.stdout.splitlines()
        expected_lines = 2 if language == "petta" else 1
        self.check(len(lines) == expected_lines, f"{language} one query result", result.stdout)
        if language == "petta":
            self.check(lines[0] == "true", "PeTTa import succeeded", result.stdout)
        value = lines[-1]
        if language != "petta" and value.startswith("[") and value.endswith("]"):
            value = value[1:-1]
        return {"True": "true", "False": "false"}.get(value, value)

    def equals(self, language, expression, expected, prefix=""):
        value = self.query(language, expression, prefix)
        self.check(value == expected, f"{language} {expression}", value)

    def refuses(self, language, expression, status, prefix=""):
        value = self.query(language, expression, prefix)
        self.check(value.startswith(("(hol:refused ", "(hol:error ")) and
                   f'"{status}"' in value, f"{language} refuses {expression}", value)


def dag_checks(checks, language):
    """Independent source-row fixtures exercise the borrowed native adapter."""
    a, b = '(V "A")', '(T "bool")'

    def var(name, ty=a):
        return f'(v "{name}" {ty})'

    def arrow(left, right):
        return f'(T "fun" {left} {right})'

    def app(f, x):
        return f'(A {f} {x})'

    def equal(left, right, ty=a):
        return app(app(f'(c "=" {arrow(ty, arrow(ty, b))})', left), right)

    def sequent(term, *hyps):
        return f'(thm ({" ".join(hyps)}) {term})'

    def node(identifier, request, parents, claim):
        return f'(HolNode {identifier} {request} ({" ".join(map(str, parents))}) {claim})'

    def query(rows, root):
        return f'let $k (hol:kernel) (hol:check-dag $k ({" ".join(rows)}) {root})'

    x, y, p = var("x"), var("y"), var("p", b)
    f = var("f", arrow(a, a))
    fx = app(f, x)
    xx, pp = equal(x, x), equal(p, p, b)
    lam = f'(L {y} {fx})'
    beta = app(f'(L {x} {x})', x)
    first = node(7, f'(REFL {x})', [], sequent(xx))
    trans = node(1009, '(TRANS)', [7, 7], sequent(xx))
    rows = [first, trans,
            node(0, f'(REFL {f})', [], sequent(equal(f, f, arrow(a, a)))),
            node(3, '(MK_COMB)', [0, 1009], sequent(equal(fx, fx))),
            node(8, f'(ABS {y})', [3], sequent(equal(lam, lam, arrow(a, a)))),
            node(9, f'(BETA {beta})', [], sequent(equal(beta, x))),
            node(12, f'(ASSUME {p})', [], sequent(p, p)),
            node(13, '(DEDUCT_ANTISYM_RULE)', [12, 12], sequent(pp)),
            node(14, '(EQ_MP)', [13, 12], sequent(p, p)),
            node(15, f'(INST (({y} {x})))', [1009], sequent(equal(y, y))),
            node(9223372036854775807, f'(INST_TYPE (({b} {a})))', [7],
                 sequent(equal(var("x", b), var("x", b), b)))]
    checks.equals(language, f'let $h ({query(rows, 14)}) (hol:sequent $h)', sequent(p, p))
    checks.equals(language, f'let $h ({query(rows, 9223372036854775807)}) (hol:sequent $h)',
                  sequent(equal(var("x", b), var("x", b), b)))
    # Exercise source-ID collisions and table growth with distinguishable
    # results; an all-equal stress graph alone cannot detect a wrong lookup.
    many = [node(i * 17 + 3, f'(REFL {var(f"x{i}")})', [],
                 sequent(equal(var(f"x{i}"), var(f"x{i}")))) for i in range(100)]
    many += [node(10000 + i, '(TRANS)', [i * 17 + 3, i * 17 + 3],
                  sequent(equal(var(f"x{i}"), var(f"x{i}")))) for i in range(100)]
    checks.equals(language, f'let $h ({query(many, 10037)}) (hol:sequent $h)',
                  sequent(equal(var("x37"), var("x37"))))
    # The same syntax is ordinary data stored in a MeTTa space. The checked
    # result is stored and queried again as an authentic owned theorem.
    checks.equals(language, 'let* (($kb (new-space)) ($k (hol:kernel)) '
                  f'($_ (add-atom $kb (HolBundle sample ({" ".join(rows)})))) '
                  '($h (match $kb (HolBundle sample $rows) (hol:check-dag $k $rows 14))) '
                  '($_2 (add-atom $kb (Checked sample $h)))) '
                  '(match $kb (Checked sample $proof) (hol:sequent $proof))', sequent(p, p))
    # Source IDs are addresses, not cache positions, and repeated edges are
    # consumed twice. Refusing a later graph never revokes an earlier root.
    checks.equals(language, f'let* (($k (hol:kernel)) ($h (hol:check-dag $k ({first}) 7)) '
                  f'($bad (hol:check-dag $k ({first} {first}) 7))) (hol:sequent $h)', sequent(xx))
    bad_cases = [
        ([], 7, "REFUSED"), ([first], 8, "REFUSED"),
        ([first, first], 7, "REFUSED"), ([trans, first], 1009, "REFUSED"),
        ([node(7, '(TRANS)', [7, 7], sequent(xx))], 7, "REFUSED"),
        ([first, node(8, '(TRANS)', [7, 999], sequent(xx))], 8, "REFUSED"),
        ([node(7, f'(REFL {x})', [], sequent(equal(y, y)))], 7, "REFUSED"),
        ([node(7, f'(ASSUME {p})', [], sequent(p))], 7, "REFUSED"),
        ([first, node(8, '(TRANS)', [7], sequent(xx))], 8, "MALFORMED"),
        ([node(7, '(AXIOM)', [], sequent(p))], 7, "MALFORMED"),
        ([node(7, f'(REFL {x})', [], '(NativeHandle "hol-thm" 2)')], 7, "MALFORMED"),
        ([first, node(8, f'(REFL {y})', [], sequent(equal(y, y))),
          node(9, '(TRANS)', [7, 8], sequent(xx))], 9, "REFUSED"),
        ([node(7, f'(ASSUME {xx})', [], sequent(xx, xx)),
          node(8, f'(ABS {x})', [7], sequent(equal(f'(L {x} {x})', f'(L {x} {x})', arrow(a, a))))],
         8, "REFUSED"),
        ([first, node(8, f'(INST (({x} {fx})))', [7], sequent(xx))], 8, "REFUSED"),
        ([node(-1, f'(REFL {x})', [], sequent(xx))], 0, "MALFORMED"),
    ]
    for bad_rows, root, status in bad_cases:
        checks.refuses(language, query(bad_rows, root), status)
    checks.equals(language, f'let $h ({query([node(7, f"(ASSUME {p})", [], sequent(p, p, p))], 7)}) '
                  '(hol:sequent $h)', sequent(p, p))
    without_claims = [f'(HolNode 7 (REFL {x}) ())', '(HolNode 1009 (TRANS) (7 7))']
    checks.equals(language, f'let $h ({query(without_claims, 1009)}) (hol:sequent $h)', sequent(xx))
    checks.equals(language, f'let $h ({query([without_claims[0], trans], 1009)}) (hol:sequent $h)', sequent(xx))
    # A typed value handle can supply a shared term without decoding it again.
    checks.equals(language, f'let* (($k (hol:kernel)) ($x (hol:term $k {x})) '
                  f'($h (hol:check-dag $k ({node(7, "(REFL $x)", [], sequent(xx))}) 7))) '
                  '(hol:sequent $h)', sequent(xx))
    # Reconstruct equality rewriting under a function context: a=b and p(a)
    # entail p(b). Imported problem assumptions remain explicit hypotheses.
    left, right = var("a"), var("b")
    predicate = var("p", arrow(a, b))
    pa, pb = app(predicate, left), app(predicate, right)
    ab = equal(left, right)
    rewrite_rows = [node(1, f'(ASSUME {ab})', [], sequent(ab, ab)),
                    node(2, f'(REFL {predicate})', [],
                         sequent(equal(predicate, predicate, arrow(a, b)))),
                    node(3, '(MK_COMB)', [2, 1], sequent(equal(pa, pb, b), ab)),
                    node(4, f'(ASSUME {pa})', [], sequent(pa, pa)),
                    node(5, '(EQ_MP)', [3, 4], sequent(pb, pa, ab))]
    checks.equals(language, f'let $h ({query(rewrite_rows, 5)}) (hol:sequent $h)', sequent(pb, pa, ab))
    rewrite_rows[-1] = node(5, '(EQ_MP)', [3, 4], sequent(pb, ab, pa))
    checks.equals(language, f'let $h ({query(rewrite_rows, 5)}) (hol:sequent $h)', sequent(pb, pa, ab))
    rewrite_rows[-1] = node(5, '(EQ_MP)', [3, 4], sequent(pb, ab, pa, ab, pa))
    checks.equals(language, f'let $h ({query(rewrite_rows, 5)}) (hol:sequent $h)', sequent(pb, pa, ab))
    rewrite_rows[-1] = node(5, '(EQ_MP)', [3, 4], sequent(pb, p, pa, ab))
    checks.refuses(language, query(rewrite_rows, 5), "REFUSED")
    rewrite_rows[-1] = node(5, '(EQ_MP)', [3, 4], sequent(pb))
    checks.refuses(language, query(rewrite_rows, 5), "REFUSED")
    ax = app(f'(L {x} {p})', x)
    ay = app(f'(L {y} {p})', x)
    alpha_hyps = [node(1, f'(ASSUME {ax})', [], sequent(ay, ay, ax))]
    checks.equals(language, f'let $h ({query(alpha_hyps, 1)}) (hol:sequent $h)', sequent(ax, ax))
    hostile = '(= (HolNode $id $req $parents $claim) corrupt)\n(= (REFL $tm) corrupt)\n'
    literal = f'(quote ({first}))' if language == "prime" else f'({first})'
    checks.equals(language, f'let* (($k (hol:kernel)) ($h (hol:check-dag $k {literal} 7))) '
                  '(hol:sequent $h)', sequent(xx), hostile)


def main():
    binary = Path(sys.argv[1]).resolve()
    checks = Checks(binary)
    paths = fixtures(ROOT / "runtime" / "hol-light-host")
    x, y = '(v "x" (V "A"))', '(v "y" (V "A"))'
    p = '(v "p" (T "bool"))'
    eq = '(T "fun" (V "A") (T "fun" (V "A") (T "bool")))'
    sequent = f'(thm () (A (A (c "=" {eq}) {x}) {x}))'
    # HOLR and its language alias preserve two equal EMIT occurrences.
    accepted = None
    for language in ("hol", "hol-light"):
        run = checks.run(["--lang", language, str(paths["identity"])])
        checks.check(run.returncode == 0, "HOLR accepted", run)
        lines = run.stdout.splitlines()
        checks.check(lines == [sequent, sequent, "(hol:accepted 9 2)"],
                     "ordered duplicate deliveries", lines)
        if accepted is not None:
            checks.check(run.stdout == accepted, "language alias equivalence")
        accepted = run.stdout
    for args, code, fragment in (
        (["--count-only", str(paths["identity"])], 0, "2\n"),
        (["--no-results", str(paths["identity"])], 0, ""),
        (["--profile", "open", str(paths["axiom"])], 0, "(hol:accepted 8 1)"),
        ([str(paths["axiom"])], 1, '"POLICY"'),
        (["--profile", "sealed", str(paths["axiom"])], 1, '"POLICY"'),
        ([str(paths["probe"])], 1, "probe stream"),
        ([str(paths["truncated"])], 2, "stream: truncated"),
        ([str(paths["trailing"])], 2, "stream: trailing"),
        (["--syntax", "metta", str(paths["identity"])], 2, "syntax"),
    ):
        run = checks.run(["--lang", "hol-light", *args])
        checks.check(run.returncode == code, f"CLI status for {args}", run)
        checks.check(fragment in run.stdout + run.stderr, f"CLI diagnosis for {args}", run)

    for language in ("he", "petta", "prime"):
        dag_checks(checks, language)
        checks.equals(language, f'let $k (hol:kernel) (hol:sequent (hol:refl $k {x}))', sequent)
        # Retain more than one registry segment, then resolve every genuine
        # theorem. Counting results alone would also count unreduced failures.
        choices = ' '.join(map(str, range(1024)))
        checks.equals(language, 'let $k (hol:kernel) (let $hs (collapse '
                      f'(let $i (superpose ({choices})) (hol:refl $k {x}))) '
                      '((size-atom $hs) (collapse (let $h (superpose $hs) (hol:standard $h)))))',
                      '(1024 (' + ' '.join(['true'] * 1024) + '))')
        for mode, events in (("", "0"), (" standard check", "0"), (" standard record", "1")):
            checks.equals(language, f'let* (($k (hol:kernel{mode})) ($h (hol:refl $k {x}))) '
                          '(let (HolStats $core $journal $events $terms $theorems $refs) '
                          '(hol:stats $k) $events)', events)
        checks.refuses(language, 'hol:kernel standard unknown', "MALFORMED")
        checks.equals(language, f'let $k (hol:kernel) (hol:type-of $k {x})', '(V "A")')
        checks.equals(language, f'let* (($k (hol:kernel)) ($t (hol:term $k {x}))) '
                      f'(hol:same-payload (hol:refl $k $t) (hol:refl $k {x}))', "true")
        checks.equals(language, f'let* (($k (hol:kernel)) ($h (hol:refl $k {x})) '
                      f'($h2 (hol:refl $k {x}))) (== (hol:event $h) (hol:event $h2))', "false")
        checks.equals(language, f'let* (($k (hol:kernel)) ($t (hol:term $k {x}))) '
                      '(hol:term-data $t)', x)
        checks.equals(language, 'let* (($k (hol:kernel)) ($t (hol:type $k (V "A")))) '
                      '(hol:type-data $t)', '(V "A")')
        checks.equals(language, f'let $k (hol:kernel) (hol:aconv $k (L {x} {x}) (L {y} {y}))', "true")
        checks.equals(language, f'let $k (hol:kernel) (hol:aconv $k {x} {y})', "false")
        checks.equals(language, f'let $k (hol:kernel) (hol:standard (hol:refl $k {x}))', "true")
        checks.refuses(language, f'let $k (hol:kernel) (hol:new-axiom $k {p})', "POLICY")
        checks.equals(language, f'let $k (hol:kernel open) (hol:standard (hol:new-axiom $k {p}))', "false")
        checks.refuses(language, f'let* (($k (hol:kernel open)) ($saved (hol:checkpoint $k)) '
                       '($_ (hol:policy $k sealed)) ($_2 (hol:restore $k $saved))) '
                       f'(hol:new-axiom $k {p})', "POLICY")
        checks.refuses(language, 'let $k (hol:kernel) (hol:policy $k open)', "POLICY")
        checks.refuses(language, f'let* (($k (hol:kernel)) ($k2 (hol:kernel)) '
                       f'($t (hol:term $k {x}))) (hol:refl $k2 $t)', "FOREIGN")
        checks.refuses(language, f'let* (($k (hol:kernel)) ($h (hol:refl $k {x}))) '
                       '(hol:sequent (NativeHandle "hol-thm" 2))', "MALFORMED")
        checks.refuses(language, f'let $k (hol:kernel) (hol:refl (NativeHandle "hol-kernel" 1) {x})',
                       "MALFORMED")
        checks.refuses(language, f'let $k (hol:kernel) (hol:assume $k {x})', "REFUSED")
        checks.refuses(language, f'let $k (hol:kernel) (hol:beta $k (A (L {x} {x}) {y}))', "REFUSED")
        checks.equals(language, f'let $k (hol:kernel) (hol:sequent (hol:beta $k (A (L {x} {x}) {x})))',
                      f'(thm () (A (A (c "=" {eq}) (A (L {x} {x}) {x})) {x}))')
        checks.refuses(language, f'let $k (hol:kernel) (hol:term $k (A {x} {x}))', "REFUSED")
        checks.refuses(language, 'let $k (hol:kernel) (hol:term $k (v "x"))', "MALFORMED")
        checks.refuses(language, 'let* (($k (hol:kernel)) ($_ (hol:budget $k 1))) '
                       f'(hol:term $k {x})', "LIMIT")
        checks.equals(language, 'let* (($k (hol:kernel)) ($_ (hol:budget $k 1)) '
                      f'($failed (hol:term $k {x})) ($_2 (hol:budget $k -1))) '
                      f'(hol:sequent (hol:refl $k {x}))', sequent)
        # Authored functions with constructor spellings cannot execute on
        # the way into the kernel or on completed syntax returned from it.
        hostile = '(= (v $n $ty) corrupt)\n(= (A $f $a) corrupt)\n(= (thm $hs $c) corrupt)\n'
        literal = f'(quote {x})' if language == "prime" else x
        checks.equals(language, f'let $k (hol:kernel) (hol:sequent (hol:refl $k {literal}))', sequent, hostile)
        identity_path = json.dumps(str(paths["identity"]))
        axiom_path = json.dumps(str(paths["axiom"]))
        truncated_path = json.dumps(str(paths["truncated"]))
        checks.equals(language, f'let* (($k (hol:kernel)) ($_ (hol:replay $k {identity_path}))) '
                      '(hol:sequent (hol:theorem $k "identity"))', sequent)
        checks.equals(language, f'let* (($k (hol:kernel)) ($_ (hol:replay $k {identity_path})) '
                      f'($failed (hol:replay $k {truncated_path}))) '
                      '(hol:sequent (hol:theorem $k "identity"))', sequent)
        checks.refuses(language, f'let $k (hol:kernel sealed) (hol:replay $k {axiom_path} open)', "POLICY")
        checks.refuses(language, f'let $k (hol:kernel) (hol:replay $k {axiom_path})', "POLICY")
        checks.equals(language, f'let* (($k (hol:kernel open)) ($_ (hol:replay $k {axiom_path}))) '
                      '(hol:standard (hol:theorem $k "axiom"))', "false")
    print(f"HolHostSummary {checks.count} 0")


if __name__ == "__main__":
    main()
