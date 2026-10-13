; A kernel judgment receives the values of its operands.  An equation
; parameter reaches type:check and type:eq as its argument's normal form,
; the form `let` delivers, never as the cell that stands for it: a derivable
; judgment moved into an equation stays derivable, and an underivable one
; stays refuted.
(hol:profile base)
!(hol:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(add-atom &self (: Wrap (-> (u 0) (u 0))))
!(add-atom &self (: wrap0 (Wrap num)))
(= (check-at $goal) (type:check wrap0 $goal))
(= (check-at-let $goal) (let $g $goal (type:check wrap0 $g)))
(= (refl-at $goal) (type:check (refl zero) $goal))
!(T1 (type:check wrap0 (Wrap num)))
!(T2 (check-at (Wrap num)))
!(T3 (check-at-let (Wrap num)))
!(T4 (type:check (refl zero) (id num zero zero)))
!(T5 (refl-at (id num zero zero)))
!(try (refl-at (id num zero zero)))
(= (check2 $t $goal) (type:check $t $goal))
(= (eq2 $a $b) (type:eq $a $b))
!(W1 (check2 (suc zero) num))
!(W2 (check2 zero num))
!(W3 (eq2 (suc zero) (suc zero)))
!(W4 (eq2 zero (suc zero)))
!(W5 (check2 wrap0 (Wrap (u 0))))
; Call-time choice: a parameter is one value in each branch, however often
; a judgment reads it; two parameters choose independently.
(= (same-twice $x) (type:eq $x $x))
(= (pair-check $x $y) (type:eq $x $y))
(= (choice) (superpose (zero (suc zero))))
!(collapse (same-twice (choice)))
!(collapse (pair-check (choice) (choice)))
