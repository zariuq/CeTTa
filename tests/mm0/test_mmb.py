#!/usr/bin/env python3
"""MMB and specification controls for the native MM0 verifier.

Every file here is written by tests/mm0/mmb_asm.py, an MMB writer that
shares no code with the verifier.  Each case states the verdict the
specification requires; with --mm0c it also runs an mm0-c build on the same
input and reports where the two differ.  The known differences are listed in
KNOWN_MM0C_DIFFERENCES with the reason.

    python3 tests/mm0/test_mmb.py runtime/native-mm0/mm0_check [--mm0c PATH]
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mmb_asm import (Mmb, bound, reg, ret, PROVABLE, STRICT, PURE, FREE,  # noqa: E402
                     UTERM, UTERM_SAVE, UREF, UDUMMY, UHYP, TERM, TERM_SAVE, REF, DUMMY,
                     THM, THM_SAVE, HYP, CONV, REFL, SYMM, CONG, UNFOLD, CONV_CUT,
                     CONV_SAVE, SAVE, SORRY, END, S_SORT, S_LOCAL_DEF)

STATUS = {0: "OK", 1: "MALFORMED", 2: "REFUSED", 3: "UNSUPPORTED", 4: "LIMIT", 5: "NOMEM",
          6: "INTERRUPTED", 7: "FOREIGN"}

BASE_SPEC = """delimiter $ ( ) $;
strict provable sort wff;
sort set;
term wi (a b: wff): wff; infixr wi: $->$ prec 25;
term wal {x: set} (a: wff x): wff; prefix wal: $A.$ prec 41;
term weq (a b: set): wff; infixl weq: $=$ prec 50;
term z: set;
def isz {x: set}: wff x = $ x = z $;
axiom ax1 (a b: wff): $ a -> b -> a $;
axiom ax_mp (a b: wff): $ a $ > $ a -> b $ > $ b $;
axiom ax5 {x: set} (a: wff): $ a -> A. x a $;
axiom ax_id (a: wff): $ a -> a $;
axiom ax_dup (a b: wff): $ a $ > $ b $ > $ a -> b $;
"""


class Theory:
    """The base theory of BASE_SPEC, with helpers that write expressions
    as unify streams (preorder) and proof streams (postfix)."""

    def __init__(self):
        m = self.m = Mmb()
        self.wff = m.sort(STRICT | PROVABLE, "wff")
        self.set = m.sort(0, "set")
        t = self.t = {}
        t["wi"] = m.term([reg(0), reg(0)], ret(0), "wi")
        t["wal"] = m.term([bound(1, 0), reg(0, 1)], ret(0), "wal")
        t["weq"] = m.term([reg(1), reg(1)], ret(0), "weq")
        t["z"] = m.term([], ret(1), "z")
        t["isz"] = m.define([bound(1, 0)], ret(0, 1),
                            [(UTERM, t["weq"]), (UREF, 0), (UTERM, t["z"]), END],
                            [(REF, 0), (TERM, t["z"]), (TERM, t["weq"]), END], name="isz")
        self.thm = {}
        self.axiom("ax1", [reg(0), reg(0)], ["a", "b"], [], ("wi", "a", ("wi", "b", "a")))
        self.axiom("ax_mp", [reg(0), reg(0)], ["a", "b"], ["a", ("wi", "a", "b")], "b")
        self.axiom("ax5", [bound(1, 0), reg(0)], ["x", "a"], [], ("wi", "a", ("wal", "x", "a")))
        self.axiom("ax_id", [reg(0)], ["a"], [], ("wi", "a", "a"))
        self.axiom("ax_dup", [reg(0), reg(0)], ["a", "b"], ["a", "b"], ("wi", "a", "b"))

    def unify(self, e, env):
        if isinstance(e, str):
            return [(UREF, env[e])]
        out = [(UTERM, self.t[e[0]])]
        for x in e[1:]:
            out += self.unify(x, env)
        return out

    def build(self, e, env):
        """Postfix commands allocating e afresh (variables by Ref)."""
        if isinstance(e, str):
            return [(REF, env[e])]
        out = []
        for x in e[1:]:
            out += self.build(x, env)
        return out + [(TERM, self.t[e[0]])]

    def statement(self, names, hyps, concl):
        env = {n: i for i, n in enumerate(names)}
        stream = self.unify(concl, env)
        for h in reversed(hyps):
            stream += [UHYP] + self.unify(h, env)
        return stream + [END], env

    def axiom(self, name, args, names, hyps, concl):
        stream, env = self.statement(names, hyps, concl)
        proof = []
        for h in hyps:
            proof += self.build(h, env) + [HYP]
        proof += self.build(concl, env) + [END]
        self.thm[name] = self.m.axiom(args, stream, proof, name=name)

    def theorem(self, name, args, names, hyps, concl, proof, local=False):
        stream, _ = self.statement(names, hyps, concl)
        return self.m.theorem(args, stream, proof + [END], local=local, name=name)


def spec_with(*lines):
    return BASE_SPEC + "".join(line + "\n" for line in lines)


# ---- the cases ---------------------------------------------------------------

def case_thm_ax1():
    T = Theory()
    env = {"a": 0, "b": 1}
    concl = ("wi", "a", ("wi", "b", "a"))
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], concl,
              [(REF, 0), (REF, 1)] + T.build(concl, env) + [(THM, T.thm["ax1"])])
    return T.m.build(), spec_with("theorem t (a b: wff): $ a -> b -> a $;")


def mp_proof(T, order):
    """theorem mp2 (a b: wff): $ a $ > $ a -> b $ > $ b $ by ax_mp, with the
    two hypothesis proofs pushed in `order`."""
    env = {"a": 0, "b": 1}
    proof = T.build("a", env) + [HYP] + T.build(("wi", "a", "b"), env) + [HYP]
    proof += [(REF, 2 + i) for i in order] + [(REF, 0), (REF, 1), (REF, 1), (THM, T.thm["ax_mp"])]
    T.theorem("mp2", [reg(0), reg(0)], ["a", "b"], ["a", ("wi", "a", "b")], "b", proof)
    return T.m.build(), spec_with("theorem mp2 (a b: wff): $ a $ > $ a -> b $ > $ b $;")


def case_mp():
    return mp_proof(Theory(), [0, 1])


def case_mp_order():
    return mp_proof(Theory(), [1, 0])


def case_dup_premise():
    """ax_dup a a cites one hypothesis row twice."""
    T = Theory()
    env = {"a": 0}
    proof = [(REF, 0), HYP, (REF, 1), (REF, 1), (REF, 0), (REF, 0)] + \
        T.build(("wi", "a", "a"), env) + [(THM, T.thm["ax_dup"])]
    T.theorem("dup", [reg(0)], ["a"], ["a"], ("wi", "a", "a"), proof)
    return T.m.build(), spec_with("theorem dup (a: wff): $ a $ > $ a -> a $;")


def case_missing_premise():
    T = Theory()
    env = {"a": 0}
    proof = [(REF, 0), HYP, (REF, 1), (REF, 0), (REF, 0)] + \
        T.build(("wi", "a", "a"), env) + [(THM, T.thm["ax_dup"])]
    T.theorem("dup", [reg(0)], ["a"], ["a"], ("wi", "a", "a"), proof)
    return T.m.build(), spec_with("theorem dup (a: wff): $ a $ > $ a -> a $;")


def case_missing_hyp():
    """The statement has two hypotheses, the proof declares one."""
    T = Theory()
    env = {"a": 0, "b": 1}
    concl = ("wi", "a", ("wi", "b", "a"))
    proof = T.build("a", env) + [HYP, (REF, 0), (REF, 1)] + T.build(concl, env) + [(THM, T.thm["ax1"])]
    T.theorem("h2", [reg(0), reg(0)], ["a", "b"], ["a", "a"], concl, proof)
    return T.m.build(), spec_with("theorem h2 (a b: wff): $ a $ > $ a $ > $ a -> b -> a $;")


def alias_case(T, shared):
    """theorem t (a b: wff): $ a -> b -> a $ via Conv.  The converted
    expression is the theorem's own conclusion when `shared`, otherwise a
    separately allocated copy of it; Refl compares allocations."""
    env = {"a": 0, "b": 1}
    concl = ("wi", "a", ("wi", "b", "a"))
    if shared:
        proof = T.build(concl, env)[:-1] + [(TERM_SAVE, T.t["wi"])]       # X, heap 2
        proof += [(REF, 0), (REF, 1), (REF, 2), (THM, T.thm["ax1"])]
    else:
        proof = T.build(concl, env)                                       # Y
        proof += [(REF, 0), (REF, 1)] + T.build(concl, env) + [(THM, T.thm["ax1"])]
    return proof + [CONV]


def case_alias_shared_refl():
    T = Theory()
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], ("wi", "a", ("wi", "b", "a")),
              alias_case(T, True) + [REFL])
    return T.m.build(), spec_with("theorem t (a b: wff): $ a -> b -> a $;")


def case_alias_fresh_refl():
    T = Theory()
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], ("wi", "a", ("wi", "b", "a")),
              alias_case(T, False) + [REFL])
    return T.m.build(), spec_with("theorem t (a b: wff): $ a -> b -> a $;")


def case_alias_fresh_cong():
    """The same copies, decomposed by Cong down to shared variables."""
    T = Theory()
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], ("wi", "a", ("wi", "b", "a")),
              alias_case(T, False) + [CONG, REFL, CONG, REFL, REFL])
    return T.m.build(), spec_with("theorem t (a b: wff): $ a -> b -> a $;")


def uref_case(shared):
    """ax_id applied to W = (x = z): the conclusion holds W itself, or a copy."""
    T = Theory()
    env = {"x": 0}
    w = ("weq", "x", ("z",))
    proof = T.build(w, env)[:-1] + [(TERM_SAVE, T.t["weq"])]               # W, heap 1
    if shared:
        proof += [(REF, 1), (REF, 1), (TERM, T.t["wi"])]
    else:
        proof += T.build(w, env) + [(REF, 1), (TERM, T.t["wi"])]
    # stack: W, concl -> Thm ax_id pops concl then the argument W
    proof += [(THM, T.thm["ax_id"])]
    T.theorem("t", [bound(1, 0)], ["x"], [], ("wi", w, w), proof)
    return T.m.build(), spec_with("theorem t {x: set}: $ x = z -> x = z $;")


def case_uref_shared():
    return uref_case(True)


def case_uref_copy():
    return uref_case(False)


def unfold_case(bad):
    """theorem u {x: set}: $ isz x -> x = z $ from ax_id (x = z) by
    Conv, Cong and Unfold of isz."""
    T = Theory()
    t = T.t
    proof = [(REF, 0), (TERM_SAVE, t["isz"])]                                # I, heap 1
    proof += [(REF, 0), (TERM, t["z"]), (TERM_SAVE, t["weq"])]                # W, heap 2
    proof += [(TERM, t["wi"])]                                               # E1 = I -> W
    proof += [(REF, 2), (REF, 2), (REF, 2), (TERM, t["wi"]), (THM, T.thm["ax_id"])]
    proof += [CONV, CONG]
    if bad == "not-def":
        # turned around, the obligation's left side is weq, not a definition
        proof += [SYMM, (REF, 2), UNFOLD]
    elif bad == "wrong-target":
        proof += [(REF, 0), (TERM, t["z"]), (TERM, t["weq"]), UNFOLD, REFL, REFL]
    else:
        proof += [(REF, 2), UNFOLD, REFL, REFL]
    T.theorem("u", [bound(1, 0)], ["x"], [], ("wi", ("isz", "x"), ("weq", "x", ("z",))), proof)
    return T.m.build(), spec_with("theorem u {x: set}: $ isz x -> x = z $;")


def case_unfold():
    return unfold_case(None)


def case_unfold_not_def():
    return unfold_case("not-def")


def case_unfold_fresh_target():
    """The unfolded value is a separate allocation, so the final Refl fails."""
    return unfold_case("wrong-target")


def ax5_case(arg, spec):
    """theorem t5 {x y: set}: $ arg -> A. x arg $ by ax5 x arg.  The argument
    is saved (heap 2) so that the conclusion holds that very allocation."""
    T = Theory()
    env = {"x": 0, "y": 1}
    concl = ("wi", arg, ("wal", "x", arg))
    proof = [(REF, 0)] + T.build(arg, env)[:-1] + [(TERM_SAVE, T.t[arg[0]])]
    proof += [(REF, 2), (REF, 0), (REF, 2), (TERM, T.t["wal"]), (TERM, T.t["wi"]), (THM, T.thm["ax5"])]
    T.theorem("t5", [bound(1, 0), bound(1, 1)], ["x", "y"], [], concl, proof)
    return T.m.build(), spec_with(f"theorem t5 {{x y: set}}: $ {spec} $;")


def case_ax5_fresh():
    return ax5_case(("weq", "y", "y"), "y = y -> A. x y = y")


def case_ax5_disjoint():
    return ax5_case(("weq", "x", "x"), "x = x -> A. x x = x")


def case_ax5_definition_dependency():
    """isz x depends on x through its return type: a disjoint-variable
    violation.  mm0-c's theorem-mode variable sets leave bound arguments out
    and accept it; the statement is false in a two-element model."""
    return ax5_case(("isz", "x"), "isz x -> A. x isz x")


def case_ax5_binder_position():
    """x occurs in the formula only as the variable a binder binds.  mm0.md's
    Vars and mm0-rs count it; mm0-c does not."""
    return ax5_case(("wal", "x", ("weq", "y", "y")), "A. x y = y -> A. x A. x y = y")


def case_ax5_context_layout():
    """theorem t (a: wff) {y z: set}: $ y = z -> A. y y = z $ by ax5 y (y = z).
    mm0.mm0 VerifyProofThm and mm0.lean safe_subst state the side condition
    over the surrounding context instead of ax5's binders; read literally it
    is vacuous here, since position 0 of the context is a regular variable."""
    T = Theory()
    env = {"a": 0, "y": 1, "z": 2}
    arg = ("weq", "y", "z")
    proof = [(REF, 1)] + T.build(arg, env)[:-1] + [(TERM_SAVE, T.t["weq"])]           # heap 3
    proof += [(REF, 3), (REF, 1), (REF, 3), (TERM, T.t["wal"]), (TERM, T.t["wi"]), (THM, T.thm["ax5"])]
    T.theorem("t", [reg(0), bound(1, 0), bound(1, 1)], ["a", "y", "z"], [], ("wi", arg, ("wal", "y", arg)), proof)
    return T.m.build(), spec_with("theorem t (a: wff) {y z: set}: $ y = z -> A. y y = z $;")


def case_dv_regular_binder_first():
    """axiom ax5r (a: wff) {x: set}: $ a -> A. x a $ applied with a := y = z,
    x := y.  mm0.md:379 checks a bound vi against binders after it ("vj
    comes after vi"); a regular binder before it cannot depend on it, so the
    pair needs the check as well (mm0-c makes it; y = z -> A. y y = z is
    false in a two-element model)."""
    T = Theory()
    T.axiom("ax5r", [reg(0), bound(1, 0)], ["a", "x"], [], ("wi", "a", ("wal", "x", "a")))
    W = ("weq", "y", "z")
    proof = [(REF, 1), (REF, 2), (TERM_SAVE, T.t["weq"]), (REF, 1)]                   # W heap 3, x := y
    proof += [(REF, 3), (REF, 1), (REF, 3), (TERM, T.t["wal"]), (TERM, T.t["wi"]), (THM, T.thm["ax5r"])]
    T.theorem("t", [reg(0), bound(1, 0), bound(1, 1)], ["b", "y", "z"], [], ("wi", W, ("wal", "y", W)), proof)
    return T.m.build(), spec_with("axiom ax5r (a: wff) {x: set}: $ a -> A. x a $;",
                                  "theorem t (b: wff) {y z: set}: $ y = z -> A. y y = z $;")


def case_bound_not_variable():
    T = Theory()
    env = {"x": 0}
    concl = ("wi", ("weq", "x", "x"), ("wal", "x", ("weq", "x", "x")))
    proof = T.build(("z",), env) + T.build(("weq", "x", "x"), env) + T.build(concl, env) + \
        [(THM, T.thm["ax5"])]
    T.theorem("t5", [bound(1, 0)], ["x"], [], concl, proof)
    return T.m.build(), spec_with("theorem t5 {x: set}: $ x = x -> A. x x = x $;")


def case_conv_ref():
    """ConvCut proves X = X, ConvSave keeps it, and Ref of the saved cell
    discharges a second obligation X =?= X (mmb.md "Ref", ConvRef)."""
    T = Theory()
    env = {"a": 0, "b": 1}
    concl = ("wi", "a", ("wi", "b", "a"))
    proof = T.build(concl, env)[:-1] + [(TERM_SAVE, T.t["wi"])]             # X, heap 2
    proof += [(REF, 2), (REF, 0), (REF, 1), (REF, 2), (THM_SAVE, T.thm["ax1"])]  # |- X, heap 3
    proof += [CONV, CONV_CUT, REFL, CONV_SAVE]                               # heap 4: X = X
    proof += [CONV, (REF, 4)]
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], concl, proof)
    return T.m.build(), spec_with("theorem t (a b: wff): $ a -> b -> a $;")


def case_conv_ref_wrong_cell():
    """The saved cell X = X cannot discharge Y =?= X for a copy Y."""
    T = Theory()
    env = {"a": 0, "b": 1}
    concl = ("wi", "a", ("wi", "b", "a"))
    proof = T.build(concl, env)                                               # Y
    proof += T.build(concl, env)[:-1] + [(TERM_SAVE, T.t["wi"])]              # X, heap 2
    proof += [(REF, 0), (REF, 1), (REF, 2), (THM_SAVE, T.thm["ax1"])]
    proof += [CONV, CONV_CUT, REFL, CONV_SAVE]
    proof += [CONV, (REF, 4)]
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], concl, proof)
    return T.m.build(), spec_with("theorem t (a b: wff): $ a -> b -> a $;")


def case_sorry():
    T = Theory()
    env = {"a": 0, "b": 1}
    concl = ("wi", "a", ("wi", "b", "a"))
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], concl, T.build(concl, env) + [SORRY])
    return T.m.build(), spec_with("theorem t (a b: wff): $ a -> b -> a $;")


def case_statement_mismatch():
    T = Theory()
    env = {"a": 0, "b": 1}
    concl = ("wi", "a", ("wi", "b", "a"))
    proof = [(REF, 0), (REF, 1)] + T.build(concl, env) + [(THM, T.thm["ax1"])]
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], ("wi", "b", ("wi", "a", "b")), proof)
    return T.m.build(), spec_with("theorem t (a b: wff): $ b -> a -> b $;")


def case_dummy_strict():
    T = Theory()
    T.theorem("t", [reg(0)], ["a"], [], ("wi", "a", "a"),
              [(DUMMY, 0), (REF, 0), (REF, 0), (REF, 0), (TERM, T.t["wi"]), (THM, T.thm["ax_id"])])
    return T.m.build(), spec_with("theorem t (a: wff): $ a -> a $;")


AX1 = ("wi", "a", ("wi", "b", "a"))
AX1_SPEC = "theorem t (a b: wff): $ a -> b -> a $;"


def ax1_proof(T):
    """ax1 a b, for theorem t (a b: wff): $ a -> b -> a $."""
    env = {"a": 0, "b": 1}
    return [(REF, 0), (REF, 1)] + T.build(AX1, env) + [(THM, T.thm["ax1"])]


def id_proof(T, a=0):
    """ax_id a: |- a -> a, for a formula variable at heap index a."""
    return [(REF, a), (REF, a), (REF, a), (TERM, T.t["wi"]), (THM, T.thm["ax_id"])]


def with_wa(T):
    """term wa (a b: wff): wff, written /\\ in the specification."""
    T.t["wa"] = T.m.term([reg(0), reg(0)], ret(0), "wa")
    return "term wa (a b: wff): wff; infixl wa: $/\\$ prec 34;"


def case_statement_wrong_term():
    """The proof proves a -> b -> a; the statement claims a /\\ (b -> a)."""
    T = Theory()
    decl = with_wa(T)
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], ("wa", "a", ("wi", "b", "a")), ax1_proof(T))
    return T.m.build(), spec_with(decl, "theorem t (a b: wff): $ a /\\ (b -> a) $;")


def case_uref_out_of_range():
    """A statement refers to the heap slot just past the arguments."""
    T = Theory()
    T.m.theorem([reg(0)], [(UTERM, T.t["wi"]), (UREF, 0), (UREF, 1), END], id_proof(T) + [END], name="t")
    return T.m.build(), spec_with("theorem t (a: wff): $ a -> a $;")


def case_statement_dummy():
    """UDummy is a definition command; a theorem statement may not bind a
    proof dummy (here |- a -> A. y a for the proof's dummy y)."""
    T = Theory()
    t = T.t
    proof = [(DUMMY, 1), (REF, 0), (REF, 0), (REF, 1), (REF, 0), (TERM, t["wal"]), (TERM, t["wi"]),
             (THM, T.thm["ax5"]), END]
    stream = [(UTERM, t["wi"]), (UREF, 0), (UTERM, t["wal"]), (UDUMMY, 1), (UREF, 0), END]
    T.m.theorem([reg(0)], stream, proof, name="t")
    return T.m.build(), spec_with("theorem t (a: wff): $ a -> A. a a $;")


def case_def_dummy_wrong_sort():
    """def d: wff = A. y y = y whose UDummy names sort wff for the set dummy
    y (without a specification, whose matcher checks the sort as well)."""
    T = Theory()
    t = T.t
    T.m.define([], ret(0), [(UTERM, t["wal"]), (UDUMMY, 0), (UTERM, t["weq"]), (UREF, 0), (UREF, 0), END],
               [(DUMMY, 1), (REF, 0), (REF, 0), (TERM, t["weq"]), (TERM, t["wal"]), END], name="d")
    return T.m.build(), None


def define_f(T):
    """def f {x: set} {.y: set}: wff x = $ A. y y = x $"""
    t = T.t
    t["f"] = T.m.define([bound(1, 0)], ret(0, 1),
                        [(UTERM, t["wal"]), (UDUMMY, 1), (UTERM, t["weq"]), (UREF, 1), (UREF, 0), END],
                        [(DUMMY, 1), (REF, 1), (REF, 0), (TERM, t["weq"]), (TERM, t["wal"]), END], name="f")
    return "def f {x: set} {.y: set}: wff x = $ A. y y = x $;"


def case_unfold_dummy_not_fresh():
    """Unfolding f b with b itself as the dummy's image: A. b b = b is not
    A. y y = b.  An image must be fresh for the arguments."""
    T = Theory()
    decl = define_f(T)
    t = T.t
    W = ("wal", "b", ("weq", "b", "b"))
    proof = [(REF, 0), (TERM_SAVE, t["f"])]                                             # F, heap 1
    proof += [(REF, 0), (REF, 0), (REF, 0), (TERM, t["weq"]), (TERM_SAVE, t["wal"])]   # W, heap 2
    proof += [(TERM, t["wi"])] + id_proof(T, 2)                                         # F -> W; |- W -> W
    proof += [CONV, CONG, (REF, 2), UNFOLD, REFL, REFL]
    T.theorem("bad", [bound(1, 0)], ["b"], [], ("wi", ("f", "b"), W), proof)
    return T.m.build(), spec_with(decl, "theorem bad {b: set}: $ f b -> A. b b = b $;")


def define_h(T):
    """def h {.y: set}: wff = $ (A. y y = y) -> (A. y y = y) $ with the
    repeated subterm saved by UTermSave"""
    t = T.t
    t["h"] = T.m.define([], ret(0),
                        [(UTERM, t["wi"]), (UTERM_SAVE, t["wal"]), (UDUMMY, 1), (UTERM, t["weq"]),
                         (UREF, 1), (UREF, 1), (UREF, 0), END],
                        [(DUMMY, 1), (REF, 0), (REF, 0), (TERM, t["weq"]), (TERM_SAVE, t["wal"]),
                         (REF, 1), (TERM, t["wi"]), END], name="h")
    return "def h {.y: set}: wff = $ (A. y y = y) -> (A. y y = y) $;"


def case_unfold_saved_dummy_term():
    """Unfolding h onto (A. y y = y) -> (A. y y = y) for the theorem's own
    variable y.  The saved subterm contains the image; only arguments and
    earlier images must avoid it (mmb.md UDummy; mm0-c skips saved terms)."""
    T = Theory()
    decl = define_h(T)
    t = T.t
    A = ("wal", "y", ("weq", "y", "y"))
    proof = [(TERM, t["h"])]
    proof += [(REF, 0), (REF, 0), (REF, 0), (TERM, t["weq"]), (TERM_SAVE, t["wal"])]   # A, heap 1
    proof += [(REF, 1), (TERM_SAVE, t["wi"]), (TERM, t["wi"])] + id_proof(T, 2)          # W, heap 2
    proof += [CONV, CONG, (REF, 2), UNFOLD, REFL, REFL]
    T.theorem("th", [bound(1, 0)], ["y"], [], ("wi", ("h",), ("wi", A, A)), proof)
    return T.m.build(), spec_with(decl, "theorem th {y: set}: $ h -> (A. y y = y) -> (A. y y = y) $;")


def case_def_unaccounted():
    """def bad {y: set}: wff = $ all2 y (y = y) $, where all2's second
    argument does not depend on its first: y is free in the value."""
    T = Theory()
    t = T.t
    t["all2"] = T.m.term([bound(1, 0), reg(0)], ret(0), "all2")
    T.m.define([bound(1, 0)], ret(0), [(UTERM, t["all2"]), (UREF, 0), (UTERM, t["weq"]), (UREF, 0), (UREF, 0), END],
               [(REF, 0), (REF, 0), (REF, 0), (TERM, t["weq"]), (TERM, t["all2"]), END], name="bad")
    return T.m.build(), spec_with("term all2 {x: set} (a: wff): wff;", "def bad {y: set}: wff = $ all2 y (y = y) $;")


def case_def_hidden_dependency():
    """def g {y: set}: wff = $ isz y $: isz's value depends on its bound
    argument, so g's value depends on y."""
    T = Theory()
    t = T.t
    T.m.define([bound(1, 0)], ret(0), [(UTERM, t["isz"]), (UREF, 0), END], [(REF, 0), (TERM, t["isz"]), END], name="g")
    return T.m.build(), spec_with("def g {y: set}: wff = $ isz y $;")


def case_argument_sort():
    """weq applied to formulas (without a specification, whose parser
    rejects the statement too)."""
    T = Theory()
    w = ("weq", "a", "a")
    T.theorem("t", [reg(0)], ["a"], [], ("wi", w, w), [(REF, 0), (REF, 0), (TERM_SAVE, T.t["weq"]), (REF, 1), (REF, 1),
                                                         (TERM, T.t["wi"]), (THM, T.thm["ax_id"])])
    return T.m.build(), None


def case_term_out_of_range():
    """A proof uses a term declared after it."""
    T = Theory()
    later = len(T.m.terms)
    T.m.theorem([reg(0)], [(UTERM, T.t["wi"]), (UTERM, later), (UTERM, later), END],
                [(TERM_SAVE, later), (REF, 1), (REF, 1), (TERM, T.t["wi"]), (THM, T.thm["ax_id"]), END], name="t")
    T.m.term([], ret(0), "c0")
    return T.m.build(), spec_with("theorem t (a: wff): $ c0 -> c0 $;", "term c0: wff;")


def case_theorem_cites_itself():
    """theorem t (a: wff): $ a $ by t a."""
    T = Theory()
    T.theorem("t", [reg(0)], ["a"], [], "a", [(REF, 0), (REF, 0), (THM, len(T.m.thms))])
    return T.m.build(), spec_with("theorem t (a: wff): $ a $;")


def case_def_uses_theorem():
    T = Theory()
    T.m.define([reg(0)], ret(0), [(UREF, 0), END], id_proof(T) + [END], name="d")
    return T.m.build(), spec_with("def d (a: wff): wff = $ a $;")


def case_bound_arguments_alias():
    """axiom two {x y: set}: $ A. x A. y x = y $ applied to u u."""
    T = Theory()
    T.axiom("two", [bound(1, 0), bound(1, 1)], ["x", "y"], [], ("wal", "x", ("wal", "y", ("weq", "x", "y"))))
    concl = ("wal", "u", ("wal", "u", ("weq", "u", "u")))
    T.theorem("t", [bound(1, 0)], ["u"], [], concl, [(REF, 0), (REF, 0)] + T.build(concl, {"u": 0}) + [(THM, T.thm["two"])])
    return T.m.build(), spec_with("axiom two {x y: set}: $ A. x A. y x = y $;",
                                  "theorem t {u: set}: $ A. u A. u u = u $;")


def case_ref_out_of_range():
    T = Theory()
    T.theorem("t", [reg(0)], ["a"], [], ("wi", "a", "a"), [(REF, 1)] + id_proof(T))
    return T.m.build(), spec_with("theorem t (a: wff): $ a -> a $;")


def case_conv_ref_wrong_right():
    """The saved cell X = X cannot discharge X =?= Y for a copy Y (the
    cell's right side; conv-ref-wrong-cell tests its left side)."""
    T = Theory()
    env = {"a": 0, "b": 1}
    proof = T.build(AX1, env)                                                 # Y
    proof += T.build(AX1, env)[:-1] + [(TERM_SAVE, T.t["wi"])]                # X, heap 2
    proof += [(REF, 0), (REF, 1), (REF, 2), (THM_SAVE, T.thm["ax1"])]
    proof += [CONV, CONV_CUT, REFL, CONV_SAVE]
    proof += [CONV, SYMM, (REF, 4)]
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], AX1, proof)
    return T.m.build(), spec_with(AX1_SPEC)


