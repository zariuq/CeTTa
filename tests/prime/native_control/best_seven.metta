; Every recursive alternative adds a nonnegative integer to the accumulator.
; The source-bound checker inspects both recursions and both terminals.
(= (weighted $cost) (weighted (+ $cost 2)))
(= (weighted $cost) (weighted (+ $cost 3)))
(= (weighted $cost) (Route a $cost))
(= (weighted $cost) (Route b $cost))
!(select 7
   (search-policy (control WO
     (key (weighted $cost) $cost)
     (best (Route $id $cost) $cost)
     (bound (weighted $cost) $cost)
     (budget 100000)))
   (weighted 0))
