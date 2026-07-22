; Admitted-equation side of the application-correspondence differential.
(: CorrNat Type)
(: CorrZ CorrNat)
(= (corr:omega) (corr:omega))
(= (corr:status (PrimeVerdict $status $_judgment $_evidence)) $status)
(= (corr:id $x) $x)
(= (corr:dup $x) (Pair $x $x))
(= (corr:ignore-empty $x) kept-empty)
(= (corr:ignore-fault $x) kept-fault)
(= (corr:ignore-divergence $x) kept-divergence)

!(corr:id (superpose (A B)))
!(corr:dup (superpose (A A B)))
!(corr:ignore-empty (empty))
!(corr:ignore-fault (== 1 "bad"))
!(corr:ignore-divergence (corr:omega))
!(collapse (corr:id (== 1 "bad")))

; Equation-to-canonical closure transfer: the RHS closure captures the
; equation argument's thunk and a canonical App later demands it.
(= (corr:make-const $x) (Lam U $x))
!(App (corr:make-const (superpose (A B))) ignored)

; Pure equation application at the opposite mixed boundary.
(= (corr:use $x) $x)
!(corr:use (superpose (A B)))

; A demanded effect is shared once in its branch receipt and is not implicitly
; committed to the ambient state cell.
!(bind! &corr:counter (new-state 0))
(= (corr:bump)
   (let $old (get-state &corr:counter)
     (let $_ (change-state! &corr:counter (+ $old 1)) $old)))
!(corr:dup (corr:bump))
!(get-state &corr:counter)

; Same explicit producer budget and demanded divergence as the ABT route.
!(corr:status
   (prime-judge &self
     (Must (PrimeEvaluate (corr:id (corr:omega))) CorrNat)
     40))
