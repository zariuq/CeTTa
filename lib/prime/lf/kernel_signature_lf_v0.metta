; =============================================================================
; kernel_signature_lf_v0.metta
; Vendored from the MeTTapedia proof-kernel curriculum (MettaKernel/kernel, commit 1d882c9f0).
;
; A finite-admitted-signature lambda-Pi-modulo kernel seed.
;
; Source anchors: LF/lambda-Pi typing follows Harper, Honsell, and
; Plotkin's Edinburgh LF calculus (JACM 1993) and the PTS/lambda-cube view in
; Barendregt's "Lambda Calculi with Types".  Conversion is beta/delta whnf in
; the lambda-Pi-modulo tradition of Cousineau-Dowek and Dedukti
; (Assaf-Burel et al.).  Signature admission follows HHP finite signatures and
; Dedukti module discipline.
;
; Implementation notes, not source claims: explicit Bad propagation, finite-list
; first-match lookup, and fail-closed checked definitions/axiom provenance.
;
; The checker does NOT trust ambient sigT / defBody equations.  Constants,
; definitions, and axioms live in an explicit finite signature.  Definitions are
; admitted only when their body checks against their declared type.  Axioms are
; permitted only as explicit ledger entries, and proof terms are reported with
; the axioms they use.
;
; Run:
;   cetta kernel_signature_lf_v0.metta
; =============================================================================

!(import! &self kernel_binding_waist_v1)

; ---------- list helpers -----------------------------------------------------
(= (is-bad $x) (unify $x (Bad $e) True False))

(= (decl-name (DConst $n $T)) $n)
(= (decl-name (DDef $n $T $body)) $n)
(= (decl-name (DThm $n $T $body)) $n)
(= (decl-name (DAxiom $n $T)) $n)

(= (sig-has-name $n SNil) False)
(= (sig-has-name $n (SCons $d $rest))
   (if (== $n (decl-name $d)) True (sig-has-name $n $rest)))

(= (unique-sig SNil) True)
(= (unique-sig (SCons $d $rest))
   (let $hit (sig-has-name (decl-name $d) $rest)
     (if $hit False (unique-sig $rest))))

(= (ax-union ANil $ys) $ys)
(= (ax-union (ACons $x $xs) $ys) (ACons $x (ax-union $xs $ys)))

; ---------- finite signature lookup -----------------------------------------
; Declarations:
;   DConst name type    -- checked constant/constructor
;   DDef   name type body -- checked definition, unfolds during normalization
;   DThm   name type body -- checked opaque theorem/proof constant;
;                           uses of the constant preserve the body's axiom footprint
;   DAxiom name type    -- unchecked postulate, visible in theorem provenance

(= (decl-type-of SNil $c) (Bad unknown-declaration))
(= (decl-type-of (SCons (DConst $c $T) $rest) $x)
   (if (== $x $c) $T (decl-type-of $rest $x)))
(= (decl-type-of (SCons (DDef $c $T $body) $rest) $x)
   (if (== $x $c) $T (decl-type-of $rest $x)))
(= (decl-type-of (SCons (DThm $c $T $body) $rest) $x)
   (if (== $x $c) $T (decl-type-of $rest $x)))
(= (decl-type-of (SCons (DAxiom $c $T) $rest) $x)
   (if (== $x $c) $T (decl-type-of $rest $x)))

(= (def-body-of SNil $c) (Bad unknown-definition))
(= (def-body-of (SCons (DDef $c $T $body) $rest) $x)
   (if (== $x $c) $body (def-body-of $rest $x)))
(= (def-body-of (SCons (DConst $c $T) $rest) $x) (def-body-of $rest $x))
(= (def-body-of (SCons (DThm $c $T $body) $rest) $x) (def-body-of $rest $x))
(= (def-body-of (SCons (DAxiom $c $T) $rest) $x) (def-body-of $rest $x))

