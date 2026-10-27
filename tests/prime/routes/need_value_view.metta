; A computation the evaluator delays by itself has one view, its value.
;
; Call by need gives every use of a variable one value.  That holds for what
; a value contains as well: a field that still computes when evaluation stops
; at a constructor is shared by all who read the value, an answer shows no
; pending computation, and an observer that inspects structure reads the
; value.  The source text of a suspended computation is a view of explicit
; suspensions only (`delay`, quotation, a lambda).
;
; Each directive is tagged with what it observes and how the pending
; computation reaches the observer.  All three routes agree on every line.
(= (add Z $y) $y)
(= (add (S $x) $y) (S (add $x $y)))
(= (mk) (S (add Z Z)))
(= (mkc) (S (superpose (A B))))
(= (dup $x) $x)
(= (dup $x) $x)
(= (one $x) $x)
(= (same $x) (Pair $x $x))
(= (unS (S $x)) $x)
(= (both $v) (Pair (unS $v) (unS $v)))
(= (dupfield (Box $x)) (Pair $x $x))
(= (isOne (S Z)) yes)
(= (g Z) one)
(= (k2 Z $y) one)
(= (eqp $e) (== $e (S Z)))
(= (eqq $e) (== (S Z) $e))
(= (ifp $e) (if (== $e (S Z)) yes no))
(= (unp $e) (unify $e (S Z) yes no))
(= (unq $e) (unify (S Z) $e yes no))
(= (casep $e) (case $e (((S Z) yes) ($other no))))

; 1. An answer is a value, whichever way it was reached.
!(answer/call (add (S Z) Z))
!(answer/two-equations (dup (add (S Z) Z)))
!(answer/let (let $r (dup (add (S Z) Z)) $r))
!(answer/constructor-argument (same (S (add Z Z))))
!(answer/list-argument (same (Cons (add Z Z) Nil)))
!(answer/collapse (collapse (dup (add (S Z) Z))))
!(answer/superpose (superpose ((dup (mk)) (dup (mk)))))

; 2. One variable, one value: a choice inside a value is made once.
!(shared/argument (same (superpose (A B))))
!(shared/constructor-argument (let $r (same (S (superpose (A B)))) $r))
!(shared/two-equations (let $r (dup (S (superpose (A B)))) (Pair $r $r)))
!(shared/equal-to-itself (let $r (dup (S (superpose (A B)))) (== $r $r)))
!(shared/call-result (both (mkc)))
!(shared/field-of-argument (dupfield (Box (superpose (A B)))))
!(shared/taken-apart-twice (both (S (superpose (A B)))))

; 3. An observer of structure reads the value.
!(observer/equation-head (isOne (dup (add (S Z) Z))))
!(observer/case (case (dup (add (S Z) Z)) (((S Z) yes) ($other no))))
!(observer/case-parameter (casep (S (add Z Z))))
!(observer/unify (let $r (dup (add (S Z) Z)) (unify $r (S Z) yes no)))
!(observer/unify-parameter (unp (mk)))
!(observer/unify-pattern-side (unq (mk)))
!(observer/unify-pattern-side-constructor (unq (S (add Z Z))))
!(observer/equality (== (add (S Z) Z) (S Z)))
!(observer/equality-parameter (eqp (add (S Z) Z)))
!(observer/equality-parameter-right (eqq (mk)))
!(observer/equality-under-if (if (== (add (S Z) Z) (S Z)) yes no))
!(observer/equality-under-if-parameter (ifp (S (add Z Z))))
!(observer/syntax-is-not-the-value (let $r (dup (add (S Z) Z)) (unify $r (S (add Z Z)) yes no)))

; 4. A call that no equation answers keeps the values of its arguments.
!(residual/one-argument (g (add (S Z) Z)))
!(residual/two-arguments (k2 (add (S Z) Z) (add Z Z)))
!(residual/observed (let $r (g (add (S Z) Z)) (unify $r (g (S Z)) yes no)))

; 5. Fields stay lazy: what is not demanded is not computed.
(= (loop) (loop))
(= (hd (Cons $a $b)) $a)
(= (hd2 $l) (Pair (hd $l) (hd $l)))
!(lazy/unused-field (hd (Cons 1 (loop))))
!(lazy/unused-field-shared (hd2 (Cons 1 (loop))))
(= (view-cell) (Cons 1 (println! view-unused-field)))
!(lazy/computed-head (hd (view-cell)))

; 6. An explicit suspension is data: its source is its view.
!(explicit/delay (delay (add (S Z) Z)))
!(explicit/quote (quote (add (S Z) Z)))

; 7. A scalar projection can update a cell retained by its caller. Reclaiming
; the projection's temporary region must keep that shared choice. Equal
; alternatives still count twice, while separate calls remain independent.
(= (mkdup) (S (superpose (A A B))))
!(shared/projected-duplicates (both (mkdup)))
!(independent/projected-calls (Pair (unS (mkc)) (unS (mkc))))
(= region-choice (superpose (A B)))
(= (read-region-choice) region-choice)
!(shared/named-projections (Pair (read-region-choice) (read-region-choice)))

; 8. Selecting a branch does not interpret the selected call's operands.
; An undecided judgment retains the registry spelling it was handed, while
; an explicitly bound Space value remains a value in its returned syntax.
(hol:profile base)
(= (view/ask-direct $s) (hol:proves $s (pf:by view/missing) ViewGoal))
(= (view/ask-if $s) (if True (hol:proves $s (pf:by view/missing) ViewGoal) False))
(= (view/ask-case $s)
   (case True ((True (hol:proves $s (pf:by view/missing) ViewGoal)) (False no))))
!(view/ask-direct &self)
!(view/ask-if &self)
!(view/ask-case &self)
!(chain (view/ask-if &self) $r
   (unify $r (hol:proves &self (pf:by view/missing) ViewGoal) source value))
!(chain (let $s &self (if True (hol:proves $s (pf:by view/missing) ViewGoal) False)) $r
   (unify $r (hol:proves &self (pf:by view/missing) ViewGoal) source value))
