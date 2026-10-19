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

; Equal rule occurrences remain separate, and every nested result retains
; its sharing with the pending caller argument.
(= (alias-dup $x) (Pair $x $x))
(= (alias-dup $x) (Pair $x $x))
(= (alias-wrap $x $tail) (Combined $x $tail))

!(assertEqualToResult
  (alias-wrap (alias-dup $a) $a)
  ((Combined (Pair $a $a) $a) (Combined (Pair $a $a) $a)))

!(assertEqualToResult
  (alias-wrap (alias-dup (Node $a $b)) (Node $a $b))
  ((Combined (Pair (Node $a $b) (Node $a $b)) (Node $a $b))
   (Combined (Pair (Node $a $b) (Node $a $b)) (Node $a $b))))

!(assertEqualToResult
  (alias-wrap (alias-dup (Node $a $a)) (Node $a $a))
  ((Combined (Pair (Node $a $a) (Node $a $a)) (Node $a $a))
   (Combined (Pair (Node $a $a) (Node $a $a)) (Node $a $a))))

; Distinct required values cannot satisfy the two uses of the same residual.
!(assertEqualToResult
  (let (Pair Left Right) (alias-dup $a) Bad)
  ())