def case_dummy_sort_out_of_range():
    T = Theory()
    T.theorem("t", [reg(0)], ["a"], [], ("wi", "a", "a"), [(DUMMY, 9)] + id_proof(T))
    return T.m.build(), spec_with("theorem t (a: wff): $ a -> a $;")


def case_dummy_free_sort():
    T = Theory()
    fs = T.m.sort(FREE, "fs")
    T.theorem("t", [reg(0)], ["a"], [], ("wi", "a", "a"), [(DUMMY, fs)] + id_proof(T))
    return T.m.build(), spec_with("free sort fs;", "theorem t (a: wff): $ a -> a $;")


def case_dummy_limit():
    """mmb.md: at most 55 bound variables; bit 55 of a type is reserved."""
    T = Theory()
    T.theorem("t", [reg(0)], ["a"], [], ("wi", "a", "a"), [(DUMMY, 1)] * 56 + id_proof(T))
    return T.m.build(), spec_with("theorem t (a: wff): $ a -> a $;")


def case_def_hypothesis():
    T = Theory()
    T.m.define([reg(0)], ret(0), [(UREF, 0), END], [(REF, 0), HYP, (REF, 0), END], name="d")
    return T.m.build(), spec_with("def d (a: wff): wff = $ a $;")


def case_hypothesis_not_provable():
    """A hypothesis of sort set (without a specification, which cannot
    state it)."""
    T = Theory()
    T.theorem("t", [bound(1, 0), reg(0)], ["x", "a"], ["x"], ("wi", "a", "a"), [(REF, 0), HYP] + id_proof(T, 1))
    return T.m.build(), None