(= (is-axiom-name SNil $x) False)
(= (is-axiom-name (SCons (DAxiom $a $T) $rest) $x)
   (if (== $x $a) True (is-axiom-name $rest $x)))
(= (is-axiom-name (SCons (DConst $c $T) $rest) $x) (is-axiom-name $rest $x))
(= (is-axiom-name (SCons (DDef $c $T $body) $rest) $x) (is-axiom-name $rest $x))
(= (is-axiom-name (SCons (DThm $c $T $body) $rest) $x) (is-axiom-name $rest $x))

; ---------- lambda-Pi term machinery ----------------------------------------
; Public capture-avoiding `shift`/`subst` traversal comes from
; `kernel_binding_waist_v1`; this kernel is a client, not a second binding
; implementation.

(= (nf $sig (Var $k)) (Var $k))
(= (nf $sig (Srt $s)) (Srt $s))
(= (nf $sig (Con $x)) (Con $x))
(= (nf $sig (Def $x))
   (let $body (def-body-of $sig $x)
     (if (is-bad $body) (Bad unknown-definition) (nf $sig $body))))
(= (nf $sig (Pi  $A $B)) (Pi  (nf $sig $A) (nf $sig $B)))
(= (nf $sig (Lam $A $b)) (Lam (nf $sig $A) (nf $sig $b)))
(= (nf $sig (Bad $e)) (Bad $e))
(= (nf $sig (App $f $a))
   (case (nf $sig $f)
     (($nf-f
       (case (nf $sig $a)
         (($nf-a (nf-app $sig $nf-f $nf-a))))))))
(= (nf-app $sig $f $a) (unify $f (Lam $A $b) (nf $sig (subst 0 $a $b)) (App $f $a)))

(= (conv-nf $x $y)
   (unify $x (Bad $e) False
     (unify $y (Bad $e) False (== $x $y))))
(= (conv $sig $x $y) (conv-nf (nf $sig $x) (nf $sig $y)))

(= (ctx-nth $i CtxNil) (Bad var-out-of-range))
(= (ctx-nth $i (CtxCons $h $t)) (if (== $i 0) $h (ctx-nth (- $i 1) $t)))
(= (ctx-type $i $ctx) (shift (+ $i 1) 0 (ctx-nth $i $ctx)))

(= (pts-rule $sA $sB)
   (unify $sA (Srt type)
     (unify $sB (Srt type) (Srt type)
       (unify $sB (Srt kind) (Srt kind) (Bad pi-bad-codomain)))
     (Bad pi-domain-not-type)))

(= (infer $sig $ctx (Srt type)) (Srt kind))
(= (infer $sig $ctx (Srt kind)) (Bad kind-is-topsort))
(= (infer $sig $ctx (Bad $e)) (Bad $e))
(= (infer $sig $ctx (Var $i))  (ctx-type $i $ctx))
(= (infer $sig $ctx (Con $c))  (decl-type-of $sig $c))
(= (infer $sig $ctx (Def $c))  (decl-type-of $sig $c))
(= (infer $sig $ctx (Pi $A $B))
   (let $sA (nf $sig (infer $sig $ctx $A))
     (let $sB (nf $sig (infer $sig (CtxCons $A $ctx) $B)) (pts-rule $sA $sB))))
(= (infer-lam $sig $ctx $A $b)
   (let $sA (nf $sig (infer $sig $ctx $A))
     (unify $sA (Srt type)
        (let $tb (infer $sig (CtxCons $A $ctx) $b)
          (if (is-bad (nf $sig $tb)) (Bad lam-body-bad-type) (Pi $A $tb)))
        (Bad lam-domain-not-a-type))))
(= (infer $sig $ctx (Lam $A $b)) (infer-lam $sig $ctx $A $b))
(= (infer-app $sig $ctx $f $a)
   (let $tf (nf $sig (infer $sig $ctx $f))
     (unify $tf (Pi $A $B)
        (if (conv $sig (infer $sig $ctx $a) $A) (subst 0 $a $B) (Bad arg-type-mismatch))
        (Bad applied-a-non-function))))
