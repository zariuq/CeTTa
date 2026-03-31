; Bindings learned while evaluating an equation RHS must remain visible to the
; caller. Positive example: `let`, `chain`, and translated `let*` conjunctions
; keep caller-visible refinements like $x = a. Negative example: returning only
; the RHS atom and dropping the refined environment, which used to leak `$x`.

(= (wrap (thing $x)) (match &self (fact $x) done))
(fact a)
(fact b)

!(assertEqualToResult
  (let $r (wrap (thing $x)) (result $x $r))
  ((result a done) (result b done)))

!(assertEqualToResult
  (chain (wrap (thing $x)) $r (result $x $r))
  ((result a done) (result b done)))

(p a)
(p b)
(q a)

(= (deduce (p $x)) (match &self (p $x) T))
(= (deduce (q $x)) (match &self (q $x) T))
(= (deduce (And $a $b))
   (let* ((T (deduce $a))
          (T (deduce $b)))
     T))
(= (ift T $then) $then)

!(assertEqualToResult
  (ift (chain (deduce (q $x)) $r $r) $x)
  (a))

!(assertEqualToResult
  (ift (deduce (And (p $x) (q $x))) $x)
  (a))