def case_cong_different_terms():
    """Cong between a /\\ (b -> a) and a -> (b -> a), whose arguments agree."""
    T = Theory()
    decl = with_wa(T)
    t = T.t
    proof = [(REF, 0), (REF, 1), (REF, 0), (TERM_SAVE, t["wi"]), (TERM, t["wa"])]     # a /\ W, W heap 2
    proof += [(REF, 0), (REF, 1), (REF, 0), (REF, 2), (TERM, t["wi"]), (THM, T.thm["ax1"])]
    proof += [CONV, CONG, REFL, REFL]
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], ("wa", "a", ("wi", "b", "a")), proof)
    return T.m.build(), spec_with(decl, "theorem t (a b: wff): $ a /\\ (b -> a) $;")


def case_save_obligation():
    T = Theory()
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], AX1, alias_case(T, True) + [SAVE, REFL])
    return T.m.build(), spec_with(AX1_SPEC)


def case_conv_of_expression():
    """Conv finds an expression where it needs a proof: a =?= a would
    otherwise prove a."""
    T = Theory()
    T.theorem("t", [reg(0)], ["a"], [], "a", [(REF, 0), (REF, 0), CONV, REFL])
    return T.m.build(), spec_with("theorem t (a: wff): $ a $;")


def binder_case(arg):
    """theorem t (a: wff) (q: ARG): $ a -> a $"""
    T = Theory()
    T.theorem("t", [reg(0), arg], ["a", "q"], [], ("wi", "a", "a"), id_proof(T))
    return T.m.build(), spec_with("theorem t (a: wff) (q: set): $ a -> a $;")


