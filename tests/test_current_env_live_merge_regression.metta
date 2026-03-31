; Regression for live current_env merge commits.
; Several deterministic tail-reenter sites should be able to accumulate
; bindings across successive branch-local commits without clone-per-attempt
; whole-env replacement.

!(assertEqual
  (let $x first
    (let (pair $y $y) (pair second second)
      (pair $x $y)))
  (pair first second))

!(assertEqual
  (let $x outer
    (case (pair inner inner)
      (((pair $y $y) (pair $x $y)))))
  (pair outer inner))

!(assertEqual
  (let $x left
    (chain right $y
      (pair $x $y)))
  (pair left right))

!(assertEqual
  (let $x a
    (unify (pair $x b) (pair a $y)
      (pair $x $y)
      nope))
  (pair a b))