(= (infer $sig $ctx (App $f $a)) (infer-app $sig $ctx $f $a))
(= (check $sig $ctx $t $ty) (conv $sig (infer $sig $ctx $t) $ty))
(= (check-result $sig $ctx $t $ty) (check $sig $ctx $t $ty))

; ---------- signature admission ---------------------------------------------
(= (sort-ok $s)
   (unify $s (Srt type) True
     (unify $s (Srt kind) True False)))

(= (wf-decl $prefix (DConst $n $T))
   (let $IT (infer $prefix CtxNil $T)
     (let $S (nf $prefix $IT)
       (sort-ok $S))))
(= (wf-decl $prefix (DDef $n $T $body))
   (if (wf-decl $prefix (DConst $n $T))
       (check $prefix CtxNil $body $T)
       False))
(= (wf-decl $prefix (DThm $n $T $body))
   (if (wf-decl $prefix (DConst $n $T))
       (check $prefix CtxNil $body $T)
       False))
(= (wf-decl $prefix (DAxiom $n $T)) (wf-decl $prefix (DConst $n $T)))

(= (wf-sig-prefix $prefix SNil) True)
(= (wf-sig-prefix $prefix (SCons (DConst $n $T) $rest))
   (let $ok (wf-decl $prefix (DConst $n $T))
     (if $ok
       (wf-sig-prefix (SCons (DConst $n $T) $prefix) $rest)
       False)))
(= (wf-sig-prefix $prefix (SCons (DDef $n $T $body) $rest))
   (let $ok (wf-decl $prefix (DDef $n $T $body))
     (if $ok
       (wf-sig-prefix (SCons (DDef $n $T $body) $prefix) $rest)
       False)))
(= (wf-sig-prefix $prefix (SCons (DThm $n $T $body) $rest))
   (let $ok (wf-decl $prefix (DThm $n $T $body))
     (if $ok
       (wf-sig-prefix (SCons (DConst $n $T) $prefix) $rest)
       False)))
(= (wf-sig-prefix $prefix (SCons (DAxiom $n $T) $rest))
   (let $ok (wf-decl $prefix (DAxiom $n $T))
     (if $ok
       (wf-sig-prefix (SCons (DAxiom $n $T) $prefix) $rest)
       False)))

(= (sig-admitted $sig)
   (let $u (unique-sig $sig)
     (if $u (wf-sig-prefix SNil $sig) False)))

; ---------- theorem provenance over checked proof terms ----------------------
(= (decl-axioms-of SNil $sig $x) ANil)
(= (decl-axioms-of (SCons (DThm $c $T $body) $rest) $sig $x)
   (if (== $x $c) (term-axioms $sig $body) (decl-axioms-of $rest $sig $x)))
(= (decl-axioms-of (SCons (DConst $c $T) $rest) $sig $x) (decl-axioms-of $rest $sig $x))
(= (decl-axioms-of (SCons (DDef $c $T $body) $rest) $sig $x) (decl-axioms-of $rest $sig $x))
(= (decl-axioms-of (SCons (DAxiom $c $T) $rest) $sig $x) (decl-axioms-of $rest $sig $x))

(= (term-axioms $sig (Var $k)) ANil)
(= (term-axioms $sig (Srt $s)) ANil)
(= (term-axioms $sig (Con $c))
   (if (is-axiom-name $sig $c) (ACons $c ANil) (decl-axioms-of $sig $sig $c)))
(= (term-axioms $sig (Def $c)) ANil)
(= (term-axioms $sig (Pi $A $B)) (ax-union (term-axioms $sig $A) (term-axioms $sig $B)))
(= (term-axioms $sig (Lam $A $b)) (ax-union (term-axioms $sig $A) (term-axioms $sig $b)))
(= (term-axioms $sig (App $f $a)) (ax-union (term-axioms $sig $f) (term-axioms $sig $a)))
(= (term-axioms $sig (Bad $e)) ANil)

