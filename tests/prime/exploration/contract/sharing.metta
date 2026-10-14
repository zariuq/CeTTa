; Shared and independent choices.  An equation parameter is one suspended
; computation per argument occurrence: every use of it in one branch sees one
; choice, and two argument occurrences are two choices.  Predicted by the
; call-time-choice core (Machines/BranchLocalNeed): samePick_need,
; pairPickPick_need and bothMkc_need, with force_commits for the cell that is
; read again after its first use.

(= (pick) A)
(= (pick) B)
(= (same $x) (Pair $x $x))
(= (pairOf $x $y) (Pair $x $y))

; 1. Choose once, reuse: two answers, both diagonal.
!(same (pick))

; 2. Two occurrences, two independent choices: four answers.
!(pairOf (pick) (pick))

; 3. A field that is computed later is shared like a parameter.
(= (mkc) (S (pick)))
(= (unS (S $x)) $x)
(= (both $v) (Pair (unS $v) (unS $v)))
!(both (mkc))

; 4. `let` binds one value per branch; two lets are two choices.
!(let $x (pick) (Pair $x $x))
!(let $x (pick) (let $y (pick) (Pair $x $y)))

; 5. The parameter used once or not at all makes one choice or none.
(= (first $x $y) $x)
!(first (pick) (pick))