def case_binder_sort_out_of_range():
    return binder_case(reg(9))


def case_bound_strict():
    return binder_case(bound(0, 0))


def case_bound_deps():
    return binder_case(bound(1, 1))


def case_regular_deps():
    return binder_case(reg(1, 1))


def case_term_pure_sort():
    T = Theory()
    T.m.term([], ret(T.m.sort(PURE, "ps")), "pt")
    return T.m.build(), spec_with("pure sort ps;", "term pt: ps;")


def case_term_return_sort():
    """The term table's sort byte disagrees with the return type."""
    T = Theory()
    k = T.m.term([], ret(1), "c1")
    data = bytearray(T.m.build())
    data[int.from_bytes(data[16:20], "little") + 8 * k + 2] = 0
    return bytes(data), spec_with("term c1: set;")


def case_def_two_values():
    T = Theory()
    T.m.define([reg(0)], ret(0), [(UREF, 0), END], [(REF, 0), (REF, 0), END], name="d")
    return T.m.build(), spec_with("def d (a: wff): wff = $ a $;")


def case_def_value_sort():
    """def d: wff = z, a value of sort set (without a specification, which
    rejects it too)."""
    T = Theory()
    T.m.define([], ret(0), [(UTERM, T.t["z"]), END], [(TERM, T.t["z"]), END], name="d")
    return T.m.build(), None


