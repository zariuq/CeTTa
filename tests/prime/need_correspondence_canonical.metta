; Canonical ABT side of the application-correspondence differential.
(: CorrNat Type)
(: CorrZ CorrNat)
(= (corr:omega) (corr:omega))
(= (corr:status (PrimeVerdict $status $_judgment $_evidence)) $status)

!(App (Lam T (Var 0)) (superpose (A B)))
!(App (Lam T (Pair (Var 0) (Var 0))) (superpose (A A B)))
!(App (Lam T kept-empty) (empty))
!(App (Lam T kept-fault) (== 1 "bad"))
!(App (Lam T kept-divergence) (corr:omega))
!(collapse (App (Lam T (Var 0)) (== 1 "bad")))

; Canonical closure carries the delayed outer argument through a second App.
!(App (App (Lam T (Lam U (Var 1))) (superpose (A B))) ignored)

; Canonical-to-equation transfer.
(= (corr:use $x) $x)
!(App (Lam T (corr:use (Var 0))) (superpose (A B)))

; A demanded effect is shared once.
!(bind! &corr:counter (new-state 0))
(= (corr:bump)
   (let $old (get-state &corr:counter)
     (let $_ (change-state! &corr:counter (+ $old 1)) $old)))
!(App (Lam T (Pair (Var 0) (Var 0))) (corr:bump))
!(get-state &corr:counter)

; Both routes report the same incomplete observation for a demanded diverging
; producer under the same explicit budget.
!(corr:status
   (prime-judge &self
     (Must (PrimeEvaluate
             (App (Lam CorrNat (Var 0)) (corr:omega)))
           CorrNat)
     40))
