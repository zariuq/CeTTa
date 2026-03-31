; HE-backed nested substitution regressions.
; Quote blocks evaluation, not substitution, so outer bindings still rewrite
; nested binder positions before the nested form runs.

!(assertEqualToResult
  (let $x 1
    (let $x 2 $x))
  ())

!(assertEqualToResult
  (let $x 1
    (quote (let $x 2 $x)))
  ((quote (let 1 2 1))))

!(assertEqualToResult
  (let* (($x 1)
         ($x 2))
    $x)
  ())

!(assertEqualToResult
  (let* (((pair $x $y) (pair a b))
         ($x z))
    $x)
  ())

!(assertEqualToResult
  (let* (($x 1)
         ($y $x))
    $y)
  (1))

!(assertEqualToResult
  (chain 1 $x
    (quote (chain 2 $x $x)))
  ((quote (chain 2 1 1))))
