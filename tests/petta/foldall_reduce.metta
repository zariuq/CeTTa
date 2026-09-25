;; PeTTa's foldall calls its step through reduce for every value the
;; generator yields, as (step value state).  A step with several answers
;; leaves the last as the state; a step with none, or one that raises,
;; leaves the state as it was; an error in the generator ends it.

(row 1)
(row 2)
(edge a b)
(edge b c)
(edge b d)
(n p)
(n q)
(= (bump $v $s) (+ $v 1))
(= (explode $x) (superpose 1 2))
(= (amb $v $s) (+ $v 1))
(= (amb $v $s) (+ $v 2))
(= (literal-alt $v $s) (+ $v $s))
(= (literal-alt 1 $s) 99)
(= (pick $v $s) (if (> $v 1) $v (empty)))
(= (minus $v $s) (- $s $v))

!(foldall minus (match &self (row $x) $x) 10)
!(foldall amb (match &self (row $x) $x) 0)
!(foldall literal-alt (match &self (row $x) 1) 0)
!(foldall pick (match &self (row $x) $x) 0)
!(foldall % (match &self (edge $x $y) 4) 23)
!(foldall bump (match &self (n $x) (explode $x)) 0)
!(foldall + (superpose (1 2 3)) 0)
!(foldall minus (empty) 5)