def case_proof_trailing_bytes():
    """The proof ends before its statement does."""
    T = Theory()
    stream, _ = T.statement(["a", "b"], [], AX1)
    T.m.theorem([reg(0), reg(0)], stream, ax1_proof(T) + [END, END], name="t")
    return T.m.build(), spec_with(AX1_SPEC)


def case_undischarged_conversion():
    """The proof stops with a =?= (b -> a -> b) still open below |- a."""
    T = Theory()
    Y = ("wi", "b", ("wi", "a", "b"))
    proof = [(REF, 0), (REF, 1), (REF, 0)] + T.build(Y, {"a": 0, "b": 1}) + [(THM, T.thm["ax1"]), CONV]
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], "a", proof)
    return T.m.build(), spec_with("theorem t (a b: wff): $ a $;")


def case_theorem_without_proof():
    T = Theory()
    T.theorem("t", [reg(0)], ["a"], [], "a", [(REF, 0)])
    return T.m.build(), spec_with("theorem t (a: wff): $ a $;")


def case_axiom_not_provable():
    """An axiom concluding a set (without a specification, which cannot
    state it)."""
    T = Theory()
    T.axiom("ax_s", [bound(1, 0)], ["x"], [], "x")
    return T.m.build(), None


def case_statement_unfinished():
    """The statement stream ends with the conclusion half matched (without a
    specification, whose matcher checks this again)."""
    T = Theory()
    T.m.theorem([reg(0), reg(0)], [(UTERM, T.t["wi"]), (UREF, 0), END], ax1_proof(T) + [END], name="t")
    return T.m.build(), None


def case_hypothesis_inside_conclusion():
    """UHyp while the conclusion is half matched."""
    T = Theory()
    stream = [(UTERM, T.t["wi"]), (UREF, 0), UHYP, (UREF, 0), (UREF, 0), END]
    T.m.theorem([reg(0)], stream, [(REF, 0), HYP] + id_proof(T) + [END], name="t")
    return T.m.build(), spec_with("theorem t (a: wff): $ a $ > $ a -> a $;")


def case_undeclared_hypothesis():
    """The proof assumes a; the statement has no hypothesis."""
    T = Theory()
    T.theorem("t", [reg(0)], ["a"], [], "a", [(REF, 0), HYP, (REF, 1)])
    return T.m.build(), spec_with("theorem t (a: wff): $ a $;")


# ---- format ------------------------------------------------------------------

def thm_file():
    return case_thm_ax1()


def patched(data, at, value):
    b = bytearray(data)
    b[at:at + len(value)] = value
    return bytes(b)


def case_bad_magic():
    data, spec = thm_file()
    return patched(data, 0, b"MM0C"), spec


def case_bad_version():
    data, spec = thm_file()
    return patched(data, 4, b"\x02"), spec


def case_too_many_sorts():
    data, spec = thm_file()
    return patched(data, 5, bytes([129])), spec


def case_term_table_out_of_range():
    data, spec = thm_file()
    return patched(data, 16, (len(data) - 4).to_bytes(4, "little")), spec


def case_proof_out_of_range():
    data, spec = thm_file()
    return patched(data, 24, len(data).to_bytes(4, "little")), spec


def case_short_header():
    data, spec = thm_file()
    return data[:30], spec


def case_reserved_bit():
    T = Theory()
    env = {"a": 0}
    T.theorem("t", [reg(0) | (1 << 55)], ["a"], [], ("wi", "a", "a"),
              [(REF, 0)] + T.build(("wi", "a", "a"), env) + [(THM, T.thm["ax_id"])])
    return T.m.build(), spec_with("theorem t (a: wff): $ a -> a $;")


def case_local_term():
    T = Theory()
    T.m.terms.append(([], ret(1), None))
    T.m.stmts.append((S_LOCAL_DEF, None))
    return T.m.build(), BASE_SPEC


def case_unknown_opcode():
    T = Theory()
    T.theorem("t", [reg(0)], ["a"], [], ("wi", "a", "a"), [(0x2A, 0)])
    return T.m.build(), spec_with("theorem t (a: wff): $ a -> a $;")


def case_missing_statement():
    T = Theory()
    T.m.thms.append(T.m.thms[0])     # a table entry with no statement
    return T.m.build(), BASE_SPEC


def case_truncated_stream():
    data, spec = thm_file()
    return data[:len(data) - 6], spec


# ---- specification ------------------------------------------------------------

def spec_case(spec):
    data, _ = thm_file()
    return data, spec


def case_spec_sort_modifiers():
    return spec_case(spec_with("theorem t (a b: wff): $ a -> b -> a $;").replace(
        "strict provable sort wff;", "provable sort wff;"))


def case_spec_short():
    return spec_case(BASE_SPEC)


def case_spec_long():
    return spec_case(spec_with("theorem t (a b: wff): $ a -> b -> a $;",
                               "theorem t2 (a: wff): $ a -> a $;"))


def case_spec_extra_notation():
    return spec_case(spec_with("theorem t (a b: wff): $ a -> b -> a $;", "prefix z: $Z$ prec max;"))


def case_spec_wrong_statement():
    return spec_case(spec_with("theorem t (a b: wff): $ b -> a -> b $;"))


def case_spec_parenthesized():
    return spec_case(spec_with("theorem t (a b: wff): $ (a) -> ((b -> (a))) $;"))


def case_spec_deep_parentheses():
    depth = 200000
    return spec_case(spec_with("theorem t (a b: wff): $ a -> b -> " + "(" * depth + "a" + ")" * depth + " $;"))


def case_spec_associativity():
    """-> is right associative: a -> b -> a is a -> (b -> a)."""
    return spec_case(spec_with("theorem t (a b: wff): $ (a -> b) -> a $;"))


def case_spec_binder_type():
    return spec_case(spec_with("theorem t (a: wff) (b: wff a): $ a -> b -> a $;"))


def case_spec_duplicate_theorem():
    T = Theory()
    env = {"a": 0, "b": 1}
    concl = ("wi", "a", ("wi", "b", "a"))
    for _ in range(2):
        T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], concl,
                  [(REF, 0), (REF, 1)] + T.build(concl, env) + [(THM, T.thm["ax1"])])
    return T.m.build(), spec_with("theorem t (a b: wff): $ a -> b -> a $;",
                                  "theorem t (a b: wff): $ a -> b -> a $;")


def case_spec_variable_shadowing():
    return spec_case(spec_with("theorem t (a a: wff): $ a -> a -> a $;"))


def case_spec_prefix_twice():
    return spec_case(spec_with("prefix z: $Z$ prec max;", "prefix z: $Z$ prec max;",
                               "theorem t (a b: wff): $ a -> b -> a $;"))


def case_spec_precedence_conflict():
    return spec_case(spec_with("infixl weq: $=$ prec 51;", "theorem t (a b: wff): $ a -> b -> a $;"))


def case_spec_input_output():
    return spec_case(spec_with("theorem t (a b: wff): $ a -> b -> a $;", "output s_expr: $ z $;"))


def case_spec_null_byte():
    return spec_case(spec_with("theorem t (a b: wff): $ a -> b -> a $;") + "\0")


