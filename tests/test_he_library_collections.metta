; HE's library collection functions take their arguments unevaluated and
; collect them: (unique X) is (let $c (collapse X) (let $u (unique-atom $c)
; (superpose $u))), and union, intersection and subtraction collect both
; arguments before combining them.  A collection at the root is computed,
; and an arithmetic template is evaluated per row.

(edge a b)
(edge a c)
(edge b d)
(edge c d)
(edge d e)
(= (reach $x) (match &self (edge $x $y) $y))
(= (reach $x) (match &self (edge $x $y) (reach $y)))
!(unique (reach a))
!(reach a)
(= (left) (superpose (1 2 2 3)))
(= (right) (superpose (2 3 4)))
!(union (left) (right))
!(intersection (left) (right))
!(subtraction (left) (right))
!(let $u (unique (reach a)) (Seen $u))

; A collected value HE evaluates again where the elements are emitted.
(= (f $x) (Called $x))
!(unique (superpose ((f g) (f g) h)))

; Arithmetic templates, per row.
(record 1 17)
(record 2 -5)
(record 3 1000)
!(match &self (record $i $x) (result $i (% (+ (* $x $x) 7) 1000)))
(= (half $x) (/ $x 2.0))
!(collapse (match &self (record $i $x) (half $x)))