(= (kernel-check-checked $sig $term $type True)
   (let $uses (term-axioms $sig $term)
     (Ok (CheckedPrf $uses $term $type))))
(= (kernel-check-checked $sig $term $type False) (Err check-failed))
(= (kernel-check-admitted $sig $term $type True)
   (kernel-check-checked $sig $term $type (check-result $sig CtxNil $term $type)))
(= (kernel-check-admitted $sig $term $type False) (Err signature-not-admitted))
(= (kernel-check $sig $term $type)
   (case (sig-admitted $sig)
     ((True (kernel-check-checked $sig $term $type (check-result $sig CtxNil $term $type)))
      (False (Err signature-not-admitted)))))

; Batch checking keeps the admission fact lexical: `sig-admitted` is computed once
; for the batch, and no public "checked signature" token is exposed or forgeable.
(= (kernel-check-job $sig (Check $term $type))
   (kernel-check-checked $sig $term $type (check-result $sig CtxNil $term $type)))
(= (kernel-check-batch-open $sig Nil) Nil)
(= (kernel-check-batch-open $sig (Cons $job $rest))
   (Cons (kernel-check-job $sig $job) (kernel-check-batch-open $sig $rest)))
(= (kernel-check-batch $sig $jobs)
   (case (sig-admitted $sig)
     ((True (kernel-check-batch-open $sig $jobs))
      (False (Err signature-not-admitted)))))

(= (kernel-check-expect-job $sig (Expect $term $type $expected))
   (== (kernel-check-job $sig (Check $term $type)) $expected))
(= (kernel-check-batch-expect-open $sig Nil) True)
(= (kernel-check-batch-expect-open $sig (Cons $job $rest))
   (if (kernel-check-expect-job $sig $job)
     (kernel-check-batch-expect-open $sig $rest)
     False))
(= (kernel-check-batch-expect $sig $jobs)
   (case (sig-admitted $sig)
     ((True (kernel-check-batch-expect-open $sig $jobs))
      (False False))))

; Compact batch judgment checking avoids retaining the full CheckedPrf payload for
; every job.  Use the one-shot `kernel-check` path when the proof/provenance value
; itself is the artifact under test.
(= (kernel-check-judgment $sig (Accept $term $type))
   (check-result $sig CtxNil $term $type))
(= (kernel-check-judgment $sig (Reject $term $type))
   (if (check-result $sig CtxNil $term $type) False True))
(= (kernel-check-batch-judgments-open $sig Nil) True)
(= (kernel-check-batch-judgments-open $sig (Cons $job $rest))
   (if (kernel-check-judgment $sig $job)
     (kernel-check-batch-judgments-open $sig $rest)
     False))
(= (kernel-check-batch-judgments $sig $jobs)
   (case (sig-admitted $sig)
     ((True (kernel-check-batch-judgments-open $sig $jobs))
      (False False))))

; Scoped derived-theorem chains: check each DThm body once, extend the local
; checking prefix with only its opaque constant interface, then check the batch.
; This keeps the admission fact lexical and exposes no forgeable checked token.
(= (thm-chain-admitted $prefix SNil) True)
(= (thm-chain-admitted $prefix (SCons (DThm $n $T $body) $rest))
   (if (sig-has-name $n $prefix)
     False
     (let $ok (wf-decl $prefix (DThm $n $T $body))
       (if $ok
         (thm-chain-admitted (SCons (DConst $n $T) $prefix) $rest)
         False))))
(= (thm-chain-admitted $prefix (SCons (DConst $n $T) $rest)) False)
(= (thm-chain-admitted $prefix (SCons (DDef $n $T $body) $rest)) False)
(= (thm-chain-admitted $prefix (SCons (DAxiom $n $T) $rest)) False)