def case_many_hypotheses():
    """theorem t (a: wff): $ a $ > ... > $ a $ > $ a $ with 300 hypotheses
    (mm0-c's hypothesis stack holds 256)."""
    T = Theory()
    n = 300
    T.theorem("t", [reg(0)], ["a"], ["a"] * n, "a", [(REF, 0), HYP] * n + [(REF, 1)])
    return T.m.build(), spec_with("theorem t (a: wff): " + "$ a $ > " * n + "$ a $;")


def case_deep_statement():
    """ax_id applied to ((a -> a) -> a) ... -> a, nested 300 deep on the left:
    matching the statement keeps 300 right arguments pending (mm0-c's unify
    stack holds 256)."""
    T = Theory()
    n = 300
    D, text = "a", "a"
    for _ in range(n):
        D, text = ("wi", D, "a"), f"({text} -> a)"
    proof = [(REF, 0)] + [(REF, 0), (TERM, T.t["wi"])] * (n - 1) + [(REF, 0), (TERM_SAVE, T.t["wi"])]  # D, heap 1
    proof += [(REF, 1), (REF, 1), (TERM, T.t["wi"]), (THM, T.thm["ax_id"])]
    T.theorem("t", [reg(0)], ["a"], [], ("wi", D, D), proof)
    return T.m.build(), spec_with(f"theorem t (a: wff): $ {text} -> {text} $;")


def case_spec_wrong_head():
    """The file proves a -> b -> a; the specification states a /\\ (b -> a)."""
    T = Theory()
    decl = with_wa(T)
    T.theorem("t", [reg(0), reg(0)], ["a", "b"], [], AX1, ax1_proof(T))
    return T.m.build(), spec_with(decl, "theorem t (a b: wff): $ a /\\ (b -> a) $;")


def case_spec_variable_for_constant():
    """The file proves z = z -> z = z; the specification states x = x -> x = x
    for a variable whose index equals the term index of z."""
    T = Theory()
    t = T.t
    assert t["z"] == 3
    w = ("weq", ("z",), ("z",))
    proof = [(TERM, t["z"]), (TERM, t["z"]), (TERM_SAVE, t["weq"]), (REF, 4), (REF, 4), (TERM, t["wi"]),
             (THM, T.thm["ax_id"])]
    T.theorem("t", [reg(0), reg(0), reg(0), bound(1, 0)], ["a", "b", "c", "x"], [], ("wi", w, w), proof)
    return T.m.build(), spec_with("theorem t (a b c: wff) {x: set}: $ x = x -> x = x $;")


def case_spec_missing_hypothesis():
    return mp_proof(Theory(), [0, 1])[0], spec_with("theorem mp2 (a b: wff): $ a -> b $ > $ b $;")


def case_spec_extra_hypothesis():
    return spec_case(spec_with("theorem t (a b: wff): $ a $ > $ a -> b -> a $;"))


def unused_binder_file(args, names):
    """theorem t ARGS: $ a -> a $ for the formula variable a"""
    T = Theory()
    T.theorem("t", args, names, [], ("wi", "a", "a"), id_proof(T, names.index("a")))
    return T.m.build()


def case_spec_unused_binder_type():
    return (unused_binder_file([reg(0), reg(0)], ["a", "b"]),
            spec_with("theorem t (a: wff) (b: set): $ a -> a $;"))


def case_spec_binder_dependencies():
    return (unused_binder_file([bound(1, 0), reg(0)], ["x", "a"]),
            spec_with("theorem t {x: set} (a: wff x): $ a -> a $;"))


def case_spec_bound_dependencies():
    data, _ = case_ax5_fresh()
    return data, spec_with("theorem t5 {x: set} {y: set x}: $ y = y -> A. x y = y $;")


def case_spec_theorem_dummy():
    return spec_case(spec_with("theorem t {.d: set} (a b: wff): $ a -> b -> a $;"))


def extra_terms_file():
    """term c1: set; term c2 (a: wff): set;"""
    T = Theory()
    T.m.term([], ret(1), "c1")
    T.m.term([reg(0)], ret(1), "c2")
    return T.m.build()


def case_spec_arrow_types():
    return extra_terms_file(), spec_with("term c1: set;", "term c2: wff > set;")


def case_spec_arrow_type():
    return extra_terms_file(), spec_with("term c1: set;", "term c2: set > set;")


def case_spec_return_type():
    return extra_terms_file(), spec_with("term c1: wff;", "term c2 (a: wff): set;")


def case_spec_return_dependencies():
    return spec_case(spec_with(AX1_SPEC).replace("def isz {x: set}: wff x =", "def isz {x: set}: wff ="))


def case_spec_missing_argument():
    return extra_terms_file(), spec_with("term c1: set;", "term c2: set;")


def case_spec_term_for_def():
    return spec_case(spec_with(AX1_SPEC).replace("def isz {x: set}: wff x = $ x = z $;", "term isz {x: set}: wff x;"))


def case_spec_def_for_term():
    return spec_case(spec_with(AX1_SPEC).replace("term z: set;", "def z: set;"))


def case_spec_axiom_for_theorem():
    return spec_case(spec_with("axiom t (a b: wff): $ a -> b -> a $;"))


def case_spec_duplicate_term():
    T = Theory()
    T.m.term([], ret(1), "c1")
    T.m.term([], ret(1), "c1")
    return T.m.build(), spec_with("term c1: set;", "term c1: set;")


def case_spec_notation():
    return spec_case(spec_with(AX1_SPEC, "notation wi (a b: wff): wff = ($[$:max) a ($,$:0) b ($]$:0);"))


def case_spec_notation_variable_twice():
    return spec_case(spec_with(AX1_SPEC, "notation wi (a b: wff): wff = ($[$:max) a ($,$:0) a ($]$:0);"))


def case_spec_notation_unused_variable():
    return spec_case(spec_with(AX1_SPEC, "notation wi (a b: wff): wff = ($[$:max) a ($]$:0);"))


def case_spec_notation_variable_twice_misread():
    """[ b , a , b -> a ] under a notation that repeats a: mm0.mm0's
    ptCheckNotaLsV needs both a-positions to elaborate alike, so the formula
    has no elaboration.  mm0-c keeps the last one and reads a -> b -> a."""
    return spec_case(spec_with("notation wi (a b: wff): wff = ($[$:max) a ($,$:0) a ($,$:0) b ($]$:0);",
                               "theorem t (a b: wff): $ [ b , a , b -> a ] $;"))


def case_spec_infix_twice():
    return spec_case(spec_with(AX1_SPEC, "infixl weq: $=$ prec 50;"))


def case_spec_infix_nullary():
    return spec_case(spec_with(AX1_SPEC, "infixl z: $+$ prec 60;"))


def case_spec_associativity_conflict():
    return spec_case(spec_with(AX1_SPEC, "infixr weq: $==$ prec 50;"))


def coercion_case(*coercions):
    """Sorts nat, int, pv (provable), sa, sb and unary terms between them;
    the specification adds `coercions`."""
    T = Theory()
    nat, int_, pv, sa, sb = (T.m.sort(PROVABLE if n == "pv" else 0, n) for n in ("nat", "int", "pv", "sa", "sb"))
    terms = (("n2i", nat, int_), ("i2n", int_, nat), ("n2j", nat, int_), ("n2p", nat, pv), ("n2w", nat, 0),
             ("a2i", sa, int_), ("a2b", sa, sb), ("n2b", nat, sb))
    names = {nat: "nat", int_: "int", pv: "pv", sa: "sa", sb: "sb", 0: "wff"}
    for name, a, r in terms:
        T.m.term([reg(a)], ret(r), name)
    decls = ["sort nat;", "sort int;", "provable sort pv;", "sort sa;", "sort sb;"]
    decls += [f"term {name} (a: {names[a]}): {names[r]};" for name, a, r in terms]
    return T.m.build(), spec_with(*decls, *coercions)


def case_spec_coercions():
    return coercion_case("coercion n2i: nat > int;", "coercion n2p: nat > pv;", "coercion a2b: sa > sb;")


def case_spec_coercion_type():
    return coercion_case("coercion n2i: int > nat;")


def case_spec_coercion_cycle():
    return coercion_case("coercion n2i: nat > int;", "coercion i2n: int > nat;")


def case_spec_coercion_diamond():
    return coercion_case("coercion n2i: nat > int;", "coercion n2j: nat > int;")


