; Same insertion, performed by the match template rather than by the
; caller after the match returns. A later leg still starts after it.

(left a)
(left b)
(right a old)
(right b old)

!(match &self (, (left $x) (right $x $y))
   (let $_ (add-atom &self (right b new)) (w $x $y)))