(= (thm-chain-prefix $prefix SNil) $prefix)
(= (thm-chain-prefix $prefix (SCons (DThm $n $T $body) $rest))
   (thm-chain-prefix (SCons (DConst $n $T) $prefix) $rest))

(= (kernel-check-batch-judgments-with-thms-open $sig $thms $jobs)
   (case (thm-chain-admitted $sig $thms)
     ((True (kernel-check-batch-judgments-open (thm-chain-prefix $sig $thms) $jobs))
      (False False))))
(= (kernel-check-batch-judgments-with-thms $sig $thms $jobs)
   (case (sig-admitted $sig)
     ((True (kernel-check-batch-judgments-with-thms-open $sig $thms $jobs))
      (False False))))

; ---------- finite curriculum signature --------------------------------------
(= (good-sig)
   (SCons (DConst o (Srt type))
   (SCons (DConst prf (Pi (Con o) (Srt type)))
   (SCons (DConst imp (Pi (Con o) (Pi (Con o) (Con o))))
   (SCons (DConst impI
     (Pi (Con o) (Pi (Con o)
       (Pi (Pi (App (Con prf) (Var 1)) (App (Con prf) (Var 1)))
           (App (Con prf) (App (App (Con imp) (Var 2)) (Var 1)))))))
   (SCons (DConst impE
     (Pi (Con o) (Pi (Con o)
       (Pi (App (Con prf) (App (App (Con imp) (Var 1)) (Var 0)))
         (Pi (App (Con prf) (Var 2)) (App (Con prf) (Var 2)))))))
   (SCons (DConst A (Con o))
   (SCons (DConst B (Con o))
   (SCons (DDef holds (Pi (Con o) (Srt type)) (Lam (Con o) (App (Con prf) (Var 0))))
   (SCons (DConst nat (Srt type))
   (SCons (DConst z (Con nat))
   (SCons (DConst s (Pi (Con nat) (Con nat)))
   (SCons (DConst eqn (Pi (Con nat) (Pi (Con nat) (Srt type))))
   (SCons (DConst rfl (Pi (Con nat) (App (App (Con eqn) (Var 0)) (Var 0))))
   (SCons (DConst set (Srt type))
   (SCons (DConst In (Pi (Con set) (Pi (Con set) (Con o))))
   (SCons (DConst a (Con set))
   (SCons (DConst b (Con set))
   (SCons (DAxiom axMem (App (Con prf) (App (App (Con In) (Con a)) (Con b))))
   (SCons (DAxiom axSym
     (App (Con prf) (App (App (Con imp) (App (App (Con In) (Con a)) (Con b)))
                                        (App (App (Con In) (Con b)) (Con a)))))
          SNil))))))))))))))))))))

(= (bad-def-sig)
   (SCons (DConst o (Srt type))
   (SCons (DConst A (Con o))
   (SCons (DConst falseP (Con o))
   (SCons (DConst prf (Pi (Con o) (Srt type)))
   (SCons (DDef defAsPrf (App (Con prf) (Con falseP)) (Con A))
          SNil))))))

(= (thm-sig)
   (SCons (DConst nat (Srt type))
   (SCons (DConst z (Con nat))
   (SCons (DConst eqn (Pi (Con nat) (Pi (Con nat) (Srt type))))
   (SCons (DConst rfl (Pi (Con nat) (App (App (Con eqn) (Var 0)) (Var 0))))
   (SCons (DThm zRefl
             (App (App (Con eqn) (Con z)) (Con z))
             (App (Con rfl) (Con z)))
          SNil))))))

(= (bad-thm-sig)
   (SCons (DConst nat (Srt type))
   (SCons (DConst z (Con nat))
   (SCons (DConst eqn (Pi (Con nat) (Pi (Con nat) (Srt type))))
   (SCons (DConst rfl (Pi (Con nat) (App (App (Con eqn) (Var 0)) (Var 0))))
   (SCons (DThm badZRefl
             (App (App (Con eqn) (Con z)) (Con z))
             (Con z))
          SNil))))))

