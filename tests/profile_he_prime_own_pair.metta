; he-prime: a program's own declaration of a bare name types that name.  HE
; reserves no `Pair`, `pair` or `fst`; these are the program's declarations, and
; each answer is the one they give.
(: A Type)
(: a A)
(: Pair (-> $x $y Type))
(: pair (-> $x $y (Pair $x $y)))
(: fst (-> (Pair $x $y) $x))

; Positive: saturated applications, nested ones, and the program's own `fst`.
!(assertEqual
  (get-type (pair a 1))
  (Pair A Number))
!(assertEqual
  (get-type (pair (pair a a) "s"))
  (Pair (Pair A A) String))
!(assertEqual
  (get-type (fst (pair a 1)))
  A)
!(assertEqual
  (get-type (Pair A A))
  Type)

; Negative: an application of another arity, and an argument outside the
; declared domain.
!(assertEqualToResult
  (get-type (pair a))
  ())
!(assertEqualToResult
  (get-type (pair a a a))
  ())
!(assertEqualToResult
  (get-type (fst a))
  ())
