; Real Backward Chaining — Curried DTL
; =====================================
; Port of hyperon/chaining/dtl/backward/curried.metta
; Recursive proof search via nondeterministic evaluation.
;
; NOTE: Uses nested let instead of let* due to CeTTa let* bug
; (let* drops nondeterministic branches from recursive calls).
; Nested let correctly propagates all branches.

; Peano naturals for depth bound
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

; Backward chainer (curried DTL)
(: syn (-> $a Nat $b $b))

; Base case: direct KB lookup
(= (syn $kb $_ (: $prf $ccln))
   (match $kb (: $prf $ccln) (: $prf $ccln)))

; Recursive step: find rule, recursively prove premise
; Uses nested let (not let*) to preserve all nondeterministic branches
(= (syn $kb (S $k) (: ($prfabs $prfarg) $ccln))
   (let (: $prfabs (-> $prms $ccln))
        (syn $kb $k (: $prfabs (-> $prms $ccln)))
     (let (: $prfarg $prms)
          (syn $kb $k (: $prfarg $prms))
       (: ($prfabs $prfarg) $ccln))))

; =====================================================
; Knowledge base
; =====================================================

!(bind! &kb (new-space))
!(add-atom &kb (: a A))
!(add-atom &kb (: ab (-> A B)))
!(add-atom &kb (: bc (-> B C)))
!(add-atom &kb (: ModusPonens (-> (-> $p $q) (-> $p $q))))

; =====================================================
; Tests
; =====================================================

; Depth 0: axiom lookup
!(assertEqual (syn &kb Z (: a A)) (: a A))

; Depth 2: prove B — should find BOTH proofs:
;   (: (ab a) B)                — direct rule application
;   (: ((ModusPonens ab) a) B)  — via explicit ModusPonens
!(println! (prove-B (collapse (syn &kb (fromNumber 2) (: $prf B)))))

; Depth 3: prove C — should find multiple proof terms
!(println! (prove-C (collapse (syn &kb (fromNumber 3) (: $prf C)))))

; Count proofs
!(let $b (collapse (syn &kb (fromNumber 2) (: $prf B)))
  (println! (B-proofs (size-atom $b))))

!(let $c (collapse (syn &kb (fromNumber 3) (: $prf C)))
  (println! (C-proofs (size-atom $c))))

!(println! "=== Backward chaining complete ===")