(= (dup-sig)
   (SCons (DConst o (Srt type))
   (SCons (DConst o (Srt type))
          SNil)))

(= (forward-ref-sig)
   (SCons (DDef useLater (Con later) (Con later))
   (SCons (DConst later (Srt type))
          SNil)))

(= (inconsistent-ledger-sig)
   (SCons (DConst o (Srt type))
   (SCons (DConst prf (Pi (Con o) (Srt type)))
   (SCons (DConst falseP (Con o))
   (SCons (DConst B (Con o))
   (SCons (DConst falseE
     (Pi (App (Con prf) (Con falseP)) (Pi (Con o) (App (Con prf) (Var 0)))))
   (SCons (DAxiom boom (App (Con prf) (Con falseP)))
          SNil)))))))

(= (thm-axiom-sig)
   (SCons (DConst o (Srt type))
   (SCons (DConst prf (Pi (Con o) (Srt type)))
   (SCons (DConst A (Con o))
   (SCons (DAxiom axA (App (Con prf) (Con A)))
   (SCons (DThm useAx
             (App (Con prf) (Con A))
             (Con axA))
          SNil))))))

; ---------- admission regressions -------------------------------------------
!(assertEqual (sig-admitted (good-sig)) True)
!(assertEqual (sig-admitted (bad-def-sig)) False)
!(assertEqual (sig-admitted (dup-sig)) False)
!(assertEqual (sig-admitted (forward-ref-sig)) False)
!(assertEqual (sig-admitted (inconsistent-ledger-sig)) True)

; Bad is not a type.
!(assertEqual (infer (good-sig) CtxNil (Lam (Srt type) (Var 0))) (Bad lam-domain-not-a-type))
!(assertEqual (check (good-sig) CtxNil (Lam (Con o) (Srt kind)) (Pi (Con o) (Bad kind-is-topsort))) False)

; Binding waist replay: the shared waist preserves this kernel's de Bruijn behavior.
(= (lf-v0-shadow-term) (Lam (Con nat) (Lam (Con nat) (Var 2))))
(= (lf-v0-shadow-replay) (subst 0 (Var 0) (lf-v0-shadow-term)))
!(assertEqual (subst 0 (Con z) (Var 1)) (Var 0))                         ; index decrement
!(assertEqual (subst 0 (Var 0) (Lam (Con nat) (Var 1))) (Lam (Con nat) (Var 1))) ; no capture
!(assertEqual (shift 1 0 (Lam (Con nat) (Var 0))) (Lam (Con nat) (Var 0))) ; bound var fixed under binder
!(assertEqual (shift 1 0 (Lam (Con nat) (Var 1))) (Lam (Con nat) (Var 2))) ; free var shifts under binder
!(assertEqual (lf-v0-shadow-replay) (lf-v0-shadow-term))                  ; nested shadowing

; Delta conversion through an admitted definition.
!(assertEqual (conv (good-sig) (App (Def holds) (Con A)) (App (Con prf) (Con A))) True)

; Entry examples: DTT rfl, HOL implication proofs, beta, HOTG axiom composition.
!(assertEqual
   (kernel-check (good-sig)
     (App (Con rfl) (Con z))
     (App (App (Con eqn) (Con z)) (Con z)))
   (Ok (CheckedPrf ANil (App (Con rfl) (Con z)) (App (App (Con eqn) (Con z)) (Con z)))))

!(assertEqual
   (kernel-check (good-sig)
     (App (App (App (Con impI) (Con A)) (Con A)) (Lam (App (Con prf) (Con A)) (Var 0)))
     (App (Con prf) (App (App (Con imp) (Con A)) (Con A))))
   (Ok (CheckedPrf ANil
     (App (App (App (Con impI) (Con A)) (Con A)) (Lam (App (Con prf) (Con A)) (Var 0)))
     (App (Con prf) (App (App (Con imp) (Con A)) (Con A))))))

