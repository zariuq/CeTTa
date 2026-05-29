; Regression coverage for translated HE/PeTTa surfaces that rely on the
; standard identity equation, let-as-unify semantics, alpha-stable removal,
; and value-tuple type inference.

!(assertEqual (id 5) 5)

(= (g $x) (+ $x 1))
(= (f $g) ($g 1))
(= (f $g) 42)
!(assertEqualToResult (collapse (f g)) ((2 42)))
!(remove-atom &self (= (f $g) 42))
!(assertEqualToResult (collapse (f g)) ((2)))

!(assertEqual
  (let ($x (42 (if (== $x 2) 43 44))) (3 (42 $z)) (+ $x $z))
  47)

(: a A)
(: b B)
(: A Type)

(: mid (-> $a $a))
(= (mid $x) (let (a b) $x $x))

!(assertEqual (get-type (a b)) (A B))
!(assertEqualToResult (mid ($a b)) ((a b)))

!(assertEqual
  (unify ((wrap $y) $y) ($x (wrap $x)) ok fail)
  fail)
!(assertEqualToResult
  (let ($x (wrap $x)) ((wrap $y) $y) ok)
  ())
!(assertEqualToResult
  (let ($x (wrap $x)) ((wrap $y) $y) $x)
  ())