def case_spec_coercion_undirected_cycle():
    """nat > int, sa > int, sa > sb, nat > sb: no two paths join one pair
    of sorts, but mm0.md requires the undirected graph to be acyclic."""
    return coercion_case("coercion n2i: nat > int;", "coercion a2i: sa > int;",
                         "coercion a2b: sa > sb;", "coercion n2b: nat > sb;")


def case_spec_coercion_provable():
    return coercion_case("coercion n2p: nat > pv;", "coercion n2w: nat > wff;")


CASES = [
    # name, builder, expected status, expected reason fragment
    ("thm-ax1", case_thm_ax1, "OK", None),
    ("mp", case_mp, "OK", None),
    ("mp-premise-order", case_mp_order, "REFUSED", "unify failure"),
    ("premise-cited-twice", case_dup_premise, "OK", None),
    ("premise-missing", case_missing_premise, "REFUSED", None),
    ("hypothesis-missing", case_missing_hyp, "REFUSED", "hypothesis"),
    ("alias-shared-refl", case_alias_shared_refl, "OK", None),
    ("alias-fresh-refl", case_alias_fresh_refl, "REFUSED", "Refl unify failure"),
    ("alias-fresh-cong", case_alias_fresh_cong, "OK", None),
    ("uref-shared", case_uref_shared, "OK", None),
    ("uref-copy", case_uref_copy, "REFUSED", "unify failure at ref"),
    ("unfold", case_unfold, "OK", None),
    ("unfold-not-def", case_unfold_not_def, "REFUSED", "not a definition"),
    ("unfold-fresh-target", case_unfold_fresh_target, "REFUSED", None),
    ("ax5-fresh", case_ax5_fresh, "OK", None),
    ("ax5-disjoint", case_ax5_disjoint, "REFUSED", "disjoint variable violation"),
    ("ax5-definition-dependency", case_ax5_definition_dependency, "REFUSED", "disjoint variable violation"),
    ("ax5-binder-position", case_ax5_binder_position, "REFUSED", "disjoint variable violation"),
    ("ax5-context-layout", case_ax5_context_layout, "REFUSED", "disjoint variable violation"),
    ("dv-regular-binder-first", case_dv_regular_binder_first, "REFUSED", "disjoint variable violation"),
    ("bound-argument-not-variable", case_bound_not_variable, "REFUSED", "type mismatch"),
    ("conv-save-ref", case_conv_ref, "OK", None),
    ("conv-ref-wrong-cell", case_conv_ref_wrong_cell, "REFUSED", "ConvRef unify error"),
    ("sorry", case_sorry, "REFUSED", "Sorry"),
    ("statement-mismatch", case_statement_mismatch, "REFUSED", "unify failure"),
    ("dummy-strict-sort", case_dummy_strict, "REFUSED", "strict"),
    ("bad-magic", case_bad_magic, "MALFORMED", None),
    ("bad-version", case_bad_version, "MALFORMED", None),
    ("too-many-sorts", case_too_many_sorts, "MALFORMED", None),
    ("term-table-out-of-range", case_term_table_out_of_range, "MALFORMED", None),
    ("proof-out-of-range", case_proof_out_of_range, "MALFORMED", None),
    ("short-header", case_short_header, "MALFORMED", None),
    ("reserved-type-bit", case_reserved_bit, "MALFORMED", "reserved"),
    ("local-term", case_local_term, "MALFORMED", "local term"),
    ("unknown-opcode", case_unknown_opcode, "MALFORMED", "unknown opcode"),
    ("table-entry-without-statement", case_missing_statement, "MALFORMED", "not all theorems"),
    ("truncated-stream", case_truncated_stream, "MALFORMED", None),
    ("spec-sort-modifiers", case_spec_sort_modifiers, "REFUSED", "sort modifiers"),
    ("spec-short", case_spec_short, "REFUSED", "invalid command keyword"),
    ("spec-long", case_spec_long, "REFUSED", None),
    ("spec-trailing-notation", case_spec_extra_notation, "OK", None),
    ("spec-wrong-statement", case_spec_wrong_statement, "REFUSED", None),
    ("spec-parentheses", case_spec_parenthesized, "OK", None),
    ("spec-deep-parentheses", case_spec_deep_parentheses, "OK", None),
    ("spec-associativity", case_spec_associativity, "REFUSED", None),
    ("spec-binder-type", case_spec_binder_type, "REFUSED", None),
    ("spec-duplicate-theorem", case_spec_duplicate_theorem, "REFUSED", "duplicate"),
    ("spec-variable-shadowing", case_spec_variable_shadowing, "REFUSED", "shadowing"),
    ("spec-prefix-twice", case_spec_prefix_twice, "REFUSED", "prefix notation"),
    ("spec-precedence-conflict", case_spec_precedence_conflict, "REFUSED", "precedence"),
    ("spec-input-output", case_spec_input_output, "UNSUPPORTED", "input and output"),
    ("spec-null-byte", case_spec_null_byte, "MALFORMED", "Null character"),
    # Controls added for the mutation qualification (tests/mm0/mutation.py).
    # A None specification runs the file alone (--no-spec).
    ("statement-wrong-term", case_statement_wrong_term, "REFUSED", "unify failure at term"),
    ("uref-out-of-range", case_uref_out_of_range, "REFUSED", "bad ref step"),
    ("statement-dummy", case_statement_dummy, "REFUSED", "Dummy command not allowed"),
    ("def-dummy-wrong-sort", case_def_dummy_wrong_sort, "REFUSED", "unify failure at dummy"),
    ("unfold-dummy-not-fresh", case_unfold_dummy_not_fresh, "REFUSED", "dummy disjoint variable violation"),
    ("unfold-saved-dummy-term", case_unfold_saved_dummy_term, "OK", None),
    ("def-unaccounted", case_def_unaccounted, "REFUSED", "unaccounted dependencies"),
    ("def-hidden-dependency", case_def_hidden_dependency, "REFUSED", "unaccounted dependencies"),
    ("argument-sort", case_argument_sort, "REFUSED", "type mismatch"),
    ("term-out-of-range", case_term_out_of_range, "REFUSED", "term out of range"),
    ("theorem-cites-itself", case_theorem_cites_itself, "REFUSED", "theorem out of range"),
    ("def-uses-theorem", case_def_uses_theorem, "REFUSED", "invalid opcode in def"),
    ("bound-arguments-alias", case_bound_arguments_alias, "REFUSED", "disjoint variable violation"),
    ("ref-out-of-range", case_ref_out_of_range, "REFUSED", "bad ref step"),
    ("conv-ref-wrong-right", case_conv_ref_wrong_right, "REFUSED", "ConvRef unify error"),
    ("dummy-sort-out-of-range", case_dummy_sort_out_of_range, "REFUSED", "bad dummy sort"),
    ("dummy-free-sort", case_dummy_free_sort, "REFUSED", "free sort"),
    ("dummy-limit", case_dummy_limit, "REFUSED", "MMB limit 55"),
    ("def-hypothesis", case_def_hypothesis, "REFUSED", "invalid opcode in def"),
    ("hypothesis-not-provable", case_hypothesis_not_provable, "REFUSED", "provable sort"),
    ("cong-different-terms", case_cong_different_terms, "REFUSED", "Cong unify error"),
    ("save-obligation", case_save_obligation, "REFUSED", "Can't save proof obligation"),
    ("conv-of-expression", case_conv_of_expression, "REFUSED", "bad stack slot"),
    ("binder-sort-out-of-range", case_binder_sort_out_of_range, "REFUSED", "bad binder sort"),
    ("bound-binder-strict", case_bound_strict, "REFUSED", "bound variable in strict sort"),
    ("bound-binder-deps", case_bound_deps, "REFUSED", "bad binder deps"),
    ("regular-binder-deps", case_regular_deps, "REFUSED", "bad binder deps"),
    ("term-pure-sort", case_term_pure_sort, "REFUSED", "term in pure sort"),
    ("term-return-sort", case_term_return_sort, "REFUSED", "bad return type"),
    ("def-two-values", case_def_two_values, "REFUSED", "stack has != one element"),
    ("def-value-sort", case_def_value_sort, "REFUSED", "type mismatch"),
    ("proof-trailing-bytes", case_proof_trailing_bytes, "MALFORMED", "Next statement incorrect"),
    ("undischarged-conversion", case_undischarged_conversion, "REFUSED", "stack has != one element"),
    ("theorem-without-proof", case_theorem_without_proof, "REFUSED", "bad stack slot"),
    ("axiom-not-provable", case_axiom_not_provable, "REFUSED", "provable sort"),
    ("statement-unfinished", case_statement_unfinished, "REFUSED", "unfinished unify stack"),
    ("hypothesis-inside-conclusion", case_hypothesis_inside_conclusion, "REFUSED", "unfinished unify stack"),
    ("undeclared-hypothesis", case_undeclared_hypothesis, "REFUSED", "unfinished hypothesis stack"),
    ("many-hypotheses", case_many_hypotheses, "OK", None),
    ("deep-statement", case_deep_statement, "OK", None),
    ("spec-wrong-head", case_spec_wrong_head, "REFUSED", "unify failure at term"),
    ("spec-variable-for-constant", case_spec_variable_for_constant, "REFUSED", "store type error"),
    ("spec-missing-hypothesis", case_spec_missing_hypothesis, "REFUSED", "hypothesis number mismatch"),
    ("spec-extra-hypothesis", case_spec_extra_hypothesis, "REFUSED", "unfinished hypothesis stack"),
    ("spec-unused-binder-type", case_spec_unused_binder_type, "REFUSED", "variable type does not match"),
    ("spec-binder-dependencies", case_spec_binder_dependencies, "REFUSED", "variable type does not match"),
    ("spec-bound-dependencies", case_spec_bound_dependencies, "REFUSED", "bound variables should not have dependencies"),
    ("spec-theorem-dummy", case_spec_theorem_dummy, "REFUSED", "dummies are only allowed in defs"),
    ("spec-arrow-types", case_spec_arrow_types, "OK", None),
    ("spec-arrow-type", case_spec_arrow_type, "REFUSED", "variable type does not match"),
    ("spec-return-type", case_spec_return_type, "REFUSED", "return type does not match"),
    ("spec-return-dependencies", case_spec_return_dependencies, "REFUSED", "return type does not match"),
    ("spec-missing-argument", case_spec_missing_argument, "REFUSED", "incorrect number of arguments"),
    ("spec-term-for-def", case_spec_term_for_def, "REFUSED", "expecting a term"),
    ("spec-def-for-term", case_spec_def_for_term, "REFUSED", "expecting a def"),
    ("spec-axiom-for-theorem", case_spec_axiom_for_theorem, "REFUSED", "expecting an axiom"),
    ("spec-duplicate-term", case_spec_duplicate_term, "REFUSED", "duplicate"),
    ("spec-notation", case_spec_notation, "OK", None),
    ("spec-notation-variable-twice", case_spec_notation_variable_twice, "REFUSED", "notation variable used twice"),
    ("spec-notation-unused-variable", case_spec_notation_unused_variable, "REFUSED", "does not use every variable"),
    ("spec-notation-variable-twice-misread", case_spec_notation_variable_twice_misread, "REFUSED",
     "notation variable used twice"),
    ("spec-infix-twice", case_spec_infix_twice, "REFUSED", "infix notation"),
    ("spec-infix-nullary", case_spec_infix_nullary, "REFUSED", "binary operator"),
    ("spec-associativity-conflict", case_spec_associativity_conflict, "REFUSED", "left and right assoc"),
    ("spec-coercions", case_spec_coercions, "OK", None),
    ("spec-coercion-type", case_spec_coercion_type, "REFUSED", "coercion type mismatch"),
    ("spec-coercion-cycle", case_spec_coercion_cycle, "REFUSED", "coercion cycle"),
    ("spec-coercion-diamond", case_spec_coercion_diamond, "REFUSED", "coercion diamond"),
    ("spec-coercion-undirected-cycle", case_spec_coercion_undirected_cycle, "REFUSED", "coercion diamond"),
    ("spec-coercion-provable", case_spec_coercion_provable, "REFUSED", "diamond to provable"),
]

