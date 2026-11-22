; The live reading (P-DEF-4): an executable occurrence is decided when it is
; reached, against the current program: in the running query's own code, in a
; running equation body, in a running lambda body and in a nested match body
; alike.  A call already entered keeps the alternatives it selected, a value
; already built stays data, quoted code stays data, and removing the last
; equation returns later occurrences to data.

; The running query's own code, written directly, under collapse, and bound.
!(Direct (let $u (add-atom &self (= (t3 $x) v)) (t3 1)))
!(Collapsed (let $u (add-atom &self (= (t4 $x) w)) (collapse (t4 1))))
!(Inner (let $u (add-atom &self (= (t5 $x) z)) (let $r (t5 1) $r)))
!(Later (t3 1))

; Through an equation translated before, and the same code inlined.
(= (mk) (t6 1))
!(ViaEquation (let $u (add-atom &self (= (t6 $x) v)) (mk)))
!(Inlined (let $u (add-atom &self (= (t7 $x) v)) (t7 1)))

; Lambda bodies.
!(Lambda0 (let $u (add-atom &self (= (t8 $x) v)) ((|-> () (t8 1)))))
!(Lambda1 (let $u (add-atom &self (= (t9 $x) v)) ((|-> ($y) (t9 $y)) 1)))
!(LetBound (let $f (|-> ($y) (t10 $y)) (let $u (add-atom &self (= (t10 $x) v)) ($f 1))))

; A body that adds and then reaches the head: whole, split, and an equation.
!(Whole ((|-> () (let $u (add-atom &self (= (t12 $x) v)) (t12 1)))))
!(Split ((|-> () (let $u (add-atom &self (= (t13 $x) v)) ((|-> () (t13 1)))))))
(= (whole-eq) (let $u (add-atom &self (= (t14 $x) v)) (t14 1)))
!(WholeEquation (whole-eq))

; A change made by a called equation, in the caller's body.
(= (install) (add-atom &self (= (t21 $x) v)))
(= (use) (let $u (install) (t21 1)))
!(Indirect (use))

; Removal: the last equation removed returns later occurrences to data, and
; an equation added again is called again.
(= (known $x) (Pair $x))
(= (run $x) (known $x))
!(BeforeRemoval (run 7))
!(Removed (let $u (remove-atom &self (= (known $x) (Pair $x))) (run 7)))
!(RemovedDirect (let $u (add-atom &self (= (gone $x) here))
                  (let $w (remove-atom &self (= (gone $x) here)) (gone 1))))
!(Readded (let $u (add-atom &self (= (known $x) (Back $x))) (known 3)))

; Negatives: quoted code and a value already built stay data; an entered
; call keeps the alternatives it selected, while a fresh call sees the new one.
(= (old-quote) (quote (late2 5)))
!(Quoted (let $u (add-atom &self (= (late2 $n) (+ $n 1))) (old-quote)))
!(BuiltValue (let $v (t30 1) (let $u (add-atom &self (= (t30 $x) v)) $v)))
(= (pick) a)
(= (pick) b)
!(EnteredKeeps (collapse (let $r (pick) (let $u (add-atom &self (= (pick) c)) $r))))
!(FreshSees (collapse (pick)))
(edge 4 1)
(edge 4 1)
(edge a 1)
(edge 2 c)
; The nested example of the spec's question 6: the head added inside a match
; body is called by the next occurrence that reaches it.
!(Nested (let $h (collapse (match &self (edge $a $b) (let $u (add-atom &self (= (lg $b) $a)) (size-atom (collapse (lg $b)))))) (sort-atom $h)))

; A change made through `call` in a body.
(= (via-call) (let $u (call (add-atom &self (= (t41 $x) v))) (t41 1)))
!(ViaCall (via-call))
