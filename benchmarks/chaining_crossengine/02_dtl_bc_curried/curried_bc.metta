;; Row 02 — Nil Geisweiller: curried DTL backward chainer (syn).
;; Source: repos/ngeiswei-chaining-run/dtl/backward/curried.metta (+ utils.metta, inlined).
;; Single generic recursive clause (curried rules) — contrast with row 03 (n-ary).
;; Contract: prove C at depth 4 yields 4 proofs (per the upstream test's
;; assertEqualToResult). Runs on CeTTa (--lang he) and PeTTa.

;; ---- Nat ----
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

;; ---- Curried backward chainer ----
(: syn (-> $a Nat $b $b))
(= (syn $kb $_ (: $prf $ccln)) (match $kb (: $prf $ccln) (: $prf $ccln)))
(= (syn $kb (S $k) (: ($prfabs $prfarg) $ccln))
   (let* (((: $prfabs (-> $prms $ccln)) (syn $kb $k (: $prfabs (-> $prms $ccln))))
          ((: $prfarg $prms) (syn $kb $k (: $prfarg $prms))))
     (: ($prfabs $prfarg) $ccln)))

;; ---- Knowledge + rule base (curried rules) ----
!(bind! &kb (new-space))
!(add-atom &kb (: ab (→ A B)))
!(add-atom &kb (: bc (→ B C)))
!(add-atom &kb (: a A))
!(add-atom &kb (: ModusPonens (-> (→ $p $q) (-> $p $q))))
!(add-atom &kb (: Deduction (-> (→ $q $r) (-> (→ $p $q) (→ $p $r)))))
!(add-atom &kb (: . (-> (-> $q $r) (-> (-> $p $q) (-> $p $r)))))

;; ---- Query: prove C at depth 4 (4 distinct proofs) ----
!(syn &kb (fromNumber 4) (: $prf C))
