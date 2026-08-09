; Canonical ABT side of the application-correspondence differential.
(: CorrNat Type)
(: CorrZ CorrNat)
(= (corr:omega) (corr:omega))
(= (corr:status (PrimeVerdict $status $_judgment $_evidence)) $status)

!(App (Lam T (idx 0)) (superpose (A B)))
!(App (Lam T (Pair (idx 0) (idx 0))) (superpose (A A B)))
!(App (Lam T kept-empty) (empty))
!(App (Lam T kept-fault) (== 1 "bad"))
!(App (Lam T kept-divergence) (corr:omega))
!(reify (App (Lam T (idx 0)) (== 1 "bad")))

; Canonical closure carries the delayed outer argument through a second App.
!(App (App (Lam T (Lam U (idx 1))) (superpose (A B))) ignored)

; Canonical-to-equation transfer.
(= (corr:use $x) $x)
!(App (Lam T (corr:use (idx 0))) (superpose (A B)))

; A demanded effect is shared once in its branch receipt and is not implicitly
; committed to the ambient state cell.
!(bind! &corr:counter (new-state 0))
(= (corr:bump)
   (let $old (get-state &corr:counter)
     (let $_ (change-state! &corr:counter (+ $old 1)) $old)))
!(App (Lam T (Pair (idx 0) (idx 0))) (corr:bump))
!(get-state &corr:counter)

; Both routes report the same incomplete observation for a demanded diverging
; producer under the same explicit budget.
!(corr:status
   (prime-judge &self
     (Must (PrimeEvaluate
             (App (Lam CorrNat (idx 0)) (corr:omega)))
           CorrNat)
     40))