# Where mm0-c's verdict differs from the specification's, and why.
KNOWN_MM0C_DIFFERENCES = {
    "ax5-definition-dependency": "mm0-c's theorem-mode V(e) omits bound arguments (unsound)",
    "ax5-binder-position": "mm0-c's theorem-mode V(e) omits bound arguments (mm0.md Vars counts them)",
    "reserved-type-bit": "mm0-c ignores reserved bit 55",
    "local-term": "mm0-c accepts a LocalDef statement for a term",
    "spec-prefix-twice": "mm0-c overwrites the first prefix notation",
    "spec-input-output": "mm0-c reports input/output as an invalid command keyword",
    "spec-null-byte": "mm0-c rejects it as a null character too, with its own exit path",
    "spec-deep-parentheses": "mm0-c's recursive parser may exhaust the C stack",
    "many-hypotheses": "mm0-c's hypothesis stack is fixed at 256 entries",
    "deep-statement": "mm0-c's unify stack is fixed at 256 entries",
    # The formal Valid (mm0.mm0 ElaborateNota) gives notation declarations no
    # validity conditions; mm0.md and mm0-rs add the checks below, mm0-c not.
    "spec-notation-variable-twice": "mm0-c accepts a notation using a variable twice (mm0-rs: 'variable used twice in notation')",
    "spec-notation-variable-twice-misread": "mm0-c keeps the last occurrence; mm0.mm0 ptCheckNotaLsV gives this formula no elaboration",
    "spec-notation-unused-variable": "mm0-c accepts a notation leaving a variable out (mm0-rs: 'variable not used in notation')",
    "spec-infix-twice": "mm0-c overwrites the first infix notation (mm0-rs: 'constant already declared')",
    "spec-coercion-type": "mm0-c and mm0-rs accept a coercion whose term has another type; a use elaborates ill-typed",
    "spec-coercion-undirected-cycle": "mm0.md:250 requires an acyclic undirected coercion graph; mm0-c and mm0-rs check directed paths only",
}


def run(cmd, data, spec, workdir, name):
    """Check `data` against `spec`, or alone when `spec` is None."""
    mmb = workdir / (name + ".mmb")
    mmb.write_bytes(data)
    if spec is None:
        p = subprocess.run(cmd + [str(mmb)], stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
    else:
        mm0 = workdir / (name + ".mm0")
        mm0.write_text(spec)
        with open(mm0, "rb") as f:
            p = subprocess.run(cmd + [str(mmb)], stdin=f, capture_output=True, timeout=120)
    return p.returncode, (p.stdout + p.stderr).decode(errors="replace")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("native")
    ap.add_argument("--mm0c", help="an mm0-c build with its parser")
    ap.add_argument("--mm0c-np", help="an mm0-c build without its parser (-DNO_PARSER), for the cases without a specification")
    ap.add_argument("--keep", type=Path)
    args = ap.parse_args()
    native = [str(Path(args.native).resolve())]
    work = args.keep or Path(tempfile.mkdtemp(prefix="mm0-cases-"))
    work.mkdir(parents=True, exist_ok=True)
    failures = 0
    differences = []
    for name, build, expected, fragment in CASES:
        data, spec = build()
        code, out = run(native + (["--no-spec"] if spec is None else []), data, spec, work, name)
        got = STATUS.get(code, f"exit {code}")
        ok = got == expected and (fragment is None or fragment in out)
        ok = ok and "AddressSanitizer" not in out and "runtime error" not in out
        line = f"{'PASS' if ok else 'FAIL'} {name}: native {got}" + (" (no specification)" if spec is None else "")
        mm0c = args.mm0c if spec is not None else args.mm0c_np
        if mm0c:
            ccode, cout = run([mm0c], data, spec, work, name + ".mm0c")
            cverdict = "OK" if ccode == 0 else f"rejects ({ccode})"
            agree = (ccode == 0) == (expected == "OK")
            line += f"; mm0-c {cverdict}"
            if not agree:
                note = KNOWN_MM0C_DIFFERENCES.get(name, "UNEXPLAINED")
                differences.append((name, note))
                line += f"  [{note}]"
        print(line)
        if not ok:
            failures += 1
            print("   " + out.strip().replace("\n", "\n   "))
    print(f"Mm0MmbSummary {len(CASES)} {failures}")
    if differences:
        unexplained = [n for n, note in differences if note == "UNEXPLAINED"]
        print(f"mm0-c differs from the expected verdict on {len(differences)} cases"
              f" ({len(unexplained)} unexplained)")
        failures += len(unexplained)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
