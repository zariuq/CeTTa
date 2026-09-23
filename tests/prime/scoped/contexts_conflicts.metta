; Conflict boundaries.  The same incompatible assignment of one logical
; variable appears inside a search and inside a deterministic expectation.
; The variable is never silently reassigned; inside a search the conflicting
; candidate contributes nothing and the survivors remain; at a deterministic
; boundary the conflict is exposed once, by try, from the original evaluation.
!(bind! &kb (new-space))
!(add-atom &kb (p a))
!(add-atom &kb (p b))
!(add-atom &kb (q a))
; 1. Compatible refinement: the second constraint intersects.
!(collapse (match &kb (p $x) (match &kb (q $x) (both $x))))
; 2. Inside a search, the conflicting candidate is discarded, survivors stay.
!(collapse (superpose ((let (a $x) (a b) (let (a $x) (a c) (got $x))) (let (a $x) (a b) (let (a $x) (a b) (got $x))) other)))
; 3. At a deterministic boundary the plain form is silent...
!(let (a $x) (a b) (let (a $x) (a c) (got $x)))
; ...and try exposes the conflict once as an absent result of that evaluation.
!(try (let (a $x) (a b) (let (a $x) (a c) (got $x))))
; 4. try of a successful evaluation reports its results, not a re-run.
!(try (let (a $x) (a b) (let (a $x) (a b) (got $x))))
; 5. A fresh lexical binder is a new variable, not a reassignment.
!(let $x a (let $y b (Pair $x $y)))
; 6. A space write is a state change, not an assertion that both values hold.
!(add-atom &kb (counter 1))
!(let $_ (remove-atom &kb (counter 1)) (let $_ (add-atom &kb (counter 2)) (collapse (match &kb (counter $c) $c))))
; 7. No success manufactured from an error: try of an erroring program refutes.
!(try (index-atom (a) 5))