!(assertEqual
   (kernel-check (good-sig)
     (App (App (App (Con impI) (Con A)) (App (App (Con imp) (Con B)) (Con A)))
          (Lam (App (Con prf) (Con A))
               (App (App (App (Con impI) (Con B)) (Con A))
                    (Lam (App (Con prf) (Con B)) (Var 1)))))
     (App (Con prf) (App (App (Con imp) (Con A)) (App (App (Con imp) (Con B)) (Con A)))))
   (Ok (CheckedPrf ANil
     (App (App (App (Con impI) (Con A)) (App (App (Con imp) (Con B)) (Con A)))
          (Lam (App (Con prf) (Con A))
               (App (App (App (Con impI) (Con B)) (Con A))
                    (Lam (App (Con prf) (Con B)) (Var 1)))))
     (App (Con prf) (App (App (Con imp) (Con A)) (App (App (Con imp) (Con B)) (Con A)))))))

!(assertEqual
   (kernel-check (good-sig)
     (App (Lam (Con nat) (Var 0)) (Con z))
     (Con nat))
   (Ok (CheckedPrf ANil (App (Lam (Con nat) (Var 0)) (Con z)) (Con nat))))

; Admit-once batch path: one signature admission, many theorem checks, no
; forgeable checked-signature value escapes.
!(assertEqual
   (kernel-check-batch (good-sig)
     (Cons (Check (App (Con rfl) (Con z)) (App (App (Con eqn) (Con z)) (Con z)))
     (Cons (Check (Con z) (Con nat))
     (Cons (Check (Con z) (Con o))
           Nil))))
   (Cons (Ok (CheckedPrf ANil
          (App (Con rfl) (Con z))
          (App (App (Con eqn) (Con z)) (Con z))))
   (Cons (Ok (CheckedPrf ANil (Con z) (Con nat)))
   (Cons (Err check-failed)
         Nil))))

!(assertEqual
   (kernel-check-batch (bad-def-sig)
     (Cons (Check (Def defAsPrf) (App (Con prf) (Con falseP))) Nil))
   (Err signature-not-admitted))

!(assertEqual
   (kernel-check-batch-expect (good-sig)
     (Cons (Expect
       (App (Con rfl) (Con z))
       (App (App (Con eqn) (Con z)) (Con z))
       (Ok (CheckedPrf ANil
         (App (Con rfl) (Con z))
         (App (App (Con eqn) (Con z)) (Con z)))))
     (Cons (Expect (Con z) (Con o) (Err check-failed))
           Nil)))
   True)

!(assertEqual
   (kernel-check-batch-expect (bad-def-sig)
     (Cons (Expect
       (Def defAsPrf)
       (App (Con prf) (Con falseP))
       (Ok (CheckedPrf ANil (Def defAsPrf) (App (Con prf) (Con falseP)))))
       Nil))
   False)

!(assertEqual
   (kernel-check-batch-judgments (good-sig)
     (Cons (Accept
       (App (Con rfl) (Con z))
       (App (App (Con eqn) (Con z)) (Con z)))
     (Cons (Reject (Con z) (Con o))
           Nil)))
   True)

!(assertEqual
   (kernel-check-batch-judgments (bad-def-sig)
     (Cons (Accept (Def defAsPrf) (App (Con prf) (Con falseP))) Nil))
   False)

!(assertEqual
   (thm-chain-admitted (good-sig)
     (SCons (DThm localZRefl
       (App (App (Con eqn) (Con z)) (Con z))
       (App (Con rfl) (Con z)))
       SNil))
   True)

!(assertEqual
   (thm-chain-admitted (good-sig)
     (SCons (DThm z
       (App (App (Con eqn) (Con z)) (Con z))
       (App (Con rfl) (Con z)))
       SNil))
   False)

!(assertEqual
   (thm-chain-admitted (good-sig)
     (SCons (DThm badLocalZRefl
       (App (App (Con eqn) (Con z)) (Con z))
       (Con z))
       SNil))
   False)

!(assertEqual
   (kernel-check-batch-judgments-with-thms (good-sig)
     (SCons (DThm localZRefl
       (App (App (Con eqn) (Con z)) (Con z))
       (App (Con rfl) (Con z)))
       SNil)
     (Cons (Accept
       (Con localZRefl)
       (App (App (Con eqn) (Con z)) (Con z)))
       Nil))
   True)

!(assertEqual
   (kernel-check-batch-judgments-with-thms (bad-def-sig)
     (SCons (DThm localZRefl
       (App (App (Con eqn) (Con z)) (Con z))
       (App (Con rfl) (Con z)))
       SNil)
     (Cons (Accept
       (Con localZRefl)
       (App (App (Con eqn) (Con z)) (Con z)))
       Nil))
   False)

!(assertEqual
   (kernel-check-batch-judgments-with-thms-open (good-sig)
     (SCons (DThm openLocalZRefl
       (App (App (Con eqn) (Con z)) (Con z))
       (App (Con rfl) (Con z)))
       SNil)
     (Cons (Accept
       (Con openLocalZRefl)
       (App (App (Con eqn) (Con z)) (Con z)))
       Nil))
   True)

!(assertEqual (sig-admitted (thm-sig)) True)

!(assertEqual
   (kernel-check (thm-sig)
     (Con zRefl)
     (App (App (Con eqn) (Con z)) (Con z)))
   (Ok (CheckedPrf ANil (Con zRefl) (App (App (Con eqn) (Con z)) (Con z)))))

!(assertEqual
   (kernel-check (thm-axiom-sig)
     (Con useAx)
     (App (Con prf) (Con A)))
   (Ok (CheckedPrf (ACons axA ANil) (Con useAx) (App (Con prf) (Con A)))))

!(assertEqual
   (def-body-of (thm-sig) zRefl)
   (Bad unknown-definition))

!(assertEqual (sig-admitted (bad-thm-sig)) False)

!(assertEqual
   (kernel-check (good-sig)
     (App (App (App (App (Con impE) (App (App (Con In) (Con a)) (Con b)))
                                    (App (App (Con In) (Con b)) (Con a)))
               (Con axSym)) (Con axMem))
     (App (Con prf) (App (App (Con In) (Con b)) (Con a))))
   (Ok (CheckedPrf (ACons axSym (ACons axMem ANil))
     (App (App (App (App (Con impE) (App (App (Con In) (Con a)) (Con b)))
                                    (App (App (Con In) (Con b)) (Con a)))
               (Con axSym)) (Con axMem))
     (App (Con prf) (App (App (Con In) (Con b)) (Con a))))))

; Old break probes no longer break admitted signatures.
!(assertEqual
   (kernel-check (bad-def-sig) (Def defAsPrf) (App (Con prf) (Con falseP)))
   (Err signature-not-admitted))

!(assertEqual
   (kernel-check (inconsistent-ledger-sig)
     (App (App (Con falseE) (Con boom)) (Con B))
     (App (Con prf) (Con B)))
   (Ok (CheckedPrf (ACons boom ANil)
     (App (App (Con falseE) (Con boom)) (Con B))
     (App (Con prf) (Con B)))))

; Wrong proof targets still reject.
!(assertEqual
   (kernel-check (good-sig)
     (App (Con rfl) (Con z))
     (App (App (Con eqn) (Con z)) (App (Con s) (Con z))))
   (Err check-failed))

!(assertEqual
   (kernel-check (good-sig)
     (App (App (App (Con impI) (Con A)) (Con A)) (Lam (App (Con prf) (Con A)) (Var 0)))
     (App (Con prf) (App (App (Con imp) (Con A)) (Con B))))
   (Err check-failed))
